import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';

test('missing archives in an explicitly selected export fail provenance checks', () => {
  const dist = mkdtempSync(join(tmpdir(), 'winbox-missing-export-'));
  // A nested node --test must start a fresh runner, not inherit the parent's IPC context.
  const env = { ...process.env, BW_TEST_DIST: dist };
  delete env.NODE_TEST_CONTEXT;
  try {
    const result = spawnSync(process.execPath, ['--test', '--test-name-pattern=Wine exports preserve pinned',
      join(import.meta.dirname, 'provenance-artifacts.test.mjs')], {
      env, encoding: 'utf8', timeout: 10000,
    });
    assert.notEqual(result.status, 0, 'a missing export must not silently pass archive comparison');
    assert.match(result.stdout, /both pinned and exported archives are required/);
  } finally { rmSync(dist, { recursive: true, force: true }); }
});
