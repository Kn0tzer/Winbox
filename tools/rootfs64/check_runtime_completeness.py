#!/usr/bin/env python3
"""Runtime/prefix completeness guard (docs/known-gaps/runtime.md).

Verifies the claims the runtime register makes about the *shipped*
artifacts and the *source* payloads, offline:
  1. winehq10 backport DLLs in tools/rootfs64/winehq10-x86_64 match the
     sha256 pins in their README, and are present (with those hashes) in
     web/runtime/wine64.zip.
  2. prepare_prefixes.validate_service_runtime passes for the shipped
     wine64.zip/prefix64.zip and wine32.zip/prefix32.zip pairs against
     the retained support pack (services, HID, backport DLLs, gstreamer
     plugin set in the pack, not the boot zips).
  2b. The gstreamer split holds: no staged-closure member is in either
     boot zip, every member is in the retained support pack with the
     tree digests dependencies.json records, and prepare-gstreamer.py
     --check re-verifies the closure against the post-split boot zips.
  2c. The support pack's member names are unique, and its four
     libvulkan.so.1 members are the project shims whose digests
     dependencies.json records as the 'vulkan' provenance (FD-53).
  2d. The retained local runtime-assets release archive agrees with
     tools/browser/runtime-assets.json end to end: the archive's own
     size/SHA256 matches the manifest's archive pin, its member set is
     exactly the pinned file set, and every member's size/SHA256
     matches its file pin (FD-73). Resolves the retained archive via
     BW64_ASSETS_ARCHIVE, defaulting to the box's retained copy under
     /home/ubuntu/winbox-project/backups/winbox-runtime-assets-*/ named by the
     manifest itself; skips loudly when no retained copy exists (the
     download / --local restore path verifies the same agreement at
     restore time).
  3. The DirectX redist sets the register calls complete are complete in
     both wine zips (d3dx9_24..43, d3dx10_33..43, d3dcompiler_33..47,
     xaudio2_0..2_9, xinput1_1..1_4), and the d3dcompiler numbers the
     register calls non-existent (24..32, 44, 45) are absent in both.
  3b. The VC++ runtime sets are as the register states: the legacy
     msvcp/msvcr 60..120 sets in both zips; the 140 set in both, with
     vcruntime140_1 x86_64-only (upstream) and msvcp140_codecvt_ids in
     both (x86_64 side via the winehq10 backport).
  3c. wine64.zip's wineserver dispatch is the ELF: usr/lib/wine/wineserver
     and usr/bin/wineserver carry the symlink target text for
     wineserver64 (no /bin/sh script; the guest rootfs has no /bin/sh).
  3d. docs/known-gaps/runtime-artifact-manifest.md matches the shipped
     zips byte-for-byte (regenerate: gen_runtime_manifest.py). Skips
     loudly when no support pack has been built yet: the manifest's
     support provenance is generated from dist/runtime/dependencies.json,
     a build product, so pre-build there is nothing to compare against.
  3e. The ALSA-to-OSS bridge member sets match the staged source trees
     (tools/rootfs64/alsa for x86_64, tools/rootfs64/alsa32 for i386) in
     both directions, per member sha256: plugins + /usr/share/alsa config
     in the wine zips, etc/asound.conf + config tree in the glibc rootfs
     zips; glibc-rootfs64's printf-generated asound.conf stays
     byte-identical to the canonical alsa32 one.
  4. glibc-rootfs32.zip carries the DejaVu Bold/Mono faces from fonts32/
     with the README-pinned hashes.
  4b. glibc-rootfs64.zip carries the DejaVu faces from fonts64/ with the
     README-pinned hashes: the Sans/Bold/Mono set the 32-bit rootfs
     already ships, plus the Serif pair — the 64-bit rootfs carried no
     faces at all, so 64-bit font enumeration found no usable serif
     and Wine Gecko aborted at startup (FD-65).
  4c. prefix64.zip's user.reg carries the Wine font replacement
     "Arial"="DejaVu Sans" ([Software\\Wine\\Fonts\\Replacements])
     and the replacement target face ships in glibc-rootfs64.zip, so
     the 64-bit stack serves the Arial family (FD-86); the 32-bit
     stack serves it through the same replacement, written into its
     exported prefix by tools/browser/stage-wine32-fonts.py.
  5. stage-dependencies.py pins the CJK font (deb + ttc sha256) and, when
     the download cache exists, the cached deb's ttc matches the pin.
  6. When build-prefix64.sh output exists (tools/rootfs64/dist/), its
     system.reg carries the CurrentVersion + .NETFramework sections.

Exit code 0 = every non-skipped check passed. Skips are printed, never
silent. Run directly or via web/tests/suite/unit/runtime-completeness.test.mjs.
"""
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import zipfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/rootfs64'))
import prepare_prefixes as prefixes  # noqa: E402

FAILURES = []
SKIPS = []


def check(name, fn):
    try:
        fn()
    except Exception as exc:  # noqa: BLE001 - report and continue
        FAILURES.append(f'{name}: {exc}')
        print(f'FAIL {name}: {exc}')
    else:
        print(f'PASS {name}')


def skip(name, why):
    SKIPS.append(name)
    print(f'SKIP {name}: {why}')


def sha(data):
    return hashlib.sha256(data).hexdigest()


