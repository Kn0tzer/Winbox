import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('shader pre-pass resolves inside an export served at a nested path', async () => {
  const shader = readFileSync(join(repoRoot, 'web/shader.mjs'), 'utf8');
  const specifier = shader.match(/import\('([^']*spirvfix[^']*)'\)/)?.[1];
  assert.ok(specifier);
  const resolved = new URL(specifier, 'https://example.test/wine/shader.mjs');
  assert.equal(resolved.pathname, '/wine/spirvfix.mjs', 'pre-pass stays inside the exported web root');
  const prepass = await import('../../../spirvfix.mjs');
  assert.equal(typeof prepass.applySpirvFix, 'function');
});
