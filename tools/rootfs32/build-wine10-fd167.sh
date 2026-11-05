#!/usr/bin/env bash
# Rebuild the seven Wine 10.0 i386 PE DLLs carrying FD-167 fixes in
# tools/rootfs32/wine10-fd167-i386/ from the pinned wine-10.0 source
# tarball plus the patch in
# tools/buildWine/patches/wine10-fd167-contracts.patch.
#
# The 32-bit payload's PE DLLs ship from the WineHQ 10.0 deb, whose
# bluetoothapis, bthprops.cpl, winusb, mf, mfplat, wevtapi and propsys
# builds answer a set of demo-reachable exports with winebuild stubs —
# calling one raises an exception that kills the caller (defect
# register FD-167).  The patch gives those exports their documented
# behaviour (no-device contracts the guest can actually trigger, a real
# media-buffer wrapper, and graceful not-implemented answers where the
# native implementation itself is a semi-stub), and this build is the
# 32-bit route for shipping them: the base build substitutes the seven
# DLLs hash-pinned, fail-closed, exactly like the win32u backports.
#
# Build shape mirrors build-wine10-win32u.sh: a native tools tree and
# an out-of-tree cross build sharing one patched source tree, but the
# cross build targets i686-w64-mingw32 because these are PE modules.
# No sysroot is needed: PE DLLs link only against import libraries of
# other PE DLLs.
#
# After it runs, re-pin the new sha256 values in
# tools/rootfs64/check_runtime_completeness.py (WINE10_FD167_PINS32),
# in build-wine32-base.sh (FD167_SHA32), and in
# wine10-fd167-i386/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine10-fd167-i386"
WORK="${BW10FD167_WORK:-/tmp/wine10-fd167-build}"
PATCH="wine10-fd167-contracts.patch"

TARBALL_URL="https://dl.winehq.org/wine/source/10.0/wine-10.0.tar.xz"
TARBALL_SHA="c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601"

DLLS=(bluetoothapis bthprops.cpl winusb mf mfplat wevtapi propsys)

for tool in curl make flex bison i686-w64-mingw32-gcc pkg-config git sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine10-fd167: required host tool '$tool' not found" >&2; exit 1; }
done

fetch() { # file sha url
    if [ ! -f "$WORK/cache/$1" ] || [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" != "$2" ]; then
        curl -fsL -o "$WORK/cache/$1" "$3"
    fi
    [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" = "$2" ] || {
        echo "build-wine10-fd167: hash mismatch for $1" >&2; exit 1; }
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

# 2. i686 PE tree: the seven patched DLLs
mkdir -p "$WORK/pe-build"
cd "$WORK/pe-build"
if [ ! -f Makefile ]; then
    # shellcheck disable=SC2086
    CFLAGS="-O1" "$WORK/wine/configure" --host=i686-w64-mingw32 \
        --with-wine-tools="$WORK/tools-build" --disable-tests \
        $WITHOUTS > configure.log 2>&1
fi
TARGETS=()
for d in "${DLLS[@]}"; do
    if [ "$d" = "bthprops.cpl" ]; then TARGETS+=("dlls/$d/$d"); else TARGETS+=("dlls/$d/$d.dll"); fi
done
make -j"$(nproc)" "${TARGETS[@]}" > make.log 2>&1

mkdir -p "$OUT"
for d in "${DLLS[@]}"; do
    if [ "$d" = "bthprops.cpl" ]; then cp "$WORK/pe-build/dlls/$d/$d" "$OUT/$d"; else cp "$WORK/pe-build/dlls/$d/$d.dll" "$OUT/$d.dll"; fi
done

# Fail closed: every product must be an i386 PE carrying its FD-167
# exports as real exports (their non-stub nature is proven by the
# guest fixture and the export census; presence is checked here).
python3 - "$OUT" <<'EOF'
import struct, sys
out = sys.argv[1]
want = {
    "bluetoothapis.dll": ["BluetoothGATTGetServices", "BluetoothGATTGetCharacteristics",
        "BluetoothGATTGetDescriptors", "BluetoothGATTGetCharacteristicValue",
        "BluetoothGATTSetCharacteristicValue", "BluetoothGATTSetDescriptorValue",
        "BluetoothGATTRegisterEvent", "BluetoothGATTUnregisterEvent"],
    "bthprops.cpl": ["BluetoothEnableDiscovery", "BluetoothEnableIncomingConnections",
        "BluetoothIsConnectable"],
    "winusb.dll": ["WinUsb_Initialize", "WinUsb_GetAssociatedInterface",
        "WinUsb_GetOverlappedResult", "WinUsb_ControlTransfer", "WinUsb_ReadPipe",
        "WinUsb_WritePipe", "WinUsb_ResetPipe", "WinUsb_SetCurrentAlternateSetting"],
    "mf.dll": ["MFCreateDeviceSource"],
    "mfplat.dll": ["MFCreateMediaBufferWrapper"],
    "wevtapi.dll": ["EvtRender"],
    "propsys.dll": ["VariantCompare"],
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
if bad: sys.exit("build-wine10-fd167: export check FAILED")
print("export check: all FD-167 exports present in i386 DLLs")
EOF

echo "=== sha256 (re-pin in check_runtime_completeness.py, build-wine32-base.sh + README.md) ==="
sha256sum "$OUT"/*.dll "$OUT"/*.cpl
