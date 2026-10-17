#!/usr/bin/env python3
"""Restore the pinned Wine Mono payload used by the Celeste browser probe."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request

VERSION = '7.4.0'
URL = f'https://github.com/wine-mono/wine-mono/releases/download/wine-mono-{VERSION}/wine-mono-{VERSION}-x86.tar.xz'
ARCHIVE_SHA256 = '9249ece664bcf2fecb1308ea1d2542c72923df9fe3df891986f137b2266a9ba3'
LIBRARIES = {
    'libmono-2.0-x86.dll': 'a3fda5b431adfe60f0fed6973bf9bc86766732a03d7dbdfcd44fbdbf46f0b439',
    'libmono-2.0-x86_64.dll': 'e4ba9361c6956d8ebfd42d8f3bc58eb0e29af04a7ae4995145f9ea23fd37f145',
}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check(root):
    mono = root / 'mono/mono-2.0'
    for name, expected in LIBRARIES.items():
        if digest(mono / 'bin' / name) != expected:
            raise ValueError(f'{name} does not match Wine Mono {VERSION}')
    if not (mono / 'lib/mono/4.5/mscorlib.dll').is_file():
        raise ValueError('Wine Mono assemblies are missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent / 'mono32')
    args = parser.parse_args()
    root = args.output.resolve()
    if args.check or root.exists():
        check(root)
        print(f'Wine Mono {VERSION}: both runtime libraries verified ({root})')
        return
    root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mono32-', dir=root.parent) as scratch:
        temporary = Path(scratch)
        archive = temporary / 'mono.tar.xz'
        urllib.request.urlretrieve(URL, archive)
        if digest(archive) != ARCHIVE_SHA256:
            raise ValueError('Wine Mono archive checksum mismatch')
        extracted = temporary / 'extracted'
        with tarfile.open(archive) as package:
            package.extractall(extracted, filter='data')
        staged = temporary / 'staged'
        (staged / 'mono').mkdir(parents=True)
        shutil.move(extracted / f'wine-mono-{VERSION}', staged / 'mono/mono-2.0')
        check(staged)
        (staged / 'SOURCE.json').write_text(json.dumps({
            'version': VERSION, 'url': URL, 'archiveSha256': ARCHIVE_SHA256,
            'libraries': LIBRARIES,
        }, indent=2) + '\n')
        staged.rename(root)
    print(f'Prepared Wine Mono {VERSION}: {root}')


if __name__ == '__main__':
    main()
