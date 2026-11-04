#!/usr/bin/env python3
"""Boot the imported A7M4 guest image in the isolated QEMU board and drive it.

The camera kernel is not position-independent: it expects DRAM at 0x400000000,
its workspace reservations from kemco.txt, and six Sony iomap nodes in the DTB.
We therefore load a fixed-address DTB plus the kernel and initrd at the
addresses the original bootloader uses, and patch the DTB with libfdt.
"""
import ctypes
import json
import os
import re
import secrets
import shutil
import signal
import socket
import tempfile
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True

KIT = Path(__file__).resolve().parent
ROOT = KIT.parent.parent
SOURCES = ROOT / 'runtime/sources/qemu-11.1.2'
QEMU = SOURCES / 'build-sony-ui/qemu-system-aarch64'
LIBFDT = ROOT / 'runtime/sources/qemu-tools/usr/lib/libfdt.so.1'
BASE_DTB = KIT / 'qemu-virt-board/virt-packed.dtb'

DTB_ADDR = 0x400002000
KERNEL_ADDR = 0x400080000
INITRD_ADDR = 0x440000000
RAM_MB = 24576
# Guest DDR bank declared to the kernel. The camera passes mem=4G, but that leaves only
# ~240 MB usable once the memrsv= workspaces are reserved, which cannot hold the ramdisk.
# The board therefore declares one extra GiB beyond the camera's 4 GiB window.
DDR_BYTES = 0x140000000

# Sony iomap nodes: physical address -> fixed virtual address used by Sony's
# address-conversion helpers (layout shared by the CXD90057 SDK, not model code).
IOMAPS = {
    'sony-gic-alias': (0x08000000, 0xfffffffff13a1000, 0x1000),
    'sony-gicc-alias': (0x08010000, 0xfffffffff13a2000, 0x1000),
    'sony-register-backing': (0xf101d000, 0xfffffffff101d000, 0x20000),
    'sony-scu-map': (0xf1388000, 0xfffffffff1388000, 0x2000),
    'sony-timer-map': (0xf1042000, 0xfffffffff1042000, 0x4000),
}
WORKSPACE_RESERVE = ((0x404000000, 0x400000), (0x405000000, 0x800000), (0x410268000, 0x7c000))
# Boot-time drivers that touch hardware this board does not model. The list is
# inherited from the validated CXD90057 board profile.
BLACKLIST = (
    '__boottime___udif_init_udif_wdt',
    '__boottime___udif_init_udif_fusb_otg',
    '__boottime___udif_init_udif_pcie',
    '__boottime___udif_init_udif_ddrc',
    '__boottime___udif_init_pcidma',
    '__boottime_sony_usb_init',
)


def sha256_file(path):
    import hashlib
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def kemco_parameters(kemco):
    """Reservations the kernel expects from the camera bootloader cmdline."""
    lines = Path(kemco).read_text().splitlines()
    return [line for line in lines if line.startswith('memrsv=') and 'tmonitor.' not in line]


def kemco_tokens(kemco, include=()):
    """Camera bootloader parameters the guest needs here.

    The base profile forwards only the memrsv= workspace reservations. Every other
    camera parameter (cfg.uart=, exception.stop=, page_alloc.*, adrs_conv.* ...) is
    opt-in through `include` prefixes, so a profile never silently inherits a
    setting that changes kernel memory behaviour.
    """
    tokens = []
    for line in Path(kemco).read_text().splitlines():
        if line.strip() in ('kemco', '*', '') or line.startswith('#'):
            continue
        for token in line.split():
            if token.startswith('memrsv=') and 'tmonitor.' not in token:
                tokens.append(token)
            elif token.startswith(tuple(include)):
                tokens.append(token)
    return tokens


def build_cmdline(kemco, initrd_size, timers=True, extra='', include=()):
    kib = (initrd_size + 1023) // 1024
    blacklist = list(BLACKLIST)
    if not timers:
        blacklist.insert(0, '__boottime___udif_init_udif_hwtimer')
    parts = ['console=ttyAMA0', 'earlycon=pl011,0x09000000', 'loglevel=8', 'root=/dev/ram0',
             'init=/bin/sh', 'ramdisk_size=%d' % kib, 'rdinit=/init']
    parts += kemco_tokens(kemco, include=tuple(include))
    parts.append('initcall_debug')
    parts.append('initcall_blacklist=' + ','.join(blacklist))
    if extra:
        parts.append(extra)
    return ' '.join(parts)


