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
  4d. prefix64.zip's system.reg carries the PlugPlay service, the
     winebus/wineusb bus-driver services and the ROOT\\WINE bus-device
     enumeration, merged from tools/rootfs64/prefix64-reg.txt (FD-189);
     without them the Service Control Manager answers error 1060 for
     all three services and the root enumeration is empty on the
     64-bit side, while the 32-bit prefix (a real wineboot's output)
     has always carried them.
  5. stage-dependencies.py pins the CJK font (deb + ttc sha256) and, when
     the download cache exists, the cached deb's ttc matches the pin.
  6. When build-prefix64.sh output exists (tools/rootfs64/dist/), its
     system.reg carries the CurrentVersion + .NETFramework sections. and
     the FD-189 Plug-and-Play sections.

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
    'user32.dll': 'b7b39a1b3daa48ffc273328359340f210bbca996ea2135ee130c7e93005da338',
    'kernelbase.dll': '5d94b2639e2e408da5fc4fd2fc27466b990ced859603ef3e123ff4ef85c3f251',
    'kernel32.dll': 'ccf67fb5bba80af571952c5beefae8c4ca9f92ca4787da9a2424fc4a49fcd2e1',
    'iphlpapi.dll': '1653f8600391d81727a9391cffbed3c5f9bad2a76564c5fd4f2029dc815b4f80',
    'win32u.dll': '242c95ee3a1175fda6ff95f11b4f880df81e548a33b19c307a8d2eb95429136f',
    # The unix half of win32u (FD-01): the desktop-wait patch lives in
    # this module; pinning only the DLL is what let the patch ship in
    # no binary (FD-43).
    'win32u.so': 'd1a2fa71542c851eea4ec0390d7267ada98a0032226a35f9ca44eb9de84b9f78',
    # FD-167: the MSVC runtime throw helpers a bundled demo library
    # imports, and SetupDiGetDeviceInterfaceAlias, stop being spec
    # stubs (see wine8-backports-x86_64/README.md).
    'msvcp140.dll': '8525ebf0220f595714acef764ba6e3fc2e542edbe158a43fad1ebf7cc2c8b940',
    # (setupapi.dll is pinned below, in the FD-171 block: the composed
    # build carries both the FD-167 SetupDiGetDeviceInterfaceAlias and the
    # FD-171 CM_* family in it, so its bytes differ from either lane's.)
    # FD-169: the Microsoft C runtime family, rebuilt from the same
    # patched Wine 8.0 tree (wine8-msvcrt-fd169-crt.patch).
    'ucrtbase.dll': 'daed274b229dfc1c5d9ae5780dfab75d361796243c0432088b171dae1f63bbd7',
    'msvcrt.dll': 'f64900841b15e7d577d2ec610578e0ee2f01ebf9de4c148aad5431ee03f030c0',
    'msvcr70.dll': 'e7b1b1a68f16ade508a32aa2523f32ff60b2a196ff4697fd72c61c2e05ec68e5',
    'msvcr71.dll': '534758d16a7a3d9aa8de1a94db3dc019871405a0a9e8cdbe0e3e850f9942474d',
    'msvcr80.dll': 'eae4c063710b284dde64d13a533e579d714e6cb6aefc53015aca76746dbcfe82',
    'msvcr90.dll': '208802fc10e132186444bfe4992f0c96c7093291ec114cd412affbc258bb622b',
    'msvcr100.dll': '97243e6863c1bb7d8faeceae715aa5b550c7ab2f7586ad7c125de4533807b996',
    'msvcr110.dll': 'd260db9d92628d9d8ed74b058c2b29a4b449eabbe4d6f3ddb2a341be559e8649',
    'msvcr120.dll': '33f46fac7eab905612bcf3c8d290f0842a4d28d359059f72c09acd422cfa623e',
    # FD-170 whole-DLL backports (wine8-fd170-contracts.patch): Wine
    # 10.0's bluetoothapis + the FD-167 contract bodies, the FD-167
    # winusb no-device contracts, Wine 10.0's netutils NetApiBuffer*.
    'bluetoothapis.dll': '8e53bf81f6d734d47a0eb4fdd0cecadf88e76afa92714842c2804407ecbf57c7',
    'winusb.dll': '1135959a3ff6b46e31d539a4ef0a79a8882c622bc1217ad78f17ae2833bd742f',
    'netutils.dll': '696531741bb38ecf06ba0f708340aea8b6838a59f381654bb2f4401c0aa1fa92',
    # FD-171 (2026-11-05): the long-tail contract backports
    # (wine8-fd171-contracts.patch). kernelbase/kernel32 above are
    # re-pinned for the same patch (HeapSummary, ReadConsoleInputEx,
    # RegisterWaitForInputIdle); user32/iphlpapi/win32u keep their
    # previous bytes — the patch does not touch them.
    'cfgmgr32.dll': '4df21c0d34e66b103c8039dd5faa5e941ca6b8a91872f8d858bfa3ff58171514',
    'combase.dll': 'f6051d5d254e888fd190692462fd2e897cc1619dd9e2c7ba8733a33093140cad',
    'cryptext.dll': 'ab6e0b932923e4db5a757f0546e11cfd3a237b80fc106126da30221dee51b8d7',
    'd2d1.dll': '1afe8f059055abc37c92dcb7bbb2909bd143f4dcbf5d8d0d0a3d8bcaf4b5abc2',
    'hid.dll': '16b08432e431c4dc9c0bc8bd637f6ded3b691556437334a18ab2a735b57939e8',
    'mf.dll': 'f61db07e215a171fc53dfe111324acea9e4f3699f3d4458cd87227205dee208b',
    'mfplat.dll': '84a8d777a398ceec8cd63d5072a09fd7a634a2d4e081836999f48a6a2070d2e6',
    'mscms.dll': 'e1616c36ea928b5058187e454ef676cc0243ca6d091767d9ad30515bacd89958',
    'ntdll.dll': '904c4e6107acb560d7bd3f7690efb6baf8a2d8e2fea08150719e7989233dd0bb',
    'pdh.dll': 'df56ea38553a8ae6ef07d4db44f949c57f5fdb883bf2ae2c6c29a46d9ac51330',
    'prntvpt.dll': 'afb5b277b57688931ac95170bea45ead1495bc9d941c424de53cc656910663a1',
    'propsys.dll': '6f5c0e0b534cd29c127bc7a470285c9bb6aff35a9c39ca6f8b84952512a0a32c',
    'setupapi.dll': '661d8fa545f6793f903196081d82eb1a3aeffbd888342b990e71768bffa08c73',
    'sspicli.dll': 'db2d5a16a94abf7c3fc8d6726415598ccbba91d2838f9f45e9cf0e67504846e8',
    'vcomp.dll': '29e766d4382d2374526f4516d9b67c87a1d35edfc5a808b0a3bd92693b4db97c',
    'vcomp100.dll': 'a264b5c4b94bc4919b54189469b6eb23d979f38491362c8a5562c5668b070f7d',
    'vcomp110.dll': 'd2e0461eabc57f27162e6d31fa380b2c7e7faa81873914803c429c400ac2eef4',
    'vcomp120.dll': '31c21ec9642e09edacd982f934403fafdb64f3f27f8c2ed5f32f27061284e313',
    'vcomp140.dll': 'fedbfc38b1ac96500cad4767c2a4ff9d4189a74640b9a56b1d6564e73264bcd0',
    'vcomp90.dll': '9a6dcc07ee03f51ba16bc5a34993fc15f1666f909505d5f6284427d3f88ab53b',
    'windows.networking.dll': '26e57eae67a5005f9716bfc4fcd91aa13172ddef9197b7703a13e69dd632dda9',
    'wintypes.dll': '50b105d1b7725f4fb6147f923539b54fd16af621c007ecefc5f324dee48693ba',
    'wofutil.dll': '924ff1fd2c1c688a7433290b211e05e0fb2c7426f097e40937a7ee4fdfe9e1f1',
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
    'bluetoothapis.dll': 'a0544c64170238beb521b4683c95bc4b9fa5f92b3a0ae570c2242079240eef37',
    'bthprops.cpl': 'c79d780c00638dae62787ebc6b56ac3fb303ce484f87514c2e6f8ad317bb51b7',
    'mfplat.dll': '5fe1cd0d1ec886c2c7cbe79500d16792bbb265a7f1dcd2ab26812a9126819aed',
    'wevtapi.dll': '8afe9e718174bb0a8e6d020f48b107497d1b4dafe3456614fd6ed9126bec8486',
    'winusb.dll': 'd83be550a7fa79e3a2573a0cc11bcb5cd2275fb2828a8e18cf3a3f350532536e',
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
    'msvcr100.dll': 'c8e9bbb1aadcda1584fe6fa1c7371ab1d79d7f7ea01c6a9da18ee9086fb863e6',
    'msvcr110.dll': 'fcbaef875c3201f903adf82f01d9ad956f40eeaec1cf04bc6b4c97f532265cff',
    'msvcr120.dll': '5aa48ebd548dd5d985856383efc8ecc62b2cd6b75e0aa325de145d39535bd694',
    'msvcr70.dll': '0406d46a99a8926f1af0a1a9f06c97a6bd8790bd8394f6ed5c0ddd3a4c1ec266',
    'msvcr71.dll': '641c1554fbea10f82eff4bc06f520c9e6bfea744a41d2cf6ab88c77c08363b69',
    'msvcr80.dll': '13b75e2323112b54d741eee2f7565c505f63db60cc1e75f519b49acb6d8776cc',
    'msvcr90.dll': '7635463213c7588672b94915b9bb3cb37a6a16d922bfb93144d1ecf3b4f615c2',
    'msvcrt.dll': '03c832d2d826fc334dec79714295219c095cf91261263a46a98d134220a18b6b',
    'ucrtbase.dll': 'dda971fcfdea0abdc94aab827360278824d0b9f5c0eb1ef5048f2c27236cf5f8',
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
# The FD-171 i386 PE set: 21 DLLs rebuilt from the pinned wine-10.0
# source with wine10-fd171-contracts.patch (see
# tools/rootfs32/wine10-fd171-i386/README.md), substituted into the
# wine32 payload by build-wine32-base.sh under the same pins.
WINE10_FD171_PINS32 = {
    'cfgmgr32.dll': '34ceba5a529294a15710977e496170c9b62c90b51203eafea5f6c7afb9ed5e12',
    'combase.dll': '3b7a291a63eddc5d8a73a43b87a8485e11cecf4ac6627e6d21764857d9e34b61',
    'cryptext.dll': '53e417c2c68de322d5a7793dea2275709013e05e6dba61e7b1008db460efe1a1',
    'd2d1.dll': '642621d661a1b22bba36b579460e2bd943c7252acae2275ef9a20f6af7a8a9af',
    'hid.dll': '34b191001a4c8835a7cd1388d9d141e814ff7d4a087538c150fa0dc80b0aaa6a',
    'kernel32.dll': '752d40eed73ec976eb6d99dbb7cd5896952ba640ee9d32797f018c30c59cda7a',
    'kernelbase.dll': '4959557be956b09d4b79d379c1354ad7edb13673c5bffdd4bde2f5d34de0f31c',
    'mscms.dll': '331b045cfbc8cf93acd9f820f4060f73d83c6a8132db07fab88a9c3b9521bd23',
    'ntdll.dll': '5f2f866d8d0bf3529965f49f0362ed46ce9366677423417016b24391da28581e',
    'pdh.dll': '5d79d70f7a2942d159090ea0ecb8407da5370d144adcc19f025af2b62876a68c',
    'prntvpt.dll': '36e998791374b6de07747d91ffe93ea3167e0c407194aca9c0475c9c3ba19133',
    'setupapi.dll': '5fcef9aa9344772bdb1ee7c71517f3abfc456850aa4c9e64965114e02971d29f',
    'sspicli.dll': '0c4b8cb4234d468d8b1cb742b6ef4fc7a9e9b61634709aadc8561431d8e8a719',
    'vcomp.dll': '739020d398a48f14b8c290e036e2c0da9201a623756453c9feeccaf1f937d9b4',
    'vcomp100.dll': '5e62b5281b786cad54ea07a671d2b2630f1e4ed61e03f51f1b128882386f5036',
    'vcomp110.dll': '5ccad41c55939f6a084de1aaabe3633645cac511a379c0be634f34124e73b81c',
    'vcomp120.dll': '4d1a0e4ce170a21ae959fcce87a0775c8ae5fb48f1207f7f0d896b7f686a1a60',
    'vcomp140.dll': 'cd95ffcd6da529438863bc85fc90c7b10da28cf3e37ca0e30729baab1db23d63',
    'vcomp90.dll': '774c9483c79d91fea4e4d3bbbd7f40db22804714ac1d2a15d471f3631a08a075',
}


def t_wine10_fd171_source_and_zip():
    for name, pin in WINE10_FD171_PINS32.items():
        src = ROOT / 'tools/rootfs32/wine10-fd171-i386' / name
        assert sha(src.read_bytes()) == pin, f'{name} (FD-171 i386) source hash drifted from pin'
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        members = {n: z.read(n) for n in z.namelist()}
    for name, pin in WINE10_FD171_PINS32.items():
        hits = [n for n in members if n.endswith('/' + name)]
        assert hits, f'{name} missing from shipped wine32.zip'
        assert sha(members[hits[0]]) == pin, f'{name} in wine32.zip != pinned FD-171 bytes'


# The FD-167 + FD-171 combined i386 PE set: propsys.dll and mf.dll
# are touched by both lanes with disjoint functions (FD-167
# VariantCompare / MFCreateDeviceSource, FD-171
# PropVariantGetStringElem / MFTranscodeGetAudioOutputAvailableTypes),
# so neither single-lane build carries both. The combined build
# (tools/rootfs32/build-wine10-fd167-fd171.sh, both patches applied to
# the pinned wine-10.0 source) is the only source of these two members
# in wine32.zip; the single-lane builds stay in their lane dirs but
# are no longer substituted (see build-wine32-base.sh).
WINE10_FD167_FD171_PINS32 = {
    'mf.dll': '98800d6b755221094fc67d4e3ce023f41546873df893b1f7e83609ed67457a59',
    'propsys.dll': 'f3116589e19adadf5cbe4182d40e37899f70e8738a5562a56921517b692b6c7f',
}


def t_wine10_fd167_fd171_32_source_and_zip():
    for name, pin in WINE10_FD167_FD171_PINS32.items():
        src = ROOT / 'tools/rootfs32/wine10-fd167-fd171-i386' / name
        assert sha(src.read_bytes()) == pin, f'{name} (FD-167+FD-171 i386) source hash drifted from pin'
    with zipfile.ZipFile(RUNTIME / 'wine32.zip') as z:
        members = {n: z.read(n) for n in z.namelist()}
    for name, pin in WINE10_FD167_FD171_PINS32.items():
        member = f'opt/wine-stable/lib/wine/i386-windows/{name}'
        assert member in members, f'{member} missing from shipped wine32.zip'
        assert sha(members[member]) == pin, f'{name} in wine32.zip != pinned FD-167+FD-171 bytes'


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


# FD-188: the drivers/ mirror. Wine's driver loader opens every driver by
# the absolute path C:\windows\system32\drivers\<name>.sys, which the
# shipped prefixes resolve through their system32.link into the wine zips'
# windows dirs. Each wine zip must therefore mirror every .sys file from
# its windows dir into a drivers/ subdirectory, byte-identical, with no
# other files there -- a missing mirror made every driver load fail
# STATUS_DLL_NOT_FOUND (c0000135) / ZwLoadDriver c0000142. The mirror is
# laid down by build-wine32-base.sh (32-bit) and build-wine64-zip.sh /
# stage-wine64-drivers.sh (64-bit).
DRIVERS_DIRS = {
    'wine64.zip': 'usr/lib/x86_64-linux-gnu/wine/x86_64-windows',
    'wine32.zip': 'opt/wine-stable/lib/wine/i386-windows',
}


def t_drivers_dir():
    for archive, windir in DRIVERS_DIRS.items():
        with zipfile.ZipFile(RUNTIME / archive) as z:
            members = {n: z.read(n) for n in z.namelist() if not n.endswith('/')}
        sysnames = sorted(n for n in members
                          if n.startswith(windir + '/') and n.endswith('.sys')
                          and '/' not in n[len(windir) + 1:])
        assert sysnames, f'{archive}: no .sys files in {windir}'
        for n in sysnames:
            mirror = windir + '/drivers/' + n.rsplit('/', 1)[-1]
            assert mirror in members, f'{mirror} missing from {archive}'
            assert sha(members[mirror]) == sha(members[n]), \
                f'{mirror} in {archive} != its .sys sibling bytes'
        extra = sorted(n for n in members
                       if n.startswith(windir + '/drivers/') and n.rsplit('/', 1)[-1]
                       not in {s.rsplit('/', 1)[-1] for s in sysnames})
        assert not extra, f'{archive}: unexpected files in drivers/: {extra}'


INF_NAMES = ("input.inf", "winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf")


def t_driver_inf():
    # FD-188: the driver .inf files (wine.inf [InfFiles]) must be in the
    # wine zips' share/wine/ -- the producer, so a real wineboot copies
    # them into a prefix's C:\windows\inf\ -- and in the shipped 32-bit
    # prefix's inf/ (the overlay: prefix32.zip was cut before the wine
    # zips carried them). Byte-identical to the version-matched sources.
    for archive, srcdir, placements in (
        ("wine32.zip", ROOT / "tools/rootfs32/wine-inf",
         ("opt/wine-stable/share/wine", "usr/share/wine")),
        ("wine64.zip", ROOT / "tools/rootfs64/wine-inf",
         ("usr/share/wine",)),
    ):
        with zipfile.ZipFile(RUNTIME / archive) as z:
            for pl in placements:
                for n in INF_NAMES:
                    member = pl + "/" + n
                    assert member in z.namelist(), f"{member} missing from {archive}"
                    assert sha(z.read(member)) == sha((srcdir / n).read_bytes()), \
                        f"{member} in {archive} != tree source"
    with zipfile.ZipFile(RUNTIME / "prefix32.zip") as z:
        for n in INF_NAMES:
            member = "home/username/.wine/drive_c/windows/inf/" + n
            assert member in z.namelist(), f"{member} missing from prefix32.zip"
            assert sha(z.read(member)) == sha((ROOT / "tools/rootfs32/wine-inf" / n).read_bytes()), \
                f"{member} in prefix32.zip != tree source"


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


def t_prefix64_pnp():
    fragment = (ROOT / 'tools/rootfs64/prefix64-reg.txt').read_text()
    with zipfile.ZipFile(RUNTIME / 'prefix64.zip') as z:
        system = z.read('home/username/.wine/system.reg').decode(errors='replace')
    for section in ('[System\\\\CurrentControlSet\\\\Services\\\\PlugPlay]',
                    '[System\\\\CurrentControlSet\\\\Services\\\\winebus]',
                    '[System\\\\CurrentControlSet\\\\Services\\\\wineusb]',
                    '[System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEBUS]',
                    '[System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEUSB]'):
        assert section in system, \
            f'prefix64.zip system.reg: {section} missing (FD-189)'
    merged, added = prefixes.merge_registry_fragment(system, fragment)
    assert added == [], \
        f'prefix64.zip system.reg drifted from prefix64-reg.txt (FD-189): {added}'


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
        for section in ('[System\\\\CurrentControlSet\\\\Services\\\\PlugPlay]',
                        '[System\\\\CurrentControlSet\\\\Services\\\\winebus]',
                        '[System\\\\CurrentControlSet\\\\Services\\\\wineusb]',
                        '[System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEBUS]',
                        '[System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEUSB]'):
            assert section in system, f'{label}: {section} missing (FD-189)'


if __name__ == '__main__':
    check('winehq backports (source + shipped zip)', t_winehq_source_and_zip)
    check('wine8 loader backports (source + shipped zip)', t_wine8_backports_source_and_zip)
    check('FD-167 i386 PE backports (source + shipped zip)', t_wine10_fd167_32_source_and_zip)
    check('FD-169 i386 CRT rebuilds (source + shipped zip)', t_wine10_fd169_crt32_source_and_zip)
    check('FD-171 i386 PE backports (source + shipped zip)', t_wine10_fd171_source_and_zip)
    check('FD-167+FD-171 combined i386 PE (source + shipped zip)', t_wine10_fd167_fd171_32_source_and_zip)
    check('wine10 win32u backport 32-bit (source + shipped zip)', t_wine10_backport32_source_and_zip)
    check('vkd3d payloads (source + shipped zip)', t_vkd3d_payloads)
    check('vkd3d payloads 32-bit (shipped zip)', t_vkd3d_payloads32)
    check('validate_service_runtime (shipped pairs)', t_service_runtime)
    check('drivers/ mirror exact (both zips, FD-188)', t_drivers_dir)
    check('driver .inf files in share/wine/ + prefix32 inf/ (FD-188)', t_driver_inf)
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
    check('prefix64 PlugPlay + bus services (FD-189)', t_prefix64_pnp)
    check('CJK font pin', t_cjk_pin)
    check('prefix64 built artifact sections', t_prefix64_built)
    print(f'{len(FAILURES)} failures, {len(SKIPS)} skips')
    sys.exit(1 if FAILURES else 0)
