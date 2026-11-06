#!/usr/bin/env bash
# Rebuild the two Wine 10.0 i386 PE DLLs touched by BOTH the FD-167 and
# FD-171 lanes, carrying both patches, in
# tools/rootfs32/wine10-fd167-fd171-i386/ from the pinned wine-10.0
# source tarball plus the patches in tools/buildWine/patches/:
#   wine10-fd167-contracts.patch, then wine10-fd171-contracts.patch.
#
# propsys.dll and mf.dll are the only overlap between the two lanes'
# DLL sets, and the two patches touch disjoint functions in each:
#   propsys: FD-167 VariantCompare (on PropVariantCompareEx) vs
#            FD-171 PropVariantGetStringElem.
#   mf:      FD-167 MFCreateDeviceSource vs
#            FD-171 MFTranscodeGetAudioOutputAvailableTypes.
# A single-lane build of either DLL loses the other lane's function,
# and build-wine32-base.sh substitutes FD-171 last, so at composition
# the shipped wine32.zip would silently carry FD-171's propsys/mf and
# drop FD-167's VariantCompare / MFCreateDeviceSource. This combined
# build is the composed tree's answer: one build per library carrying
# both patches, hash-pinned fail-closed in build-wine32-base.sh
# (FD167_FD171 step), check_runtime_completeness.py
# (WINE10_FD167_FD171_PINS32), and wine10-fd167-fd171-i386/README.md.
# The two patches apply cleanly in sequence (verified 2026-11-06:
# fd167 then fd171, no conflicts; the touched hunks are disjoint).
#
# Build shape mirrors build-wine10-fd167.sh: a native tools tree and
# an out-of-tree i686 cross build sharing one patched source tree.
# No sysroot is needed: PE DLLs link only against import libraries of
# other PE DLLs.
#
# After it runs, re-pin the new sha256 values in
# tools/rootfs64/check_runtime_completeness.py
# (WINE10_FD167_FD171_PINS32), in build-wine32-base.sh
# (FD167_FD171_SHA32), and in wine10-fd167-fd171-i386/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine10-fd167-fd171-i386"
WORK="${BW10FD167FD171_WORK:-/tmp/wine10-fd167-fd171-build}"
PATCHES=(wine10-fd167-contracts.patch wine10-fd171-contracts.patch)

TARBALL_URL="https://dl.winehq.org/wine/source/10.0/wine-10.0.tar.xz"
TARBALL_SHA="c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601"

DLLS=(propsys mf)

for tool in curl make flex bison i686-w64-mingw32-gcc pkg-config git sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine10-fd167-fd171: required host tool '$tool' not found" >&2; exit 1; }
done

fetch() { # file sha url
    if [ ! -f "$WORK/cache/$1" ] || [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" != "$2" ]; then
        curl -fsL -o "$WORK/cache/$1" "$3"
    fi
    [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" = "$2" ] || {
        echo "build-wine10-fd167-fd171: hash mismatch for $1" >&2; exit 1; }
}

mkdir -p "$WORK/cache"
fetch "wine-10.0.tar.xz" "$TARBALL_SHA" "$TARBALL_URL"

# --- patched source tree (both patches, FD-167 first) ---
rm -rf "$WORK/wine"
mkdir -p "$WORK/wine"
tar -xf "$WORK/cache/wine-10.0.tar.xz" -C "$WORK/wine" --strip-components=1
cd "$WORK/wine"
git init -q .
for p in "${PATCHES[@]}"; do
    git apply "$REPO/tools/buildWine/patches/$p"
    echo "--- applied $p ---"
done

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

# 2. i686 PE tree: the two combined DLLs
mkdir -p "$WORK/pe-build"
cd "$WORK/pe-build"
if [ ! -f Makefile ]; then
    # shellcheck disable=SC2086
    CFLAGS="-O1" "$WORK/wine/configure" --host=i686-w64-mingw32 \
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

# Fail closed: every product must be an i386 PE carrying BOTH lanes'
# exports as real exports (their non-stub nature is proven by the
# guest fixtures and the export census; presence is checked here).
python3 - "$OUT" <<'EOF'
import struct, sys
out = sys.argv[1]
want = {
    "propsys.dll": ["VariantCompare", "PropVariantGetStringElem"],
    "mf.dll": ["MFCreateDeviceSource", "MFTranscodeGetAudioOutputAvailableTypes"],
}
def exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[pe:pe+4] == b"PE\0\0", path
    machine = struct.unpack_from("<H", data, pe+4)[0]
    assert machine == 0x14c, f"{path}: machine {machine:#x} != i386"
    nsec = struct.unpack_from("<H", data, pe+6)[0]
    opt_size = struct.unpack_from("<H", data, pe+20)[0]
    opt = pe + 24
    exp_rva, _ = struct.unpack_from("<II", data, opt+96)
    sections = []
    for i in range(nsec):
        name, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, opt+opt_size+i*40)
        sections.append((vaddr, max(vsize, rawsize), rawptr))
    def off(rva):
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr+size: return rawptr + (rva-vaddr)
        raise ValueError("rva")
    eoff = off(exp_rva)
    _, _, _, _, _, _, nfuncs, nnames, funcs_rva, names_rva, ords_rva = struct.unpack_from("<IIHHIIIIIII", data, eoff)
    names = []
    for i in range(nnames):
        (nrva,) = struct.unpack_from("<I", data, off(names_rva)+i*4)
        noff = off(nrva)
        end = data.index(b"\0", noff)
        names.append(data[noff:end].decode())
    return set(names)
bad = 0
for dll, funcs in want.items():
    have = exports(f"{out}/{dll}")
    for f in funcs:
        if f not in have:
            print(f"MISSING EXPORT: {dll} {f}")
            bad = 1
if bad: sys.exit("build-wine10-fd167-fd171: export check FAILED")
print("export check: all FD-167 + FD-171 exports present in i386 DLLs")
EOF

echo "=== sha256 (re-pin in check_runtime_completeness.py, build-wine32-base.sh + README.md) ==="
sha256sum "$OUT"/*.dll
