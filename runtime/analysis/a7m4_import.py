#!/usr/bin/env python3
"""Import an ILCE-7M4 BODYDATA package into an offline QEMU rootfs.

The package is the official Sony update container. Only the partitions the
updater ships are used (nflasha3/7/8/15); the guest rootfs is assembled from
them plus the settings/tmp seeds the updater writes to /setting and /tmp.

Nothing here modifies the source firmware: the container is decrypted into a
cache directory and all later edits stay inside it.
"""
import argparse
import hashlib
import io
import json
import os
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

KIT = Path(__file__).resolve().parent
ROOT = KIT.parent.parent
CACHE = ROOT / 'runtime/bodydata-cache'
ANALYSIS = KIT

# Pinned identity of the accepted packages (ILCE-7M4, region 0 = ALLLANG).
# The published system sources are for 6.00; 6.02 is the locally supplied
# update package. Both share the CXD90057 platform and partition layout.
MODEL = 0x1030081
VERSIONS = ('6.00', '6.01', '6.02')
REGION = 0
SCHEMA = 1

PART = '0700_part_image/dev/'
SETTINGS = '0800_appli/setting/'
TMP_SEED = '0800_appli/tmp/'
BACKUP = '0110_backup/SYSIPSX-DSLR/LS/CH89101_ALLLANG.bin'
BOOT_FILES = ('av-cam.bin', 'bonobo.bin', 'btfw32.tlv', 'btfw32a.tlv', 'btnv32.bin',
              'wole_app.bin', 'sabin/idt_cam.bin')

# Services whose kernel/hardware backing does not exist in this VM. The library is
# removed so libIMDB's dlopen fails cleanly instead of entering a NULL callback;
# the same entries are also dropped from the IMDB target mask.
HARDWARE_ONLY_LIBS = ('libUsbBusPower.so',)
# Kernel modules that need the CXD90057 USB IC. Measured 2026-11-02: loading
# usb_current/usb_buspwr makes kernel threads udm_usbcur/udm_usbprtm fault at 0x0,
# and the firmware's own USB service fails to register its uipc queue either way.
# They are removed from the image so the guest never insmods them.
HARDWARE_ONLY_MODULES = ('usb_current.ko', 'usb_buspwr.ko', 'usb_beets.ko',
                         'usb_portMonitor.ko', 'usbg_buspower.ko', 'usbg_sen.ko',
                         'usb_extcmd.ko')
IMDB_MASK_CLEAR = {'UsbBusPower', 'InfraUsbCmn', 'sensapi', 'UsbControl', 'BusPower',
                   'Hotplug', 'InfraCrypt', 'InfraMediaPwrMgr', 'NetServiceTethering',
                   'TetheringResource'}

# libObj.so directly needs both of these, so they must stay on disk.
KEEP_LIBS = ('libInfraUsbCmn.so', 'libUsbControl.so')

LIBIMDB_STRIDE = 0x48
LIBIMDB_SENTINEL = 40


