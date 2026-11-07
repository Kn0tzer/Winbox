#!/usr/bin/env bash
# Rebuild the Wine 8.0 backport DLLs in
# tools/rootfs64/wine8-backports-x86_64/ from the pinned wine-8.0 source
# tag plus the backport patches in tools/buildWine/patches/.
# See wine8-backports-x86_64/README.md for why these exist.
#
# Build shape: three out-of-tree builds sharing one patched source tree.
#   1. a native tools tree (winebuild/widl/wrc/wmc/winegcc for the host),
#   2. a --host=x86_64-w64-mingw32 tree that emits only PE binaries,
#   3. a --host=x86_64-linux-gnu tree that emits the unix half of win32u
#      (win32u.so). The desktop-wait patch's functions live in the unix
#      half: a win32u.dll rebuilt without its win32u.so ships the patch
#      in no binary at all (FD-01, reopened 2026-10-31 per FD-43).
#      This tree is configured WITH freetype and fontconfig: win32u's
#      font driver and fontconfig enumeration live in the unix half,
#      and a win32u.so built --without-freetype --without-fontconfig
#      leaves GDI with no enumerable or realizable fonts at all in the
#      guest (font files on disk do not help; EnumFontFamiliesEx
#      returns zero families and GetTextMetrics fails), which is what
#      starved Wine Gecko into its 'unable to find a usable font'
#      abort (FD-65). The build host needs the x86_64 freetype and
#      fontconfig development files visible to configure (system
#      packages, or FREETYPE_CFLAGS/FREETYPE_LIBS and
#      FONTCONFIG_CFLAGS/FONTCONFIG_LIBS pointing at a sysroot); the
#      post-build check below fails loudly if the products are not in
#      the binary. With a sysroot, also export
#      LDFLAGS="-Wl,-rpath-link,<sysroot>/usr/lib/x86_64-linux-gnu":
#      without it the freetype link test fails on libfreetype's
#      transitive libpng16 ("checking for -lfreetype... not found")
#      even with the FREETYPE_* variables set, and configure aborts
#      (seen on the 2026-11-06 batch-9 rebuild; the FD-168 lane's
#      sysroot build carried that LDFLAGS). The guest runtime already carries the matching
#      runtime libraries (libfreetype.so.6, libfontconfig.so.1 and
#      their dependency chain in the prefix64 overlay).
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
    wine8-win32u-desktop-wait.patch
    wine8-fd167-contracts.patch
    wine8-win32u-fd168-backports.patch
    wine8-win32u-fd168-slice2.patch
    wine8-win32u-fd168-slice3.patch
    wine8-msvcrt-fd169-crt.patch
    wine8-fd169r-crt2.patch
    wine8-fd170-contracts.patch
    wine8-fd171-contracts.patch
)
DLLS=(msvcp140_2 msvcp140_atomic_wait user32 kernelbase kernel32 iphlpapi win32u msvcp140 setupapi ucrtbase msvcrt msvcr70 msvcr71 msvcr80 msvcr90 msvcr100 msvcr110 msvcr120 bluetoothapis winusb netutils propsys ntdll cfgmgr32 pdh combase mfplat cryptext d2d1 hid mf mscms prntvpt sspicli vcomp vcomp90 vcomp100 vcomp110 vcomp120 vcomp140 windows.networking wintypes wofutil)

for tool in git make flex bison x86_64-w64-mingw32-gcc x86_64-linux-gnu-gcc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine8-backports: required host tool '$tool' not found" >&2; exit 1; }
done

mkdir -p "$WORK"
if [ ! -d "$WORK/wine" ]; then
    git clone --depth 1 --branch wine-8.0 https://github.com/wine-mirror/wine.git "$WORK/wine"