WINEHQ_PINS = {
    'msmpeg2vdec.dll': 'bbf91fc84f2bc66c70802e41e5ff1ee2e18335ef9cf07769957b0fb37484d046',
    'msvcp140_codecvt_ids.dll': '2215e3312bcc817c628669ec69baff3f050ec0bef91c099e0d12fd66f2c3d9b1',
}
# Wine 8.0 loader backports (tools/rootfs64/wine8-backports-x86_64): four
# core DLLs rebuilt from the pinned wine-8.0 source with the three
# wine8-*.patch backports in tools/buildWine/patches (the GetPointer*Info
# family in user32, CopyFile2 in kernelbase/kernel32, GetAnycastIpAddress-
# Table in iphlpapi). Re-pins are deliberate, as with the payloads above:
# a rebuild that changes bytes fails here until the new bytes are reviewed
# and pinned.
WINE8_BACKPORT_PINS = {
    'user32.dll': '3c495fc6ae4a2bcac424107100359fe4a95f9cc8db0f5568f2e7707f07edf72d',
    'kernelbase.dll': '3e8b4283df0c9e71dd3fef5c67ceb2aa9740d4faf57dd58d83357b87d65ebf90',
    'kernel32.dll': 'd3d35f7a3488aa59a80c115d458d7be4d76bcceda79bdd9eaae35bc5ccb41de8',
    'iphlpapi.dll': 'c9b9faf78c0ec4372a26f043f4d0d16dc363961d7cbd4fd8b42f0e13560ad283',
    'win32u.dll': '21f5e5438a19fc157643f54b75d798e10ada2a1674e8976120b37d958f063870',
    # The unix half of win32u (FD-01): the desktop-wait patch lives in
    # this module; pinning only the DLL is what let the patch ship in
    # no binary (FD-43).
    'win32u.so': 'b2aa883bfb47627ff63c6be97e4da54de45b46ef159c3c29a13ad91ac18f7ec0',
    # FD-167: the MSVC runtime throw helpers a bundled demo library
    # imports, and SetupDiGetDeviceInterfaceAlias, stop being spec
    # stubs (see wine8-backports-x86_64/README.md).
    'msvcp140.dll': 'c8255d140b6bdbba5d7e0d61b6eea3de05f00dba7e1b8344bce08da54142d58a',
    'setupapi.dll': 'c7e1555da0fedb6153044bb823185a995f268903bfe55c4cf26193c80ede6d21',
    # FD-169: the Microsoft C runtime family, rebuilt from the same
    # patched Wine 8.0 tree (wine8-msvcrt-fd169-crt.patch).
    'ucrtbase.dll': '6f790d4721f8562c021bfddbb77cdf5da686e64507d02741888fdcc156c057d9',
    'msvcrt.dll': '23bad1e9016e8f3f95ba5784a6a1ad383f4d9cc65a21e6aafddc4fbda1a78f84',
    'msvcr70.dll': '33a64d83cefcaba027c2a4d00e1ce1d5fdc81de3fcc7eabb719dec5b817cb3b2',
    'msvcr71.dll': '6e37ada00292280e3e4dc21d6d4432fa331a057ecbf31e00399d19e33f425a38',
    'msvcr80.dll': '8cb9576ab57bc10b8010268572a1410cf62f15b7faee5202ed58e2ae6a45d0f8',
    'msvcr90.dll': 'b68c1d96070e6dae7a366616cb717c8278886f7dd902b0e1ce5e3687537e1728',
    'msvcr100.dll': '49dc3a015b4be2d16f7629d106845fd84d4966be23a13e9d82c0caf90bf6997c',
    'msvcr110.dll': 'c82bf206e4e93ba8c12113bcad3a67ca8988347f78a66def81921270e7872a69',
    'msvcr120.dll': '89054bd4059865c9b2c404f817f3ad9fa7913676fc9c4e98185fd17f2a53dbfb',
    # FD-170 whole-DLL backports (wine8-fd170-contracts.patch): Wine
    # 10.0's bluetoothapis + the FD-167 contract bodies, the FD-167
    # winusb no-device contracts, Wine 10.0's netutils NetApiBuffer*.
    'bluetoothapis.dll': '306a2b6fea134bc0e553272c04200301925d3f78b9428820d10e7f7342ea8f78',
    'winusb.dll': 'cb65e30f375e3d05544c6c6c0e2a4dea44c364bf68c77bf5bd93a18fa33cd987',
    'netutils.dll': '47652b97794e174c3a0574b04ca621b4665516abc67db18b1c0023ec112beb83',
}
# The D3D12 payload staged into wine64.zip at system placement by
# build-wine64-zip.sh / stage-vkd3d-payloads.sh (2026-10-26 restage):
# bw64vulkan.dll must be the bw64 PE shim (the vendored vkd3d-proton's
# loader tries that name first to bypass Wine 8.0's winevulkan, which
# cannot expose VK_KHR_maintenance5/6), and the d3d12 pair must be the
# vkd3d-proton 3.1.0 build (tools/vkd3d/BUILD.md). d3d12core.dll was a
# winehq10 backport until this restage; the pair now moves together
# from the proton build. Re-pins are deliberate: a vkd3d or shim
# rebuild that changes bytes fails here until the new bytes are
# reviewed and pinned.
# 2026-10-28 restage: the pair was rebuilt from the vendored source
# at 954e72aa1 so the staged d3d12core.dll carries the section-4.29
# dxbc-spirv utof fix (the previous pins predated that rebuild and
# uav12 UINT cases still failed PSO creation against them).
VKD3D_PINS = {
    'bw64vulkan.dll': '03edc142351a1079a2563ad456b8012c10faebf815f8ee8f77e73bf722df7eab',
    # vulkan-1.dll and winevulkan.dll carry the SAME shim bytes
    # (surface closure, 2026-10-27): Wine's dxgi creates D3D12
    # swapchain surfaces through a factory that loads winevulkan.dll
    # by name, native Vulkan apps and DXVK's dxgi load vulkan-1.dll,
    # and all three PE Vulkan names must resolve to one handle domain
    # (the bridge) or the surface call faults inside winevulkan.
    'vulkan-1.dll': '03edc142351a1079a2563ad456b8012c10faebf815f8ee8f77e73bf722df7eab',
    'winevulkan.dll': '03edc142351a1079a2563ad456b8012c10faebf815f8ee8f77e73bf722df7eab',
    'd3d12.dll': 'a96b4e9d53ed0bed7c52a9610677e4b4ec4eb13fb5ad4f44a175c7a8da7242f1',
    'd3d12core.dll': '3d3bfe43cbec123fcdeb8f2b72ee2beaefb7a63eeaf37493bbace5c5ba54e790',
}
FONTS32_PINS = {
    'DejaVuSans-Bold.ttf': '5c1247acef7f2b8522a31742c76d6adcb5569bacc0be7ceaa4dc39dd252ce895',
    'DejaVuSansMono.ttf': 'c805f9436dbc268644c1d9584f01a601a653e028e08fd74b9b949f6cf8304d88',
    'DejaVuSansMono-Bold.ttf': '3a3c502eeff669a231549e80df9f7c49de109bafe303170409e905d0b31a38fe',
}
FONTS64_PINS = {
    'DejaVuSans.ttf': 'ae7b7855e115a5966d8b1b3f80f254ccc117ec86f9965e202ee2940453837280',
    'DejaVuSans-Bold.ttf': '5c1247acef7f2b8522a31742c76d6adcb5569bacc0be7ceaa4dc39dd252ce895',
    'DejaVuSansMono.ttf': 'c805f9436dbc268644c1d9584f01a601a653e028e08fd74b9b949f6cf8304d88',
    'DejaVuSansMono-Bold.ttf': '3a3c502eeff669a231549e80df9f7c49de109bafe303170409e905d0b31a38fe',
    'DejaVuSerif.ttf': '8f2c103bfa3fd5de71f1b92b18f21906b5a26871fb7e19a9a4c9af539c3cc7ab',
    'DejaVuSerif-Bold.ttf': '847b33e13925f19ff87e4d934d6b3cf7cac35ce16424f6f670e40c2f377cf2df',
}
CJK_TTC_SHA = '2420e8078af796b19a3f6ef13de527a1a91c1e7171eea115926c614ced1009b3'
RUNTIME = ROOT / 'web/runtime'


