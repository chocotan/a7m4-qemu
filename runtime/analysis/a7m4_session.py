#!/usr/bin/env python3
"""Guest-side bring-up for the A7M4 QEMU runtime: module order and app start.

The order comes from the camera's own /sbin/initrc/insmod_drv1 and insmod_drv2.
Modules that need hardware this board does not model are listed in OMITTED with
the observed reason, so the profile stays auditable instead of silently shrinking.
"""
import json
import time
from pathlib import Path

# (module path, insmod arguments, why)
MODULES = [
    ('/kmod/devno.ko', '', 'device-node manager (nflasha/uipc nodes)'),
    ('/usr/kmod/osal_utm.ko', '', 'OS abstraction: threads/timers/queues'),
    ('/usr/kmod/osal_uipc.ko', '', 'OS abstraction: user IPC transport'),
    ('/kmod/dma330.ko', 'phy_inst_area=0x4102E4000', 'UDIF PL330 memory/peri DMA'),
    ('/usr/kmod/ldec.ko', '', 'LZ77 decode device'),
    ('/usr/kmod/utimer.ko', '', 'unit timer manager'),
    ('/usr/kmod/backup.ko', 'Backup_load=1 Backup_save=0', 'settings bank reader'),
    ('/usr/kmod/osal_ulogio.ko', 'ulogio_initfile=/setting/ulogio.bin', 'log sink'),
    ('/usr/kmod/input.ko', '', 'input event queue'),
    ('/usr/kmod/ioexp.ko', '', 'I/O expander for body controls'),
    ('/usr/kmod/indctr.ko', '', 'indicator LED'),
    ('/usr/kmod/udate.ko', '', 'date/time distribution'),
    ('/usr/kmod/stream.ko', '', 'media stream'),
    ('/usr/kmod/stream2.ko', '', 'media stream (second instance)'),
    ('/usr/kmod/dmm.ko', 'DmmConfig=/setting/DmmCfg.bin', 'dynamic memory manager'),
    ('/usr/kmod/icdt.ko', '', 'image data memory free helper'),
    ('/usr/kmod/kikilog.ko', '', 'kikilog log transport'),
    ('/usr/kmod/upm.ko', '', 'power manager'),
    ('/usr/kmod/grm_ma.ko', '', 'graphics resource manager memory arbiter'),
    ('/usr/kmod/mcmn_drv.ko', '', 'media common driver'),
    ('/usr/kmod/codec_drv.ko', '', 'external audio codec'),
]

# module -> (observed behaviour, what a real implementation needs)
OMITTED = {
    '/kmod/crypt-emmc.ko': ('needs the eMMC controller; the guest rootfs is a ramdisk',
                            'CXD90057 eMMC/flashtor controller model'),
    '/kmod/emmc.ko': ('needs the eMMC controller', 'CXD90057 eMMC/flashtor controller model'),
    '/usr/kmod/pciedma.ko': ('not shipped in this update package', 'PCIe DMA engine model'),
    '/usr/kmod/pci_ep_cp.ko': ('PCIe endpoint to the CP; no CP host model', 'PCIe EP model'),
    '/usr/kmod/pci_ep_zq.ko': ('not shipped in this update package', 'PCIe EP model'),
    '/usr/kmod/pcieatu.ko': ('PCIe address translation unit absent', 'PCIe ATU model'),
    '/usr/kmod/pcie-if.ko': ('PCIe link to CP absent', 'PCIe link model'),
    '/usr/kmod/usb_portMonitor.ko': ('not shipped in this update package', 'CXD90057 USB IC driver'),
    '/usr/kmod/usb_buspwr.ko': ('not shipped in this update package', 'CXD90057 USB IC driver'),
    '/usr/kmod/usb_current.ko': ('not shipped in this update package', 'CXD90057 USB IC driver'),
    '/usr/kmod/usb_beets.ko': ('not shipped in this update package', 'USB battery/charging path'),
    '/usr/kmod/usbg_buspower.ko': ('not shipped in this update package', 'USB gadget model'),
    '/usr/kmod/usbg_sen.ko': ('not shipped in this update package', 'USB gadget model'),
    '/usr/kmod/usb_extcmd.ko': ('not shipped in this update package', 'USB gadget model'),
    '/usr/kmod/nfc.ko': ('not shipped in this update package', 'NFC controller'),
    '/usr/kmod/grm_gles.ko': ('not shipped in this update package', 'GLES graphics driver'),
    '/usr/kmod/accel.ko': ('not shipped in this update package', 'accelerometer'),
    '/usr/kmod/mag.ko': ('not shipped in this update package', 'magnetometer'),
    '/usr/kmod/emg.ko': ('not shipped in this update package', 'gyro/emergency sensor'),
    '/usr/kmod/sata_drv.ko': ('not shipped in this update package', 'SATA controller'),
    '/usr/kmod/msen_drv.ko': ('not shipped in this update package', 'media sensor'),
}

