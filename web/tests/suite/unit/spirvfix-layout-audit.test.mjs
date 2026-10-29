// web/tests/suite/unit/spirvfix-layout-audit.test.mjs — block-layout audit.
//
// Campaign 4.16 named the hazard: a SPIR-V block whose decorated layout
// (member Offset / ArrayStride / MatrixStride) differs from the layout
// WGSL computes is CONVERTED SILENTLY WRONG by naga for a whole shape
// family -- naga validates MatrixStride only on direct struct members and
// array strides only against alignment rules, never for equality. Proven
// silent on the vendored naga (campaign 4.20): array<mat3> at MatrixStride
// 12/20, array<mat2x3>/array<mat4x3> at MatrixStride 12, runtime arrays of
// mat3, and padded scalar arrays (array<f32> at ArrayStride 32 uniform /
// 16 storage, where WGSL strides are 16 / 4). The guest's bytes are read
// at WGSL offsets; nothing errors anywhere.
//
// The audit (auditBlockLayouts in tools/spirvfix/spirvfix.mjs) judges the
// POST-spirvfix module -- storage classes final, R13 demotions applied --
// with the same layout model R13 rewrites with, and web/shader.mjs throws
// "spirv-block-layout: ..." on a proven inequality, so the tier rejects
// loudly at translation with a named token instead of misreading.
//
// Fixtures: hand-built modules in testdata/spirv/layout/ (.spvasm sources
// alongside), one per shape in the honesty matrix, plus the R13 corpus.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs';
import { applySpirvFix, parseSpirv, serializeSpirv, auditBlockLayouts } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';
import { toWGSL } from '../../../../web/shader.mjs';

const L = (name) => new Uint8Array(fs.readFileSync(new URL(`../../../../testdata/spirv/layout/${name}`, import.meta.url)));
const R13 = (name) => new Uint8Array(fs.readFileSync(new URL(`../../../../testdata/spirv/r13/${name}`, import.meta.url)));
const wgsl = (bytes) => String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));
const verdicts = (bytes) => auditBlockLayouts(applySpirvFix(bytes).bytes);

// Shapes the audit must reject. SILENT = naga converts the module today
// (the audit is the only thing between the guest and wrong pixels);
// LOUD = naga also rejects, but with an unnamed error -- the audit names it.
const REJECTS = [
  ['ubo-arr-mat3-ms20.spv', 'SILENT', /MatrixStride 20 .*mat3x3<f32>.*WGSL matrix stride 16/],
  ['ssbo-arr-mat3-ms12.spv', 'SILENT', /MatrixStride 12 .*mat3x3<f32>.*WGSL matrix stride 16/],
  ['ssbo-rarr-mat3-ms12.spv', 'SILENT', /MatrixStride 12 .*mat3x3<f32>.*WGSL matrix stride 16/],
  ['ssbo-arr-mat4x3-ms12.spv', 'SILENT', /MatrixStride 12 .*mat4x3<f32>.*WGSL matrix stride 16/],
  ['ubo-arr-mat2x3-ms12.spv', 'SILENT', /MatrixStride 12 .*mat2x3<f32>.*WGSL matrix stride 16/],
  ['ubo-mixed-mat3arr-ms12.spv', 'SILENT', /member 1: MatrixStride 12 .*mat3x3<f32>/],
  ['ssbo-arr-f32-s16.spv', 'SILENT', /ArrayStride 16 .*array<f32, 4>.*WGSL storage array stride 4/],
  ['ubo-arr-f32-s32.spv', 'SILENT', /ArrayStride 32 .*array<f32, 4>.*WGSL uniform array stride 16/],
  ['ubo-mat3-ms12.spv', 'LOUD', /MatrixStride 12 .*mat3x3<f32>.*WGSL matrix stride 16/],
  ['ubo-mat3x2-ms12.spv', 'LOUD', /MatrixStride 12 .*mat3x2<f32>.*WGSL matrix stride 8/],
  ['push-mat3-ms12.spv', 'LOUD', /MatrixStride 12 .*mat3x3<f32>.*WGSL matrix stride 16/],
];
for (const [f, loudness, detail] of REJECTS) {
  test(`${f}: audit rejects (${loudness} at naga today)`, () => {
    const fixed = applySpirvFix(L(f)).bytes;
    const v = auditBlockLayouts(fixed);
    assert.equal(v.length, 1, 'exactly one block variable expected');
    assert.equal(v[0].verdict, 'reject', `detail: ${v[0].detail}`);
    assert.match(v[0].detail, detail);
    if (loudness === 'SILENT') {
      // The load-bearing pin: without the audit this module converts and
      // misreads. If a future naga starts rejecting it, the audit verdict
      // stays correct but this classification must be revisited.
      assert.doesNotThrow(() => wgsl(fixed), 'the SILENT fixtures must convert at naga');
    } else {
      assert.throws(() => wgsl(fixed), 'the LOUD fixtures fail at naga too (unnamed there)');
    }
  });
  test(`${f}: toWGSL (the tier entry point) rejects with the spirv-block-layout token`, async () => {
    await assert.rejects(() => toWGSL('spirv', L(f), 'fragment'), /spirv-block-layout: /);
  });
}

