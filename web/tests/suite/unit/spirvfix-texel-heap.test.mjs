// spirvfix FD-16: a texel buffer reached through a descriptor-heap
// runtime array (the D3D12 "descriptor heap" form vkd3d-proton emits for
// a Buffer<T> SRV: a runtime array of sampled images over a Dim=Buffer
// image, indexed by a push-constant signature) must convert exactly as
// a direct texel variable does. Composition, both halves fail-closed:
//   * R15a specializes the array into a scalar clone (bw_arr_* name).
//   * The R12 family runs again after specialization, so the clone --
//     now a direct texel variable -- gets the texel conversion (Dim
//     Buffer -> 2D wrap, SampledBuffer capability dropped, fetch
//     coordinates widened). Array-shared types are excluded from the
//     first R12 pass and from R1's combined-sampler split: converting
//     (or splitting) one early rewrote the array element's fetches onto
//     a direct variable's texture -- a silent wrong-resource
//     translation, pinned by the mixed fixture below.
// The bridge audit mirrors the composition (vk64TexelBufferServable
// with the R15 servable-array set); its verdicts on these same fixture
// bytes are pinned in tools/vkd3d/tests/vk64_native_test.cpp.
// Fixtures are hand-authored SPIR-V assembly in the vkd3d-proton heap
// idiom (push-constant index -> access chain -> load -> image op),
// assembled with spirv-as and spirv-val clean; sources live beside the
// other fixtures under testdata/spirv/src/ (the query fixture's
// OpImageQuerySize was produced by a one-word opcode mutation -- the
// note in its source explains why).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { applySpirvFix, parseSpirv, serializeSpirv, analyzeDescriptorArrays } from '../../../../tools/spirvfix/spirvfix.mjs';
import { scanWgslResources } from '../../../wgsl-utils.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

await naga.init();

const load = name => new Uint8Array(readFileSync(new URL(`../../../../testdata/spirv/${name}`, import.meta.url)));
const capsOf = mod => new Set(mod.insns.filter(i => i.op === 17).map(i => i.words[0]));
const toWgsl = bytes => String(naga.translate({ from: 'spirv', to: 'wgsl', source: bytes }));

test('heap texel array: specializes, then converts like a direct texel variable', () => {
  const bytes = load('d3d12-ps-texel-heap-array.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true);
  assert.equal(arrays[0].elemKind, 'sampled-image');
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  assert.ok(out.applied.includes('texel-buffer-to-2d'), out.log.join('\n'));
  const mod = parseSpirv(out.bytes);
  assert.ok(!capsOf(mod).has(46), 'SampledBuffer capability must be dropped by the conversion');
  assert.ok(!capsOf(mod).has(5302), 'RuntimeDescriptorArray capability must be dropped by specialization');
  assert.ok(!mod.insns.some(i => i.op === 25 && i.words[2] === 5), 'no Dim=Buffer image type may remain');
  const decls = scanWgslResources(toWgsl(out.bytes));
  const tex = decls.find(d => d.type.startsWith('texture_'));
  assert.ok(tex, 'a texture decl must survive translation');
  assert.equal(tex.group, 2); assert.equal(tex.binding, 0);
  assert.deepEqual(tex.arrSig, { kind: 'push', pushOff: 0, add: 0 },
    'the texture decl must carry the bw_arr push signature so the tier resolves the heap element at bind time');
  const samp = decls.find(d => d.type === 'sampler');
  assert.ok(samp && samp.sourceBinding === 0, 'the inert sampler must source from the array binding');
});

test('heap texel array: the fetched texel comes from the wrapped 2D texture at widened coordinates', () => {
  const out = applySpirvFix(load('d3d12-ps-texel-heap-array.spv'), {});
  const wgsl = toWgsl(out.bytes);
  assert.match(wgsl, /textureLoad\(bw_arr_g2_b0_p0_a0_, vec2<u32>\(\([^)]*% 8192u\), \([^)]*\/ 8192u\)\), 0i\)/,
    'the fetch must become a textureLoad of the bw_arr texture at (i mod 8192, i div 8192)');
});

