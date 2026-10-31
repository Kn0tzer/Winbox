#!/usr/bin/env bash
# Rebuild the Wine 10.0 i386 win32u unix module in
# tools/rootfs32/wine10-backports-i386/ from the pinned wine-10.0
# source tarball plus the patch in
# tools/buildWine/patches/wine10-win32u-desktop-wait.patch.
# See wine10-backports-i386/README.md for why this exists.
#
# The 32-bit payload's win32u comes from the WineHQ 10.0 deb (a
# different provenance from the 64-bit set's Wine 8 source build), so
# the FD-01 remedy could not reach it: this build is the 32-bit route
# (defect register FD-02). It replaces ONLY the unix module win32u.so;
# the PE half (win32u.dll) ships untouched from the deb — the patch's
# two functions live in the unix half.
#
# Build shape: two out-of-tree builds sharing one patched source tree.
#   1. a native tools tree (winebuild/widl/wrc/wmc/winegcc for the host),
#   2. a --host=i686-linux-gnu tree that emits dlls/win32u/win32u.so.
# Everything compiles with the host's native toolchain.
#
# Freetype/fontconfig are compiled IN, deliberately: the WineHQ stock
# module's font driver dlopens libfreetype.so.6 and libfontconfig.so.1
# at runtime and the guest ships both, so a --without build (the Wine 8
# backports' shape) would silently strip font rendering from every
# 32-bit guest. The host has no i386 dev packages, so the headers and
# link libraries come from a private sysroot assembled from the pinned
# bookworm debs below (their versions match the guest's runtime
# libraries: freetype 2.12.1 is the base pipeline's own closure pin;
# fontconfig 2.14.1 is the glibc-rootfs32 payload's version). The
# configure result is checked fail-closed: HAVE_FREETYPE and both
# SONAME defines must be present, or the
# build aborts instead of shipping a quietly degraded module. Two more
# fail-closed invariants on the product: no undefined __isoc23_*
# symbols (the base build's patchelf injection set is derived by
# scanning for exactly those; a new hit would drift it), and the
# module must link ntdll.so.
#
# After it runs, re-pin the new sha256 in
# tools/rootfs64/check_runtime_completeness.py (WINE10_BACKPORT32_PINS),
# in build-wine32-base.sh (BACKPORT32_SHA), and in
# wine10-backports-i386/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine10-backports-i386"
WORK="${BW10_WORK:-/tmp/wine10-backports-build}"
PATCH="wine10-win32u-desktop-wait.patch"