test('r13/ubo-mat3arr-stride12: the 4.16 corpus fixture is caught', () => {
  const v = verdicts(R13('ubo-mat3arr-stride12.spv'));
  assert.equal(v[0].verdict, 'reject');
  assert.match(v[0].detail, /MatrixStride 12 .*WGSL matrix stride 16/);
  // ...and it is in the silent class: it converts at naga today.
  assert.doesNotThrow(() => wgsl(applySpirvFix(R13('ubo-mat3arr-stride12.spv')).bytes));
});

// Shapes the audit must NOT reject: every decorated value equals WGSL's.
const PASSES = [
  'ubo-mat3-ms16.spv',       // mat3 at the WGSL stride
  'ubo-mat3x2-ms8.spv',      // mat3x2: WGSL column stride for vec2 columns is 8
  'push-scalars.spv',        // dxil-spirv root-constant shape
  'ssbo-rarr-vec4-s16.spv',  // runtime array, std430 stride
];
for (const f of PASSES) {
  test(`${f}: audit passes and the module converts end to end`, async () => {
    const fixed = applySpirvFix(L(f)).bytes;
    const v = auditBlockLayouts(fixed);
    assert.ok(v.every((e) => e.verdict === 'ok'), JSON.stringify(v));
    assert.doesNotThrow(() => wgsl(fixed));
    const out = await toWGSL('spirv', L(f), 'fragment');
    assert.ok(String(out).length > 0, 'toWGSL must produce WGSL');
  });
}

// Ordering pin 1 (R13): a storage-expressible-only uniform block is judged
// in its FINAL space. On the raw module (still Uniform) the audit rejects
// in uniform space; after applySpirvFix demotes it to a storage variable
// the same decorations are exactly WGSL's storage layout and it passes.
// An audit running before spirvfix would false-reject every R13 block.
test('ordering: the audit judges the post-R13 storage class, not the pre-demotion one', () => {
  const raw = R13('ubo-f32-stride4.spv');
  const pre = auditBlockLayouts(raw);
  assert.equal(pre[0].space, 'uniform');
  assert.equal(pre[0].verdict, 'reject');
  const post = verdicts(raw);
  assert.equal(post[0].space, 'storage');
  assert.equal(post[0].verdict, 'ok');
});

// Ordering pin 2 (R2): a spec-constant array length is unmodelled on the
// raw module (verdict 'unknown' -- the audit stays silent rather than
// guess); after spec folding the length is a plain constant and the block
// is judged on its merits.
test('ordering: spec-constant lengths are unknown pre-fix and judged post-fix', () => {
  const raw = L('ubo-specarr-f32-s16.spv');
  assert.equal(auditBlockLayouts(raw)[0].verdict, 'unknown');
  assert.equal(verdicts(raw)[0].verdict, 'ok');
});

