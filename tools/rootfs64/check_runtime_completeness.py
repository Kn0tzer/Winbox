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
     xaudio2_0..2_9, xinput1_1..1_4).
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
                if n.endswith(('d3d12core.dll', 'msmpeg2vdec.dll'))}


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
    if not built.exists():
        skip('prefix64 built artifact', 'build-prefix64.sh output not present')
        return
    with zipfile.ZipFile(built) as z:
        system = z.read('home/username/.wine/system.reg').decode(errors='replace')
    assert '[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion]' in system
    assert '[Software\\\\Microsoft\\\\.NETFramework]' in system


if __name__ == '__main__':
    check('winehq backports (source + shipped zip)', t_winehq_source_and_zip)
    check('validate_service_runtime (shipped pairs)', t_service_runtime)
    check('DirectX redist sets complete (both zips)', t_directx_sets)
    check('fonts32 faces (source + shipped zip)', t_fonts32)
    check('CJK font pin', t_cjk_pin)
    check('prefix64 built artifact sections', t_prefix64_built)
    print(f'{len(FAILURES)} failures, {len(SKIPS)} skips')
    sys.exit(1 if FAILURES else 0)
