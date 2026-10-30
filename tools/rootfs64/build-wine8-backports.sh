#!/usr/bin/env bash
# Rebuild the four Wine 8.0 loader-backport DLLs in
# tools/rootfs64/wine8-backports-x86_64/ from the pinned wine-8.0 source
# tag plus the three backport patches in tools/buildWine/patches/.
# See wine8-backports-x86_64/README.md for why these exist.
#
# Build shape: two out-of-tree builds sharing one patched source tree.
#   1. a native tools tree (winebuild/widl/wrc/wmc/winegcc for the host),
#   2. a --host=x86_64-w64-mingw32 tree that emits only PE binaries.
# Everything compiles with the host's native toolchain — deliberately NOT
# in the linux/amd64 container, whose binfmt/QEMU layer crashes on this
# ARM64 host (see stage-wine64-payloads.sh's header).
#
# After it runs, re-pin the new sha256 in
# tools/rootfs64/check_runtime_completeness.py (WINE8_BACKPORT_PINS) and
# in wine8-backports-x86_64/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine8-backports-x86_64"
WORK="${BW8_WORK:-/tmp/wine8-backports-build}"
PATCHES=(
    wine8-user32-pointer-family.patch
    wine8-kernelbase-copyfile2.patch
    wine8-iphlpapi-anycast-table.patch
)
DLLS=(user32 kernelbase kernel32 iphlpapi)

for tool in git make flex bison x86_64-w64-mingw32-gcc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine8-backports: required host tool '$tool' not found" >&2; exit 1; }
done

mkdir -p "$WORK"
if [ ! -d "$WORK/wine" ]; then
    git clone --depth 1 --branch wine-8.0 https://github.com/wine-mirror/wine.git "$WORK/wine"
fi
cd "$WORK/wine"
git checkout -- .  # pristine tag state before (re)applying the patches
for p in "${PATCHES[@]}"; do
    git apply "$REPO/tools/buildWine/patches/$p"
    echo "--- applied $p ---"
done

# 1. native tools tree
mkdir -p "$WORK/tools-build"
cd "$WORK/tools-build"
if [ ! -f Makefile ]; then
    "$WORK/wine/configure" --disable-tests --without-x --without-freetype \
        --without-fontconfig --without-dbus --without-gnutls --without-udev \
        --without-sdl --without-vulkan --without-opengl > configure.log 2>&1
fi
make -j"$(nproc)" tools/winebuild/winebuild tools/widl/widl tools/wrc/wrc \
    tools/wmc/wmc tools/winegcc/winegcc > make.log 2>&1
echo "--- native build tools ready ---"

# 2. mingw cross tree (PE binaries only)
mkdir -p "$WORK/x64-build"
cd "$WORK/x64-build"
if [ ! -f Makefile ]; then
    CFLAGS="-O1" "$WORK/wine/configure" --host=x86_64-w64-mingw32 \
        --with-wine-tools="$WORK/tools-build" --disable-tests \
        --without-freetype --without-fontconfig --without-gnutls \
        --without-gstreamer --without-dbus --without-sdl --without-vulkan \
        --without-opengl --without-osmesa --without-alsa --without-pulse \
        --without-cups --without-udev --without-usb --without-sane \
        --without-gphoto --without-krb5 --without-ldap --without-openal \
        --without-pcap --without-v4l2 --without-x > configure.log 2>&1
fi
targets=(); for d in "${DLLS[@]}"; do targets+=("dlls/$d/$d.dll"); done
make -j"$(nproc)" "${targets[@]}" > make.log 2>&1

mkdir -p "$OUT"
for d in "${DLLS[@]}"; do
    cp "$WORK/x64-build/dlls/$d/$d.dll" "$OUT/$d.dll"
    echo "--- built $d.dll ---"
done
echo "=== sha256 (re-pin these in check_runtime_completeness.py + README.md) ==="
sha256sum "$OUT"/*.dll