def wine64_members():
    with zipfile.ZipFile(RUNTIME / 'wine64.zip') as z:
        return {n: z.read(n) for n in z.namelist()
                if n.endswith(('msmpeg2vdec.dll', 'msvcp140_codecvt_ids.dll'))}


def t_vkd3d_payloads():
    shim = ROOT / 'tools/vkd3d/probe/bw64vulkan.dll'
    assert sha(shim.read_bytes()) == VKD3D_PINS['bw64vulkan.dll'], \
        'bw64vulkan.dll source hash drifted from pin'
    with zipfile.ZipFile(RUNTIME / 'wine64.zip') as z:
        members = {n: z.read(n) for n in z.namelist()
                   if n.rsplit('/', 1)[-1] in VKD3D_PINS}
    for name, pin in VKD3D_PINS.items():
        hits = [n for n in members if n.rsplit('/', 1)[-1] == name]
        assert hits, f'{name} missing from shipped wine64.zip'
        assert sha(members[hits[0]]) == pin, f'{name} in wine64.zip != pinned vkd3d payload bytes'


# The 32-bit D3D12 payload staged into wine32.zip at system placement
# by stage-vkd3d-payloads32.sh (x86 vkd3d lane, 2026-10-28): the PE32
# vkd3d-proton 3.1.0 build (tools/vkd3d/build.w32; recipe in
# tools/vkd3d/BUILD.md, scripted as tools/vkd3d/build-win32.sh)
# replaces WineHQ wine-stable 10.0's bundled pair in the i386 tree —
# the tree prefix32's system32.link points at. As on the 64-bit side,
# Wine's own d3d12core has no DXIL frontend, and the pair moves
# together. Re-pins are deliberate: a vkd3d rebuild that changes bytes
# fails here until the new bytes are reviewed and pinned. (The PE
# build is not byte-reproducible — linker timestamp + the version
# header embeds the enclosing repo's git describe — so the canonical
# bytes are the staged ones, transferred and hash-verified, exactly
# like the 64-bit pair.)
VKD3D32_PINS = {
    'opt/wine-stable/lib/wine/i386-windows/d3d12.dll': '872c3d65ad3febfb3fdbd935ca2272624a0e711724dd4013001dbc9ffd3ab0e1',
    'opt/wine-stable/lib/wine/i386-windows/d3d12core.dll': '71e9c174084474d84e04893f0cef9eaea9bbede43201308e46b19b01fd727600',
}


def t_vkd3d_payloads32():
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        members = {n: z.read(n) for n in z.namelist()
                   if n in VKD3D32_PINS}
    for name, pin in VKD3D32_PINS.items():
        assert name in members, f'{name} missing from shipped wine32.zip'
        assert sha(members[name]) == pin, f'{name} in wine32.zip != pinned vkd3d32 payload bytes'


