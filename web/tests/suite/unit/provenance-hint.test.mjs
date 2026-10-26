// web/tests/suite/unit/provenance-hint.test.mjs — the "no build export" message
// must say how to fix it, and the runner must show it before any result.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { missingExportMessage, provenanceHint, provenanceDistRoot } from '../lib/provenance-hint.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const suiteDir = join(repoRoot, 'web/tests/suite');

// A child `node --test` must not inherit the parent's test context, or it reports
// in the parent's internal event format instead of readable output.
function childEnv(extra) {
  const { NODE_TEST_CONTEXT, ...env } = process.env;
  return { ...env, ...extra };
}

function withTempRepo(fn) {
  const root = mkdtempSync(join(tmpdir(), 'provenance-hint-'));
  try { return fn(root); } finally { rmSync(root, { recursive: true, force: true }); }
}

test('the missing-export message gives both fixes and names the directory', () => {
  const msg = missingExportMessage('/x/dist');
  assert.ok(msg.includes('/x/dist'), 'message must name the directory it checked');
  assert.ok(msg.includes('tools/bw64-build.sh --export-only'), 'message must name the export command');
  assert.ok(msg.includes('BW_TEST_DIST=<dist>'), 'message must name the BW_TEST_DIST override');
});

test('provenanceHint is null when the export exists and the fix message when it does not', () => {
  withTempRepo((root) => {
    assert.ok(provenanceHint(root, {}).includes('tools/bw64-build.sh --export-only'), 'absent dist/ must produce the hint');
    mkdirSync(join(root, 'dist/runtime'), { recursive: true });
    writeFileSync(join(root, 'dist/runtime/BUILD_INFO.json'), '{}');
    assert.equal(provenanceHint(root, {}), null, 'present dist/ must produce no hint');
  });
});

test('BW_TEST_DIST redirects the hint to the named export', () => {
  withTempRepo((root) => {
    const other = join(root, 'elsewhere');
    assert.equal(provenanceDistRoot(root, { BW_TEST_DIST: other }), other);
    assert.ok(provenanceHint(root, { BW_TEST_DIST: other }).includes(other), 'hint must name the BW_TEST_DIST export');
    mkdirSync(join(other, 'runtime'), { recursive: true });
    writeFileSync(join(other, 'runtime/BUILD_INFO.json'), '{}');
    assert.equal(provenanceHint(root, { BW_TEST_DIST: other }), null);
  });
});

test('runner --unit prints the fix before any unit result when the export is absent', () => {
  withTempRepo((root) => {
    const r = spawnSync(process.execPath, [join(suiteDir, 'runner.mjs'), '--unit', '--filter=no-such-unit-test'], {
      env: childEnv({ BW_TEST_DIST: join(root, 'missing-dist') }),
      encoding: 'utf8',
      timeout: 120000,
    });
    const out = r.stdout;
    const noteAt = out.indexOf('NOTE: no build export');
    assert.ok(noteAt >= 0, `runner must print the export hint, got: ${out}`);
    assert.ok(out.includes('tools/bw64-build.sh --export-only'), 'runner hint must name the export command');
    assert.ok(noteAt < out.indexOf('Unit tests'), 'the hint must come before the unit section header');
  });
});

test('provenance-artifacts failure message carries the fix when the export is absent', () => {
  withTempRepo((root) => {
    const r = spawnSync(process.execPath, ['--test', join(suiteDir, 'unit/provenance-artifacts.test.mjs')], {
      env: childEnv({ BW_TEST_DIST: join(root, 'missing-dist') }),
      encoding: 'utf8',
      timeout: 120000,
    });
    const out = `${r.stdout}\n${r.stderr}`;
    assert.notEqual(r.status, 0, 'provenance must keep failing without an export');
    assert.ok(out.includes('Fix: run tools/bw64-build.sh --export-only'), `failure must carry the fix, got: ${out.slice(0, 800)}`);
    assert.ok(out.includes('BW_TEST_DIST=<dist>'), 'failure must name the BW_TEST_DIST override');
  });
});
