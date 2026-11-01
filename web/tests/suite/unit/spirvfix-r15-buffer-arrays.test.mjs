// GUARD (R15a admission): a module whose DESCRIPTOR runtime arrays are all
// servable must be admitted at vkCreateShaderModule even when the module
// also carries BUFFER-CONTENT runtime arrays -- vkd3d's SSBO-offsets block
// (a runtime array inside a storage-buffer struct) and its CBV heap (a bare
// StorageBuffer variable of runtime-array-of-struct type). Those arrays are
// buffer content, not descriptors: specialization leaves them in place, the
// tier serves them through the standard buffer path, and naga accepts the
// retained RuntimeDescriptorArray capability declaration above them.
//
// Regression (FD-55 lane, 2026-11-01): the bridge audit's skip5302 orphan
// sweep counted ANY surviving OpTypeRuntimeArray against the capability,
// so the two REAL Godot vertex modules below -- captured from the demo's
// guest stream, where each rejection killed a Godot pipeline with
// 0x80004005 -- were rejected with VK_ERROR_FEATURE_NOT_PRESENT although
// their descriptor arrays are textbook R15a shapes and the tier translates
// them cleanly. The fixtures are the exact dumped modules:
//   godot-vs-heap-plus-ssbo-offsets.spv (vkd3d dump 480d68cc811ceb30)
//   godot-vs-heap-plus-cbv-heap.spv     (vkd3d dump 29caf5794322da20)
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  analyzeDescriptorArrays, applySpirvFix, parseSpirv,
} from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');

const FIXTURES = [
  'godot-vs-heap-plus-ssbo-offsets.spv',
  'godot-vs-heap-plus-cbv-heap.spv',
];
const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));

// Descriptor (UniformConstant) runtime-array variables remaining in a
// parsed module, and the count of surviving OpTypeRuntimeArray types.
function arrayShape(mod) {
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  let descriptorArrayVars = 0, runtimeArrayTypes = 0;
  for (const ins of mod.insns) {
    if (ins.op === 29) runtimeArrayTypes++; // OpTypeRuntimeArray
    if (ins.op !== 59) continue; // OpVariable
    const ptr = byId.get(ins.words[0]);
    if (!ptr || ptr.op !== 32) continue; // OpTypePointer
    const pointee = byId.get(ptr.words[2]);
    if (pointee && pointee.op === 29 && ins.words[2] === 0) descriptorArrayVars++; // storage class UniformConstant
  }
  return { descriptorArrayVars, runtimeArrayTypes };
}

for (const name of FIXTURES) {
  test(`${name}: descriptor arrays servable, buffer arrays ride, tier translates`, () => {
    const bytes = load(name);
    const mod = parseSpirv(bytes);
    const before = arrayShape(mod);
    assert.ok(before.descriptorArrayVars >= 1, 'fixture must contain a descriptor array');
    assert.ok(before.runtimeArrayTypes >= 2, 'fixture must mix descriptor and buffer-content runtime arrays');

    // Every UniformConstant (descriptor) array the analyzer records is
    // servable -- that is the whole admission predicate for cap 5302.
    const { arrays } = analyzeDescriptorArrays(mod);
    const descriptorRecords = arrays.filter(a => a.set === 2);
    assert.ok(descriptorRecords.length >= 1, 'descriptor heap array recorded');
    for (const a of descriptorRecords) assert.ok(a.servable, `descriptor array set${a.set}/b${a.binding} must be servable (${a.reason})`);

    // The tier serves the module: specialization removes the descriptor
    // arrays, at least one buffer-content runtime array survives BY DESIGN,
    // and naga translates the result.
    const fixed = applySpirvFix(bytes, {});
    const after = arrayShape(parseSpirv(new Uint8Array(fixed.bytes)));
    assert.equal(after.descriptorArrayVars, 0, 'descriptor arrays must be specialized away');
    assert.ok(after.runtimeArrayTypes >= 1, 'buffer-content runtime arrays must survive specialization');
    const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
    assert.ok(wgsl.length > 0, 'naga must translate the specialized module');
  });
}

test('bridge audit gates cap 5302 on descriptor arrays only (mirror guard)', () => {
  // The C++ sweep must consult the UniformConstant-pointee set before
  // letting a surviving runtime array veto the capability; the old blanket
  // form (any surviving OpTypeRuntimeArray vetoes) must not return.
  assert.match(bridge, /descArrayTypes\.count\(in\.resultId\)/);
  assert.match(bridge, /in\.op == 32 && in\.n >= 3 && in\.w\[1\] == 0\) descArrayTypes\.insert\(in\.w\[2\]\)/);
  assert.doesNotMatch(bridge, /if \(in\.op == 29 && !dead\.count\(&in\)\) \{ out\.skip5302 = false; break; \}/);
});