def sha256_file(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class PackageIdentity:
    def __init__(self, model, version, region, crypter, sha):
        self.model = model
        self.version = version
        self.region = region
        self.crypter = crypter
        self.sha = sha


def _sdk():
    sdk = KIT / 'vendor/fwtool-sdk'
    host = KIT / 'vendor/host-python'
    if host.is_dir():
        sys.path.insert(0, str(host))
    if str(sdk) not in sys.path:
        sys.path.insert(0, str(sdk))
    from fwtool.sony import dat as datmod, fdat as fdatmod
    return datmod, fdatmod


def read_package(package):
    """Decrypt the container and return (identity, open FDAT stream, temp path)."""
    dat, fdat = _sdk()
    package = Path(package)
    digest = sha256_file(package)
    tmp = Path(tempfile.mkdtemp(prefix='a7m4-fdat-'))
    plain = tmp / 'firmware.fdat'
    with package.open('rb') as source:
        if not dat.isDat(source):
            raise SystemExit('not a Sony BODYDATA DAT container: %s' % package)
        container = dat.readDat(source)
        crypter, decrypted = fdat.decryptFdat(container.firmwareData)
        with plain.open('wb') as output:
            shutil.copyfileobj(decrypted, output, 1 << 20)
    stream = plain.open('rb')
    info = fdat.readFdat(stream)
    identity = PackageIdentity(info.model, info.version, info.region, crypter, digest)
    if (identity.model, identity.region) != (MODEL, REGION) or identity.version not in VERSIONS:
        stream.close()
        raise SystemExit('unsupported firmware: model=%#x version=%s region=%d '
                         '(this runtime accepts ILCE-7M4 %s ALLLANG)'
                         % (identity.model, identity.version, identity.region,
                            '/'.join(VERSIONS)))
    return identity, info, stream, tmp


def extract_members(stream, members, destination):
    """Extract an explicit member allowlist from the FDAT tar payload."""
    import tarfile
    stream.seek(0)
    wanted = set(members)
    found = set()
    with tarfile.open(fileobj=stream, mode='r:') as archive:
        for entry in archive:
            name = entry.name.lstrip('./')
            if name not in wanted:
                continue
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if entry.issym():
                # Only relative links inside the payload are accepted (/tmp/mtab -> ../proc/mounts).
                link = entry.linkname
                if Path(link).is_absolute():
                    raise SystemExit('unsafe absolute symlink in package: %s' % name)
                target.symlink_to(link)
            elif entry.isreg():
                with archive.extractfile(entry) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output, 1 << 20)
            else:
                continue
            found.add(name)
    missing = wanted - found
    if missing:
        raise SystemExit('package is missing required members: %s' % ', '.join(sorted(missing)))


def extract_tree(stream, prefix, destination):
    """Extract one trusted package subtree, preserving relative symlinks."""
    import tarfile
    stream.seek(0)
    found = 0
    with tarfile.open(fileobj=stream, mode='r:') as archive:
        for entry in archive:
            name = entry.name.lstrip('./')
            if not name.startswith(prefix):
                continue
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if entry.issym():
                link = entry.linkname
                if Path(link).is_absolute():
                    raise SystemExit('unsafe absolute symlink in package: %s' % name)
                if target.exists() or target.is_symlink():
                    target.unlink()
                target.symlink_to(link)
            elif entry.isreg():
                with archive.extractfile(entry) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output, 1 << 20)
            found += 1
    if not found:
        raise SystemExit('package subtree missing: %s' % prefix)


def extract_fat(image, destination):
    """Read a FAT partition image with the bundled fwtool parser (no mtools needed)."""
    sys.path.insert(0, str(KIT / 'vendor/fwtool'))
    from fwtool.archive.fat import readFat
    data = Path(image).read_bytes()
    total = 0
    for entry in readFat(io.BytesIO(data)):
        name = entry.path.lstrip('/')
        if not name:
            continue
        target = destination / name
        if stat.S_ISDIR(entry.mode):
            target.mkdir(parents=True, exist_ok=True)
            continue
        total += entry.size
        if total > 64 << 20:
            raise SystemExit('FAT payload exceeds bound')
        target.parent.mkdir(parents=True, exist_ok=True)
        contents = entry.contents.read(entry.size)
        if len(contents) != entry.size:
            raise SystemExit('truncated FAT member: %s' % name)
        target.write_bytes(contents)
        os.utime(target, (entry.mtime, entry.mtime))


def extract_ext2(image, destination):
    """Extract an ext2 partition image with debugfs (read-only, unprivileged)."""
    destination.mkdir(parents=True, exist_ok=True)
    run = subprocess.run(['debugfs', '-R', 'rdump / %s' % destination, str(image)],
                         capture_output=True, text=True, timeout=600,
                         env={**os.environ, 'LC_ALL': 'C'})
    errors = [line for line in (run.stdout + run.stderr).splitlines()
              if line and not line.startswith('debugfs ')
              and 'Operation not permitted while changing ownership' not in line]
    if run.returncode or errors:
        raise SystemExit('debugfs extraction failed: %s' % '; '.join(errors[:3]))


