#!/usr/bin/env bash
# Rebuild the Wine 10.0 i386 msvcp140_2 and msvcp140_atomic_wait DLLs
# carrying the FD-169 remainder implementations in
# tools/rootfs32/wine10-fd169r-i386/ from the pinned wine-10.0 source
# tarball plus the patch in
# tools/buildWine/patches/wine10-fd169r-crt2.patch.
#
# msvcp140_2 gains the C++17 special mathematical functions
# (__std_smf_*): hand-written implementations validated against a
# 50-digit mpmath reference grid. msvcp140_atomic_wait gains the
# execution wait/wake pair, the CRT allocation pair and the tzdb
# deleters, backported from upstream Wine (post-10.0); the tzdb
# getters stay stubs because the browser build has no time-zone
# database to answer them with (see the FD-169 remainder record).
#
# Build shape mirrors build-wine10-fd169.sh: a native tools tree and
# an out-of-tree cross build sharing one patched source tree; the
# cross build targets i686-w64-mingw32 because these are PE modules.
# No sysroot is needed: PE DLLs link only against import libraries of
# other PE DLLs.
#
# After it runs, re-pin the new sha256 values in
# tools/rootfs64/check_runtime_completeness.py (WINE10_FD169R_PINS32),
# in build-wine32-base.sh (FD169R_SHA32), and in
# wine10-fd169r-i386/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine10-fd169r-i386"
WORK="${BW10FD169R_WORK:-/tmp/wine10-fd169r-build}"
PATCH="wine10-fd169r-crt2.patch"

TARBALL_URL="https://dl.winehq.org/wine/source/10.0/wine-10.0.tar.xz"
TARBALL_SHA="c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601"

DLLS=(msvcp140_2 msvcp140_atomic_wait)

for tool in curl make flex bison i686-w64-mingw32-gcc pkg-config git sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine10-fd169r: required host tool '$tool' not found" >&2; exit 1; }
done

fetch() { # file sha url
    if [ ! -f "$WORK/cache/$1" ] || [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" != "$2" ]; then
        curl -fsL -o "$WORK/cache/$1" "$3"
    fi
    [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" = "$2" ] || {
        echo "build-wine10-fd169r: hash mismatch for $1" >&2; exit 1; }
}

mkdir -p "$WORK/cache"
fetch "wine-10.0.tar.xz" "$TARBALL_SHA" "$TARBALL_URL"

# --- patched source tree ---
rm -rf "$WORK/wine"
mkdir -p "$WORK/wine"
tar -xf "$WORK/cache/wine-10.0.tar.xz" -C "$WORK/wine" --strip-components=1
cd "$WORK/wine"
git apply "$REPO/tools/buildWine/patches/$PATCH"
echo "--- applied $PATCH ---"

WITHOUTS="--without-x --without-gnutls --without-gstreamer --without-dbus \
--without-sdl --without-opengl --without-osmesa --without-freetype \
--without-fontconfig --without-vulkan \
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

# 2. i686 PE tree: the two patched DLLs
mkdir -p "$WORK/pe-build"
cd "$WORK/pe-build"
if [ ! -f Makefile ]; then
    # CFLAGS follows the WineHQ binary profile (-g -O2); -O1 was
    # measured to miscompile parts of this family (FD-169).
    # shellcheck disable=SC2086
    CFLAGS="-g -O2" "$WORK/wine/configure" --host=i686-w64-mingw32 \
        --with-wine-tools="$WORK/tools-build" --disable-tests \
        $WITHOUTS > configure.log 2>&1
fi
TARGETS=()
for d in "${DLLS[@]}"; do TARGETS+=("dlls/$d/$d.dll"); done
make -j"$(nproc)" "${TARGETS[@]}" > make.log 2>&1

mkdir -p "$OUT"
for d in "${DLLS[@]}"; do
    cp "$WORK/pe-build/dlls/$d/$d.dll" "$OUT/$d.dll"
done

# Stamp the Wine builtin signature (tools/rootfs32/stamp-wine-builtin.sh):
# the mingw linker writes a plain DOS stub, but the Wine loader only
# treats a PE DLL as a builtin when it carries the "Wine builtin DLL"
# signature -- without it the 32-bit boot dies at ntdll.dll. Stamp
# every product before the export check, so the checked bytes are the
# shipped bytes.
for d in "${DLLS[@]}"; do "$HERE/stamp-wine-builtin.sh" "$OUT/$d.dll"; done

# Fail closed: every product must be an i386 PE carrying a sample of
# its FD-169 remainder exports (their non-stub nature is proven by the
# export census and the guest fixture; presence + architecture is
# checked here).
python3 - "$OUT" <<'EOF'
import struct, sys
out = sys.argv[1]
want = {
    "msvcp140_2.dll": ["___std_smf_beta@16", "___std_smf_hypot3@24",
                       "___std_smf_cyl_bessel_j@16", "___std_smf_ellint_3@24",
                       "___std_smf_riemann_zeta@8", "___std_smf_sph_legendre@16"],
    "msvcp140_atomic_wait.dll": ["__std_execution_wait_on_uchar",
                                 "__std_execution_wake_by_address_all",
                                 "__std_tzdb_delete_time_zones",
                                 "__std_tzdb_delete_current_zone",
                                 "__std_tzdb_delete_leap_seconds"],
}
def exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[pe:pe+4] == b"PE\0\0", path
    machine = struct.unpack_from("<H", data, pe+4)[0]
    assert machine == 0x14c, f"{path}: machine {machine:#x} != i386"
    nsec = struct.unpack_from("<H", data, pe+6)[0]
    opt_size = struct.unpack_from("<H", data, pe+20)[0]
    opt = pe+24
    dd = opt + (96 if struct.unpack_from("<H", data, opt)[0] == 0x10b else 112)
    erva, esize = struct.unpack_from("<II", data, dd)
    secs = []
    for i in range(nsec):
        s = opt+opt_size+i*40
        va, vsz, ra = struct.unpack_from("<III", data, s+12)[0], struct.unpack_from("<I", data, s+8)[0], struct.unpack_from("<I", data, s+20)[0]
        secs.append((va, vsz, ra))
    def off(rva):
        for va, vsz, ra in secs:
            if va <= rva < va+vsz: return ra + (rva-va)
        raise ValueError(hex(rva))
    eo = off(erva)
    n_names = struct.unpack_from("<I", data, eo+24)[0]
    names_rva = struct.unpack_from("<I", data, eo+32)[0]
    names = set()
    for i in range(n_names):
        nrva = struct.unpack_from("<I", data, off(names_rva)+i*4)[0]
        no = off(nrva)
        end = data.index(b"\0", no)
        names.add(data[no:end].decode())
    return names
for dll, names in want.items():
    have = exports(out + "/" + dll)
    missing = [n for n in names if n not in have]
    assert not missing, f"{dll}: missing FD-169 remainder exports {missing}"
    print(f"--- {dll}: i386, FD-169 remainder sample exports present ---")
EOF
echo "=== sha256 (re-pin these in check_runtime_completeness.py + build-wine32-base.sh + README.md) ==="
sha256sum "$OUT"/*.dll
