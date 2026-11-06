#!/usr/bin/env bash
# Rebuild the Wine 10.0 i386 PE DLLs carrying FD-171 implementations in
# tools/rootfs32/wine10-fd171-i386/ from the pinned wine-10.0 source
# tarball plus the patch in
# tools/buildWine/patches/wine10-fd171-contracts.patch.
#
# The 32-bit payload's PE DLLs ship from the WineHQ 10.0 deb, whose
# builds answer 96 of the FD-171 long-tail exports with winebuild
# stubs (the config-manager family in setupapi/cfgmgr32, ntdll's
# splay-tree generic tables and fast activation-context calls, pdh's
# formatted counter arrays, combase's HSTRING marshaling and fail-fast
# entry points, kernelbase/kernel32's HeapSummary and
# ReadConsoleInputEx, and a set of small single-function DLLs). The
# patch backports the upstream implementations (adapted where the
# 10.0 tree already carries part of a family), and this build is the
# 32-bit route for shipping them: the base build substitutes the DLLs
# hash-pinned, fail-closed, exactly like the FD-167 set.
#
# Build shape mirrors build-wine10-fd167.sh: a native tools tree and
# an out-of-tree i686 cross build sharing one patched source tree.
# The PE build runs in two waves: combase's RoFailFast* entry points
# call RaiseFailFastException, which upstream resolves from
# kernelbase's import library without declaring the import, so
# kernelbase/kernel32/ntdll (the import-library providers) build
# first.
#
# After it runs, re-pin the new sha256 values in
# tools/rootfs64/check_runtime_completeness.py (WINE10_FD171_PINS32),
# in build-wine32-base.sh (FD171_SHA32), and in
# wine10-fd171-i386/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine10-fd171-i386"
WORK="${BW10FD171_WORK:-/tmp/wine10-fd171-build}"
PATCH="wine10-fd171-contracts.patch"

TARBALL_URL="https://dl.winehq.org/wine/source/10.0/wine-10.0.tar.xz"
TARBALL_SHA="c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601"

DLLS=(propsys ntdll setupapi cfgmgr32 pdh combase kernelbase kernel32 cryptext d2d1 hid mf mscms prntvpt sspicli vcomp vcomp90 vcomp100 vcomp110 vcomp120 vcomp140)
WAVE1=(kernelbase kernel32 ntdll)

for tool in curl make flex bison i686-w64-mingw32-gcc pkg-config git sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine10-fd171: required host tool '$tool' not found" >&2; exit 1; }
done

