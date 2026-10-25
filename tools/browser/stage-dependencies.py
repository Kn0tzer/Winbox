#!/usr/bin/env python3
"""Build the shared, lazy Windows runtime: Wine Mono, Wine Gecko, OpenAL Soft,
the graphics bridge and the GStreamer media plugin closure.
Application files are never included."""
import argparse
import hashlib
import io
import importlib.util
import json
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('wine_mono', ROOT / 'tools/rootfs64/prepare-mono32.py')
mono = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mono)

# The two bundled Wine runtimes are pinned to different Wine releases, and
# mshtml only accepts the exact Gecko version it was built against.
GECKO = {
    'wine64': {
        'version': '2.47.3',
        'dir': 'wine-gecko-2.47.3-x86_64',
        'url': 'https://dl.winehq.org/wine/wine-gecko/2.47.3/wine-gecko-2.47.3-x86_64.tar.xz',
        'sha256': '0beac419c20ee2e68a1227b6e3fa8d59fec0274ed5e82d0da38613184716ef75',
    },
    'wine32': {
        'version': '2.47.4',
        'dir': 'wine-gecko-2.47.4-x86',
        'url': 'https://dl.winehq.org/wine/wine-gecko/2.47.4/wine-gecko-2.47.4-x86.tar.xz',
        'sha256': '2cfc8d5c948602e21eff8a78613e1826f2d033df9672cace87fed56e8310afb6',
    },
}
OPENAL_VERSION = '1.25.2'
OPENAL = {
    'version': OPENAL_VERSION,
    'url': f'https://github.com/kcat/openal-soft/releases/download/{OPENAL_VERSION}/openal-soft-{OPENAL_VERSION}-bin.zip',
    'sha256': '67a0c4b800bd860c93c04f38caf8cbe4875f9c84700ac430efc451f70e265434',
    'files': {
        'i386': f'openal-soft-{OPENAL_VERSION}-bin/bin/Win32/soft_oal.dll',
        'x86_64': f'openal-soft-{OPENAL_VERSION}-bin/bin/Win64/soft_oal.dll',
    },
    'targets': {
        'i386': 'opt/wine-stable/lib/wine/i386-windows/OpenAL32.dll',
        'x86_64': 'usr/lib/x86_64-linux-gnu/wine/x86_64-windows/OpenAL32.dll',
    },
}

# CJK fallback font. The boot zips ship no CJK-capable face at all (DejaVu
# has no CJK glyphs; Wine's fixedsys_jp/jvgasys are bitmap stubs), so any
# CJK-locale game renders tofu. WenQuanYi Micro Hei is a single 5 MB TTC
# covering CJK Unified Ideographs + kana + hangul, licensed Apache-2.0 or
# GPL-3+ with the font exception (freely redistributable, unlike Noto CJK
# at ~120 MB per full package). Pinned from the Debian bookworm pool —
# the same provenance pattern as the Gecko/OpenAL pins above. It rides
# the lazy support pack, not the boot zips: fontconfig scans
# /usr/share/fonts recursively (both rootfs fonts.conf setups do), and
# the package's own 65-wqy-microhei.conf wires the zh/ja/ko aliases.
CJK_FONT = {
    'name': 'WenQuanYi Micro Hei',
    'version': '0.2.0-beta-3.1',
    'url': 'https://deb.debian.org/debian/pool/main/f/fonts-wqy-microhei/fonts-wqy-microhei_0.2.0-beta-3.1_all.deb',
    'debSha256': '239163df984eea5c9da3b551aee374936222cdbd67f1a1be3cdcf3d76143ad66',
    'ttcSha256': '2420e8078af796b19a3f6ef13de527a1a91c1e7171eea115926c614ced1009b3',
    'license': 'Apache-2.0 or GPL-3+ with font exception',
    'members': {
        'ttc': 'usr/share/fonts/truetype/wqy/wqy-microhei.ttc',
        'conf': 'etc/fonts/conf.avail/65-wqy-microhei.conf',
        'copyright': 'usr/share/doc/fonts-wqy-microhei/copyright',
    },
}


