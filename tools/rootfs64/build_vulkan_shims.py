#!/usr/bin/env python3
"""Build both guest Vulkan shims, or verify them before staging/probing.

Usage: python3 tools/rootfs64/build_vulkan_shims.py [--check] [--arch 32|64]
The manifest binds the compiled library to the exact freestanding source.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

FLAGS = ['-shared', '-fPIC', '-O2', '-fvisibility=hidden', '-nostdlib',
         '-ffreestanding', '-Wl,-soname,libvulkan.so.1']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(root, arch):
    directory = Path(root) / ('libvk' + arch)
    source = directory / ('libvk' + arch + '.c')
    binary = directory / 'libvulkan.so.1'
    try:
        manifest = json.loads((directory / 'BUILD_INFO.json').read_text())
        expected = {'sourceSha256': digest(source), 'binarySha256': digest(binary),
                    'arch': arch, 'flags': FLAGS}
        if any(manifest.get(k) != v for k, v in expected.items()):
            raise ValueError('source or binary does not match its build manifest')
        header = binary.read_bytes()[:20]
        elf_class, machine = (1, 3) if arch == '32' else (2, 62)
        if header[:4] != b'\x7fELF' or header[4] != elf_class or int.from_bytes(header[18:20], 'little') != machine:
            raise ValueError('incorrect guest ELF architecture')
    except (OSError, KeyError, IndexError, json.JSONDecodeError, ValueError) as e:
        raise ValueError(f'Invalid libvk{arch} build: {e}. Run {Path(__file__).name} --arch {arch}.') from e
    return manifest


def build(root, arch, compiler):
    directory = Path(root) / ('libvk' + arch)
    source = directory / ('libvk' + arch + '.c')
    binary = directory / 'libvulkan.so.1'
    source_hash = digest(source)
    fd, temporary = tempfile.mkstemp(prefix='.libvulkan-', dir=directory)
    os.close(fd)
    try:
        subprocess.run([compiler, *FLAGS, '-o', temporary, str(source)], check=True)
        if digest(source) != source_hash:
            raise ValueError('Source changed during compilation; retry the build')
        manifest = {'arch': arch, 'sourceSha256': source_hash, 'binarySha256': digest(Path(temporary)),
                    'flags': FLAGS, 'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]}
        os.chmod(temporary, 0o755)
        os.replace(temporary, binary)
        info = directory / 'BUILD_INFO.json'
        info.write_text(json.dumps(manifest, indent=2) + '\n')
        check(root, arch)
        print(f'Built libvk{arch}: {manifest["binarySha256"]}')
    finally:
        Path(temporary).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--arch', choices=['32', '64'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    for arch in [args.arch] if args.arch else ['32', '64']:
        if args.check:
            check(root, arch)
            print(f'libvk{arch}: source and binary verified')
        else:
            compiler = os.environ.get('VK' + arch + '_CC', 'i686-linux-gnu-gcc' if arch == '32' else 'x86_64-linux-gnu-gcc')
            build(root, arch, compiler)


if __name__ == '__main__':
    main()