fi
cd "$WORK/wine"
git checkout -- .  # pristine tag state before (re)applying the patches
# checkout alone does not remove files a patch ADDED on a previous run
# (the FD-170 patch adds headers + DLL sources); a re-run would then die
# in git apply with "already exists". Clean the two patched areas so the
# reset really is pristine. This tree is a build-scratch clone by
# design; nothing else may live in it.
git clean -fdq -- dlls include
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
# Wave 1 first: combase's FD-171 RoFailFast* entry points call
# RaiseFailFastException, resolved from kernelbase's import library
# without a declared import edge, so the import-library providers
# must exist before combase links.
make -j"$(nproc)" dlls/kernelbase/kernelbase.dll dlls/kernel32/kernel32.dll dlls/ntdll/ntdll.dll > make.log 2>&1
make -j"$(nproc)" "${targets[@]}" >> make.log 2>&1

mkdir -p "$OUT"
for d in "${DLLS[@]}"; do
    cp "$WORK/x64-build/dlls/$d/$d.dll" "$OUT/$d.dll"
    echo "--- built $d.dll ---"
done

# The mingw cross tree links these as plain PE files, but they ship as
# Wine builtins: a full Wine build has winebuild stamp the 32-byte
# "Wine builtin DLL" signature over the DOS stub at offset 64
# (make_builtin_files in tools/winebuild/spec32.c), and both the
# wineserver and the unix loader key the builtin treatment off that
# signature -- for the syscall DLL (win32u) the loader only registers
# its syscall table, and thereby user32 session initialization, when
# the signature is present. An unstamped win32u.dll loads fine yet
# every process's user32 initialization silently does nothing
# (FD-40). Stamp every rebuilt DLL the same way winebuild does.
python3 - "$OUT" "${DLLS[@]}" <<'PYEOF'
import struct, sys
out = sys.argv[1]
sig = b"Wine builtin DLL" + b"\0" * 16
assert len(sig) == 32
for name in sys.argv[2:]:
    path = out + "/" + name + ".dll"
    with open(path, "r+b") as f:
        head = f.read(64)
        assert head[:2] == b"MZ", path
        (e_lfanew,) = struct.unpack("<I", head[0x3C:0x40])
        assert e_lfanew >= 64 + len(sig), (path, e_lfanew)
        f.seek(64)
        f.write(sig)
    print("--- stamped Wine builtin signature: " + name + ".dll ---")
PYEOF
# 3. x86_64-linux-gnu cross tree (ELF unix modules only): build the
# unix half of win32u from the same patched source tree as the PE half
# above, so the two halves of the desktop-wait fix can never drift.
mkdir -p "$WORK/x64linux-build"
cd "$WORK/x64linux-build"
if [ ! -f Makefile ]; then
    CFLAGS="-O1" "$WORK/wine/configure" --host=x86_64-linux-gnu --enable-win64 \
        --with-wine-tools="$WORK/tools-build" --disable-tests \
        --without-gnutls \
        --without-gstreamer --without-dbus --without-sdl --without-vulkan \
        --without-opengl --without-osmesa --without-alsa --without-pulse \
        --without-cups --without-udev --without-usb --without-sane \
        --without-gphoto --without-krb5 --without-ldap --without-openal \
        --without-pcap --without-v4l2 --without-x > configure.log 2>&1
fi
make -j"$(nproc)" dlls/win32u/win32u.so > make-unix.log 2>&1
# FD-65 guard: the unix half must actually carry the font driver and
# the fontconfig enumerator (both are dlopen'ed by SONAME, so the
# literals are in the binary when support is compiled in). A build
# that silently dropped them must fail here, not ship.
for soname in libfreetype.so libfontconfig.so; do
    if ! grep -a -q "$soname" "$WORK/x64linux-build/dlls/win32u/win32u.so"; then
        echo "build-wine8-backports: win32u.so is missing $soname support (FD-65 guard)" >&2
        exit 1
    fi
done
echo "--- win32u.so carries freetype + fontconfig support (FD-65 guard) ---"
cp "$WORK/x64linux-build/dlls/win32u/win32u.so" "$OUT/win32u.so"
echo "--- built win32u.so (unix half) ---"

echo "=== sha256 (re-pin these in check_runtime_completeness.py + README.md) ==="
sha256sum "$OUT"/*.dll "$OUT"/win32u.so