# The 32-bit win32u unix module (tools/rootfs32/wine10-backports-i386):
# Wine 10.0's win32u.so rebuilt from the pinned wine-10.0 source tarball
# with the desktop-init patience patch
# (tools/buildWine/patches/wine10-win32u-desktop-wait.patch; FD-02 --
# the 32-bit twin of the FD-01 fix in the Wine 8 set). The base build
# (tools/rootfs32/build-wine32-base.sh) substitutes it for the WineHQ
# deb's module before the .dll.so duplication and the usr/ re-layout,
# so all four shipped copies carry these bytes; the PE half
# (win32u.dll) ships untouched from the deb -- the patch's functions
# live in the unix half. Built WITH freetype/fontconfig compiled in
# (the stock module dlopens both libraries and the guest ships both).
# Re-pins are deliberate, as with the backports above.
WINE10_BACKPORT32_SHA = '9d538e799c0b88b2c7ee1e15b9d71a979414d65de878459470d6395c12542df8'
WINE10_BACKPORT32_MEMBERS = (
    'opt/wine-stable/lib/wine/i386-unix/win32u.so',
    'opt/wine-stable/lib/wine/i386-unix/win32u.dll.so',
    'usr/lib/i386-linux-gnu/wine/win32u.so',
    'usr/lib/i386-linux-gnu/wine/win32u.dll.so',
)


def t_wine10_backport32_source_and_zip():
    src = ROOT / 'tools/rootfs32/wine10-backports-i386' / 'win32u.so'
    assert sha(src.read_bytes()) == WINE10_BACKPORT32_SHA, \
        'win32u.so (32-bit backport) source hash drifted from pin'
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        members = {n: z.read(n) for n in z.namelist()}
    for name in WINE10_BACKPORT32_MEMBERS:
        assert name in members, f'{name} missing from shipped wine32.zip'
        assert sha(members[name]) == WINE10_BACKPORT32_SHA, \
            f'{name} in wine32.zip != pinned 32-bit win32u backport bytes'


# FD-167: seven Wine 10.0 PE modules rebuilt from the same pinned
# wine-10.0 source with the FD-167 contract patch (real implementations
# for demo-reachable spec stubs; see wine10-fd167-i386/README.md).
# Substituted into wine32.zip by build-wine32-base.sh, fail-closed.
WINE10_FD167_PINS32 = {
    'bluetoothapis.dll': '8ecb95b3feb654c63b70bc76459bc96974653422d309e619a35166e764d598c6',
    'bthprops.cpl': '72a28006aa12165bea114afd39cd20590a8b4570c2ed57ec9b2a5716d4e8cd2e',
    'mf.dll': '9babdf82f66204521efa1f8c5a7dbae9ef3ac183059b6355e5806e504ed7d5c4',
    'mfplat.dll': 'fa96363d4d1aeb474dc10543e8771188a06aac35d18ca7040dcec9dcd4f673f7',
    'propsys.dll': 'f39cbd2072635db066497d0f10cc4d512b15bac0375a13e311f0ec40ac3ae7a0',
    'wevtapi.dll': '1c5d1890ff8fe50b29b62cd735ded3985313cd77a660b42d8c5a9d786f9185c5',
    'winusb.dll': '984757871bde57acae11c31eae97b2fb4e3d7de9868c40bed95b526dc87bfa7f',
}


def t_wine10_fd167_32_source_and_zip():
    for name, pin in WINE10_FD167_PINS32.items():
        src = ROOT / 'tools/rootfs32/wine10-fd167-i386' / name
        assert sha(src.read_bytes()) == pin, \
            f'{name} (FD-167 32-bit backport) source hash drifted from pin'
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        members = {n: z.read(n) for n in z.namelist()}
    for name, pin in WINE10_FD167_PINS32.items():
        member = 'opt/wine-stable/lib/wine/i386-windows/' + name
        assert member in members, f'{member} missing from shipped wine32.zip'
        assert sha(members[member]) == pin, \
            f'{member} in wine32.zip != pinned FD-167 backport bytes'
# FD-169: the nine i386 CRT DLLs are committed rebuilds (wine-10.0 +
# FD-169 patch, tools/rootfs32/build-wine10-fd169.sh) substituted into
# wine32.zip by build-wine32-base.sh. Re-pins are deliberate.
WINE10_FD169_PINS32 = {
    'msvcr100.dll': 'f15f7a9b6eb6ce7e0fa921a3343f91cac34f808ee443eda609630ce0f81dab9d',
    'msvcr110.dll': '2b5bff73c1e77843723e3335c106a53b9ae4119839aceb16d610ebe61d7299c5',
    'msvcr120.dll': '0e4ba5e74c22db4de9a8889d89d247b708e8ba657029b75b5f7dd1f9dbb58b92',
    'msvcr70.dll': '1d5c6c16b3d095039142037a92256cd660f00fa29ecaddd07ef0722b2130f6b7',
    'msvcr71.dll': '60ac37db454b821d6245fce39830e37126a065f0386f836d98d4e89bf9565b41',
    'msvcr80.dll': '7aa234283cd4c0dd378540184db724360a189d47013ae4259d4622c2f19ecd7c',
    'msvcr90.dll': 'c3f3062bb3ba6e5084da24ad66dd5dec18cc3633e0fb0b166d54a0d5a827dada',
    'msvcrt.dll': 'f3cebbf0855200a772733b1d0aa431fdd77cea78b1cb03b6427311921ca98cec',
    'ucrtbase.dll': '7c1782913e4c865252348fd38cbb402bc3d8dd6bd6bf088501d4ffbaa3c5264c',
}