def build_dtb(base, output, cmdline, initrd_size, smp=1):
    fdt = ctypes.CDLL(str(LIBFDT))
    fdt.fdt_getprop.restype = ctypes.c_void_p
    source = ctypes.create_string_buffer(Path(base).read_bytes())
    buffer = ctypes.create_string_buffer(16384)
    if fdt.fdt_open_into(source, buffer, len(buffer)) != 0:
        raise RuntimeError('fdt_open_into failed')
    chosen = fdt.fdt_path_offset(buffer, b'/chosen')
    if chosen < 0:
        raise RuntimeError('/chosen missing from base DTB')
    value = cmdline.encode() + b'\0'
    if fdt.fdt_setprop(buffer, chosen, b'bootargs', value, len(value)) != 0:
        raise RuntimeError('cannot set bootargs')
    if 'fullsystem.pin-control-memory=1' in cmdline.split():
        value = struct.pack('>QQ', 0x500000000, 0x2000)
        if fdt.fdt_setprop(buffer, chosen, b'qemu,a7c2-pin-control-buffer-owner', value, len(value)) != 0:
            raise RuntimeError('cannot declare virtual pin-control owner')
    for name, number in ((b'linux,initrd-start', INITRD_ADDR),
                         (b'linux,initrd-end', INITRD_ADDR + initrd_size)):
        if fdt.fdt_setprop(buffer, chosen, name, struct.pack('>Q', number), 8) != 0:
            raise RuntimeError('cannot set ' + name.decode())
    for name, (phys, virt, size) in IOMAPS.items():
        node = fdt.fdt_add_subnode(buffer, 0, name.encode())
        if node < 0:
            raise RuntimeError('cannot add ' + name)
        for prop, raw in ((b'device_type', b'iomap\0'), (b'phys', struct.pack('>Q', phys)),
                          (b'virt', struct.pack('>Q', virt)), (b'size', struct.pack('>Q', size)),
                          (b'memtype', struct.pack('>I', 1))):
            if fdt.fdt_setprop(buffer, node, prop, raw, len(raw)) != 0:
                raise RuntimeError('cannot set %s/%s' % (name, prop.decode()))
    # Move the declared DDR bank to Sony's physical base and size it for the ramdisk.
    node = fdt.fdt_next_node(buffer, -1, None)
    while node >= 0:
        length = ctypes.c_int()
        pointer = fdt.fdt_getprop(buffer, node, b'device_type', ctypes.byref(length))
        if pointer and ctypes.string_at(pointer, length.value).rstrip(b'\0') == b'memory':
            ram = DDR_BYTES
            if fdt.fdt_setprop(buffer, node, b'reg', struct.pack('>QQ', 0x400000000, ram), 16) != 0:
                raise RuntimeError('cannot relocate DDR bank')
            break
        node = fdt.fdt_next_node(buffer, node, None)
    else:
        raise RuntimeError('no memory node in base DTB')
    fdt.fdt_add_mem_rsv.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64]
    for base_addr, length in WORKSPACE_RESERVE:
        if fdt.fdt_add_mem_rsv(buffer, base_addr, length) != 0:
            raise RuntimeError('cannot reserve Sony workspace')
    if smp > 1:
        import runpy
        runpy.run_path(str(KIT / 'native_smp.py'))['configure'](fdt, buffer)
    if fdt.fdt_pack(buffer) != 0:
        raise RuntimeError('cannot pack DTB')
    size = struct.unpack_from('>I', buffer.raw, 4)[0]
    Path(output).write_bytes(buffer.raw[:size])
    return Path(output)