TARBALL_URL="https://dl.winehq.org/wine/source/10.0/wine-10.0.tar.xz"
TARBALL_SHA="c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601"
# name|sha256|url — bookworm i386 dev+runtime debs for the sysroot.
# The last five are the two libraries' own runtime dependencies: a
# real i386 dev install has them system-wide, and configure's link
# tests need the whole DT_NEEDED chain resolvable (via -rpath-link
# into the sysroot) or the freetype test fails on undefined png/zlib
# references. (The extraction keeps the debs' pre-usrmerge layout, so
# both the usr/lib and lib sysroot dirs are named.) They never reach
# the product: win32u dlopens freetype and fontconfig at runtime and
# links neither.
SYSROOT_DEBS=(
    "libfreetype-dev_2.12.1+dfsg-5+deb12u4_i386.deb|5e691e4cd480214e33554ed00e42a90c3bc7afd245612d6021a76f2679672148|http://deb.debian.org/debian/pool/main/f/freetype/libfreetype-dev_2.12.1+dfsg-5+deb12u4_i386.deb"
    "libfreetype6_2.12.1+dfsg-5+deb12u4_i386.deb|5945f490940c8c7dd5054aa58a0fdeac5b362ce0ef648e9cce1d7b68a0914fc7|http://deb.debian.org/debian/pool/main/f/freetype/libfreetype6_2.12.1+dfsg-5+deb12u4_i386.deb"
    "libfontconfig-dev_2.14.1-4_i386.deb|54304806090234fcf34cc7a83cdfdd16eea77390a181d2419ab0622be3d37ef5|http://deb.debian.org/debian/pool/main/f/fontconfig/libfontconfig-dev_2.14.1-4_i386.deb"
    "libfontconfig1_2.14.1-4_i386.deb|f581ad1f64c1efadd4df10bb09d7e1a036c7df0db3fde325c941a149a829216f|http://deb.debian.org/debian/pool/main/f/fontconfig/libfontconfig1_2.14.1-4_i386.deb"
    "zlib1g_1.2.13.dfsg-1_i386.deb|3ca84a1245347652b7e1f8694db222851f7f1e7060515b21e1439b0cf84f96e1|http://deb.debian.org/debian/pool/main/z/zlib/zlib1g_1.2.13.dfsg-1_i386.deb"
    "libpng16-16_1.6.39-2+deb12u5_i386.deb|6b18ebe0faab22a7cb6d031bc3dffb9c572277c6501ca7b51fbe480a90b2b4da|http://deb.debian.org/debian/pool/main/libp/libpng1.6/libpng16-16_1.6.39-2+deb12u5_i386.deb"
    "libbrotli1_1.0.9-2+b6_i386.deb|75b14942a601dbac638595a9d827f91c12a9f95fb7503f2dc5471fcf57bd4bc1|http://deb.debian.org/debian/pool/main/b/brotli/libbrotli1_1.0.9-2+b6_i386.deb"
    "libbz2-1.0_1.0.8-5+b1_i386.deb|275c8f3e8d82f0c58f286e7ec0ca3001380895906be1f1f5b4d929f4469a54a2|http://deb.debian.org/debian/pool/main/b/bzip2/libbz2-1.0_1.0.8-5+b1_i386.deb"
    "libexpat1_2.5.0-1+deb12u2_i386.deb|002fa75e3acfa317aee466c83e4f709db027d25110d2c548fd9483e196e3659d|http://deb.debian.org/debian/pool/main/e/expat/libexpat1_2.5.0-1+deb12u2_i386.deb"
)

for tool in curl dpkg-deb make flex bison i686-linux-gnu-gcc pkg-config git readelf sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine10-win32u: required host tool '$tool' not found" >&2; exit 1; }
done

