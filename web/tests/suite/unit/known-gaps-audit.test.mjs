// web/tests/suite/unit/known-gaps-audit.test.mjs — the known-gaps gate must fail CI
// when an UNOWNED file gains a silent gap, and must pass on the real repository.
//
// tools/known-gaps-audit.mjs --check is the gate. This test runs it against the
// checkout, then runs it against fixture trees where the right answer is known:
// an unregistered gap must fail, a count above the register must fail, and
// symlinks and vendored trees must not be read.

import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import {
  checkUnowned, ownerOf, parseAllowlist, parseRegister, renderHits, scan, writeHitsSection, UNOWNED,
} from '../../../../tools/known-gaps-audit.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '../../../..');
const scriptPath = join(repoRoot, 'tools/known-gaps-audit.mjs');

const hit = (file, line = 1, term = 'TODO') => ({ file, line, term, text: `// ${term} x`, area: ownerOf(file) });

const covWith = (register) => [
  '# fixture',
  '<!-- known-gaps:register:begin -->',
  '```json',
  JSON.stringify(register, null, 2),
  '```',
  '<!-- known-gaps:register:end -->',
  '',
].join('\n');

function fixtureTree(files) {
  const dir = mkdtempSync(join(tmpdir(), 'known-gaps-audit-'));
  for (const [rel, body] of Object.entries(files)) {
    const p = join(dir, rel);
    mkdirSync(dirname(p), { recursive: true });
    writeFileSync(p, body);
  }
  return dir;
}

test('the repository passes the known-gaps --check gate', () => {
  const r = spawnSync(process.execPath, [scriptPath, '--check'], { cwd: repoRoot, encoding: 'utf8' });
  assert.equal(r.status, 0, `--check failed:\n${r.stdout}${r.stderr}`);
});

test('ownerOf: the first matching prefix wins, and unmatched files are UNOWNED', () => {
  assert.equal(ownerOf('source/kernel/syscall64.cpp'), 'kernel');
  assert.equal(ownerOf('source/emulation/cpu/cpu64.cpp'), 'kernel');
  assert.equal(ownerOf('source/opengl/glfunctions_ext1.cpp'), 'gl');
  assert.equal(ownerOf('web/gl-state.mjs'), 'gl');
  assert.equal(ownerOf('source/vulkan/vk64bridge.cpp'), 'vulkan');
  assert.equal(ownerOf('web/vkcache.mjs'), 'vulkan');
  assert.equal(ownerOf('project/emscripten/build.sh'), 'boot/prefix');
  assert.equal(ownerOf('tools/browser/probe.mjs'), 'boot/prefix');
  assert.equal(ownerOf('tools/run_x64_smoke.sh'), 'tests/tools');
  assert.equal(ownerOf('source/io/fsvirtualnode.cpp'), UNOWNED);
  assert.equal(ownerOf('source/x11/xdrawable.cpp'), UNOWNED);
  assert.equal(ownerOf('web/app.mjs'), UNOWNED);
});

test('checkUnowned: an unregistered, unallowlisted UNOWNED hit is a violation', () => {
  const { violations } = checkUnowned([hit('source/io/new.cpp', 7)], {}, {});
  assert.equal(violations.length, 1);
  assert.match(violations[0], /not in the register or allowlist/);
  assert.match(violations[0], /line 7/);
});

test('checkUnowned: a count above the register is a violation; a count below it is only a note', () => {
  const register = { 'source/io/new.cpp': { count: 1, severity: 'low', area: 'fs', gap: 'x' } };
  const grew = checkUnowned([hit('source/io/new.cpp'), hit('source/io/new.cpp', 2)], register, {});
  assert.equal(grew.violations.length, 1);
  assert.match(grew.violations[0], /new silent gap/);

  const shrank = checkUnowned([], register, {});
  assert.equal(shrank.violations.length, 0);
  assert.ok(shrank.notes.some(n => /register entry has no unowned hits/.test(n)));
});

test('checkUnowned: an allowlist count is enforced like a register count', () => {
  const allowlist = { 'source/util/karray.h': { file: 'source/util/karray.h', count: 1, reason: 'bounds invariant' } };
  assert.equal(checkUnowned([hit('source/util/karray.h')], {}, allowlist).violations.length, 0);
  assert.equal(checkUnowned([hit('source/util/karray.h'), hit('source/util/karray.h', 9)], {}, allowlist).violations.length, 1);
});