// Sabotage pairs: the verdict must track the DECORATIONS, not the fixture.
test('sabotage: MatrixStride 16 -> 12 on a passing module turns it into a reject', () => {
  const mod = parseSpirv(L('ubo-mat3-ms16.spv'));
  const deco = mod.insns.find((i) => i.op === 72 && i.words[2] === 7); // MemberDecorate MatrixStride
  assert.equal(deco.words[3], 16);
  deco.words[3] = 12;
  const v = auditBlockLayouts(serializeSpirv(mod));
  assert.equal(v[0].verdict, 'reject');
  assert.match(v[0].detail, /MatrixStride 12/);
});
test('sabotage: MatrixStride 12 -> 16 on the silent fixture turns it into a pass', () => {
  const mod = parseSpirv(R13('ubo-mat3arr-stride12.spv'));
  const deco = mod.insns.find((i) => i.op === 72 && i.words[2] === 7);
  assert.equal(deco.words[3], 12);
  deco.words[3] = 16;
  const bytes = serializeSpirv(mod);
  assert.equal(auditBlockLayouts(bytes)[0].verdict, 'ok');
  assert.doesNotThrow(() => wgsl(bytes), 'with the stride fixed the module is honestly servable');
});
test('sabotage: ArrayStride 16 -> 4 on the padded SSBO array turns it into a pass', () => {
  const mod = parseSpirv(L('ssbo-arr-f32-s16.spv'));
  const deco = mod.insns.find((i) => i.op === 71 && i.words[1] === 6); // Decorate ArrayStride
  assert.equal(deco.words[2], 16);
  deco.words[2] = 4;
  assert.equal(auditBlockLayouts(serializeSpirv(mod))[0].verdict, 'ok');
});

// Corpus gate: no real captured module may be rejected. The only permitted
// rejects in the whole testdata corpus are the three intentionally
// unservable R13 fixtures (two scalar-layout blocks and the block whose
// R13 demotion is refused, leaving it in a uniform space its layout does
// not fit -- unservable at naga today too, just unnamed there).
test('corpus: zero false positives across testdata/spirv (3 known-unservable excepted)', () => {
  const root = new URL('../../../../testdata/spirv/', import.meta.url);
  const files = [];
  const walk = (dir, prefix) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      if (e.isDirectory()) { if (e.name !== 'rejected' && e.name !== 'layout') walk(new URL(e.name + '/', dir), prefix + e.name + '/'); }
      else if (e.name.endsWith('.spv')) files.push(prefix + e.name);
    }
  };
  walk(root, '');
  const rejected = [];
  for (const f of files) {
    const bytes = new Uint8Array(fs.readFileSync(new URL(f, root)));
    for (const e of auditBlockLayouts(applySpirvFix(bytes).bytes) || [])
      if (e.verdict === 'reject') rejected.push(f);
  }
  assert.deepEqual(rejected.sort(), [
    'r13/ubo-mat3arr-stride12.spv',
    'r13/ubo-pointer-phi.spv',
    'r13/ubo-vec3-stride12.spv',
  ]);
});

// Wiring pins: shader.mjs must run the audit on the post-fix bytes and
// throw the token; the web adapter must re-export the auditor.
test('wiring: shader.mjs enforces the audit and the adapter exports it', () => {
  const shader = fs.readFileSync(new URL('../../../../web/shader.mjs', import.meta.url), 'utf8');
  assert.match(shader, /auditBlockLayouts\(source\)/);
  assert.match(shader, /throw new Error\('spirv-block-layout: '/);
  const adapter = fs.readFileSync(new URL('../../../../web/spirvfix.mjs', import.meta.url), 'utf8');
  assert.match(adapter, /auditBlockLayouts/);
});
