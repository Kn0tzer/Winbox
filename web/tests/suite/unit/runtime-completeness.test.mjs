import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';

// Guard for docs/known-gaps/runtime.md: the register's IMPLEMENTED /
// NOT-A-GAP rows cite this test for the shipped-artifact claims (winehq10
// backport DLLs + hashes in wine64.zip, gstreamer plugin set, DirectX
// redist completeness, DejaVu Bold/Mono faces in glibc-rootfs32.zip, CJK
// font pin, prefix64 registry sections). The python checker does the
// zip work; see tools/rootfs64/check_runtime_completeness.py.
test('runtime completeness: shipped artifacts match the runtime register', () => {
  const checker = resolve(import.meta.dirname, '../../../../tools/rootfs64/check_runtime_completeness.py');
  const result = spawnSync('python3', ['-B', checker], { encoding: 'utf8', timeout: 300000 });
  assert.equal(result.status, 0, result.stdout + result.stderr);
});