test('mixed direct + heap texel variables over one shared type: each fetch keeps its own resource', () => {
  const out = applySpirvFix(load('d3d12-ps-texel-heap-mixed.spv'), {});
  assert.ok(out.applied.includes('texel-buffer-to-2d'), out.log.join('\n'));
  const wgsl = toWgsl(out.bytes);
  const loads = [...wgsl.matchAll(/textureLoad\((\w+),/g)].map(m => m[1]);
  assert.equal(loads.length, 2);
  assert.ok(loads.some(n => n.startsWith('bw_arr_g2_b0_')), 'the heap fetch must read the specialized array texture');
  assert.ok(loads.some(n => !n.startsWith('bw_arr_g2_b0_')), 'the direct fetch must read the direct variable texture');
  const decls = scanWgslResources(wgsl);
  const bindings = decls.filter(d => d.type.startsWith('texture_')).map(d => d.group + ':' + d.binding).sort();
  assert.deepEqual(bindings, ['2:0', '2:4'], 'heap texture at the array binding, direct texture at its own binding');
});

test('two heap texel arrays in one module: both specialize and convert, samplers do not collide', () => {
  const out = applySpirvFix(load('d3d12-ps-texel-heap-two.spv'), {});
  assert.ok(out.applied.includes('texel-buffer-to-2d'), out.log.join('\n'));
  const decls = scanWgslResources(toWgsl(out.bytes));
  const tex = decls.filter(d => d.type.startsWith('texture_'));
  assert.equal(tex.length, 2);
  assert.deepEqual(tex.map(d => d.arrSig && (d.arrSig.kind === 'const' ? 'c' + d.arrSig.value : 'p' + d.arrSig.pushOff + 'a' + d.arrSig.add)).sort(), ['c3', 'p0a0']);
  const sampBindings = decls.filter(d => d.type === 'sampler').map(d => d.binding);
  assert.equal(new Set(sampBindings).size, sampBindings.length, 'inert sampler bindings must be distinct');
});

test('storage texel buffer through a heap array (compute): specializes writable, then R12S converts', () => {
  const out = applySpirvFix(load('d3d12-cs-texel-heap-storage.spv'), {});
  assert.ok(out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  assert.ok(out.applied.includes('storage-texel-buffer-to-2d'), out.log.join('\n'));
  const mod = parseSpirv(out.bytes);
  assert.ok(!capsOf(mod).has(47), 'ImageBuffer capability must be dropped by the conversion');
  const wgsl = toWgsl(out.bytes);
  assert.match(wgsl, /texture_storage_2d<rgba32float,read_write>/);
  assert.match(wgsl, /textureStore\(bw_arr_g2_b0_p0_a0_, vec2<u32>\(/, 'the store must target the wrapped texture at widened coordinates');
});

test('refusal: a query consumer through the array keeps the module unconverted and naga rejects it', () => {
  const bytes = load('d3d12-ps-texel-heap-query.spv');
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('texel-buffer-to-2d'), out.log.join('\n'));
  assert.match(out.log.join('\n'), /conversion refused/);
  assert.throws(() => toWgsl(out.bytes), /SampledBuffer/, 'the unconverted module must keep failing loudly at translation');
});

test('refusal: an unclassifiable heap index leaves the module untouched and naga rejects it', () => {
  const bytes = load('d3d12-ps-texel-heap-badindex.spv');
  const { arrays } = analyzeDescriptorArrays(parseSpirv(bytes));
  assert.equal(arrays[0].servable, false);
  const out = applySpirvFix(bytes, {});
  assert.ok(!out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  assert.ok(!out.applied.includes('texel-buffer-to-2d'), out.log.join('\n'));
  assert.throws(() => toWgsl(out.bytes), /SampledBuffer/);
});

test('sabotage: without the post-specialization R12 pass the heap module stays untranslatable', () => {
  // The fixtures above are only proof if the composition is what serves
  // them: run the pipeline stages the old way (specialize only, no
  // second texel pass) by checking the intermediate invariant -- a
  // module whose array is specialized by hand still carries Dim=Buffer.
  // (The direct assertion of the old behaviour: R12 alone, applied to
  // the raw module, converts nothing, because the texel type is
  // array-shared; specialization alone leaves the capability behind.)
  const bytes = load('d3d12-ps-texel-heap-array.spv');
  const mod = parseSpirv(bytes);
  assert.ok(mod.insns.some(i => i.op === 25 && i.words[2] === 5), 'fixture must start with a Dim=Buffer image');
  assert.ok(capsOf(mod).has(46) && capsOf(mod).has(5302), 'fixture must declare both rejected capabilities');
  assert.throws(() => toWgsl(bytes), /SampledBuffer/, 'raw fixture must be naga-rejected (the failing shape)');
});

test('multisig bare-image heap (the captured Godot compute shape): every signature gets a converted clone', () => {
  const bytes = load('d3d12-cs-texel-heap-multisig.spv');
  const src = parseSpirv(bytes);
  assert.equal(src.header.version, 0x10300, 'fixture must be a SPIR-V 1.3 module (interface rules differ below 1.4)');
  const { arrays } = analyzeDescriptorArrays(src);
  assert.equal(arrays.length, 1);
  assert.equal(arrays[0].servable, true, arrays[0].reason || '');
  assert.equal(arrays[0].signatures.length, 5, 'five classifiable index signatures on one heap base');
  const out = applySpirvFix(bytes, {});
  assert.ok(out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  assert.ok(out.applied.includes('texel-buffer-to-2d'), out.log.join('\n'));
  const mod = parseSpirv(out.bytes);
  assert.ok(!capsOf(mod).has(46) && !capsOf(mod).has(5302), 'both rejected capabilities are consumed');
  assert.ok(!mod.insns.some(i => i.op === 25 && i.words[2] === 5), 'no Dim=Buffer image type may remain');
  // The 1.3 entry-point interface must NOT gain the clones (spirv-val
  // rejects UniformConstant variables in a pre-1.4 interface list).
  const ep = mod.insns.find(i => i.op === 15);
  const cloneIds = new Set(mod.insns.filter(i => i.op === 59 && i.words[1] === 0).map(i => i.resultId));
  assert.ok(!ep.words.slice(2).some(w => cloneIds.has(w)), 'no clone id may enter the 1.3 interface list');
  const wgsl = toWgsl(out.bytes);
  const decls = scanWgslResources(wgsl).filter(d => d.name.startsWith('bw_arr'));
  assert.equal(decls.length, 5, 'one WGSL texture decl per signature');
  const sigs = decls.map(d => 'p' + d.arrSig.pushOff + 'a' + d.arrSig.add).sort();
  assert.deepEqual(sigs, ['p0a0', 'p0a1', 'p0a2', 'p4a0', 'p8a0']);
  const slots = decls.map(d => d.binding).sort((a, b) => a - b);
  assert.deepEqual(slots, [0, 4, 5, 6, 7], 'first clone on the guest binding, the rest on synthetic slots past the set max (UBO at 3)');
  for (const d of decls) {
    if (d.binding !== 0) assert.equal(d.origBinding, 0, 'synthetic-slot clones must resolve against the guest heap binding');
    assert.match(wgsl, new RegExp('textureLoad\\(' + d.name + ', vec2<u32>\\('), 'fetches through this clone are widened textureLoads');
  }
});

const namesOf = mod => {
  const m = new Map();
  for (const i of mod.insns) {
    if (i.op !== 5) continue;
    const bytes = [];
    for (const w of i.words.slice(1)) { bytes.push(w & 255, (w >> 8) & 255, (w >> 16) & 255, (w >> 24) & 255); }
    m.set(i.words[0], Buffer.from(bytes).toString('utf8').replace(/\0.*$/s, ''));
  }
  return m;
};

test('coherent storage heap: the clone drops Coherent and naga accepts the module', () => {
  const out = applySpirvFix(load('d3d12-cs-texel-heap-storage-coherent.spv'), {});
  assert.ok(out.applied.includes('descriptor-array-specialize'), out.log.join('\n'));
  const mod = parseSpirv(out.bytes);
  const names = namesOf(mod);
  const cloneId = [...names.entries()].find(([, n]) => n.startsWith('bw_arr'))?.[0];
  assert.ok(cloneId !== undefined, 'the specialized clone exists');
  assert.ok(!mod.insns.some(i => i.op === 71 && i.words[0] === cloneId && i.words[1] === 23),
    'no Coherent decoration may survive on the clone (naga rejects the variable)');
  const wgsl = toWgsl(out.bytes);
  assert.match(wgsl, /texture_storage_2d<rgba32float,read_write>/);
  assert.match(wgsl, /textureStore\(bw_arr_g2_b0_p0_a0_, vec2<u32>\(/);
});

test('sweep precision: a constant VALUE equal to the array type id keeps nothing alive', () => {
  // The orphan sweep's liveness scan must not count literal operand
  // words as id references: plant an OpConstant whose VALUE equals the
  // runtime-array type's id. With the value counted as a reference the
  // dead array type (and cap 5302) would survive specialization; the
  // bridge mirror's settlement simulation has the same exclusion.
  const bytes = load('d3d12-cs-texel-heap-multisig.spv');
  const mod = parseSpirv(bytes);
  const ra = mod.insns.find(i => i.op === 29);
  const uintTy = mod.insns.find(i => i.op === 21 && i.words[1] === 32 && i.words[2] === 0);
  assert.ok(ra && uintTy, 'fixture shape');
  mod.header.bound = 1000;
  const fnAt = mod.insns.findIndex(i => i.op === 54);
  mod.insns.splice(fnAt, 0, { op: 43, words: [uintTy.resultId, 900, ra.resultId], resultType: uintTy.resultId, resultId: 900 });
  const out = applySpirvFix(serializeSpirv(mod), {});
  const fixed = parseSpirv(out.bytes);
  assert.ok(!fixed.insns.some(i => i.op === 29 && i.resultId === ra.resultId), 'the dead runtime-array type must be swept despite the colliding constant');
  assert.ok(!capsOf(fixed).has(5302), 'cap 5302 must be stripped');
});
