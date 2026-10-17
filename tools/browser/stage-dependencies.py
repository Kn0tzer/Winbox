#!/usr/bin/env python3
"""Build the shared, lazy Wine Mono and graphics runtime, without app files."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('wine_mono', ROOT / 'tools/rootfs64/prepare-mono32.py')
mono = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mono)


def stage(destination):
    payload = ROOT / 'tools/rootfs64/mono32'
    subprocess.run(['python3', str(ROOT / 'tools/rootfs64/prepare-mono32.py')], check=True)
    graphics = {
        'i386': ROOT / 'tools/rootfs64/libgl32/libGL.so.1',
        'x86_64': ROOT / 'tools/rootfs64/libgl64/libGL.so.1',
    }
    provenance = {
        'wineMono': {'version': mono.VERSION, 'url': mono.URL,
                     'archiveSha256': mono.ARCHIVE_SHA256, 'treeSha256': mono.TREE_SHA256},
        'openGL': {arch: mono.digest(path) for arch, path in graphics.items()},
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
