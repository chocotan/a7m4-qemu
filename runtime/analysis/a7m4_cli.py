#!/usr/bin/env python3
"""Command line entry point for the ILCE-7M4 QEMU runtime."""
import argparse
import json
import shutil
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
KIT = Path(__file__).resolve().parent
ROOT = KIT.parent.parent
sys.path.insert(0, str(KIT))

import a7m4_import
import a7m4_qemu
import a7m4_session


def default_bodydata():
    for candidate in (ROOT / 'incoming/bodydata/BODYDATA.DAT',
                      ROOT / 'BODYDATA.DAT', Path.cwd() / 'BODYDATA.DAT'):
        if candidate.is_file():
            return candidate
    return None


def need_prepared():
    rootfs = a7m4_import.ANALYSIS / 'menu-root'
    cpio = a7m4_import.ANALYSIS / 'menu-root.cpio'
    if not (rootfs / 'init').is_file() or not cpio.is_file():
        raise SystemExit('runtime not prepared; run: ./start.sh prepare --bodydata <BODYDATA.DAT>')
    return rootfs, cpio


def cmd_prepare(args):
    bodydata = args.bodydata or default_bodydata()
    if bodydata is None:
        raise SystemExit('BODYDATA.DAT not found; pass --bodydata')
    if args.check:
        identity, _, stream, temporary = a7m4_import.read_package(bodydata)
        stream.close()
        shutil.rmtree(temporary)
        print('A7M4_PACKAGE_OK model=%#x version=%s sha256=%s' %
              (identity.model, identity.version, identity.sha))
        return
    a7m4_import.prepare(Path(bodydata), a7m4_import.ANALYSIS / 'native-assets.json')


def cmd_shell(args):
    _, cpio = need_prepared()
    run_dir = a7m4_import.ANALYSIS / 'menu-runs' / time.strftime('shell-%Y%m%d-%H%M%S')
    with a7m4_qemu.Guest(run_dir, cpio, smp=1) as guest:
        guest.wait_ready(timeout=args.timeout)
        print('GUEST_READY ' + str(run_dir), flush=True)
        for command in args.command:
            output, rc = guest.run(command, timeout=args.timeout)
            sys.stdout.write(output.decode('utf-8', 'replace'))
            sys.stdout.flush()
            print('[rc=%d]' % rc, flush=True)


def cmd_app(args):
    _, cpio = need_prepared()
    run_dir = a7m4_import.ANALYSIS / 'menu-runs' / time.strftime('app-%Y%m%d-%H%M%S')
    run_dir.mkdir(parents=True, exist_ok=True)
    display = args.display or ('gtk,gl=off' if args.window else 'none')
    with a7m4_qemu.Guest(run_dir, cpio, smp=args.smp, extra_cmdline='a7m4.run=1',
                         display=display) as guest:
        guest.wait_ready(timeout=args.timeout)
        stream = a7m4_session.watch(guest, seconds=args.seconds)
        facts = a7m4_session.evidence(stream)
        shot = run_dir / 'screen.png'
        guest.screendump(run_dir / 'screen.ppm')
        a7m4_qemu.ppm_to_png(run_dir / 'screen.ppm', shot)
        facts['screenshot'] = str(shot)
        facts['console'] = str(run_dir / 'console.log')
        a7m4_session.write_evidence(run_dir / 'evidence.json', facts,
                                    {'run_dir': str(run_dir), 'smp': args.smp})
    print(json.dumps({k: facts[k] for k in
                      ('has_grm_area', 'has_uipc_warning', 'has_liro_symbol_error',
                       'has_crash_dump', 'has_abort_or_fault', 'has_create_screen',
                       'bytes')}, indent=2))
    print('SCREENSHOT ' + str(shot))
    print('EVIDENCE ' + str(run_dir / 'evidence.json'))


def cmd_status(args):
    manifest = a7m4_import.ANALYSIS / 'native-assets.json'
    if not manifest.is_file():
        raise SystemExit('not prepared yet')
    data = json.loads(manifest.read_text())
    print('model=%s version=%s package_sha256=%s' %
          (data['model'], data['version'], data['package_sha256']))
    print('libimdb_sha256=%s' % data['libimdb_sha256'])
    print('removed_libs=%s' % ', '.join(data['removed_libs']))
    print('removed_modules=%s' % ', '.join(data['removed_modules']))
    print('cpio=%s bytes=%d sha256=%s' % (data['cpio']['path'], data['cpio']['size'],
                                          data['cpio']['sha256']))


def main():
    parser = argparse.ArgumentParser(description='ILCE-7M4 offline QEMU runtime')
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser('prepare', help='import the official BODYDATA package')
    p.add_argument('--bodydata', type=Path)
    p.add_argument('--check', action='store_true', help='only verify the package identity')
    p.set_defaults(func=cmd_prepare)

    p = sub.add_parser('shell', help='boot the guest and run shell commands')
    p.add_argument('--command', action='append', default=[])
    p.add_argument('--timeout', type=int, default=300)
    p.set_defaults(func=cmd_shell)

    p = sub.add_parser('app', help='load the camera modules and start bootin.elf')
    p.add_argument('--seconds', type=int, default=45)
    p.add_argument('--smp', type=int, default=2)
    p.add_argument('--timeout', type=int, default=300)
    p.add_argument('--window', action='store_true', help='open the native QEMU window (GTK)')
    p.add_argument('--display', default='', help='override the QEMU display backend')
    p.set_defaults(func=cmd_app)

    p = sub.add_parser('status', help='show the imported runtime identity')
    p.set_defaults(func=cmd_status)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