def t_wine10_fd169_crt32_source_and_zip():
    for name, pin in WINE10_FD169_PINS32.items():
        src = ROOT / 'tools/rootfs32/wine10-fd169-i386' / name
        assert sha(src.read_bytes()) == pin, f'{name} (FD-169 i386) source hash drifted from pin'
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        members = {n: z.read(n) for n in z.namelist()}
    for name, pin in WINE10_FD169_PINS32.items():
        member = f'opt/wine-stable/lib/wine/i386-windows/{name}'
        assert member in members, f'{member} missing from shipped wine32.zip'
        assert sha(members[member]) == pin, f'{name} in wine32.zip != pinned FD-169 bytes'


def t_wine8_backports_source_and_zip():
    for name, pin in WINE8_BACKPORT_PINS.items():
        src = ROOT / 'tools/rootfs64/wine8-backports-x86_64' / name
        assert sha(src.read_bytes()) == pin, f'{name} source hash drifted from README pin'
    with zipfile.ZipFile(RUNTIME / 'wine64.zip') as z:
        members = {n: z.read(n) for n in z.namelist()}
    for name, pin in WINE8_BACKPORT_PINS.items():
        hits = [n for n in members if n.endswith(name)]
        assert hits, f'{name} missing from shipped wine64.zip'
        assert sha(members[hits[0]]) == pin, f'{name} in wine64.zip != pinned bytes'


def t_winehq_source_and_zip():
    for name, pin in WINEHQ_PINS.items():
        src = ROOT / 'tools/rootfs64/winehq10-x86_64' / name
        assert sha(src.read_bytes()) == pin, f'{name} source hash drifted from README pin'
    members = wine64_members()
    for name, pin in WINEHQ_PINS.items():
        hits = [n for n in members if n.endswith(name)]
        assert hits, f'{name} missing from shipped wine64.zip'
        assert sha(members[hits[0]]) == pin, f'{name} in wine64.zip != pinned bytes'


def support_dir():
    # The export that built the support pack: build.mjs points this at its
    # own output runtime dir; the default is the canonical dist/ export.
    return Path(os.environ.get('BW64_SUPPORT_DIR', ROOT / 'dist/runtime'))


def support_pack_ready():
    return (support_dir() / 'dependencies.json').is_file()


def support_zip_path():
    deps = json.loads((support_dir() / 'dependencies.json').read_text())
    return support_dir() / deps['archive']


def t_service_runtime():
    if not support_pack_ready():
        skip('validate_service_runtime (shipped pairs)',
             'no support pack built yet (dependencies.json absent); '
             'the build.mjs / apply-runtime-poststeps gates run this '
             'check against the freshly built pack')
        return
    support = support_zip_path()
    prefixes.validate_service_runtime(RUNTIME / 'prefix64.zip', RUNTIME / 'wine64.zip', support)
    prefixes.validate_service_runtime(RUNTIME / 'prefix32.zip', RUNTIME / 'wine32.zip', support)


def t_gstreamer_split():
    if not support_pack_ready():
        skip('gstreamer closure rides the support pack',
             'no support pack built yet (dependencies.json absent); '
             'the build.mjs / apply-runtime-poststeps gates run this '
             'check against the freshly built pack')
        return
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'sd', ROOT / 'tools/browser/stage-dependencies.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    support = support_zip_path()
    with zipfile.ZipFile(support) as z:
        support_names = set(z.namelist())
    deps = json.loads((support_dir() / 'dependencies.json').read_text())
    gst = deps.get('gstreamer')
    assert gst, 'dependencies.json lacks the gstreamer provenance section'
    for arch, stage_name, boot in (('x86_64', 'gstreamer64', 'wine64.zip'),
                                   ('i386', 'gstreamer32', 'wine32.zip')):
        root = ROOT / 'tools/rootfs64' / stage_name
        paths = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
        assert paths, f'{stage_name}: staged tree is empty'
        missing = (paths - module.GSTREAMER_ARCHIVE_EXCLUDES) - support_names
        assert not missing, (f'{stage_name}: {len(missing)} closure members missing '
                             f'from the support pack, e.g. {sorted(missing)[:3]}')
        with zipfile.ZipFile(RUNTIME / boot) as z:
            leaked = paths & set(z.namelist())
        assert not leaked, (f'{stage_name}: {len(leaked)} closure members back in '
                            f'{boot}, e.g. {sorted(leaked)[:3]}')
        recorded = gst['arches'][arch]
        assert recorded['members'] == len(paths), \
            f'{stage_name}: dependencies.json records {recorded["members"]} members, staged {len(paths)}'
        assert recorded['treeSha256'] == module.tree_digest(root), \
            f'{stage_name}: dependencies.json tree digest drifted from the staged tree'
    result = subprocess.run(
        [sys.executable, str(ROOT / 'tools/rootfs64/prepare-gstreamer.py'), '--check'],
        capture_output=True, text=True)
    assert result.returncode == 0, \
        f'prepare-gstreamer.py --check failed: {result.stdout} {result.stderr}'


