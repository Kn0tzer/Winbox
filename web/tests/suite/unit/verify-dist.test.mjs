import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, symlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';

const repo = resolve(import.meta.dirname, '../../../..');
function fixture(run) {
  const root = mkdtempSync(join(tmpdir(), 'winbox-dist-'));
  const dist = join(root, 'dist');
  mkdirSync(join(dist, 'runtime'), { recursive: true });
  const archive = Buffer.from('dependency archive hash fixture');
  const manifest = { archive: 'dependencies.zip', sha256: createHash('sha256').update(archive).digest('hex'), bytes: archive.length };
  writeFileSync(join(dist, 'app.mjs'), "export const runtimeReleaseId = '012345abcdef';");
  writeFileSync(join(dist, 'runtime/index.html'), '<script>const runtimeUrl = "?v=012345abcdef";</script>');
  writeFileSync(join(dist, 'runtime/dependencies.zip'), archive);
  writeFileSync(join(dist, 'runtime/dependencies.json'), JSON.stringify(manifest));
  const verify = () => spawnSync(process.execPath, [join(repo, 'tools/browser/verify-dist.mjs')], {
    env: { ...process.env, BW_TEST_DIST: dist }, encoding: 'utf8', timeout: 10000,
  });
  try { run({ root, dist, verify }); }
  finally { rmSync(root, { recursive: true, force: true }); }
}

test('post-export verifier accepts matching hashes and substituted modules', () => fixture(({ verify }) => {
  const result = verify();
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Browser export verified/);
}));

for (const [name, path, contents, message] of [
  ['worker syntax', 'worker.mjs', 'export const = ;', /syntax error in worker.mjs/],
  ['runtime shell version', 'runtime/index.html', '__RUNTIME_VERSION__', /unsubstituted __RUNTIME_VERSION__/],
  ['worker version', 'worker.mjs', "export const version = '__RUNTIME_VERSION__';", /unsubstituted __RUNTIME_VERSION__/],
  ['ZIP hash', 'runtime/dependencies.zip', 'corrupt bytes', /archive hash mismatch/],
]) test(`post-export verifier rejects broken ${name}`, () => fixture(({ dist, verify }) => {
  writeFileSync(join(dist, path), contents);
  const result = verify();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, message);
}));

test('symlinked source modules also receive syntax checks', () => fixture(({ root, dist, verify }) => {
  writeFileSync(join(root, 'bad.mjs'), 'export const = ;');
  symlinkSync(join(root, 'bad.mjs'), join(dist, 'linked.mjs'));
  const result = verify();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /syntax error in linked.mjs/);
}));
