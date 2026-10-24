// web/tests/suite/unit/spirvfix-unsupported-interface.test.mjs — spirvfix must
// report, not silently skip, a builtin whose interface shape it cannot rewrite.
//
// tools/spirvfix/spirvfix.mjs leaves a PointSize/PointCoord/DrawParameters/Layer
// builtin in place when its variable has an unexpected storage class. The shader
// then keeps a builtin WebGPU cannot translate, so the only signal is this log
// line. Dropping it would hide the reason a shader fails to compile.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';

const buildModule = (insns, bound = 100) => ({
  header: { magic: 0x07230203, version: 0x00010600, generator: 0, bound, schema: 0 },
  insns,
});

// PointSize (BuiltIn 1) on an Input-storage variable. The rewrite expects
// Output storage for PointSize, so the shape is unsupported.
const pointSizeOnInput = () => buildModule([
  { op: 22, words: [10, 32] },            // OpTypeFloat 32
  { op: 32, words: [11, 1, 10] },         // OpTypePointer Input float
  { op: 59, words: [11, 12, 1] },         // OpVariable %12 Input
  { op: 71, words: [12, 11, 1] },         // OpDecorate %12 BuiltIn PointSize
]);

// Layer (BuiltIn 9) on a fragment entry point, with Output storage where the
// fragment-layer rewrite expects Input. Same silent-skip risk, different rule.
const layerOnOutput = () => buildModule([
  { op: 22, words: [10, 32] },            // OpTypeFloat 32
  { op: 32, words: [11, 3, 10] },         // OpTypePointer Output float
  { op: 59, words: [11, 12, 3] },         // OpVariable %12 Output
  { op: 71, words: [12, 11, 9] },         // OpDecorate %12 BuiltIn Layer
  { op: 15, words: [4, 1, 0x6E69616D, 0] }, // OpEntryPoint Fragment %1 "main"
]);

test('fragment Layer rewrite logs the unsupported interface shape and leaves the builtin', () => {
  const r = applySpirvFix(serializeSpirv(layerOnOutput()));
  assert.ok(
    r.log.includes('spirvfix: left Layer builtin %12 unchanged (unsupported interface shape)'),
    `expected the Layer unsupported-shape warning, got: ${JSON.stringify(r.log)}`,
  );
  assert.ok(!r.applied.includes('fragment-layer-to-zero'), 'the Layer rewrite must not report itself as applied');
});

test('strip-point-builtins logs the unsupported interface shape and leaves the builtin', () => {
  const r = applySpirvFix(serializeSpirv(pointSizeOnInput()));
  assert.ok(
    r.log.includes('spirvfix: left point builtin %12 unchanged (unsupported interface shape)'),
    `expected the unsupported-shape warning, got: ${JSON.stringify(r.log)}`,
  );
  assert.ok(!r.applied.includes('strip-point-builtins'), 'the rewrite must not report itself as applied');
  const out = parseSpirv(r.bytes);
  assert.ok(
    out.insns.some(i => i.op === 71 && i.words[0] === 12 && i.words[1] === 11 && i.words[2] === 1),
    'the PointSize decoration must survive untouched',
  );
});
