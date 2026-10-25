import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('Vulkan shim provenance rejects changed source and replaced binaries', () => {
  const dir = mkdtempSync(join(tmpdir(), 'vk-shim-provenance-'));
  try {
    execFileSync('python3', ['-c', `
import sys,shutil
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,sys.argv[2])
from build_vulkan_shims import build,check
root=Path(sys.argv[1]);d=root/'libvk32';d.mkdir()
src=d/'libvk32.c'
shutil.copyfile(sys.argv[2]+'/libvk32/libvk32.c',src)
build(root,'32','i686-linux-gnu-gcc')
check(root,'32')
original=src.read_bytes();src.write_bytes(original+b'\\n// changed source\\n')
try:check(root,'32');raise AssertionError('Stale source accepted')
except ValueError:pass
src.write_bytes(original)
binary=d/'libvulkan.so.1';binary.write_bytes(binary.read_bytes()+b'changed')
try:check(root,'32');raise AssertionError('Replaced binary accepted')
except ValueError:pass
`, dir, join(repoRoot, 'tools/rootfs64')], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