class Guest:
    """One QEMU guest: serial console plus bounded command transactions."""

    def __init__(self, run_dir, rootfs, kernel=None, smp=1, kalimba=None, extra_cmdline='',
                 timers=True, display='none', kemco_include=(), ramfb=True):
        self.dir = Path(run_dir).resolve()
        self.dir.mkdir(parents=True, exist_ok=True)
        self.rootfs = Path(rootfs).resolve()
        self.kernel = Path(kernel or (self.rootfs.parent / 'menu-root/system/vmlinux.bin')).resolve()
        if not self.kernel.is_file():
            raise SystemExit('kernel image missing: %s' % self.kernel)
        if not QEMU.is_file():
            raise SystemExit('QEMU binary missing; run ./build-qemu.sh first')
        kemco = self.rootfs.parent / 'menu-root/seed-setting/kemco.txt'
        if not kemco.is_file():
            kemco = kalimba
        if kemco is None or not Path(kemco).is_file():
            raise SystemExit('kemco.txt missing from the imported rootfs')
        self.cmdline = build_cmdline(kemco, self.rootfs.stat().st_size, timers, extra_cmdline,
                                     include=tuple(kemco_include))
        self.dtb = build_dtb(BASE_DTB, self.dir / 'virt.dtb', self.cmdline,
                             self.rootfs.stat().st_size, smp=smp)
        self.console = self.dir / 'console.log'
        self._log = self.console.open('wb')
        args = [str(QEMU), '-machine', 'virt,secure=off,gic-version=2', '-cpu', 'cortex-a35',
                '-smp', str(smp), '-m', str(RAM_MB), '-nodefaults', '-display', display,
                '-monitor', 'none', '-serial', 'stdio', '-nic', 'none', '-no-reboot',
                *(['-device', 'ramfb,id=a7m4-screen'] if ramfb else []),
                *(['-name', 'Sony A7M4 - experimental LCD'] if display != 'none' else []),
                '-device', 'loader,file=%s,addr=%#x' % (self.dtb, DTB_ADDR),
                '-device', 'loader,file=%s,addr=%#x,force-raw=on' % (self.kernel, KERNEL_ADDR),
                '-device', 'loader,file=%s,addr=%#x,force-raw=on' % (self.rootfs, INITRD_ADDR),
                '-device', 'loader,addr=%#x,cpu-num=0' % KERNEL_ADDR,
                '-kernel', str(self.kernel), '-initrd', str(self.rootfs),
                '-append', self.cmdline]
        # QMP lives in a short private path: the run directory can exceed the
        # 108-byte unix socket limit.
        self._qmp_dir = Path(tempfile.mkdtemp(prefix='a7m4-qmp-'))
        self.qmp_path = self._qmp_dir / 'sock'
        args += ['-qmp', 'unix:%s,server=on,wait=off' % self.qmp_path]
        environment = {**os.environ, 'QEMU_SONY_TIMERS': '1' if timers else '0'}
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=self._log,
                                     stderr=subprocess.STDOUT, cwd=str(self.dir), env=environment)
        self._closed = False

    def wait_ready(self, timeout=300):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError('QEMU exited before the guest shell; see console.log')
            if b'A7M4_GUEST_READY' in self.console.read_bytes():
                time.sleep(0.4)
                return
            time.sleep(0.2)
        raise TimeoutError('guest did not reach the shell')

    def run(self, command, timeout=300):
        """Run one shell command and require its exact completion marker."""
        marker = 'A7M4_DONE_' + secrets.token_hex(8)
        start = self.console.stat().st_size
        text = command.rstrip()
        tail = ' true; rc=$?' if text.endswith('&') else '; rc=$?'
        self.proc.stdin.write((text + tail + '; echo; echo ' + marker + ':$rc\n').encode())
        self.proc.stdin.flush()
        data = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.console.read_bytes()[start + len(data):]
            if chunk:
                data.extend(chunk)
            match = re.search(rb'\n' + marker.encode() + rb':([0-9]+)\r?\n', bytes(data))
            if match:
                return bytes(data), int(match.group(1))
            if self.proc.poll() is not None:
                raise RuntimeError('QEMU exited during: ' + command[:60])
            time.sleep(0.05)
        raise TimeoutError('no completion marker for: ' + command[:60])

    def qmp(self, command, arguments=None):
        if not hasattr(self, '_qmp'):
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and not self.qmp_path.exists():
                time.sleep(0.05)
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(30)
            sock.connect(str(self.qmp_path))
            stream = sock.makefile('rwb')
            json.loads(stream.readline())          # greeting
            stream.write(b'{"execute":"qmp_capabilities"}\n'); stream.flush()
            json.loads(stream.readline())
            self._qmp = stream
        message = {'execute': command}
        if arguments:
            message['arguments'] = arguments
        self._qmp.write((json.dumps(message) + '\n').encode())
        self._qmp.flush()
        while True:
            reply = json.loads(self._qmp.readline())
            if 'error' in reply:
                raise RuntimeError('QMP %s failed: %s' % (command, reply['error']))
            if 'return' in reply:
                return reply['return']

    def screendump(self, path):
        """Save the current console (ramfb) frame as PPM."""
        self.qmp('screendump', {'filename': str(Path(path).resolve())})
        return Path(path)

    def pmemsave(self, address, size, path):
        self.qmp('pmemsave', {'val': address, 'size': size,
                              'filename': str(Path(path).resolve())})
        return Path(path)

    def stop(self):
        if self._closed:
            return
        self._closed = True
        try:
            if self.proc.stdin:
                self.proc.stdin.close()
        except Exception:
            pass
        if self.proc.poll() is None:
            try:
                self.qmp('quit')
                self.proc.wait(20)
            except Exception as error:
                (self.dir / 'shutdown-error.txt').write_text(str(error) + '\n')
                self._closed = False
                raise RuntimeError('QMP shutdown failed; VM retained for diagnosis') from error
        self._log.close()
        if hasattr(self, '_qmp'):
            try:
                self._qmp.close()
            except Exception:
                pass
        shutil.rmtree(self._qmp_dir, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()


def ppm_to_png(ppm, png):
    """Convert QEMU's P6 screendump to PNG (Pillow when present, raw PPM otherwise)."""
    ppm, png = Path(ppm), Path(png)
    try:
        from PIL import Image
    except ImportError:
        return ppm
    Image.open(ppm).save(png)
    return png


def main():
    import argparse
    parser = argparse.ArgumentParser(description='boot the imported A7M4 guest')
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--smp', type=int, default=1)
    parser.add_argument('--command', action='append', default=[])
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--keep', action='store_true')
    args = parser.parse_args()
    with Guest(args.run_dir, args.rootfs, smp=args.smp) as guest:
        guest.wait_ready()
        print('GUEST_READY', flush=True)
        for command in args.command:
            output, rc = guest.run(command, timeout=args.timeout)
            sys.stdout.write(output.decode('utf-8', 'replace'))
            print('[rc=%d]' % rc, flush=True)


if __name__ == '__main__':
    main()