test('parseAllowlist rejects an entry with no reason, a duplicate, or a bad count', () => {
  assert.throws(() => parseAllowlist(JSON.stringify({ entries: [{ file: 'a.cpp', count: 1, reason: '  ' }] })), /needs a non-empty reason/);
  assert.throws(() => parseAllowlist(JSON.stringify({ entries: [{ file: 'a.cpp', count: 1, reason: 'x' }, { file: 'a.cpp', count: 1, reason: 'y' }] })), /duplicate/);
  assert.throws(() => parseAllowlist(JSON.stringify({ entries: [{ file: 'a.cpp', count: -1, reason: 'x' }] })), /non-negative integer/);
  assert.throws(() => parseAllowlist(JSON.stringify({ entries: [{ count: 1, reason: 'x' }] })), /no file/);
});

test('parseRegister: a missing block or fence throws, and a bad severity throws', () => {
  assert.throws(() => parseRegister('# no block here'), /no register block/);
  assert.throws(() => parseRegister('<!-- known-gaps:register:begin -->\nnot a fence\n<!-- known-gaps:register:end -->'), /one ```json fence/);
  const bad = covWith({ 'a.cpp': { count: 1, severity: 'urgent', area: 'fs', gap: 'x' } });
  assert.throws(() => parseRegister(bad), /severity must be high, medium or low/);
  const ok = parseRegister(covWith({ 'a.cpp': { count: 2, severity: 'high', area: 'x11', gap: 'x' } }));
  assert.equal(ok['a.cpp'].count, 2);
});

test('writeHitsSection is idempotent, and appends when the markers are absent', () => {
  const hits = [hit('source/io/new.cpp', 3, 'kpanic(')];
  const section = renderHits(hits);
  const md = '# doc\n\nprose\n';
  const once = writeHitsSection(md, section);
  assert.equal(writeHitsSection(once, section), once);
  assert.ok(once.startsWith('# doc'));
  assert.ok(once.includes('### `source/io/new.cpp` (1)'));
  assert.ok(once.includes('| 3 | `kpanic(` |'));
});

test('scan: vendored, excluded and symlinked trees are not read, and symlinks are recorded', () => {
  const outside = mkdtempSync(join(tmpdir(), 'known-gaps-outside-'));
  writeFileSync(join(outside, 'secret.cpp'), '// TODO outside the tree\n');
  const root = fixtureTree({
    'source/io/real.cpp': '// TODO real gap\nint x;\n',
    'source/io/clean.cpp': 'int y;\n',
    'lib/imgui/imgui.cpp': '// TODO vendored\n',
    'web/vendor/mod.mjs': '// TODO vendored module\n',
    'web/runtime/x.mjs': '// TODO runtime mirror\n',
    'node_modules/pkg/index.js': '// TODO dependency\n',
    'tools/dxvk/shader.cpp': '// TODO third party\n',
    'tools/known-gaps-audit.mjs': '// TODO the audit itself\n',
    'source/util/stb_image.h': '// TODO vendored stb\n',
  });
  try {
    symlinkSync(outside, join(root, 'source/io/linked'));
    const { hits, files, symlinks } = scan(root);
    assert.deepEqual(hits.map(h => h.file), ['source/io/real.cpp']);
    assert.equal(hits[0].area, UNOWNED);
    assert.equal(files, 2, 'only source/io/real.cpp and source/io/clean.cpp are read');
    assert.deepEqual(symlinks, ['source/io/linked']);
  } finally {
    rmSync(root, { recursive: true, force: true });
    rmSync(outside, { recursive: true, force: true });
  }
});

test('--check exits 1 for a new unregistered silent gap, and 0 once it is registered', () => {
  const root = fixtureTree({
    'docs/known-gaps/coverage.md': covWith({}),
    'source/io/new.cpp': '// TODO a new silent gap\n',
  });
  try {
    const failed = spawnSync(process.execPath, [scriptPath, '--check', `--root=${root}`], { encoding: 'utf8' });
    assert.equal(failed.status, 1, failed.stdout + failed.stderr);
    assert.match(failed.stderr, /FAIL source\/io\/new\.cpp/);

    writeFileSync(join(root, 'docs/known-gaps/coverage.md'), covWith({
      'source/io/new.cpp': { count: 1, severity: 'low', area: 'fs', gap: 'registered for the test' },
    }));
    const passed = spawnSync(process.execPath, [scriptPath, '--check', `--root=${root}`], { encoding: 'utf8' });
    assert.equal(passed.status, 0, passed.stdout + passed.stderr);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
