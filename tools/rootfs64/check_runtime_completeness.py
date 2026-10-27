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
    'msmpeg2vdec.dll': 'bbf91fc84f2bc66c70802e41e5ff1ee2e18335ef9cf07769957b0fb37484d046',
    'msvcp140_codecvt_ids.dll': '2215e3312bcc817c628669ec69baff3f050ec0bef91c099e0d12fd66f2c3d9b1',
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
VKD3D_PINS = {
    'bw64vulkan.dll': '3d0c3c987282850ba9c98e1d0f04741022aba02755e7d513079ef908c090fa9f',
    'd3d12.dll': 'd77fdad5d3a1426287cec7e642ca8e43f1f52c2f54c7de3bb24a64174ec2284a',
    'd3d12core.dll': 'd2bd847a7d564fb64aee51461998b0014767b73a3b4f0ec46a977702da0076b3',
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
        missing = paths - support_names
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
    check('vkd3d payloads (source + shipped zip)', t_vkd3d_payloads)
    check('validate_service_runtime (shipped pairs)', t_service_runtime)
    check('gstreamer closure rides the support pack (absent from boot zips)', t_gstreamer_split)
    check('DirectX redist sets complete (both zips)', t_directx_sets)
    check('VC++ runtime sets (both zips, asymmetries pinned)', t_vcpp_sets)
    check('wineserver dispatch is ELF (no /bin/sh script)', t_wineserver_dispatch)
    check('manifest matches shipped zips', t_manifest)
    check('ALSA-to-OSS bridge members (staging-tree anchored, both arches)', t_alsa_bridge)
    check('fonts32 faces (source + shipped zip)', t_fonts32)
    check('CJK font pin', t_cjk_pin)
    check('prefix64 built artifact sections', t_prefix64_built)
    print(f'{len(FAILURES)} failures, {len(SKIPS)} skips')
    sys.exit(1 if FAILURES else 0)
