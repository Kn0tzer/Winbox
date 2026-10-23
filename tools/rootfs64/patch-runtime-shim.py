#!/usr/bin/env python3
"""Swap the guest Vulkan shims inside an already-built runtime export.

The shims (libvulkan.so.1, 32- and 64-bit) are not in wine64.zip: they ship in
the content-addressed shared archive dist/runtime/support-<sha256>.zip that
tools/browser/stage-dependencies.py writes, and dist/runtime/dependencies.json
names it (archive, bytes, sha256) and records the shim hashes under "vulkan".
A fresh `node tools/browser/build.mjs` always restages the shims from
tools/rootfs64/libvk{32,64}/ (after build_vulkan_shims.py --check), so a normal
export needs no patching. Use this script only to refresh an existing export
without a full rebuild:

  python3 tools/rootfs64/patch-runtime-shim.py <runtime-dir> [--keep-old]

It replaces lib/ and usr/lib/ {i386,x86_64}-linux-gnu/libvulkan.so.1 and the
"vulkan" hashes in winbox-runtime.json, rewrites the archive under its new
content hash name, updates dependencies.json, and removes the old archive.
All other archive entries are copied unchanged. Run it on a copy when testing:
the runtime directory is modified in place.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ARCHES = {'i386': '32', 'x86_64': '64'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def patch(runtime, keep_old=False):
    runtime = Path(runtime).resolve()
    subprocess.run(['python3', str(ROOT / 'tools/rootfs64/build_vulkan_shims.py'), '--check'], check=True)
    shims = {arch: (ROOT / f'tools/rootfs64/libvk{bits}/libvulkan.so.1').read_bytes() for arch, bits in ARCHES.items()}
    replacements = {f'{prefix}/{arch}-linux-gnu/libvulkan.so.1': data
                    for arch, data in shims.items() for prefix in ('lib', 'usr/lib')}
    manifest_path = runtime / 'dependencies.json'
    manifest = json.loads(manifest_path.read_text())
    old_name = manifest['archive']
    old = runtime / old_name
    hashes = {arch: digest(data) for arch, data in shims.items()}
    if manifest.get('vulkan') == hashes and digest(old.read_bytes()) == manifest['sha256']:
        print(f'Shims already current ({old_name[:20]}...); nothing to do')
        return manifest
    with tempfile.NamedTemporaryFile(dir=runtime, suffix='.zip', delete=False) as temporary:
        pending = Path(temporary.name)
    try:
        seen = set()
        with zipfile.ZipFile(old) as source, zipfile.ZipFile(
                pending, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as target:
            for info in source.infolist():
                data = source.read(info)
                if info.filename in replacements:
                    data = replacements[info.filename]
                    seen.add(info.filename)
                elif info.filename == 'winbox-runtime.json':
                    provenance = json.loads(data)
                    provenance['vulkan'] = hashes
                    data = (json.dumps(provenance, indent=2) + '\n').encode()
                target.writestr(info, data)
        missing = set(replacements) - seen
        if missing:
            raise SystemExit(f'{old_name} has no entries for {sorted(missing)}; wrong archive?')
        sha256 = digest(pending.read_bytes())
        name = f'support-{sha256}.zip'
        pending.chmod(0o644)
        pending.replace(runtime / name)
    finally:
        pending.unlink(missing_ok=True)
    manifest.update({'archive': name, 'bytes': (runtime / name).stat().st_size, 'sha256': sha256, 'vulkan': hashes})
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    if name != old_name and not keep_old:
        old.unlink()
    print(f'Patched shims: {old_name[:20]}... -> {name[:20]}... ({manifest["bytes"]:,} bytes)')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('runtime', type=Path, help='export runtime directory (dist/runtime)')
    parser.add_argument('--keep-old', action='store_true', help='keep the previous support archive')
    args = parser.parse_args()
    patch(args.runtime, args.keep_old)
