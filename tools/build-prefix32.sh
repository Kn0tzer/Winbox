#!/bin/bash
# Build a real 32-bit Wine prefix for Boxedwine64's browser runtime.
# Runs the staged 32-bit Wine natively (via qemu-i386 on aarch64) to generate
# an arch-correct (x86) prefix, then packages the minimal structure that the
# browser launcher expects (mirroring prefix64.zip's layout).
#
# Prerequisites (on the build box):
#   /home/ubuntu/wine32-stage/stage            - staged Debian wine32 8.0
#   /home/ubuntu/wine32-stage/rootfs32         - 32-bit glibc rootfs (ld-linux.so.2)
#   /home/ubuntu/wine32-stage/img386/usr/i686-w64-mingw32/lib/zlib1.dll
#   /lib/ld-linux.so.2 -> rootfs32/lib/ld-linux.so.2 (for the wine ELF INTERP)
#
# Output: web/runtime/prefix32.zip
set -e

STAGE=/home/ubuntu/wine32-stage/stage
ROOTFS32=/home/ubuntu/wine32-stage/rootfs32
WORK=/tmp/build-prefix32
PREFIX=$WORK/prefix
W32ROOT=$WORK/wine32root
OUT_ZIP=/tmp/prefix32.zip

rm -rf "$WORK"
mkdir -p "$WORK"

# 1. Mirror the /usr layout the wine binary expects (it resolves paths
#    relative to its own location).
mkdir -p "$W32ROOT/usr/lib" "$W32ROOT/usr/share"
ln -sfn "$STAGE/usr/lib/wine" "$W32ROOT/usr/lib/wine"
ln -sfn "$STAGE/usr/lib/i386-linux-gnu" "$W32ROOT/usr/lib/i386-linux-gnu"
ln -sfn "$STAGE/usr/share/wine" "$W32ROOT/usr/share/wine"
# Wine looks for "wineserver"; Debian ships it as "wineserver32".
ln -sfn "$STAGE/usr/lib/wine/wineserver32" "$W32ROOT/usr/lib/wine/wineserver"
# Debian wine32 omits zlib1.dll but its user32.dll imports it; stage it so
# wineboot can complete.
cp "$STAGE/usr/lib/i386-linux-gnu/wine/i386-windows/zlib1.dll" \
   "$W32ROOT/usr/lib/i386-linux-gnu/wine/i386-windows/zlib1.dll" 2>/dev/null || {
    cp /home/ubuntu/wine32-stage/img386/usr/i686-w64-mingw32/lib/zlib1.dll \
       "$W32ROOT/usr/lib/i386-linux-gnu/wine/i386-windows/zlib1.dll"
}

export LD_LIBRARY_PATH="$ROOTFS32/lib/i386-linux-gnu:$W32ROOT/usr/lib/i386-linux-gnu/wine/i386-unix"

# 2. First wineboot pass: creates the prefix; programs fail on missing
#    zlib1.dll in the fresh system32, so stage it there and re-run.
WINEPREFIX="$PREFIX" WINEARCH=win32 \
WINEDLLPATH="$W32ROOT/usr/lib/i386-linux-gnu/wine" \
"$W32ROOT/usr/lib/wine/wine" wineboot --init >/dev/null 2>&1 || true
cp "$W32ROOT/usr/lib/i386-linux-gnu/wine/i386-windows/zlib1.dll" \
   "$PREFIX/drive_c/windows/system32/"

# 3. Second wineboot pass: completes initialization.
WINEPREFIX="$PREFIX" WINEARCH=win32 \
WINEDLLPATH="$W32ROOT/usr/lib/i386-linux-gnu/wine" \
"$W32ROOT/usr/lib/wine/wine" wineboot --init 2>&1 | tail -3

# 4. Package the minimal browser prefix: registry hives (arch-correct x86),
#    dosdevices links, and drive_c/windows/system32.link -> the wine install
#    dir inside wine32.zip (same approach as prefix64.zip). The full 589MB
#    native prefix is NOT shipped; system32 resolves via the .link at runtime.
PKG=$WORK/pkg
mkdir -p "$PKG/home/username/.wine/dosdevices" \
         "$PKG/home/username/.wine/drive_c/windows"
cp "$PREFIX/system.reg" "$PREFIX/user.reg" "$PREFIX/userdef.reg" \
   "$PKG/home/username/.wine/"
printf "disable\n" > "$PKG/home/username/.wine/.update-timestamp"
printf "../drive_c" > "$PKG/home/username/.wine/dosdevices/c(_colon_).link"
printf "/" > "$PKG/home/username/.wine/dosdevices/z(_colon_).link"
printf "/usr/lib/i386-linux-gnu/wine/i386-windows" \
    > "$PKG/home/username/.wine/drive_c/windows/system32.link"

# Bundle the 32-bit deskpin.exe (desktop pin for x86 sessions; the 64-bit
# deskpin.exe cannot run under 32-bit wine). The launcher spawns it as
# Z:\home\username\deskpin32.exe in ?arch=x86 mode.
DESKPIN32="$HERE/gltest/deskpin32.exe"
if [ -f "$DESKPIN32" ]; then
  cp "$DESKPIN32" "$PKG/home/username/deskpin32.exe"
  echo "  + bundled deskpin32.exe"
else
  echo "  NOTE: $DESKPIN32 missing - x86 sessions will have no desktop pin." >&2
fi

# 5. Zip it.
rm -f "$OUT_ZIP"
(cd "$PKG" && zip -q -r "$OUT_ZIP" home)
echo "Built $OUT_ZIP ($(stat -c%s "$OUT_ZIP") bytes)"
grep -q '"PROCESSOR_ARCHITECTURE"="x86"' "$PKG/home/username/.wine/system.reg" \
    && echo "Registry arch: x86 OK" || echo "WARNING: arch key missing"