APP_COMMAND = "/usr/bin/bootin.elf 'target=qemu ' dmode=3 2>&1 &"


def setup_commands(include_missing=False):
    """Ordered guest commands that reproduce the camera's initrc module load."""
    commands = []
    for path, arguments, _ in MODULES:
        line = 'insmod %s' % path
        if arguments:
            line += ' ' + arguments
        commands.append(line + '; echo MOD_OK ' + Path(path).stem + '=$?')
    if include_missing:
        for path in OMITTED:
            commands.append('insmod %s; echo MOD_TRY %s=$?' % (path, Path(path).stem))
    commands.append('ls -l /dev/uipc /dev/ldec /dev/grm 2>&1')
    return commands


def run_app(guest, seconds=45, command=None, poll=1.0):
    """Start the original boot manager and collect console evidence until it stalls.

    The app occupies the single guest CPU, so post-start shell commands are not
    reliable; everything observed here comes from the console stream itself.
    """
    command = command or APP_COMMAND
    console = guest.console
    offset = console.stat().st_size
    guest.proc.stdin.write((command + '\n').encode())
    guest.proc.stdin.flush()
    started = time.monotonic()
    observed = bytearray()
    while time.monotonic() - started < seconds:
        chunk = console.read_bytes()[offset + len(observed):]
        if chunk:
            observed.extend(chunk)
            offset += 0
        if guest.proc.poll() is not None:
            break
        time.sleep(poll)
    return bytes(observed)


def watch(guest, seconds=45, poll=1.0):
    """Collect console output for a guest that runs the session by itself."""
    console = guest.console
    offset = 0
    started = time.monotonic()
    observed = bytearray()
    while time.monotonic() - started < seconds:
        chunk = console.read_bytes()[offset + len(observed):]
        if chunk:
            observed.extend(chunk)
        if guest.proc.poll() is not None:
            break
        time.sleep(poll)
    return bytes(observed)


def evidence(text):
    """Summarise the console stream into checkable facts (no success claims)."""
    blob = text.decode('utf-8', 'replace')
    lines = blob.splitlines()
    facts = {
        'bytes': len(text),
        'has_grm_area': 'FixedAreaTop' in blob,
        'has_uipc_warning': 'No snd_msg registered' in blob,
        'has_liro_symbol_error': 'liro: Unknown symbol' in blob,
        'has_crash_dump': '[register dump]' in blob or 'Exception Syndrome' in blob,
        'has_abort_or_fault': 'Abort (PC=' in blob or 'DABT' in blob or 'Data abort' in blob,
        'has_create_screen': 'create_screen' in blob.lower(),
        'lines': [line for line in lines if line.strip()][-40:],
    }
    return facts


def write_evidence(path, facts, extra=None):
    payload = dict(facts)
    if extra:
        payload.update(extra)
    Path(path).write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
    return payload
