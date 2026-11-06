// web/tests/suite/unit/dll-export-census.test.mjs — the DLL export census
// must classify synthetic PEs correctly, its committed baseline must be
// internally consistent, and when the staged runtime zips are present in
// this tree the census of the shipped system libraries must match the
// baseline exactly (a new unimplemented-stub export, or one silently
// resolved without a baseline regeneration, fails the suite).
//
// tools/conformance/dll_export_census.py is the census. Exit-code
// contract: 0 = matches baseline, 1 = drift, 2 = the wine zips are a
// different vintage than the baseline (a restage regenerates the
// baseline deliberately, so a tree carrying another vintage skips).

import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '../../../..');
const scriptPath = join(repoRoot, 'tools/conformance/dll_export_census.py');
const baselinePath = join(repoRoot, 'tools/conformance/dll_export_baseline.json');
const runtimeDir = join(repoRoot, 'web/runtime');

function python3Available() {
  const r = spawnSync('python3', ['--version'], { encoding: 'utf8' });
  return !r.error && r.status === 0;
}

test('dll export census: classifier verdicts on synthetic PEs', (t) => {
  if (!python3Available()) return t.skip('python3 not available');
  const r = spawnSync('python3', [scriptPath, '--self-test'], { encoding: 'utf8' });
  assert.equal(r.status, 0, `self-test failed:\n${r.stdout}\n${r.stderr}`);
  assert.match(r.stdout, /classifier verdicts correct/);
});

test('dll export census: baseline is internally consistent', () => {
  const b = JSON.parse(readFileSync(baselinePath, 'utf8'));
  assert.equal(b.format, 1);
  for (const fn of ['wine64.zip', 'wine32.zip']) {
    assert.match(b.zip_sha256[fn] || '', /^[0-9a-f]{64}$/, `${fn} sha256 recorded`);
  }
  const sums = { dlls: 0, slots: 0, named: 0, forwarders: 0, stubs: 0, code: 0, data: 0, empty: 0 };
  let namedStubs = 0;
  for (const [key, v] of Object.entries(b.dlls)) {
    sums.dlls += 1;
    for (const f of ['slots', 'named', 'forwarders', 'stubs', 'code', 'data', 'empty']) sums[f] += v[f];
    assert.deepEqual(v.stub_names, [...v.stub_names].sort(), `${key}: stub_names sorted`);
    assert.equal(new Set(v.stub_names).size, v.stub_names.length, `${key}: stub_names unique`);
    assert.ok(v.stub_names.length <= v.stubs, `${key}: named stubs never exceed stub slots`);
    namedStubs += v.stub_names.length;
  }
  assert.deepEqual(b.totals, sums, 'baseline totals equal the per-DLL sums');
  assert.ok(namedStubs > 0, 'baseline records a non-empty stub surface');
});

test('dll export census: staged runtime matches the baseline', (t) => {
  if (!python3Available()) return t.skip('python3 not available');
  if (!existsSync(join(runtimeDir, 'wine64.zip')) || !existsSync(join(runtimeDir, 'wine32.zip'))) {
    return t.skip('runtime zips not staged in this tree (run bootstrap first)');
  }
  const r = spawnSync('python3',
    [scriptPath, '--runtime-dir', runtimeDir, '--check', '--baseline', baselinePath],
    { encoding: 'utf8', timeout: 110000, maxBuffer: 16 * 1024 * 1024 });
  if (r.status === 2) return t.skip('staged runtime is a different vintage than the baseline');
  assert.equal(r.status, 0,
    `census drifted from the baseline:\n${r.stdout}\n${r.stderr}`);
  assert.match(r.stdout, /matches the baseline exactly/);
});
