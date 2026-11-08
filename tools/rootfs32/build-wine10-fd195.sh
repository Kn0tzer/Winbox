#!/usr/bin/env bash
# Rebuild the Wine 10.0 i386 setupapi.dll carrying both FD-171's
# config-manager implementations and FD-195's device-install fix in
# tools/rootfs32/wine10-fd195-i386/ from the pinned wine-10.0 source
# tarball plus the patches in tools/buildWine/patches/.
#
# FD-195: SetupInstallFromInfSectionW unconditionally called
# CoInitialize before iterating the RegisterDlls/UnregisterDlls
# section fields. In the PnP manager's install thread (a service
# process) that initialization never returns in this environment --
# creating the thread's COM apartment window needs the process's
# window layer fully usable, which is not the case when a bus
# driver reports devices before the session desktop can service
# window creation -- so the first bus device's install stalls
# forever and no bus device (including the gamepad) is ever
# enumerated. wine10-fd195-setupapi-coinstall.patch only pays the
# initialization when the section actually registers DLLs.
#
# FD-195 also rebuilds hidclass.sys in this build: hidclass
# delivers each input report only to the per-open queues that
# exist when the report arrives, so a controller state report
# produced during device start is lost to every program that
# opens the device later. wine10-fd195-hidclass-last-report.patch
# makes hidclass remember the most recent report of a controller
# collection and seed it into a newly opened queue, so a state
# reader (XInput / DirectInput) opening the device after the
# report still observes the current controller state.
#
# setupapi.dll is also in the FD-171 set; the two lanes' changes are
# in disjoint source files of the same DLL, so -- exactly like the
# FD-167+FD-171 combined build for propsys/mf -- this build carries
# both patches and build-wine32-base.sh substitutes it last for
# setupapi.dll (the FD-171 pin set no longer lists setupapi.dll).
#
# Build shape mirrors build-wine10-fd171.sh: a native tools tree and
# an out-of-tree i686 cross build sharing one patched source tree,
# with the import-library providers built in a first wave.
#
# After it runs, re-pin the new sha256 in
# tools/rootfs64/check_runtime_completeness.py (WINE10_FD195_PINS32),
# in build-wine32-base.sh (FD195_SHA32), and in
# wine10-fd195-i386/README.md.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/wine10-fd195-i386"
WORK="${BW10FD195_WORK:-/tmp/wine10-fd195-build}"
PATCHES=(wine10-fd171-contracts.patch wine10-fd195-setupapi-coinstall.patch wine10-fd195-hidclass-last-report.patch)

TARBALL_URL="https://dl.winehq.org/wine/source/10.0/wine-10.0.tar.xz"
TARBALL_SHA="c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601"

DLLS=(setupapi)
WAVE1=(kernelbase kernel32 ntdll)

for tool in curl make flex bison i686-w64-mingw32-gcc pkg-config git sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build-wine10-fd195: required host tool '$tool' not found" >&2; exit 1; }
done

fetch() { # file sha url
    if [ ! -f "$WORK/cache/$1" ] || [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" != "$2" ]; then
        curl -fsL -o "$WORK/cache/$1" "$3"
    fi
    [ "$(sha256sum "$WORK/cache/$1" | cut -d' ' -f1)" = "$2" ] || {
        echo "build-wine10-fd195: hash mismatch for $1" >&2; exit 1; }
}

mkdir -p "$WORK/cache"
fetch "wine-10.0.tar.xz" "$TARBALL_SHA" "$TARBALL_URL"

# --- patched source tree ---
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
make -j"$(nproc)" dlls/hidclass.sys/hidclass.sys >> make.log 2>&1

mkdir -p "$OUT"
for d in "${DLLS[@]}"; do
    cp "$WORK/pe-build/dlls/$d/$d.dll" "$OUT/$d.dll"
done
cp "$WORK/pe-build/dlls/hidclass.sys/hidclass.sys" "$OUT/hidclass.sys"

# Stamp the Wine builtin signature (tools/rootfs32/stamp-wine-builtin.sh):
# the mingw linker writes a plain DOS stub, but the Wine loader only
# treats a PE DLL as a builtin when it carries the "Wine builtin DLL"
# signature -- without it the 32-bit boot dies at ntdll.dll. Stamp
# every product before the export check, so the checked bytes are the
# shipped bytes.
for d in "${DLLS[@]}"; do "$HERE/stamp-wine-builtin.sh" "$OUT/$d.dll"; done
"$HERE/stamp-wine-builtin.sh" "$OUT/hidclass.sys"

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
    "setupapi.dll": CM + ["SetupInstallFromInfSectionW"],
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
if bad: sys.exit("build-wine10-fd195: export check FAILED")
print("export check: all FD-171 exports present in i386 setupapi.dll")
EOF

echo "=== sha256 (re-pin in check_runtime_completeness.py WINE10_FD195_PINS32, build-wine32-base.sh FD195_SHA32 + README.md) ==="
sha256sum "$OUT"/*.dll "$OUT"/*.sys
