import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { resolve } from 'node:path';

const web = resolve(import.meta.dirname, '../../..');
const build = await readFile(resolve(web, '../tools/browser/build.mjs'), 'utf8');

test('browser export copies every page module that web/*.mjs imports', async () => {
  const exported = new Set([...build.matchAll(/'([A-Za-z0-9_.-]+\.m?js)'/g)].map(m => m[1]));
  exported.add('direct-folder.mjs'); exported.add('frame-metrics.mjs');
  const missing = new Set();
  for (const file of (await readdir(web)).filter(f => f.endsWith('.mjs'))) {
    if (!exported.has(file)) continue;
    const source = await readFile(resolve(web, file), 'utf8');
    for (const [, dep] of source.matchAll(/(?:from\s+|import\()\s*['"]\.\/([A-Za-z0-9_.-]+\.m?js)['"]/g)) {
      if (!exported.has(dep)) missing.add(`${file} -> ${dep}`);
    }
  }
  assert.deepEqual([...missing], []);
});