class ELF64:
    """Minimal ELF reader: program headers plus .dynsym lookups."""

    def __init__(self, data):
        if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
            raise ValueError('expected AArch64 little-endian ELF64')
        self.data = data
        phoff = struct.unpack_from('<Q', data, 32)[0]
        entsize, count = struct.unpack_from('<HH', data, 54)
        self.loads = []
        for i in range(count):
            kind, _, off, va, _, filesz, memsz, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i*entsize)
            if kind == 1:
                self.loads.append((va, filesz, off))
        # Section headers are needed for .dynsym/.dynstr only.
        shoff = struct.unpack_from('<Q', data, 40)[0]
        shentsize, shnum = struct.unpack_from('<HH', data, 58)
        self.sections = []
        for i in range(shnum):
            _, kind, _, va, off, size, link, _, _, entsize = struct.unpack_from(
                '<IIQQQQIIQQ', data, shoff + i*shentsize)
            self.sections.append({'type': kind, 'va': va, 'off': off, 'size': size,
                                  'link': link, 'entsize': entsize})

    def offset(self, va, size=1):
        for start, length, off in self.loads:
            if start <= va and va + size <= start + length:
                return off + (va - start)
        return None

    def cstring(self, va):
        off = self.offset(va)
        if off is None:
            return None
        end = self.data.find(b'\0', off)
        if end < 0:
            return None
        raw = self.data[off:end]
        return raw.decode('ascii', 'replace') if all(32 <= c < 127 for c in raw) else None

    def dynsym_address(self, name):
        """Return the st_value of a defined dynamic symbol, or None."""
        dynsym = next((s for s in self.sections if s['type'] == 11), None)
        dynstr = next((s for s in self.sections if s['type'] == 3), None)
        if not dynsym or not dynstr:
            return None
        text = self.data
        for i in range(dynsym['size'] // dynsym['entsize']):
            off = dynsym['off'] + i*dynsym['entsize']
            st_name, st_info, _, st_shndx, st_value, _ = struct.unpack_from('<IBBHQQ', text, off)
            if st_name == 0 or st_shndx == 0:
                continue
            start = dynstr['off'] + st_name
            end = text.find(b'\0', start)
            if text[start:end].decode('ascii', 'replace') == name:
                return st_value
        return None


def patch_imdb(data):
    """Clear the boot target bits of hardware-only services in libIMDB's table."""
    elf = ELF64(data)
    table = elf.dynsym_address('imdb_raw')
    if table is None:
        raise SystemExit('libIMDB: imdb_raw symbol not found; refusing to guess offsets')
    entries = table + 0x100
    out = bytearray(data)
    changed = []
    va = entries
    for _ in range(1024):
        first = struct.unpack_from('<I', out, elf.offset(va, 4))[0]
        if first == LIBIMDB_SENTINEL:
            break
        path = elf.cstring(struct.unpack_from('<Q', out, elf.offset(va, 8))[0]) or ''
        mask_off = elf.offset(va + 8, 4)
        mask = struct.unpack_from('<I', out, mask_off)[0]
        key = Path(path).name
        key = key[3:] if key.startswith('lib') else key
        key = key[:-3] if key.endswith('.so') else key
        if key in IMDB_MASK_CLEAR and mask & 7:
            struct.pack_into('<I', out, mask_off, mask & ~7)
            changed.append((key, path, mask))
        va += LIBIMDB_STRIDE
    else:
        raise SystemExit('libIMDB: table did not terminate; layout changed')
    return bytes(out), changed


INIT_TEMPLATE = """#!/bin/busybox sh
# A7M4 QEMU guest init: the device mounts these from nflasha partitions.
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/cgroup
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp
cp -a /tmp-seed/. /tmp/
mount -t tmpfs tmpfs /tmp_wbshd
mount -t tmpfs tmpfs /log
echo 0 > /proc/sys/kernel/randomize_va_space
mount -t tmpfs tmpfs /setting
cp -a /seed-setting/. /setting/
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/usr/lib:/lib
export HOME=/
export TOPRC=/etc/toprc
echo A7M4_GUEST_READY
if grep -q 'a7m4.run=1' /proc/cmdline; then
  # Unattended session: the host cannot use the serial port as a shell when the
  # guest runs with more than one vCPU, so the bring-up is executed by init.
  /bin/sh /simulation/session.sh
fi
exec /bin/sh </dev/ttyAMA0 >/dev/ttyAMA0 2>&1
"""


def version_record(version):
    """libInfraGetVersion reads minor/major packed BCD from updater/dat4."""
    if version not in VERSIONS:
        raise ValueError('unsupported version record: %s' % version)
    major, minor = (int(part) for part in version.split('.'))
    return bytes(((minor // 10 << 4) | minor % 10,
                  (major // 10 << 4) | major % 10))


def copy_seed(source, destination):
    """Copy the updater's /tmp and /tmp-like payloads, preserving symlinks."""
    destination.mkdir(parents=True, exist_ok=True)
    for item in sorted(Path(source).iterdir()):
        target = destination / item.name
        if item.is_symlink():
            if target.exists() or target.is_symlink():
                target.unlink()
            target.symlink_to(os.readlink(item))
        elif item.is_dir():
            copy_seed(item, target)
        else:
            shutil.copy2(item, target)


def build_rootfs(fw, dest, identity):
    """Assemble the guest rootfs from the extracted partitions and seeds."""
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    parts = fw / '0700_part_image/dev'
    # 1. nflasha7 is the ext2 rootfs; nflasha15 is /usr.
    rootfs_stage = dest.parent / (dest.name + '.rootfs')
    usr_stage = dest.parent / (dest.name + '.usr')
    extract_ext2(parts / 'nflasha7', rootfs_stage)
    extract_ext2(parts / 'nflasha15', usr_stage)
    for entry in rootfs_stage.iterdir():
        shutil.move(str(entry), str(dest / entry.name))
    merged_usr = dest / 'usr'
    if merged_usr.exists():
        # The rootfs ships an empty /usr mount point; merge nflasha15 into it.
        for entry in usr_stage.iterdir():
            target = merged_usr / entry.name
            if target.exists() and target.is_dir() and entry.is_dir():
                shutil.copytree(entry, target, dirs_exist_ok=True, symlinks=True)
            else:
                shutil.move(str(entry), str(target))
        shutil.rmtree(usr_stage)
    else:
        shutil.move(str(usr_stage), str(merged_usr))
    # 2. Device mount points and the /system FAT, /cp FAT.
    for name in ('setting', 'system', 'cp', 'log', 'cert', 'lens', 'tmp', 'tmp_wbshd',
                 'var', 'rootfs', 'initrd'):
        (dest / name).mkdir(exist_ok=True)
    (dest / 'var/run').mkdir(exist_ok=True)
    (dest / 'var/log').mkdir(exist_ok=True)
    extract_fat(parts / 'nflasha3', dest / 'system')
    extract_fat(parts / 'nflasha8', dest / 'cp')
    # 3. Settings, /tmp and /etc seeds the updater installs on the device.
    seed = dest / 'seed-setting'
    copy_seed(fw / 'setting', seed)
    backup = fw / 'backup' / Path(BACKUP).name
    shutil.copy2(backup, seed / 'Backup.bin')
    # The updater normally creates dat4; the original version service needs it
    # before its worker starts. Store selected-package identity on virtual RAM.
    (seed / 'updater').mkdir(exist_ok=True)
    (seed / 'updater/dat4').write_bytes(version_record(identity.version))
    tmp_seed = dest / 'tmp-seed'
    copy_seed(fw / 'tmp', tmp_seed)
    (dest / 'etc').mkdir(exist_ok=True)
    copy_seed(tmp_seed, dest / 'etc')
    lens = fw / 'lens' / 'lensfile.bin'
    if lens.is_file():
        shutil.copy2(lens, dest / 'lens/lensfile.bin')
    # Keep pinned originals outside the mutable guest tree for the derived UI profile.
    originals = dest.parent / 'ui-originals'
    if originals.exists():
        shutil.rmtree(originals)
    originals.mkdir()
    for name in ('libIMDB.so', 'libUsbBusPower.so'):
        source = dest / 'usr/lib' / name
        if source.is_file():
            shutil.copy2(source, originals / name)
    # 4. Remove libraries whose hardware service cannot initialise in this VM.
    # libIMDB's mask table layout differs from the A7C2 generation, so no mask edit
    # is attempted here; the absence of the library makes IMDB's dlopen fail cleanly.
    removed_modules = []
    for name in HARDWARE_ONLY_MODULES:
        target = dest / 'usr/kmod' / name
        if target.exists():
            target.unlink()
            removed_modules.append(name)
    removed_libs = []
    for name in HARDWARE_ONLY_LIBS:
        target = dest / 'usr/lib' / name
        if target.exists() and name not in KEEP_LIBS:
            target.unlink()
            removed_libs.append(name)
    (dest / 'etc/ld.so.preload').write_text('/usr/lib/libMWF.so\n/usr/lib/libObj.so\n')
    # Optional guest diagnostics (built by tools/build-guest.sh) are shipped in the
    # image but only loaded when a run asks for them.
    tap_dir = KIT / 'tap'
    if tap_dir.is_dir():
        (dest / 'simulation').mkdir(exist_ok=True)
        for item in sorted(tap_dir.glob('*.so')):
            shutil.copy2(item, dest / 'simulation' / item.name)
    write_session(dest)
    (dest / 'init').write_text(INIT_TEMPLATE)
    (dest / 'init').chmod(0o755)
    return {
        'model': '%#x' % identity.model, 'version': identity.version, 'region': identity.region,
        'package_sha256': identity.sha, 'crypter': identity.crypter,
        'removed_libs': removed_libs,
        'removed_modules': removed_modules,
        'libimdb_sha256': sha256((dest / 'usr/lib/libIMDB.so').read_bytes()),
        'rootfs': {'files': 0},
    }


def write_session(dest):
    """Guest bring-up script used by the unattended profile (a7m4.run=1)."""
    sys.path.insert(0, str(KIT))
    import a7m4_session
    lines = ['#!/bin/busybox sh', 'echo A7M4_SESSION_START']
    for command in a7m4_session.setup_commands():
        lines.append(command)
    lines.append('echo A7M4_SESSION_MODULES_DONE')
    lines.append(a7m4_session.APP_COMMAND)
    lines.append('echo A7M4_SESSION_APP_STARTED')
    (dest / 'simulation').mkdir(exist_ok=True)
    script = dest / 'simulation/session.sh'
    script.write_text('\n'.join(lines) + '\n')
    script.chmod(0o755)


def build_cpio(root, output):
    """Build a raw newc cpio; the camera kernel has no gzip initrd support."""
    listing = subprocess.run(['find', '.', '-print0'], cwd=root, check=True,
                             capture_output=True)
    with output.open('wb') as stream:
        subprocess.run(['cpio', '--null', '-o', '-H', 'newc', '--quiet'],
                       cwd=root, input=listing.stdout, stdout=stream, check=True)
    return output.stat().st_size


def prepare(package, manifest_path, rootfs_name='menu-root', cpio_name='menu-root.cpio'):
    identity, info, stream, tmp = read_package(package)
    print('IDENTIFIED model=%#x version=%s region=%d crypter=%s package_sha256=%s'
          % (identity.model, identity.version, identity.region, identity.crypter, identity.sha))
    try:
        fw = tmp / 'fw'
        members = [PART + n for n in ('nflasha3', 'nflasha7', 'nflasha8', 'nflasha15')]
        members += [BACKUP]
        if info.firmware is None:
            raise SystemExit('FDAT has no firmware payload')
        extract_members(info.firmware, members, fw)
        extract_tree(info.firmware, '0800_appli/', fw)
        (fw / 'backup').mkdir(exist_ok=True)
        shutil.move(str(fw / BACKUP), str(fw / 'backup' / Path(BACKUP).name))
        for sub in ('setting', 'tmp', 'lens'):
            shutil.move(str(fw / ('0800_appli/' + sub)), str(fw / sub))
        dest = ANALYSIS / rootfs_name
        manifest = build_rootfs(fw, dest, identity)
        cpio = ANALYSIS / cpio_name
        size = build_cpio(dest, cpio)
        manifest['cpio'] = {'path': cpio.name, 'size': size, 'sha256': sha256_file(cpio)}
        manifest['rootfs']['files'] = sum(1 for _ in dest.rglob('*'))
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        print('A7M4_PREPARED rootfs=%s cpio=%s (%d bytes) files=%d'
              % (dest, cpio.name, size, manifest['rootfs']['files']))
        return manifest
    finally:
        stream.close()
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bodydata', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, default=ANALYSIS / 'native-assets.json')
    parser.add_argument('--check', action='store_true', help='verify the pinned identity only')
    args = parser.parse_args()
    if args.check:
        identity, info, stream, tmp = read_package(args.bodydata)
        stream.close()
        shutil.rmtree(tmp, ignore_errors=True)
        print('A7M4_PACKAGE_OK model=%#x version=%s sha256=%s'
              % (identity.model, identity.version, identity.sha))
        return
    prepare(args.bodydata, args.manifest)


if __name__ == '__main__':
    main()