def ensure_cjk_font():
    """Download the pinned fonts-wqy-microhei .deb and pull the TTC,
    fontconfig alias and license out of its data.tar (pure-python ar/tar
    parsing; .deb layout is stable and the hashes are pinned)."""
    cache = ROOT / 'tools/rootfs64/cjk-font'
    archive_path = cache / 'archives' / Path(CJK_FONT['url']).name
    download(CJK_FONT['url'], archive_path, CJK_FONT['debSha256'])
    data = archive_path.read_bytes()
    assert data[:7] == b'!<arch>', 'not a .deb (ar archive)'
    offset = 8
    payload = None
    while offset < len(data):
        header = data[offset:offset + 60]
        name = header[:16].decode().strip()
        size = int(header[48:58].decode().strip())
        body = data[offset + 60:offset + 60 + size]
        if name.startswith('data.tar'):
            payload = body
            break
        offset += 60 + size + (size % 2)
    assert payload is not None, 'no data.tar in .deb'
    result = {}
    with tarfile.open(fileobj=io.BytesIO(payload)) as tar:
        for key, member in CJK_FONT['members'].items():
            extracted = tar.extractfile('./' + member) or tar.extractfile(member)
            assert extracted is not None, f'{member} missing from .deb'
            result[key] = extracted.read()
    if digest(result['ttc']) != CJK_FONT['ttcSha256']:
        raise SystemExit('wqy-microhei.ttc sha256 mismatch against pin')
    return result


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tree_digest(root):
    """Stable digest of names, sizes and bytes, independent of mtimes."""
    entries = []
    for path in sorted(root.rglob('*')):
        if path.is_file():
            data = path.read_bytes()
            entries.append(f'{path.relative_to(root).as_posix()}\0{len(data)}\0{digest(data)}')
    return digest('\n'.join(entries).encode())


def download(url, destination, expected):
    if destination.exists() and digest(destination.read_bytes()) == expected:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + '.part')
    request = urllib.request.Request(url, headers={'User-Agent': 'winbox-dependency-stager/1'})
    with urllib.request.urlopen(request, timeout=120) as response, temporary.open('wb') as output:
        shutil.copyfileobj(response, output)
    actual = digest(temporary.read_bytes())
    if actual != expected:
        temporary.unlink(missing_ok=True)
        raise SystemExit(f'{url}: expected sha256 {expected}, got {actual}')
    temporary.replace(destination)


def ensure_gecko():
    cache = ROOT / 'tools/rootfs64/gecko'
    result = {}
    for name, entry in GECKO.items():
        directory = cache / entry['dir']
        if not (directory / 'VERSION').is_file() or not (directory / 'xul.dll').is_file():
            archive = cache / 'archives' / Path(entry['url']).name
            download(entry['url'], archive, entry['sha256'])
            with tarfile.open(archive, 'r:xz') as tar:
                tar.extractall(cache, filter='data')
        if not (directory / 'VERSION').is_file() or not (directory / 'xul.dll').is_file():
            raise SystemExit(f'Wine Gecko {name} extraction is incomplete: {directory}')
        result[name] = {**entry, 'root': directory,
                        'files': sorted(path for path in directory.rglob('*') if path.is_file()),
                        'treeSha256': tree_digest(directory)}
    return result


# GStreamer demux/decode plugins + their DT_NEEDED closure, staged by
# tools/rootfs64/prepare-gstreamer.py from sha256-pinned Debian bookworm
# debs (gstreamer1.0-plugins-base/-good/-libav + the libav stack). Until
# the 2026-10-25 lazy split these trees were baked into the wine64/wine32
# boot zips (+85 MB compressed per arch) although only Media
# Foundation / DirectShow programs ever load them. They ride this shared
# archive instead: it is mounted at the guest root before Wine starts and
# its members are range-fetched on first read, so the plugins appear at
# the exact guest paths winegstreamer's plugin scan expects
# (usr/lib/<arch>-linux-gnu/gstreamer-1.0/) while non-media programs never
# download the bytes. The boot zips must NOT carry them;
# check_runtime_completeness.py pins both directions.
GSTREAMER = {
    'x86_64': 'gstreamer64',
    'i386': 'gstreamer32',
}


