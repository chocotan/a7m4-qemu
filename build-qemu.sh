#!/usr/bin/env bash
# Offline build of the patched QEMU used by this package.
# The pinned Sony board/input patches come from this directory; the base archive
# must be supplied locally (no download).
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
stage_only=0
if [[ "${1:-}" == --stage-only ]]; then
  stage_only=1
  shift
fi
if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
  cat <<'USAGE'
Usage: ./build-qemu.sh [--stage-only] [SOURCE_ARCHIVE]

Builds qemu-system-aarch64 with the Sony board/input patches and the host libfdt
used for DTB patching. The archive defaults to source/vendor/qemu-11.1.2.tar.xz.
Nothing is downloaded; the base tarball identity is verified against
source/manifest.json.
USAGE
  exit 0
fi
archive_input="${1:-$here/source/vendor/qemu-11.1.2.tar.xz}"
[[ -f "$archive_input" ]] || {
  echo "Missing QEMU source archive. Download https://download.qemu.org/qemu-11.1.2.tar.xz into source/vendor/." >&2
  exit 2
}
for tool in python3 bwrap patch ninja cc pkg-config flock make flex bison; do
  command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 2; }
done
pkg-config --exists gtk+-3.0 sdl2 pixman-1 glib-2.0 libseccomp || {
  echo 'GTK3, SDL2, pixman, glib and libseccomp development files are required.' >&2; exit 2;
}
mkdir -p "$here/source/build" "$here/runtime/tmp"
exec 9>>"$here/runtime/tmp/.build.lock"
flock 9
stage="$(mktemp -d "$here/source/build/qemu-XXXXXX")"
python3 - "$here" "$(realpath -- "$archive_input")" "$stage" <<'PY'
import hashlib, json, shutil, sys, tarfile
from pathlib import Path
root, archive, stage = map(Path, sys.argv[1:])
manifest = json.loads((root / 'source/manifest.json').read_text())
actual = hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
if actual != manifest['base_sha256']:
    raise SystemExit('QEMU base archive identity differs from source/manifest.json')
def qemu_filter(member, destination):
    # Unused EDK2 host-tool link; never bind an archive link to the host filesystem.
    if (member.name == 'qemu-11.1.2/roms/edk2/EmulatorPkg/Unix/Host/X11IncludeHack'
            and member.issym() and member.linkname == '/opt/X11/include'):
        print('SKIP_UNUSED_EDK2_ABSOLUTE_LINK', member.name)
        return None
    return tarfile.data_filter(member, destination)
with tarfile.open(archive) as source:
    source.extractall(stage, filter=qemu_filter)
# The vendored dtc is identity-checked so the DTB tooling cannot drift silently.
dtc = root / 'source/dtc'
expected = manifest['dtc_source_files']
actual_files = {p.relative_to(dtc).as_posix() for p in dtc.rglob('*') if p.is_file()}
if actual_files != set(expected):
    raise SystemExit('DTC source inventory differs')
for name, sha in expected.items():
    path = dtc / name
    if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != sha:
        raise SystemExit('DTC source identity differs: ' + name)
shutil.copytree(dtc, stage / 'qemu-11.1.2/subprojects/dtc', dirs_exist_ok=True)
PY
src="$stage/qemu-11.1.2"
for patch in qemu-sony-native.patch qemu-directory-sd.patch qemu-sony-usb.patch; do
  echo "APPLY $patch"
  patch --batch -p1 -d "$src" < "$here/source/$patch"
done
mkdir -p "$stage/build" "$stage/tmp" "$stage/cache"
bwrap --unshare-net --die-with-parent --clearenv --ro-bind / / \
  --bind "$stage" "$stage" --dev /dev --proc /proc --chdir "$stage/build" \
  --setenv PATH /usr/bin:/bin --setenv LC_ALL C --setenv TMPDIR "$stage/tmp" \
  --setenv XDG_CACHE_HOME "$stage/cache" --setenv PYTHONDONTWRITEBYTECODE 1 \
  --setenv PIP_NO_INDEX 1 --setenv PIP_NO_CACHE_DIR 1 \
  /bin/bash -c 'set -e
    "$1/configure" --target-list=aarch64-softmmu --without-default-features \
      --enable-tcg --enable-gtk --enable-sdl --enable-pixman --enable-seccomp \
      --enable-fdt=internal --disable-opengl --disable-download --disable-docs \
      --disable-containers --disable-werror --disable-tools --enable-vvfat --enable-qcow1
    ninja -j"$3" qemu-system-aarch64
    cc -shared -fPIC -O2 -I "$1/subprojects/dtc/libfdt" \
      "$1"/subprojects/dtc/libfdt/*.c -Wl,-soname,libfdt.so.1 -o libfdt.so.1
    make -C "$1/subprojects/dtc" -j"$3" NO_PYTHON=1 NO_YAML=1 EXTRA_CFLAGS=-Wno-error dtc
  ' build "$src" "$(nproc)"
if [[ "$stage_only" == 1 ]]; then
  printf 'STAGED_QEMU_BINARY=%s\n' "$stage/build/qemu-system-aarch64"
  exit 0
fi
base="$here/runtime/sources/qemu-11.1.2"
mkdir -p "$base/build-sony-ui" "$here/runtime/sources/qemu-tools/usr/lib" \
  "$here/runtime/sources/qemu-tools/usr/bin"
install -m 0755 "$stage/build/qemu-system-aarch64" "$base/build-sony-ui/qemu-system-aarch64.new"
install -m 0644 "$stage/build/libfdt.so.1" "$here/runtime/sources/qemu-tools/usr/lib/libfdt.so.1.new"
install -m 0755 "$src/subprojects/dtc/dtc" "$here/runtime/sources/qemu-tools/usr/bin/dtc.new"
mv -f "$base/build-sony-ui/qemu-system-aarch64.new" "$base/build-sony-ui/qemu-system-aarch64"
mv -f "$here/runtime/sources/qemu-tools/usr/lib/libfdt.so.1.new" "$here/runtime/sources/qemu-tools/usr/lib/libfdt.so.1"
mv -f "$here/runtime/sources/qemu-tools/usr/bin/dtc.new" "$here/runtime/sources/qemu-tools/usr/bin/dtc"
"$base/build-sony-ui/qemu-system-aarch64" -display help | grep -x gtk
printf 'QEMU built. Source/build logs remain in %s\n' "$stage"
