#!/usr/bin/env python3
"""Build the shared, lazy Windows runtime: Wine Mono, Wine Gecko, OpenAL Soft
and the graphics bridge. Application files are never included."""
import argparse
import hashlib
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
            add('winbox-runtime.json', (json.dumps(provenance, indent=2) + '\n').encode())
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