fetch() { # file sha url
    if [ ! -f "$WORK/cache/$1" ] || [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" != "$2" ]; then
        curl -fsL -o "$WORK/cache/$1" "$3"
    fi
    [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" = "$2" ] || {
        echo "build-wine10-win32u: hash mismatch for $1" >&2; exit 1; }
}

mkdir -p "$WORK/cache"
fetch "wine-10.0.tar.xz" "$TARBALL_SHA" "$TARBALL_URL"
for row in "${SYSROOT_DEBS[@]}"; do
    IFS='|' read -r name sha url <<<"$row"
    fetch "$name" "$sha" "$url"
done

# --- patched source tree ---
rm -rf "$WORK/wine"
mkdir -p "$WORK/wine"
tar -xf "$WORK/cache/wine-10.0.tar.xz" -C "$WORK/wine" --strip-components=1
cd "$WORK/wine"
git apply "$REPO/tools/buildWine/patches/$PATCH"
echo "--- applied $PATCH ---"

# --- sysroot: i386 freetype/fontconfig headers + link libraries ---
SYSROOT="$WORK/sysroot"
rm -rf "$SYSROOT"
for row in "${SYSROOT_DEBS[@]}"; do
    IFS='|' read -r name _ _ <<<"$row"
    dpkg-deb -x "$WORK/cache/$name" "$SYSROOT"
done
PCDIR="$SYSROOT/usr/lib/i386-linux-gnu/pkgconfig"
# The Debian .pc files point at /usr (freetype2.pc with literal paths);
# retarget them at the sysroot and drop the private-requires noise
# (only the two direct libraries are linked; their own DT_NEEDEDs are
# the guest's business, exactly as with the stock module).
sed -i \
    -e "s|^prefix=.*|prefix=$SYSROOT/usr|" \
    -e "s|^exec_prefix=.*|exec_prefix=$SYSROOT/usr|" \
    -e "s|^libdir=.*|libdir=$SYSROOT/usr/lib/i386-linux-gnu|" \
    -e "s|^includedir=.*|includedir=$SYSROOT/usr/include|" \
    -e "s|^Requires.private:.*|Requires.private:|" \
    -e "s|-L/usr/lib/i386-linux-gnu|-L$SYSROOT/usr/lib/i386-linux-gnu|g" \
    "$PCDIR/freetype2.pc"
sed -i \
    -e "s|^prefix=.*|prefix=$SYSROOT/usr|" \
    -e "s|^Requires.private:.*|Requires.private:|" \
    "$PCDIR/fontconfig.pc"
mkdir -p "$WORK/bin"
{
    echo '#!/bin/sh'
    echo "export PKG_CONFIG_LIBDIR=$PCDIR"
    echo 'exec /usr/bin/pkg-config "$@"'
} > "$WORK/bin/i686-linux-gnu-pkg-config"
chmod +x "$WORK/bin/i686-linux-gnu-pkg-config"
export PATH="$WORK/bin:$PATH"
i686-linux-gnu-pkg-config --modversion freetype2 fontconfig >/dev/null || {
    echo "build-wine10-win32u: sysroot pkg-config probe failed" >&2; exit 1; }

WITHOUTS="--without-x --without-gnutls --without-gstreamer --without-dbus \
--without-sdl --without-vulkan --without-opengl --without-osmesa \
--without-alsa --without-pulse --without-cups --without-udev \
--without-usb --without-sane --without-gphoto --without-krb5 \
--without-ldap --without-openal --without-pcap --without-v4l2"

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

# 2. i686 tree: win32u.so only, freetype + fontconfig compiled in
mkdir -p "$WORK/i686-build"
cd "$WORK/i686-build"
if [ ! -f Makefile ]; then
    # shellcheck disable=SC2086
    CFLAGS="-O1" LDFLAGS="-Wl,-rpath-link,$SYSROOT/usr/lib/i386-linux-gnu -Wl,-rpath-link,$SYSROOT/lib/i386-linux-gnu" \
        "$WORK/wine/configure" --host=i686-linux-gnu \
        --with-wine-tools="$WORK/tools-build" --disable-tests \
        $WITHOUTS > configure.log 2>&1
fi
for marker in HAVE_FREETYPE SONAME_LIBFREETYPE SONAME_LIBFONTCONFIG; do
    grep -q "#define $marker" include/config.h || {
        echo "build-wine10-win32u: configure did not define $marker — refusing to ship a degraded module" >&2
        exit 1; }
done
make -j"$(nproc)" dlls/win32u/win32u.so > make.log 2>&1

mkdir -p "$OUT"
cp "$WORK/i686-build/dlls/win32u/win32u.so" "$OUT/win32u.so"
if readelf --dyn-syms -W "$OUT/win32u.so" | grep " UND " | grep -q "__isoc23"; then
    echo "build-wine10-win32u: built module carries undefined __isoc23_* symbols — would drift the base build's injection set" >&2
    exit 1
fi
readelf -dW "$OUT/win32u.so" | grep -q "NEEDED.*ntdll\.so" || {
    echo "build-wine10-win32u: built module does not link ntdll.so" >&2; exit 1; }
echo "--- built win32u.so (i386 unix half, freetype + fontconfig in) ---"

echo "=== sha256 (re-pin in check_runtime_completeness.py, build-wine32-base.sh + README.md) ==="
sha256sum "$OUT/win32u.so"