def t_support_pack_member_uniqueness():
    # FD-53: the shipped support pack once carried two different files
    # under usr/lib/x86_64-linux-gnu/libvulkan.so.1 (the project shim,
    # staged deliberately, and the media closure's distro loader,
    # staged incidentally), so the Vulkan library the guest loaded
    # depended on the reader's duplicate handling rather than on the
    # pinned archive hash. Member names must be unique archive-wide,
    # and the Vulkan members must be the shims the manifest pins.
    if not support_pack_ready():
        skip('support pack member uniqueness (FD-53)',
             'no support pack built yet (dependencies.json absent); '
             'the build.mjs / apply-runtime-poststeps gates run this '
             'check against the freshly built pack')
        return
    deps = json.loads((support_dir() / 'dependencies.json').read_text())
    vulkan = deps.get('vulkan')
    assert vulkan, 'dependencies.json lacks the vulkan provenance section'
    with zipfile.ZipFile(support_zip_path()) as z:
        names = z.namelist()
        duplicates = sorted(n for n, c in Counter(names).items() if c > 1)
        assert not duplicates, \
            f'support pack has duplicate member names: {duplicates[:5]}'
        for arch in ('i386', 'x86_64'):
            for prefix in ('lib', 'usr/lib'):
                member = f'{prefix}/{arch}-linux-gnu/libvulkan.so.1'
                assert names.count(member) == 1, \
                    f'support pack must carry {member} exactly once'
                actual = sha(z.read(member))
                assert actual == vulkan[arch], \
                    (f'{member}: sha256 {actual[:12]}... is not the pinned '
                     f'project shim {vulkan[arch][:12]}...')


def assets_archive_path():
    # The retained local copy of the pinned runtime-assets release
    # archive. BW64_ASSETS_ARCHIVE points at an archive directly (the
    # BW64_SUPPORT_DIR pattern); the default is the box's retained
    # copy, found by the manifest's own archive name under
    # /home/ubuntu/winbox-project/backups/winbox-runtime-assets-*/ so a re-pinned
    # generation is located without editing this checker.
    override = os.environ.get('BW64_ASSETS_ARCHIVE')
    if override:
        return Path(override)
    manifest = json.loads(
        (ROOT / 'tools/browser/runtime-assets.json').read_text())
    found = sorted(Path('/home/ubuntu/winbox-project/backups').glob(
        f'winbox-runtime-assets-*/{manifest["archive"]["name"]}'))
    return found[-1] if found else None


def t_assets_archive():
    # FD-73: the retained archive once disagreed with the manifest's
    # file pins for both Wine archives — it had been built before the
    # FD-40/FD-01/FD-02 payload fixes landed and was never rebuilt as
    # their file pins moved — so bootstrap.py --local failed on a
    # fresh checkout while --check passed on every tree already
    # carrying the pinned files: no verification path ever read the
    # archive. Verify the retained archive against the manifest end
    # to end (its own pin, its member set, every member's bytes), so
    # a future re-pin that leaves the archive stale fails here
    # instead of at the next fresh checkout.
    archive = assets_archive_path()
    if archive is None:
        skip('retained runtime-assets archive agrees with the file pins (FD-73)',
             'no retained archive found under /home/ubuntu/winbox-project/backups; '
             'bootstrap.py verifies the same agreement at restore time '
             'on the download / --local path')
        return
    assert archive.is_file(), f'retained archive does not exist: {archive}'
    manifest = json.loads(
        (ROOT / 'tools/browser/runtime-assets.json').read_text())
    pin = manifest['archive']
    digest = hashlib.sha256()
    with archive.open('rb') as source:
        for chunk in iter(lambda: source.read(1 << 22), b''):
            digest.update(chunk)
    assert archive.stat().st_size == pin['bytes'] \
        and digest.hexdigest() == pin['sha256'], \
        f'{archive.name}: size/SHA256 mismatch against the manifest archive pin'
    expected = {entry['path']: entry for entry in manifest['files']}
    with tarfile.open(archive, 'r:') as bundle:
        members = [m for m in bundle.getmembers() if m.isfile()]
        names = [m.name for m in members]
        assert len(names) == len(expected) and set(names) == set(expected), \
            'archive entries differ from the pinned manifest'
        for member in members:
            entry = expected[member.name]
            digest = hashlib.sha256()
            size = 0
            with bundle.extractfile(member) as source:
                for chunk in iter(lambda: source.read(1 << 22), b''):
                    digest.update(chunk)
                    size += len(chunk)
            assert size == entry['bytes'] \
                and digest.hexdigest() == entry['sha256'], \
                (f'{member.name}: archive carries size/SHA256 {size}/'
                 f'{digest.hexdigest()[:12]}... but the file pin is '
                 f'{entry["bytes"]}/{entry["sha256"][:12]}...')


def t_directx_sets():
    # d3dcompiler set is exact, not a range: 44/45 exist in NEITHER pinned
    # upstream build (Debian wine 8.0 amd64 and WineHQ wine-stable 10.0
    # i386 agree on {33..43, 46, 47}); see runtime.md DirectX section.
    ranges = [('d3dx9_%d.dll', range(24, 44)), ('d3dx10_%d.dll', range(33, 44)),
              ('d3dcompiler_%d.dll', list(range(33, 44)) + [46, 47]),
              ('xaudio2_%d.dll', range(0, 10)),
              ('xinput1_%d.dll', range(1, 5))]
    for archive in ('wine64.zip', 'wine32.zip'):
        with zipfile.ZipFile(RUNTIME / archive) as z:
            names = {n.rsplit('/', 1)[-1] for n in z.namelist()}
        for pattern, values in ranges:
            for v in values:
                assert pattern % v in names, f'{archive}: missing {pattern % v}'
        for v in list(range(24, 33)) + [44, 45]:
            assert 'd3dcompiler_%d.dll' % v not in names, \
                f'{archive}: d3dcompiler_{v}.dll appeared; register census says it must not exist'


