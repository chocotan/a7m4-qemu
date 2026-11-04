#!/usr/bin/env python3
"""Build isolated copies and preserve raw console evidence for the original UI."""
import argparse
import hashlib
import io
import json
import re
import shutil
import struct
import stat
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
KIT = HERE.parent
sys.path.insert(0, str(KIT))
from a7m4_import import build_cpio, version_record, _sdk
from a7m4_qemu import Guest
from a7m4_session import setup_commands
from bitmap_preview import bitmap_png


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_shared(root, output, sources):
    command = ['clang', '--target=aarch64-linux-gnu', '-std=gnu11', '-fuse-ld=lld', '-nostdlib',
               '-O2', '-mno-outline-atomics', '-shared', '-fPIC', '-Wall', '-Wextra',
               '-DSYSTEM_SOUND_OWNER_LIBRARY', '-DHOSD_PATH="/log/qemu/a7m4-osd-frame.bin"',
               '-isystem', '/usr/aarch64-linux-gnu/include',
               *map(str, sources),
               *[str(root / 'lib' / name) for name in
                 ('libc-2.25.so', 'libdl-2.25.so', 'libpthread-2.25.so', 'ld-2.25.so')],
               '-o', str(output)]
    subprocess.run(command, check=True)


def identity_fat(identity):
    """Only DAT2/DAT3 from verified package fields; no updater image or code."""
    _sdk()
    from fwtool.archive import UnixFile
    from fwtool.archive.fat import readFat, writeFat
    contents = {'/DAT2': struct.pack('<I', int(identity['model'], 0)),
                '/DAT3': struct.pack('<I', identity['region'])}
    files = [UnixFile(name, len(data), 315532800, stat.S_IFREG | 0o444,
                      0, 0, io.BytesIO(data)) for name, data in contents.items()]
    image = io.BytesIO()
    writeFat(files, 4 * 1024 * 1024, image)
    image.seek(0)
    recovered = {entry.path: entry.contents.read() for entry in readFat(image)}
    if recovered != contents:
        raise RuntimeError('identity FAT roundtrip mismatch')
    return image.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=90)
    parser.add_argument('--seed-power', action='store_true')
    parser.add_argument('--hdmi-observer', action='store_true')
    parser.add_argument('--inspect-thread', action='store_true')
    parser.add_argument('--updater-close', action='store_true')
    parser.add_argument('--capid-storage', action='store_true')
    parser.add_argument('--sa-address', action='store_true')
    parser.add_argument('--dmm-storage', action='store_true')
    parser.add_argument('--firmware-identity', action='store_true')
    parser.add_argument('--display-model', action='store_true')
    parser.add_argument('--skip-setup', action='store_true',
                        help='prepare original setup-completed Backup bytes and a verified native clock in this VM')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600:
        parser.error('--seconds must be between 1 and 600')
    baseline = KIT / 'menu-root'
    identity = json.loads((KIT / 'native-assets.json').read_text())
    package = KIT.parents[1] / 'BODYDATA.DAT'
    if identity['model'] != '0x1030081' or identity['region'] != 0:
        raise RuntimeError('manifest is not an A7M4 ALLLANG package')
    if digest(package) != identity['package_sha256']:
        raise RuntimeError('package identity does not match the imported manifest')
    unchanged = {str(p): digest(p) for p in
                 (baseline / 'etc/ld.so.preload', baseline / 'simulation/session.sh',
                  baseline / 'seed-setting/Backup.bin')}
    run = KIT / 'menu-runs' / time.strftime('ui-trace-%Y%m%d-%H%M%S')
    run.mkdir(parents=True, exist_ok=False)
    root, cpio = run / 'root', run / 'root.cpio'
    try:
        # Separate inodes: changing a preload or session in a hardlink clone
        # would silently change the baseline too.
        shutil.copytree(baseline, root, symlinks=True)
        updater = root / 'seed-setting/updater'
        updater.mkdir(exist_ok=True)
        (updater / 'dat4').write_bytes(version_record(identity['version']))
        sim = root / 'simulation'
        sim.mkdir(exist_ok=True)
        if args.firmware_identity:
            (sim / 'firmware-identity.fat').write_bytes(identity_fat(identity))
        for name, define in (('hdmi-register', 'A7M4_HDMI_REGISTER_SELFTEST'),
                             ('updater-close', 'A7M4_UPDATER_CLOSE_SELFTEST'),
                             ('capid-service', 'CAPID_SELFTEST'),
                             ('sa-address-service', 'SA_ADDRESS_SELFTEST'),
                             ('hdmi-config', 'A7M4_HDMI_CONFIG_SELFTEST'),
                             ('dmm-service', 'DMM_SERVICE_SELFTEST'),
                             ('display-service', 'A7M4_DISPLAY_SELFTEST'),
                             ('release-inhibit', 'A7M4_RELEASE_SELFTEST'),
                             ('timecode-service', 'A7M4_TIMECODE_SELFTEST')):
            selftest = run / (name + '-selftest')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-D' + define,
                            str(HERE / (name + '.c')), '-o', str(selftest)], check=True)
            subprocess.run([str(selftest)], check=True)
        pin_test = run / 'pin-registry-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                        str(HERE / 'test-pin-registry.c'),
                        str(HERE / 'device-models/pin-registry-core.c'), '-o', str(pin_test)], check=True)
        subprocess.run([str(pin_test)], check=True)
        link_test = run / 'pin-link-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                        str(HERE / 'test-pin-link.c'), str(HERE / 'pin-link.c'),
                        str(HERE / 'device-models/pin-registry-core.c'), '-o', str(link_test)], check=True)
        subprocess.run([str(link_test)], check=True)
        if args.skip_setup:
            for source in ('test-setup-complete', 'test-date-clock'):
                selftest = run / (source + '-selftest')
                subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(HERE / (source + '.c')),
                                '-o', str(selftest)], check=True)
                subprocess.run([str(selftest)], check=True)
            for source, library in (('seed-setup-complete', 'usr/lib/libbackup.so'),
                                    ('seed-date-clock', 'usr/lib/libosal_uipc.so')):
                subprocess.run(['clang', '--target=aarch64-linux-gnu', '-std=gnu11',
                                '-fuse-ld=lld', '-nostdlib', '-O2', '-mno-outline-atomics',
                                '-Wall', '-Wextra', '-isystem', '/usr/aarch64-linux-gnu/include',
                                str(HERE / (source + '.c')), str(root / library),
                                str(root / 'lib/libc-2.25.so'), str(root / 'lib/ld-2.25.so'),
                                '-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1',
                                '-o', str(sim / source)], check=True)
        osd_test = run / 'osd-content-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                        str(HERE / 'test-headless-osd.c'), '-o', str(osd_test)], check=True)
        subprocess.run([str(osd_test)], check=True)
        gpe_test = run / 'gpe-model-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                        str(HERE / 'gpe-min/test-gpe-model.c'), str(HERE / 'gpe-min/gpe-sprite.c'),
                        '-o', str(gpe_test)], check=True)
        subprocess.run([str(gpe_test)], check=True)
        lz_test = run / 'sony-lz77-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                        str(HERE / 'test-sony-lz77.c'), str(HERE / 'sony-lz77.c'),
                        '-o', str(lz_test)], check=True)
        subprocess.run([str(lz_test)], check=True)
        dig_test = run / 'dig-level-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                        str(HERE / 'test-dig-level-device.c'), str(HERE / 'dig-level-device.c'),
                        '-o', str(dig_test)], check=True)
        subprocess.run([str(dig_test)], check=True)
        audio_test = run / 'audio-model-selftest'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread', '-DFULL_AUDIO_TEST',
                        '-DSYSTEM_SOUND_OWNER_TEST', str(HERE / 'test-a7m4-audio.c'),
                        *[str(HERE / 'device-models' / name) for name in
                          ('audio-device.c', 'audio-capabilities.c', 'sound-table.c', 'system-sound-owner.c')],
                        str(HERE / 'dmm-service.c'), '-o', str(audio_test)], check=True)
        subprocess.run([str(audio_test), str(root / 'cp/SysSound.bin')], check=True)
        build_shared(root, sim / 'ui-trace.so',
                     [HERE / name for name in ('ui-trace.c', 'hdmi-register.c',
                      'updater-close.c', 'capid-service.c', 'capid-adapter.c',
                      'sa-address-service.c', 'hdmi-config.c', 'dmm-service.c',
                      'dmm-adapter.c', 'display-service.c', 'release-inhibit.c',
                      'timecode-service.c', 'pin-adapter.c', 'pin-link.c', 'dig-level-device.c',
                      'resource-lz.c', 'sony-lz77.c', 'headless-osd.c', 'osd-adapter.c',
                      'device-models/pin-registry-core.c', 'device-models/vef-device.c',
                      'device-models/scalar-device.c', 'device-models/adf-device.c',
                      'device-models/hdmi-pin-device.c',
                      'device-models/audio-device.c', 'device-models/audio-capabilities.c',
                      'device-models/sound-table.c', 'device-models/system-sound-owner.c')])
        if args.display_model:
            subprocess.run(['clang', '--target=aarch64-linux-gnu', '-std=gnu11',
                            '-fuse-ld=lld', '-nostdlib', '-O2', '-mno-outline-atomics',
                            '-isystem', '/usr/aarch64-linux-gnu/include',
                            str(HERE / 'device-models/system-sound-owner.c'),
                            str(root / 'lib/libc-2.25.so'), str(root / 'lib/ld-2.25.so'),
                            '-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1',
                            '-o', str(sim / 'system-sound-owner')], check=True)
        if args.seed_power:
            subprocess.run(['clang', '--target=aarch64-linux-gnu', '-std=gnu11',
                            '-fuse-ld=lld', '-nostdlib', '-O2', '-mno-outline-atomics',
                            str(HERE / 'seed-power-on.c'),
                            str(root / 'usr/lib/libbackup.so'),
                            str(root / 'lib/libc-2.25.so'), str(root / 'lib/ld-2.25.so'),
                            '-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1',
                            '-o', str(sim / 'seed-power-on')], check=True)
        if args.inspect_thread:
            subprocess.run(['clang', '--target=aarch64-linux-gnu', '-std=gnu11', '-fuse-ld=lld', '-nostdlib',
                            '-O2', '-mno-outline-atomics', '-Wall', '-Wextra',
                            '-isystem', '/usr/aarch64-linux-gnu/include',
                            str(HERE / 'thread-inspect.c'),
                            str(root / 'lib/libc-2.25.so'), str(root / 'lib/ld-2.25.so'),
                            '-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1',
                            '-o', str(sim / 'thread-inspect')], check=True)
        snapshots = run / 'sources'
        snapshots.mkdir()
        for name in ('ui-trace.c', 'hdmi-register.c', 'updater-close.c',
                     'run-ui-trace.py', 'thread-inspect.c', 'seed-power-on.c',
                     'capid-service.c', 'capid-service.h', 'capid-adapter.c',
                     'sa-address-service.c', 'hdmi-config.c', 'hdmi-config.h',
                     'dmm-service.c', 'dmm-service.h', 'dmm-adapter.c',
                     'display-service.c', 'display-service.h',
                     'release-inhibit.c', 'release-inhibit.h',
                     'timecode-service.c', 'timecode-service.h',
                     'pin-adapter.c', 'test-pin-registry.c', 'pin-link.c', 'test-pin-link.c',
                     'test-a7m4-audio.c', 'dig-level-device.c', 'dig-level-device.h',
                     'test-dig-level-device.c', 'resource-lz.c', 'sony-lz77.c', 'sony-lz77.h',
                     'test-sony-lz77.c', 'bitmap_preview.py', 'headless-osd.c', 'headless-osd.h',
                     'test-headless-osd.c', 'osd-adapter.c', 'seed-setup-complete.c',
                     'test-setup-complete.c', 'seed-date-clock.c', 'test-date-clock.c'):
            shutil.copy2(HERE / name, snapshots / name)
        shutil.copytree(HERE / 'device-models', snapshots / 'device-models')
        gpe = HERE / 'gpe-min'
        shutil.copytree(gpe, snapshots / 'gpe-min')
        build_shared(root, sim / 'gpe-min.so', [gpe / name for name in
                     ('gpe-min.c', 'gpe-raster.c', 'gpe-sprite.c')])
        (root / 'etc/ld.so.preload').write_text(
            '/usr/lib/libMWF.so\n/usr/lib/libObj.so\n'
            '/simulation/gpe-min.so\n/simulation/ui-trace.so\n')
        script = ['#!/bin/busybox sh', 'echo A7M4_UI_TRACE_START']
        for command in setup_commands():
            if args.seed_power and command.startswith('insmod /usr/kmod/upm.ko'):
                script.append('/simulation/seed-power-on 1 || exit $?')
            script.append(command)
            if args.skip_setup and command.startswith('insmod /usr/kmod/udate.ko'):
                script += ['/simulation/seed-date-clock || exit $?',
                           '/simulation/seed-setup-complete || exit $?']
        if args.firmware_identity:
            script += ['mkdir -p /mnt',
                       '[ -b /dev/ram1 ] || mknod /dev/ram1 b 1 1 || exit $?',
                       'rm -f /dev/nflasha1',
                       'ln -s /dev/ram1 /dev/nflasha1 || exit $?',
                       'dd if=/simulation/firmware-identity.fat of=/dev/ram1 bs=4096 || exit $?',
                       'echo VIRTUAL_IDENTITY_READBACK',
                       'dd if=/dev/ram1 bs=4096 count=1024 | /usr/bin/openssl dgst -sha256']
        if args.hdmi_observer:
            script.append('export A7M4_HDMI_OBSERVER=1')
        if args.updater_close:
            script.append('export A7M4_UPDATER_CLOSE=1')
        if args.capid_storage:
            script.append('export A7M4_CAPID_STORAGE=1')
        if args.sa_address:
            script.append('export A7M4_SA_ADDRESS=1')
        if args.dmm_storage:
            script.append('export A7M4_DMM_STORAGE=1')
        if args.display_model:
            script += ['mkdir -p /log/qemu',
                       '/simulation/system-sound-owner || exit $?',
                       'export A7M4_DISPLAY_MODEL=1']
        if args.inspect_thread:
            script += [
                '/simulation/thread-inspect 0; echo INSPECT_BAD_TID_RC=$?',
                '(', 'sleep 10', 'for sample in 1 2 3; do',
                '  for comm in /proc/[0-9]*/task/[0-9]*/comm; do',
                '    read -r name < "$comm" || continue',
                '    case "$name" in AppFw|SeqQueueCommon|SeqQueueDispSwi|SeqThreadPool1|SeqThreadPool2|ExternalOSDIF|MPGRM_GPE0|MPGRM_GPE0_COMP|grm_gpe|uxengine_res1|uxengine_res2|IMD-RM|IMD-Default)',
                '      path=${comm%/comm}; tid=${path##*/}',
                '      echo THREAD_INSPECT_TARGET name=$name tid=$tid',
                '      /simulation/thread-inspect "$tid"',
                '    ;; esac', '  done', '  sleep 10', 'done', ') &']
        script += ['echo A7M4_TRACE_SETUP_COMPLETE',
                   "/bin/busybox taskset 1 /usr/bin/bootin.elf 'target=qemu ' dmode=3 &", 'wait']
        (sim / 'session.sh').write_text('\n'.join(script) + '\n')
        build_cpio(root, cpio)
        metadata = {'package_identity': {key: identity[key] for key in
                      ('model', 'region', 'version', 'package_sha256')},
                    'dat4_hex': version_record(identity['version']).hex(),
                    'identity_fat_sha256': digest(sim / 'firmware-identity.fat')
                        if args.firmware_identity else None,
                    'virtual_power_record': args.seed_power,
                    'hdmi_observer_model': args.hdmi_observer,
                    'thread_inspection': args.inspect_thread,
                    'empty_updater_close_model': args.updater_close,
                    'capid_storage_model': args.capid_storage,
                    'sa_address_model': args.sa_address,
                    'dmm_storage_model': args.dmm_storage,
                    'display_model': args.display_model, 'skip_setup': args.skip_setup,
                    'seconds': args.seconds,
                    'baseline': unchanged, 'cpio_sha256': digest(cpio),
                    'probe_sha256': digest(sim / 'ui-trace.so'),
                    'source_sha256': digest(HERE / 'ui-trace.c')}
        (run / 'config.json').write_text(json.dumps(metadata, indent=2) + '\n')
        print('RUN ' + str(run), flush=True)
        with Guest(run, cpio, kernel=baseline / 'system/vmlinux.bin',
                   kalimba=baseline / 'seed-setting/kemco.txt', smp=2,
                   extra_cmdline='a7m4.run=1 memrsv=0x00160000@0x41c000000:uc' +
                   (' memrsv=0x00005000@0x41c200000:uc' if args.capid_storage else '') +
                   (' fullsystem.sound-owner=1 memrsv=0x00020000@0x41d000000:uc' if args.display_model else '')) as guest:
            guest.wait_ready(180)
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                if guest.proc.poll() is not None:
                    raise RuntimeError('QEMU exited early; see ' + str(guest.console))
                time.sleep(1)
            guest.screendump(run / 'screen.ppm')
            rendered = guest.console.read_text(errors='replace')
            bitmaps = set()
            for match in re.finditer(r'GPE_MIN FILL addr=(0x[0-9a-f]+) bytes=(\d+).*?result=0', rendered):
                address, count = int(match[1], 16), int(match[2])
                if 0x410000000 <= address < 0x500000000 and 0 < count <= 0x4000000 and address + count <= 0x500000000:
                    bitmaps.add((address, count))
            for address, count in sorted(bitmaps)[:8]:
                guest.pmemsave(address, count, run / ('bitmap-%x.bin' % address))
            if args.capid_storage:
                guest.pmemsave(0x41c200000, 0x5000, run / 'capid-storage.bin')
            if args.dmm_storage:
                guest.pmemsave(0x41c000000, 0x160000, run / 'dmm-storage.bin')
            if args.display_model:
                guest.pmemsave(0x41d000000, 0x20000, run / 'system-sound-storage.bin')
        text = (run / 'console.log').read_text(errors='replace')
        facts = {'create_screen_enter': text.count('UI_TRACE CREATE_SCREEN_ENTER'),
                 'create_screen_return': text.count('UI_TRACE CREATE_SCREEN_RETURN'),
                 'gpe_fill': text.count('GPE_MIN FILL '),
                 'gpe_copy': text.count('GPE_MIN COPY '),
                 'gpe_flush': text.count('GPE_MIN FLUSH '),
                 'pin_metadata_connections': len(re.findall(r'PIN_LINK_METADATA [^\n]*request_free_rc=0', text)),
                 'pin_connection_replies_freed': len(re.findall(r'PIN_LINK_NATIVE_REPLY_FREE [^\n]*rc=0 native_reply_freed=1', text)),
                 'pin_connection_faults': text.count('PIN_LINK_FAULT ') + text.count('PIN_LINK_REQUEST_FREE_FAULT '),
                 'has_exception': '[Exception Syndrome]' in text,
                 'resource_lz_decoded': text.count('A7M4_RESOURCE_LZ_OK '),
                 'resource_lz_failed': text.count('A7M4_RESOURCE_LZ_FAIL '),
                 'osd_logged_snapshots': text.count('OSD_HEADLESS_PERSISTED '),
                 'osd_logged_completions_after_persist': text.count('OSD_HEADLESS_ACK '),
                 'osd_latest_persisted_sequence': max(map(int, re.findall(r'OSD_HEADLESS_PERSISTED\s+seq=(\d+)', text)), default=0),
                 'osd_latest_ack_sequence': max(map(int, re.findall(r'OSD_HEADLESS_ACK\s+seq=(\d+)', text)), default=0),
                 'osd_consumer_started': 'A7M4_OSD_OWNER_STARTED ' in text,
                 'osd_consumer_failed': text.count('OSD_HEADLESS_FAILED ') + text.count('OSD_HEADLESS_LEASE_FAILED ') +
                                        text.count('A7M4_OSD_START_FAILED '),
                 'capture_verified': False,
                 'setup_complete': 'A7M4_TRACE_SETUP_COMPLETE' in text,
                 'first_setup_seed_readback': 'SETUP_COMPLETED_READBACK_OK ' in text,
                 'native_clock_readback': 'DATE_CLOCK_READY ' in text,
                 'original_screen_ids': [int(value, 0) for value in re.findall(
                     r'UI_TRACE CREATE_SCREEN_ENTER [^\n]*id=(0x[0-9a-f]+|\d+)', text)]}
        facts['first_setup_bypassed'] = bool(
            args.skip_setup and facts['first_setup_seed_readback'] and facts['native_clock_readback'] and
            0x355 in facts['original_screen_ids'] and 0x12e not in facts['original_screen_ids'])
        facts['touch_guide_created'] = 0x1e9 in facts['original_screen_ids']
        facts['first_setup_fields'] = {
            field: int(value) for field, value in re.findall(
                r'SETUP_SEED_READBACK id=(0x[0-9a-f]+) bytes=1 value=(\d+)', text)}
        clocks = re.findall(r'DATE_CLOCK_READBACK bytes=48 raw=([0-9a-f]{96})', text)
        facts['native_clock_reply_hex'] = clocks[-1] if clocks else None
        facts['original_bitmaps'] = [
            {'file': path.name, 'bytes': path.stat().st_size,
             'sha256': digest(path), 'nonzero_bytes': sum(value != 0 for value in path.read_bytes())}
            for path in sorted(run.glob('bitmap-*.bin'))]
        for bitmap in facts['original_bitmaps']:
            if bitmap['bytes'] == 614400:
                path = run / bitmap['file']
                path.with_suffix('.png').write_bytes(bitmap_png(path.read_bytes()))
                bitmap['rgba4444_png'] = path.with_suffix('.png').name
        if args.firmware_identity:
            guest_sums = re.findall(r'SHA2?-?256\(stdin\)= ([0-9a-f]{64})', text)
            facts['identity_readback_verified'] = metadata['identity_fat_sha256'] in guest_sums
        if (run / 'system-sound-storage.bin').is_file():
            raw = (run / 'system-sound-storage.bin').read_bytes()
            source = (baseline / 'cp/SysSound.bin').read_bytes()
            facts['sound_qmp_readback'] = {
                'bytes': len(source), 'source_sha256': hashlib.sha256(source).hexdigest(),
                'source_matches': raw[:len(source)] == source,
                'guard_zero': not any(raw[len(source):]), 'audio_output_verified': False}
        if (run / 'capid-storage.bin').is_file():
            raw = (run / 'capid-storage.bin').read_bytes()
            facts['capid_qmp_readback'] = {
                'bytes': len(raw), 'sha256': digest(run / 'capid-storage.bin'),
                'header': list(struct.unpack_from('<5I', raw)),
                'guard_zero': not any(raw[0x4014:])}
        (run / 'evidence.json').write_text(json.dumps(facts, indent=2) + '\n')
        print(json.dumps(facts), flush=True)
        if not facts['setup_complete']:
            raise RuntimeError('guest setup failed before app launch; see console.log')
        if args.skip_setup and (not facts['first_setup_bypassed'] or facts['touch_guide_created']):
            raise RuntimeError('original UI still requires first setup or the touch guide; see console.log')
        if args.firmware_identity and not facts['identity_readback_verified']:
            raise RuntimeError('virtual identity FAT readback did not match; see console.log')
        if facts['has_exception']:
            raise RuntimeError('original guest exception preserved; see console.log')
        if args.display_model and (not facts['osd_consumer_started'] or facts['osd_consumer_failed']):
            raise RuntimeError('OSD consumer did not start or rejected an incomplete frame; see console.log')
        if args.display_model and (not facts['sound_qmp_readback']['source_matches'] or
                                   not facts['sound_qmp_readback']['guard_zero']):
            raise RuntimeError('virtual sound RAM readback mismatch; see console.log')
    except Exception as error:
        failure = {'exception': type(error).__name__, 'reason': str(error)}
        if isinstance(error, subprocess.CalledProcessError):
            failure.update(command=list(map(str, error.cmd)), returncode=error.returncode)
        (run / 'failure.json').write_text(json.dumps(failure, indent=2) + '\n')
        raise
    finally:
        for filename, expected in unchanged.items():
            if digest(Path(filename)) != expected:
                raise RuntimeError('baseline changed: ' + filename)
        if root.exists():
            shutil.rmtree(root)
        if cpio.exists():
            cpio.unlink()
        print('EVIDENCE ' + str(run), flush=True)


if __name__ == '__main__':
    main()