def ensure_gstreamer():
    result = {}
    for arch, dirname in GSTREAMER.items():
        root = ROOT / 'tools/rootfs64' / dirname
        plugin_dir = root / 'usr/lib' / f'{arch}-linux-gnu' / 'gstreamer-1.0'
        if not (plugin_dir / 'libgstlibav.so').is_file():
            raise SystemExit(
                f'GStreamer payload missing for {arch}: {plugin_dir} — '
                'run tools/rootfs64/prepare-gstreamer.py first')
        files = sorted(path for path in root.rglob('*') if path.is_file())
        result[arch] = {'root': root, 'files': files,
                        'treeSha256': tree_digest(root),
                        'bytes': sum(len(path.read_bytes()) for path in files)}
    return result


def ensure_openal():
    cache = ROOT / 'tools/rootfs64/openal'
    archive = cache / Path(OPENAL['url']).name
    download(OPENAL['url'], archive, OPENAL['sha256'])
    result = {}
    with zipfile.ZipFile(archive) as source:
        for name, path in OPENAL['files'].items():
            data = source.read(path)
            result[name] = {'bytes': data, 'sha256': digest(data)}
        result['license'] = source.read(f'openal-soft-{OPENAL_VERSION}-bin/COPYING')
    return result


def stage(destination):
    payload = ROOT / 'tools/rootfs64/mono32'
    subprocess.run(['python3', str(ROOT / 'tools/rootfs64/prepare-mono32.py')], check=True)
    graphics = {
        'i386': ROOT / 'tools/rootfs64/libgl32/libGL.so.1',
        'x86_64': ROOT / 'tools/rootfs64/libgl64/libGL.so.1',
    }
    subprocess.run(['python3', str(ROOT / 'tools/rootfs64/build_vulkan_shims.py'), '--check'], check=True)
    vulkan = {arch: ROOT / f'tools/rootfs64/libvk{bits}/libvulkan.so.1'
              for arch, bits in [('i386', '32'), ('x86_64', '64')]}
    direct3d = {}
    for arch, bits, machine, target in [
        ('i386', '32', 0x14c, 'opt/wine-stable/lib/wine/i386-windows'),
        ('x86_64', '64', 0x8664, 'usr/lib/x86_64-linux-gnu/wine/x86_64-windows'),
    ]:
        direct3d[arch] = {}
        for name in ('d3d9', 'd3d11', 'dxgi'):
            path = ROOT / f'tools/dxvk/build.w{bits}/src/{name}/{name}.dll'
            data = path.read_bytes()
            pe = int.from_bytes(data[60:64], 'little')
            if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0' or int.from_bytes(data[pe+4:pe+6], 'little') != machine:
                raise ValueError(f'Incorrect DXVK architecture: {path}')
            direct3d[arch][name] = {'data': data, 'target': f'{target}/{name}.dll'}
    gecko = ensure_gecko()
    openal = ensure_openal()
    cjk = ensure_cjk_font()
    gstreamer = ensure_gstreamer()
    provenance = {
        'wineMono': {'version': mono.VERSION, 'url': mono.URL,
                     'archiveSha256': mono.ARCHIVE_SHA256, 'treeSha256': mono.TREE_SHA256},
        'wineGecko': {
            'archives': {name: {'version': entry['version'], 'url': entry['url'],
                                'archiveSha256': entry['sha256'],
                                'treeSha256': entry['treeSha256']}
                         for name, entry in gecko.items()},
        },
        'openAL': {
            'version': OPENAL_VERSION, 'url': OPENAL['url'], 'archiveSha256': OPENAL['sha256'],
            'files': {name: {'sha256': entry['sha256'], 'bytes': len(entry['bytes'])}
                      for name, entry in openal.items() if name != 'license'},
        },
        'cjkFont': {'name': CJK_FONT['name'], 'version': CJK_FONT['version'],
                    'url': CJK_FONT['url'], 'debSha256': CJK_FONT['debSha256'],
                    'ttcSha256': CJK_FONT['ttcSha256'], 'license': CJK_FONT['license'],
                    'bytes': len(cjk['ttc'])},
        'gstreamer': {
            'source': 'tools/rootfs64/prepare-gstreamer.py (sha256-pinned Debian bookworm debs)',
            'arches': {arch: {'members': len(entry['files']), 'bytes': entry['bytes'],
                              'treeSha256': entry['treeSha256']}
                       for arch, entry in gstreamer.items()},
        },
        'openGL': {arch: mono.digest(path) for arch, path in graphics.items()},
        'vulkan': {arch: mono.digest(path) for arch, path in vulkan.items()},
        'direct3D': {
            'source': json.loads((ROOT / 'tools/dxvk/SOURCES.json').read_text()),
            'files': {arch: {name: {'sha256': digest(entry['data']), 'bytes': len(entry['data']), 'target': entry['target']}
                             for name, entry in files.items()} for arch, files in direct3d.items()},
        },
    }
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination, suffix='.zip', delete=False) as temporary:
        pending = Path(temporary.name)
    try:
        with zipfile.ZipFile(pending, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            def add(name, data):
                entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, data)

            # A normal Wine installation: not relative to an application's EXE.
            add('home/username/.wine/drive_c/windows/mono.link', b'/opt/winbox/mono')
            add(f'usr/share/wine/mono/wine-mono-{mono.VERSION}.link', b'/opt/winbox/mono/mono-2.0')
            for path in sorted((payload / 'mono').rglob('*')):
                if path.is_file():
                    add('opt/winbox/' + path.relative_to(payload).as_posix(), path.read_bytes())
            for arch, path in graphics.items():
                data = path.read_bytes()
                for prefix in ['lib', 'usr/lib']:
                    add(f'{prefix}/{arch}-linux-gnu/libGL.so.1', data)
            for arch, path in vulkan.items():
                for prefix in ['lib', 'usr/lib']:
                    add(f'{prefix}/{arch}-linux-gnu/libvulkan.so.1', path.read_bytes())
            for files in direct3d.values():
                for entry in files.values():
                    add(entry['target'], entry['data'])
            add('opt/winbox/licenses/dxvk-LICENSE', (ROOT / 'tools/dxvk/src/LICENSE').read_bytes())
            # Wine's own loader finds Gecko under /usr/share/wine/gecko, which is
            # checked before the per-prefix installation path. The upstream
            # tarballs are the exact package contents, VERSION file included.
            for entry in gecko.values():
                for path in entry['files']:
                    relative = path.relative_to(entry['root']).as_posix()
                    add(f'usr/share/wine/gecko/{entry["dir"]}/{relative}', path.read_bytes())
            # OpenAL Soft is a native PE. Wine's loader searches the per-arch
            # directory under WINEDLLPATH after the app and system directories;
            # the shared archive is mounted first, so no prefix registry is used.
            for name in ('i386', 'x86_64'):
                add(OPENAL['targets'][name], openal[name]['bytes'])
            add('opt/winbox/licenses/openal-soft-COPYING', openal['license'])
            # CJK fallback (see CJK_FONT): fontconfig in both rootfs zips
            # scans /usr/share/fonts recursively; the conf wires the
            # zh/ja/ko family aliases the upstream package ships.
            add('usr/share/fonts/truetype/wqy/wqy-microhei.ttc', cjk['ttc'])
            add('etc/fonts/conf.d/65-wqy-microhei.conf', cjk['conf'])
            add('opt/winbox/licenses/wqy-microhei-copyright', cjk['copyright'])
            # GStreamer media closure (see GSTREAMER above): every staged
            # file lands at its guest path; symlinks are stored dereferenced
            # (full ELF bytes under each soname), exactly as the boot zips
            # carried them before the split, so the guest linker resolves
            # DT_NEEDED sonames without link support in the lazy mount.
            gst_names = []
            for entry in gstreamer.values():
                for path in entry['files']:
                    name = path.relative_to(entry['root']).as_posix()
                    gst_names.append(name)
                    add(name, path.read_bytes())
            add('winbox-runtime.json', (json.dumps(provenance, indent=2) + '\n').encode())
        with zipfile.ZipFile(pending) as check_archive:
            staged_names = set(check_archive.namelist())
        missing = [name for name in gst_names if name not in staged_names]
        if missing:
            raise SystemExit(f'GStreamer closure incomplete in support archive: {missing[:3]}')
        sha256 = mono.digest(pending)
        name = f'support-{sha256}.zip'
        size = pending.stat().st_size
        pending.chmod(0o644)
        pending.replace(destination / name)
        manifest = {'version': 1, 'archive': name, 'bytes': size, 'sha256': sha256, **provenance}
        (destination / 'dependencies.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'Shared runtime: {size:,} bytes ({sha256[:12]})')
        return manifest
    finally:
        pending.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    stage(parser.parse_args().destination)
