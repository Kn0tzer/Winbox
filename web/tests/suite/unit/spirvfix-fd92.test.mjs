// FD-92 — spirvfix's synthesized definitions must land behind the module
// definitions they reference. A valid module may define a type AFTER its
// first global variable (the captured 369,400-byte Godot module defines
// OpTypeBool at instruction 333, past its first variable at 299);
// lower-mid-shader-discard spliced its synthesized Private/Function
// pointer-to-bool types at the first-variable anchor, ahead of
// OpTypeBool, and spirv-val rejected the fixed module: "Operand
// '54[%bool]' requires a previous definition". The minimal module below
// carries the same shape in miniature: a global variable precedes
// OpTypeBool, and a helper query plus a non-entry discard make the rule
// synthesize both pointer-to-bool types and the bool constants.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applySpirvFix, parseSpirv, idOperandUseIndex } from '../../../../tools/spirvfix/spirvfix.mjs';

function buildLateBoolModule() {
  const words = [0x07230203, 0x00010000, 0, 13, 0];
  const put = (op, ...ops) => words.push((((ops.length + 1) << 16) | op) >>> 0, ...ops.map(w => w >>> 0));
  const str = s => {
    const b = [...s].map(c => c.charCodeAt(0)); b.push(0);
    while (b.length % 4) b.push(0);
    const o = [];
    for (let i = 0; i < b.length; i += 4)
      o.push((b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0);
    return o;
  };
  put(17, 1);                                            // OpCapability Shader
  put(17, 5379);                                         // OpCapability DemoteToHelperInvocationEXT
  put(10, ...str('SPV_EXT_demote_to_helper_invocation')); // OpExtension
  put(14, 0, 1);                                         // OpMemoryModel Logical GLSL450
  put(15, 4, 10, ...str('main'));                        // OpEntryPoint Fragment %main
  put(16, 10, 7);                                       // OpExecutionMode %main OriginUpperLeft
  put(19, 2);                                            // %2 = OpTypeVoid
  put(33, 3, 2);                                         // %3 = OpTypeFunction %2
  put(22, 4, 32);                                        // %4 = OpTypeFloat 32
  put(32, 5, 6, 4);                                      // %5 = OpTypePointer Private %4
  put(59, 5, 6, 6);                                      // %6 = OpVariable %5 Private  (first global variable)
  put(20, 7);                                            // %7 = OpTypeBool  (defined AFTER the first variable)
  put(54, 2, 8, 0, 3);                                   // %8 = OpFunction %2 None %3
  put(248, 9);                                           // %9 = OpLabel
  put(252);                                              // OpKill (non-entry discard site)
  put(56);                                               // OpFunctionEnd
  put(54, 2, 10, 0, 3);                                  // %10 = OpFunction %2 None %3 (main)
  put(248, 11);                                          // %11 = OpLabel
  put(5381, 7, 12);                                      // %12 = OpIsHelperInvocationEXT %7
  put(253);                                              // OpReturn
  put(56);                                               // OpFunctionEnd
  return new Uint8Array(new Uint32Array(words).buffer);
}

test('FD-92: synthesized pointer-to-bool types land behind a late OpTypeBool', () => {
  const fixed = applySpirvFix(buildLateBoolModule());
  assert.ok(fixed.applied.includes('lower-mid-shader-discard'), 'the discard lowering fires');
  const out = parseSpirv(fixed.bytes);
  assert.ok(!out.insns.some(i => i.op === 5381), 'helper queries are lowered to flag loads');

  const firstFn = out.insns.findIndex(i => i.op === 54);
  const decl = out.insns.slice(0, firstFn);
  const boolId = decl.find(i => i.op === 20)?.resultId;
  assert.ok(boolId != null, 'the module keeps its OpTypeBool');
  const boolAt = decl.findIndex(i => i.resultId === boolId);
  const ptrs = decl.map((ins, idx) => [ins, idx]).filter(([i]) => i.op === 32 && i.words[2] === boolId);
  assert.equal(ptrs.length, 2, 'both synthesized pointer-to-bool types (Private + Function) are present');
  for (const [, idx] of ptrs) assert.ok(idx > boolAt, 'every pointer-to-bool type follows OpTypeBool');

  // The general contract spirv-val enforces on the declaration region:
  // every grammar-typed id operand (and every result type) names a
  // definition that precedes it in the region.
  const byId = new Map(out.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const defAt = new Map();
  decl.forEach((ins, idx) => { if (ins.resultId != null && !defAt.has(ins.resultId)) defAt.set(ins.resultId, idx); });
  decl.forEach((ins, idx) => {
    const refs = new Set(ins.words);
    if (ins.resultType != null) refs.add(ins.resultType);
    for (const id of refs) {
      if (id === ins.resultId || !defAt.has(id)) continue;
      if (id !== ins.resultType && idOperandUseIndex(byId, ins, id) < 0) continue; // a literal word, not an id operand
      assert.ok(defAt.get(id) < idx, `instruction ${idx} (op ${ins.op}) references %${id}, defined at ${defAt.get(id)}`);
    }
  });
});