def t_vcpp_sets():
    legacy = ['msvcp%d.dll' % n for n in (60, 70, 71, 80, 90, 100, 110, 120)] + \
             ['msvcr%d.dll' % n for n in (70, 71, 80, 90, 100, 110, 120)] + \
             ['msvcrt.dll', 'msvcrt20.dll', 'msvcrt40.dll']
    set140 = ['msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
              'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll',
              'vcruntime140.dll', 'ucrtbase.dll', 'concrt140.dll']
    for archive in ('wine64.zip', 'wine32.zip'):
        with zipfile.ZipFile(RUNTIME / archive) as z:
            names = {n.rsplit('/', 1)[-1] for n in z.namelist()}
        for name in legacy + set140:
            assert name in names, f'{archive}: missing VC++ runtime {name}'
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        names32 = {n.rsplit('/', 1)[-1] for n in z.namelist()}
    with zipfile.ZipFile(RUNTIME / 'wine64.zip') as z:
        names64 = {n.rsplit('/', 1)[-1] for n in z.namelist()}
    assert 'vcruntime140_1.dll' in names64 and 'vcruntime140_1.dll' not in names32, \
        'vcruntime140_1 asymmetry changed (x64/ARM64-only upstream)'


def t_wineserver_dispatch():
    with zipfile.ZipFile(RUNTIME / 'wine64.zip') as z:
        lib = z.read('usr/lib/wine/wineserver')
        usr_bin = z.read('usr/bin/wineserver')
        server64 = z.read('usr/lib/wine/wineserver64')
    target = b'/usr/lib/wine/wineserver64'
    assert lib == target, f'usr/lib/wine/wineserver is not the ELF symlink: {lib[:40]!r}'
    assert usr_bin == target, f'usr/bin/wineserver target drifted: {usr_bin[:40]!r}'
    assert server64.startswith(b'\x7fELF'), 'wineserver64 is not an ELF binary'


def t_manifest():
    # The manifest's support-pack provenance row is generated from
    # dist/runtime/dependencies.json (gen_runtime_manifest.py reads that
    # exact file), so pre-build the comparison has nothing to compare
    # against: skip loudly, like the other support-pack checks. The
    # shipped-zip rows stay guarded in that state by the zip checks
    # above, which fail on any missing or drifted restored asset; once
    # dist/ exists this check runs the full byte-for-byte comparison.
    if not (ROOT / 'dist/runtime/dependencies.json').is_file():
        skip('manifest matches shipped zips',
             'no support pack built yet (dist/runtime/dependencies.json '
             'absent); the comparison runs once a build produces dist/')
        return
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'gen_manifest', ROOT / 'tools/rootfs64/gen_runtime_manifest.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    current = (ROOT / 'docs/known-gaps/runtime-artifact-manifest.md').read_text()
    assert current == module.build_manifest(), \
        'runtime-artifact-manifest.md drifted from shipped zips; regenerate it'


def _alsa_class_member(name):
    return ('alsa-lib/' in name or name.startswith('usr/share/alsa/')
            or name == 'etc/asound.conf')


def t_alsa_bridge():
    t64 = ROOT / 'tools/rootfs64/alsa'
    t32 = ROOT / 'tools/rootfs64/alsa32'

    def tree(root, keep):
        out = {}
        for q in root.rglob('*'):
            if q.is_file() and q.name != 'README':
                rel = q.relative_to(root).as_posix()
                if keep(rel):
                    out[rel] = q
        return out

    # (zip, expected {member: source file}) anchored on the staged trees.
    plan = [
        ('wine64.zip', tree(t64, lambda n: True)),
        ('wine32.zip', tree(t32, lambda n: '/alsa-lib/' in n)),
        ('glibc-rootfs32.zip',
         tree(t32, lambda n: n == 'etc/asound.conf'
              or n.startswith('usr/share/alsa/'))),
    ]
    # The staging sources themselves must still carry both OSS plugins
    # per arch, so an emptied tree cannot re-baseline the expectation.
    for label, expected in plan[:2]:
        plugins = {n.rsplit('/', 1)[-1] for n in expected
                   if '/alsa-lib/' in n}
        assert plugins == {'libasound_module_pcm_oss.so',
                           'libasound_module_ctl_oss.so'}, \
            f'{label}: staging tree plugin set drifted: {sorted(plugins)}'
    for archive, expected in plan:
        with zipfile.ZipFile(RUNTIME / archive) as z:
            infos = {i.filename: i for i in z.infolist()
                     if not i.filename.endswith('/')}
            actual = {n for n in infos if _alsa_class_member(n)}
            missing = sorted(set(expected) - actual)
            assert not missing, (
                f'{archive}: {len(missing)} ALSA bridge members missing, '
                f'e.g. {missing[:3]}')
            unexpected = sorted(actual - set(expected))
            assert not unexpected, (
                f'{archive}: {len(unexpected)} unexpected ALSA-class '
                f'members, e.g. {unexpected[:3]}')
            for name, src in expected.items():
                digest = sha(z.read(name))
                assert digest == sha(src.read_bytes()), (
                    f'{archive}: {name} bytes differ from the staged '
                    f'source {src.relative_to(ROOT)}')
    # glibc-rootfs64.zip's etc/asound.conf is printf-generated by
    # build-wine64-zip.sh (no source file in alsa/); it must stay
    # byte-identical to the canonical config in alsa32/.
    canonical = (t32 / 'etc/asound.conf').read_bytes()
    with zipfile.ZipFile(RUNTIME / 'glibc-rootfs64.zip') as z:
        assert 'etc/asound.conf' in z.namelist(), \
            'glibc-rootfs64.zip: etc/asound.conf missing'
        assert z.read('etc/asound.conf') == canonical, \
            'glibc-rootfs64.zip: etc/asound.conf differs from alsa32 canonical'


