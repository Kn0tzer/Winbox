#!/usr/bin/env python3
"""Runtime/prefix completeness guard (docs/known-gaps/runtime.md).

Verifies the claims the runtime register makes about the *shipped*
artifacts and the *source* payloads, offline:
  1. winehq10 backport DLLs in tools/rootfs64/winehq10-x86_64 match the
     sha256 pins in their README, and are present (with those hashes) in
     web/runtime/wine64.zip.
  2. prepare_prefixes.validate_service_runtime passes for the shipped
     wine64.zip/prefix64.zip and wine32.zip/prefix32.zip pairs (services,
     HID, backport DLLs, gstreamer plugin set).
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
     zips byte-for-byte (regenerate: gen_runtime_manifest.py).
  4. glibc-rootfs32.zip carries the DejaVu Bold/Mono faces from fonts32/
     with the README-pinned hashes.
  5. stage-dependencies.py pins the CJK font (deb + ttc sha256) and, when
     the download cache exists, the cached deb's ttc matches the pin.
  6. When build-prefix64.sh output exists (tools/rootfs64/dist/), its
     system.reg carries the CurrentVersion + .NETFramework sections.

Exit code 0 = every non-skipped check passed. Skips are printed, never
silent. Run directly or via web/tests/suite/unit/runtime-completeness.test.mjs.
"""
import hashlib
import sys
import zipfile
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
    'd3d12core.dll': 'a00b2ea3454d2569dcebf52bb99d79f18c8153071990601953773e0d9537cd1e',
    'msmpeg2vdec.dll': 'bbf91fc84f2bc66c70802e41e5ff1ee2e18335ef9cf07769957b0fb37484d046',
    'msvcp140_codecvt_ids.dll': '2215e3312bcc817c628669ec69baff3f050ec0bef91c099e0d12fd66f2c3d9b1',
}
FONTS32_PINS = {
    'DejaVuSans-Bold.ttf': '5c1247acef7f2b8522a31742c76d6adcb5569bacc0be7ceaa4dc39dd252ce895',
    'DejaVuSansMono.ttf': 'c805f9436dbc268644c1d9584f01a601a653e028e08fd74b9b949f6cf8304d88',
    'DejaVuSansMono-Bold.ttf': '3a3c502eeff669a231549e80df9f7c49de109bafe303170409e905d0b31a38fe',
}
CJK_TTC_SHA = '2420e8078af796b19a3f6ef13de527a1a91c1e7171eea115926c614ced1009b3'
RUNTIME = ROOT / 'web/runtime'


def wine64_members():
    with zipfile.ZipFile(RUNTIME / 'wine64.zip') as z:
        return {n: z.read(n) for n in z.namelist()
                if n.endswith(('d3d12core.dll', 'msmpeg2vdec.dll', 'msvcp140_codecvt_ids.dll'))}


def t_winehq_source_and_zip():
    for name, pin in WINEHQ_PINS.items():
        src = ROOT / 'tools/rootfs64/winehq10-x86_64' / name
        assert sha(src.read_bytes()) == pin, f'{name} source hash drifted from README pin'
    members = wine64_members()
    for name, pin in WINEHQ_PINS.items():
        hits = [n for n in members if n.endswith(name)]
        assert hits, f'{name} missing from shipped wine64.zip'
        assert sha(members[hits[0]]) == pin, f'{name} in wine64.zip != pinned bytes'


def t_service_runtime():
    prefixes.validate_service_runtime(RUNTIME / 'prefix64.zip', RUNTIME / 'wine64.zip')
    prefixes.validate_service_runtime(RUNTIME / 'prefix32.zip', RUNTIME / 'wine32.zip')


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
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'gen_manifest', ROOT / 'tools/rootfs64/gen_runtime_manifest.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    current = (ROOT / 'docs/known-gaps/runtime-artifact-manifest.md').read_text()
    assert current == module.build_manifest(), \
        'runtime-artifact-manifest.md drifted from shipped zips; regenerate it'


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
    check('validate_service_runtime (shipped pairs)', t_service_runtime)
    check('DirectX redist sets complete (both zips)', t_directx_sets)
    check('VC++ runtime sets (both zips, asymmetries pinned)', t_vcpp_sets)
    check('wineserver dispatch is ELF (no /bin/sh script)', t_wineserver_dispatch)
    check('manifest matches shipped zips', t_manifest)
    check('fonts32 faces (source + shipped zip)', t_fonts32)
    check('CJK font pin', t_cjk_pin)
    check('prefix64 built artifact sections', t_prefix64_built)
    print(f'{len(FAILURES)} failures, {len(SKIPS)} skips')
    sys.exit(1 if FAILURES else 0)
