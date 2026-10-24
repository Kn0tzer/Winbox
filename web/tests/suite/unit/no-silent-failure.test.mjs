import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { scanSilentFailures } from '../lib/silent-failure-scan.mjs';

const baselinePath = resolve(import.meta.dirname, '../known-silent.json');

test('silent failure counts never increase above the reviewed per-file baseline', async () => {
  const baseline = JSON.parse(await readFile(baselinePath, 'utf8'));
  assert.equal(baseline.version, 1);
  const current = await scanSilentFailures();
  const decreases = [];
  const increases = [];
  for (const file of new Set([...Object.keys(baseline.files || {}), ...Object.keys(current)])) {
    const oldCounts = baseline.files?.[file] || {};
    const newCounts = current[file] || {};
    for (const category of ['emptyCatch', 'unloggedDefault', 'unloggedErrno']) {
      const before = Number(oldCounts[category] || 0);
      const after = Number(newCounts[category] || 0);
      if (after > before) increases.push(`${file} ${category}: ${before} -> ${after}`);
      else if (after < before) decreases.push(`${file} ${category}: ${before} -> ${after}`);
    }
  }
  if (decreases.length) console.log(`Silent-failure baseline can be lowered after review:\n${decreases.join('\n')}`);
  assert.deepEqual(increases, [], `silent-failure count increased; add a real fix or review and update web/tests/suite/known-silent.json:\n${increases.join('\n')}`);
});
