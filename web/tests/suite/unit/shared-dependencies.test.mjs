import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('shared runtime rejects changed assemblies, injected files and symlinks', () => {
  const result = spawnSync('python3', ['-B', join(repoRoot, 'tools/browser/test-mono-integrity.py')], { encoding: 'utf8', timeout: 30000 });
  assert.equal(result.status, 0, result.stderr || result.error?.message);
});