def t_fonts32():
    for name, pin in FONTS32_PINS.items():
        src = ROOT / 'tools/rootfs64/fonts32' / name
        assert sha(src.read_bytes()) == pin, f'fonts32/{name} hash drifted'
    with zipfile.ZipFile(RUNTIME / 'glibc-rootfs32.zip') as z:
        names = set(z.namelist())
        for name in FONTS32_PINS:
            entry = 'usr/share/fonts/truetype/dejavu/' + name
            assert entry in names, f'{entry} missing from glibc-rootfs32.zip'
            assert sha(z.read(entry)) == FONTS32_PINS[name], f'{entry} bytes != pin'


def t_fonts64():
    for name, pin in FONTS64_PINS.items():
        src = ROOT / 'tools/rootfs64/fonts64' / name
        assert sha(src.read_bytes()) == pin, f'fonts64/{name} hash drifted'
    with zipfile.ZipFile(RUNTIME / 'glibc-rootfs64.zip') as z:
        names = set(z.namelist())
        for name in FONTS64_PINS:
            entry = 'usr/share/fonts/truetype/dejavu/' + name
            assert entry in names, f'{entry} missing from glibc-rootfs64.zip'
            assert sha(z.read(entry)) == FONTS64_PINS[name], f'{entry} bytes != pin'


def t_prefix64_arial():
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'stage_wine32_fonts', ROOT / 'tools/browser/stage-wine32-fonts.py')
    fonts = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fonts)
    with zipfile.ZipFile(RUNTIME / 'prefix64.zip') as z:
        registry = z.read('home/username/.wine/user.reg')
    assert fonts.configure_replacements(registry) == registry, \
        'prefix64.zip user.reg: Arial font replacement missing or drifted (FD-86)'
    assert b'"Arial"="DejaVu Sans"' in registry, \
        'prefix64.zip user.reg: "Arial"="DejaVu Sans" replacement missing (FD-86)'
    with zipfile.ZipFile(RUNTIME / 'glibc-rootfs64.zip') as z:
        assert 'usr/share/fonts/truetype/dejavu/DejaVuSans.ttf' in z.namelist(), \
            'glibc-rootfs64.zip: DejaVuSans.ttf (the Arial replacement target) missing'


def t_cjk_pin():
    text = (ROOT / 'tools/browser/stage-dependencies.py').read_text()
    assert CJK_TTC_SHA in text, 'stage-dependencies.py lost the CJK ttc pin'
    assert 'wqy-microhei.ttc' in text, 'stage-dependencies.py lost the CJK staging'
    cache = ROOT / 'tools/rootfs64/cjk-font/archives'
    debs = list(cache.glob('*.deb')) if cache.exists() else []
    if not debs:
        skip('cjk deb cache', 'no cached deb; pin checked statically only')
        return
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'sd', ROOT / 'tools/browser/stage-dependencies.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.ensure_cjk_font()
    assert sha(result['ttc']) == CJK_TTC_SHA


def t_prefix64_built():
    built = ROOT / 'tools/rootfs64/dist/prefix64.zip'
    targets = [('web/runtime/prefix64.zip', RUNTIME / 'prefix64.zip')]
    if built.exists():
        targets.append(('tools/rootfs64/dist/prefix64.zip', built))
    else:
        skip('prefix64 built artifact', 'build-prefix64.sh output not present')
    for label, path in targets:
        with zipfile.ZipFile(path) as z:
            system = z.read('home/username/.wine/system.reg').decode(errors='replace')
        assert '[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion]' in system, label
        assert '[Software\\\\Microsoft\\\\.NETFramework]' in system, label


if __name__ == '__main__':
    check('winehq backports (source + shipped zip)', t_winehq_source_and_zip)
    check('wine8 loader backports (source + shipped zip)', t_wine8_backports_source_and_zip)
    check('wine10 win32u backport 32-bit (source + shipped zip)', t_wine10_backport32_source_and_zip)
    check('vkd3d payloads (source + shipped zip)', t_vkd3d_payloads)
    check('vkd3d payloads 32-bit (shipped zip)', t_vkd3d_payloads32)
    check('validate_service_runtime (shipped pairs)', t_service_runtime)
    check('gstreamer closure rides the support pack (absent from boot zips)', t_gstreamer_split)
    check('support pack member uniqueness + Vulkan shim pins (FD-53)', t_support_pack_member_uniqueness)
    check('retained runtime-assets archive agrees with the file pins (FD-73)', t_assets_archive)
    check('DirectX redist sets complete (both zips)', t_directx_sets)
    check('VC++ runtime sets (both zips, asymmetries pinned)', t_vcpp_sets)
    check('wineserver dispatch is ELF (no /bin/sh script)', t_wineserver_dispatch)
    check('manifest matches shipped zips', t_manifest)
    check('ALSA-to-OSS bridge members (staging-tree anchored, both arches)', t_alsa_bridge)
    check('fonts32 faces (source + shipped zip)', t_fonts32)
    check('fonts64 faces (source + shipped zip)', t_fonts64)
    check('prefix64 Arial font replacement (FD-86)', t_prefix64_arial)
    check('CJK font pin', t_cjk_pin)
    check('prefix64 built artifact sections', t_prefix64_built)
    print(f'{len(FAILURES)} failures, {len(SKIPS)} skips')
    sys.exit(1 if FAILURES else 0)