fetch() { # file sha url
    if [ ! -f "$WORK/cache/$1" ] || [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" != "$2" ]; then
        curl -fsL -o "$WORK/cache/$1" "$3"
    fi
    [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" = "$2" ] || {
        echo "build-wine10-fd171: hash mismatch for $1" >&2; exit 1; }
}

mkdir -p "$WORK/cache"
fetch "wine-10.0.tar.xz" "$TARBALL_SHA" "$TARBALL_URL"

# --- patched source tree ---
rm -rf "$WORK/wine"
mkdir -p "$WORK/wine"
tar -xf "$WORK/cache/wine-10.0.tar.xz" -C "$WORK/wine" --strip-components=1
cd "$WORK/wine"
git init -q .
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

# 2. i686 PE tree: the patched DLLs, in two waves (see header)
mkdir -p "$WORK/pe-build"
cd "$WORK/pe-build"
if [ ! -f Makefile ]; then
    # shellcheck disable=SC2086
    CFLAGS="-O1" "$WORK/wine/configure" --host=i686-w64-mingw32 \
        --with-wine-tools="$WORK/tools-build" --disable-tests \
        $WITHOUTS > configure.log 2>&1
fi
W1TARGETS=()
for d in "${WAVE1[@]}"; do W1TARGETS+=("dlls/$d/$d.dll"); done
make -j"$(nproc)" "${W1TARGETS[@]}" > make.log 2>&1
TARGETS=()
for d in "${DLLS[@]}"; do TARGETS+=("dlls/$d/$d.dll"); done
make -j"$(nproc)" "${TARGETS[@]}" >> make.log 2>&1

mkdir -p "$OUT"
for d in "${DLLS[@]}"; do
    cp "$WORK/pe-build/dlls/$d/$d.dll" "$OUT/$d.dll"
done

# Fail closed: every product must be an i386 PE carrying its FD-171
# exports as real exports (their non-stub nature is proven by the
# guest fixtures and the export census; presence is checked here).
python3 - "$OUT" <<'EOF'
import struct, sys
out = sys.argv[1]
CM = ["CM_Enumerate_Classes_Ex", "CM_Enumerate_EnumeratorsA", "CM_Enumerate_EnumeratorsW",
    "CM_Enumerate_Enumerators_ExA", "CM_Enumerate_Enumerators_ExW", "CM_Get_Class_Key_NameA",
    "CM_Get_Class_Key_NameW", "CM_Get_Class_Key_Name_ExA", "CM_Get_Class_Key_Name_ExW",
    "CM_Get_Device_ID_List_ExA", "CM_Get_Device_ID_List_ExW", "CM_Get_Device_ID_List_Size_ExA",
    "CM_Get_Device_ID_List_Size_ExW", "CM_Get_Device_ID_Size_Ex", "CM_Get_Device_Interface_ListA",
    "CM_Get_Device_Interface_ListW", "CM_Get_Device_Interface_List_ExA", "CM_Get_Device_Interface_List_ExW",
    "CM_Open_Class_KeyA", "CM_Open_Class_KeyW", "CM_Open_Class_Key_ExA", "CM_Open_Class_Key_ExW",
    "CM_Open_DevNode_Key_Ex"]
want = {
    "propsys.dll": ["PropVariantGetStringElem"],
    "ntdll.dll": ["RtlSplay", "RtlDelete", "RtlDeleteNoSplay", "RtlInsertElementGenericTable",
        "RtlDeleteElementGenericTable", "RtlEnumerateGenericTable", "RtlIsGenericTableEmpty",
        "RtlRealPredecessor", "RtlRealSuccessor", "RtlSubtreePredecessor", "RtlSubtreeSuccessor",
        "RtlActivateActivationContextUnsafeFast", "RtlDeactivateActivationContextUnsafeFast",
        "RtlQueryInformationActiveActivationContext"],
    "setupapi.dll": CM, "cfgmgr32.dll": CM,
    "pdh.dll": ["PdhGetFormattedCounterArrayA", "PdhGetFormattedCounterArrayW"],
    "combase.dll": ["HSTRING_UserMarshal", "HSTRING_UserSize", "HSTRING_UserFree",
        "HSTRING_UserUnmarshal", "RoGetErrorReportingFlags", "RoFailFastWithErrorContext",
        "RoFailFastWithErrorContextInternal2"],
    "kernelbase.dll": ["HeapSummary", "ReadConsoleInputExA", "ReadConsoleInputExW"],
    "kernel32.dll": ["HeapSummary", "ReadConsoleInputExA", "ReadConsoleInputExW",
        "RegisterWaitForInputIdle"],
    "cryptext.dll": ["CryptExtOpenCER", "CryptExtOpenCERW"],
    "d2d1.dll": ["D2D1CreateDeviceContext"],
    "hid.dll": ["HidP_SetData"],
    "mf.dll": ["MFTranscodeGetAudioOutputAvailableTypes"],
    "mscms.dll": ["WcsGetCalibrationManagementState"],
    "prntvpt.dll": ["BindPTProviderThunk", "UnbindPTProviderThunk",
        "ConvertDevModeToPrintTicketThunk2", "ConvertPrintTicketToDevModeThunk2",
        "GetPrintCapabilitiesThunk2"],
    "sspicli.dll": ["SspiMarshalAuthIdentity", "SspiUnmarshalAuthIdentity"],
    "vcomp.dll": ["omp_get_wtick"], "vcomp90.dll": ["omp_get_wtick"],
    "vcomp100.dll": ["omp_get_wtick"], "vcomp110.dll": ["omp_get_wtick"],
    "vcomp120.dll": ["omp_get_wtick"], "vcomp140.dll": ["omp_get_wtick"],
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
if bad: sys.exit("build-wine10-fd171: export check FAILED")
print("export check: all FD-171 exports present in i386 DLLs")
EOF

echo "=== sha256 (re-pin in check_runtime_completeness.py, build-wine32-base.sh + README.md) ==="
sha256sum "$OUT"/*.dll
