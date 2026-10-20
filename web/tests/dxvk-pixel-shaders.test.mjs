import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../tools/spirvfix/spirvfix.mjs';
import { createShaderCache, rewritePushConstantsWGSL, scanWgslResources } from '../vkwebgpu.mjs';
import * as naga from '../vendor/naga/node.js';
const present = new Uint8Array(readFileSync(new URL('../../testdata/spirv/dxvk-live/present.frag.spv', import.meta.url)));
test('split combined samplers retain their original Vulkan descriptor binding after Naga translation', () => {
  const raw = new Uint8Array(readFileSync(new URL('../../testdata/spirv/p6-multitex.frag.spv', import.meta.url)));
  const anonymous = parseSpirv(raw);
  anonymous.insns = anonymous.insns.filter(i => i.op !== 5); // OpName is optional.
  for (const source of [raw, serializeSpirv(anonymous)]) {
    const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(source).bytes });
    const resources = scanWgslResources(wgsl);
    const textures = resources.filter(d => d.type.startsWith('texture_'));
    const samplers = resources.filter(d => d.type === 'sampler');
    assert.equal(samplers.length, 2);
    const split = samplers.filter(d => d.sourceBinding !== undefined);
    assert.deepEqual(split.map(d => d.sourceBinding), [1]);
    assert.ok(textures.some(d => d.binding === split[0].sourceBinding));
    assert.notEqual(split[0].binding, split[0].sourceBinding);
    assert.ok(samplers.some(d => d.binding === 4 && d.sourceBinding === undefined), 'separate samplers keep their own binding');
  }
});
test('real DXVK SPIR-V 1.6 combined samplers preserve entrypoint interfaces', () => {
  const fixed = applySpirvFix(present);
  assert.doesNotThrow(() => naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
  const twice = applySpirvFix(fixed.bytes);
  assert.deepEqual(twice.bytes, fixed.bytes);
  assert.deepEqual(twice.applied, []);
});
const live = name => new Uint8Array(readFileSync(new URL('../../testdata/spirv/dxvk-live/' + name, import.meta.url)));
test('real triangle shaders drop point builtins for every topology', () => {
  for (const name of ['tri9.vert.spv', 'tri9.frag.spv']) {
    const raw = live(name);
    for (const options of [{}, { rasterTopology: 0 }, { rasterTopology: 3 }, { rasterTopology: 99 }]) {
      const fixed = applySpirvFix(raw, options);
      assert.ok(fixed.applied.includes('strip-point-builtins'), name);
      const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
      assert.doesNotMatch(wgsl, /@builtin\(point_size\)|@builtin\(point_coord\)/);
      assert.doesNotThrow(() => naga.validate(naga.parseWgsl(wgsl)), name);
    }
  }
});

test('demote with continuing shader computation keeps executing and translates', () => {
  const module = parseSpirv(live('tri9.frag.spv'));
  const demote = module.insns.findIndex(i => i.op === 5380);
  const value = module.insns.find(i => i.op === 44);
  const id = module.header.bound++;
  module.insns.splice(demote + 1, 0, { op: 83, words: [value.resultType, id, value.resultId], resultType: value.resultType, resultId: id });
  const fixed = applySpirvFix(serializeSpirv(module), { rasterTopology: 3 });
  assert.ok(!fixed.applied.includes('tail-demote-to-kill'));
  assert.ok(fixed.applied.includes('lower-mid-shader-discard'));
  assert.equal(parseSpirv(fixed.bytes).insns.some(i => i.op === 5380), false);
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes }));
  assert.doesNotThrow(() => naga.validate(naga.parseWgsl(wgsl)));
});
test('shader cache accepts and separates point, non-point and unknown contexts', async () => {
  const code = live('tri9.vert.spv'), cache = createShaderCache({ log() {} });
  const shader = { code, size: code.length, hash: 'real-dxvk-tri9-vs' };
  const triangle = await cache.get('vertex', shader, { rasterTopology: 3 });
  const point = await cache.get('vertex', shader, { rasterTopology: 0 });
  const unknown = await cache.get('vertex', shader);
  assert.ok(triangle && point && unknown);
  assert.equal(cache.stats.translated, 3, 'topology variants receive separate cache entries');
  await cache.get('vertex', shader, { rasterTopology: 0 });
  assert.equal(cache.stats.hits, 1);
});
test('real naga immediate declarations become one shared push UBO per stage', () => {
  for (const name of ['tri9.vert.spv', 'tri9.frag.spv', 'present.vert.spv', 'present.frag.spv']) {
    const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(live(name), { rasterTopology: 3 }).bytes });
    const rewritten = rewritePushConstantsWGSL(wgsl);
    assert.equal(rewritten.pushBlocks, name === 'present.vert.spv' ? 0 : 1, name);
    assert.doesNotMatch(rewritten.code, /var<immediate>|var<push_constant>/);
    if (rewritten.pushBlocks) assert.match(rewritten.code, /@group\(3\) @binding\(0\) var<uniform>/);
    assert.equal(rewritePushConstantsWGSL(rewritten.code).pushBlocks, 0);
  }
});
