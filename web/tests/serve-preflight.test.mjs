// Regression check for the serve() preflight in tools/browser/serve.mjs:
// a probe pointed at an unbuilt/stripped dist must fail immediately naming
// the missing root, not start a server that 404s every asset and burn its
// whole boot timeout reporting a misleading guest failure.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { serve } from '../../tools/browser/serve.mjs';

test('serve refuses a missing dist root loudly', async () => {
  await assert.rejects(
    () => serve(join(tmpdir(), 'bw64-no-such-dist-serve-preflight')),
    /BW64-FAIL serve: dist root does not exist/);
});

test('serve refuses a root that is a file, not a directory', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'bw64-serve-file-'));
  const file = join(dir, 'not-a-dir');
  await writeFile(file, 'x');
  await assert.rejects(() => serve(file), /does not exist or is not a directory/);
});

test('serve serves an existing directory and still 404s missing files', async (t) => {
  const dir = await mkdtemp(join(tmpdir(), 'bw64-serve-ok-'));
  await writeFile(join(dir, 'index.html'), 'ok');
  const server = await serve(dir);
  t.after(() => server.close());
  const base = `http://127.0.0.1:${server.address().port}`;
  const ok = await fetch(base + '/index.html');
  assert.equal(ok.status, 200);
  assert.equal(await ok.text(), 'ok');
  const missing = await fetch(base + '/nope.zip');
  assert.equal(missing.status, 404);
});
