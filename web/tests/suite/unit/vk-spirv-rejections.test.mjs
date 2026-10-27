// GUARD (shader capability): a shader the renderer cannot execute must fail at
// VkCreateShaderModule with a greppable reason and a zero output handle.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { rewritePushConstantsWGSL } from '../../../vkwebgpu.mjs';
import { applySpirvFix } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');

function body(source, header) {
  const start = source.indexOf(header);
  assert.notEqual(start, -1, `missing ${header}`);
  const open = source.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}' && --depth === 0) return source.slice(open, i + 1);
  }
  throw new Error(`unbalanced function ${header}`);
}

test('unsupported and unknown SPIR-V capabilities reject before publishing a shader module', () => {
  const audit = body(bridge, 'static VkResult vk64AuditSpirv(');
  for (const token of ['spirv-malformed', 'spirv-capability:', 'spirv-capability-unknown:', 'spirv-compute-entry', 'spirv-storage-image-write', 'spirv-input-attachment', 'spirv-storage-buffer-write']) {
    assert.ok(audit.includes(token), `${token} must be logged`);
  }
  assert.match(audit, /vk64Unsupported\(\("spirv-capability:"/);
  assert.match(audit, /VK_ERROR_INVALID_SHADER_NV/);
  assert.match(audit, /w\[i \+ 3\] == 6\) inputAttachment = true/);
  // Typed-UAV reads (Typed-UAV-Load lane): storage-image modules pass the
  // audit when they only READ; the write form keeps a named rejection, and
  // capabilities 55/56 are used-conditionally exempt exactly like 33/4427.
  assert.match(audit, /\(c == 55 \|\| c == 56\) && !storageImageWrite\) continue/);
  assert.match(audit, /if \(storageImageWrite\) \{ vk64Unsupported\("spirv-storage-image-write"/);
  assert.match(bridge, /case 55: return "StorageImageReadWithoutFormat"/);
  assert.match(bridge, /case 56: return "StorageImageWriteWithoutFormat"/);
  assert.match(audit, /input attachments have no WebGPU subpass-input mapping; VK_ERROR_FEATURE_NOT_PRESENT/);
  assert.match(audit, /return unsupported \? VK_ERROR_FEATURE_NOT_PRESENT : VK_SUCCESS/);
  const handlers = [...bridge.matchAll(/case VK64_fn_vkCreateShaderModule: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(handlers.length, 2);
  for (const handler of handlers) {
    assert.match(handler, /VkResult auditResult = vk64AuditSpirv\(m->code\)/);
    assert.match(handler, /dropObj\(id\);[\s\S]{0,80}return auditResult;/);
    assert.match(handler, /wr64(?:_32)?\([^\n]+, 0\)/);
  }
});

test('multiple push-constant blocks in one stage are invalid Vulkan SPIR-V', () => {
  const one = rewritePushConstantsWGSL('var<immediate> constants: vec4<f32>;');
  const two = rewritePushConstantsWGSL('var<immediate> first: vec4<f32>; var<immediate> second: vec4<f32>;');
  assert.equal(one.pushBlocks, 1);
  assert.equal(two.pushBlocks, 2);
  const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.match(page, /if \(vsR\.pushBlocks > 1 \|\| fsR\.pushBlocks > 1\)/,
    'the guard is per shader stage, as required by Vulkan push-constant interface rules');
});

test('destroyed shader modules remain available through live Vulkan pipelines', () => {
  const destroys = [...bridge.matchAll(/case VK64_fn_vkDestroyShaderModule: \{([\s\S]*?)\n    \}/g)].map(m => m[1]);
  assert.equal(destroys.length, 2);
  assert.match(destroys[0], /if \(m\) m->destroyed\+\+;/);
  assert.match(destroys[1], /if \(module\) module->destroyed = 1;/);
  for (const destroy of destroys) assert.doesNotMatch(destroy, /dropObj\(/);
  assert.match(bridge, /struct ShaderModule : Obj \{[\s\S]*?std::vector<U8> code;[\s\S]*?Retain the SPIR-V words/);
  assert.match(bridge, /auto emitShader = \[&\]\(ShaderModule\* sm, U32 stage\)[\s\S]*?p\.bytes\(sm->code\)/);
});

// The capability whitelist: capabilities the page tier verifiably translates
// must NOT route to vk64Unsupported. Each whitelisted cap is pinned twice —
// its nullptr case in vk64SpirvCapabilityName and its exemption in the audit
// loop — so removing either half turns this test red. Caps 2 (Geometry)
// and 35 (SampleRateShading) joined on 2026-10-26: 35 is translation-
// served (naga translates the SampleId read), and 2 is governed by the
// geometry-stage disposition — meta fragment modules are translation-
// served via spirvfix R11 (conversion cases below), true geometry
// stages are recorded and their draws governed at submit; see
// vk-gs-passthrough.test.mjs for those pins.
test('page-tier-translated capabilities are whitelisted in the audit', () => {
  const table = body(bridge, 'static const char* vk64SpirvCapabilityName(');
  const audit = body(bridge, 'static VkResult vk64AuditSpirv(');
  const exempt = [...audit.matchAll(/c != (\d+)/g)].map((m) => Number(m[1])).sort((a, b) => a - b);
  assert.deepEqual(exempt, [0, 1, 2, 32, 35, 43, 50, 51, 68, 69, 4464, 4465, 4466, 4467, 4468, 5301, 5345, 5379],
    'the audit loop exemption set is exactly {Matrix, Shader, Geometry, ShaderNonUniform, ClipDistance, Sampled1D, SampleRateShading, ImageQuery, DerivativeControl, ShaderLayer, ShaderViewportIndex, the float-controls family, ShaderNonUniform indexing, VulkanMemoryModel, DemoteToHelperInvocationEXT} (union of the integ and vkd3d sets; 5254 ShaderViewportIndexLayerEXT stays a named rejection and is absent by design)');
  for (const cap of [2, 32, 35, 43, 50, 51, 68, 69, 4464, 4465, 4466, 4467, 4468, 5301, 5345, 5379]) {
    assert.ok(table.includes(`case ${cap}: return nullptr;`),
      `cap ${cap} must be a nullptr (non-rejecting) case in vk64SpirvCapabilityName`);
  }
});

// CullDistance (33) and DrawParameters (4427) are declared unconditionally by
// DXVK's vertex-shader init. Their declared-but-unused forms are servable
// (naga accepts an unused CullDistance declaration as-is; spirvfix R5 strips
// an unused DrawParameters declaration), so the audit exempts them ONLY when
// no matching BuiltIn variable is present; the used forms stay named
// rejections (see the corpus test below for why they must stay rejected).
test('CullDistance and DrawParameters reject only when actually used', () => {
  const table = body(bridge, 'static const char* vk64SpirvCapabilityName(');
  const audit = body(bridge, 'static VkResult vk64AuditSpirv(');
  assert.ok(table.includes('case 33: return "CullDistance";'), 'CullDistance stays a named rejection for the used form');
  assert.ok(table.includes('case 4427: return "DrawParameters";'), 'DrawParameters stays a named rejection for the used form');
  assert.ok(audit.includes('if (c == 33 && !cullUsed) continue;'), 'declared-but-unused CullDistance is exempted');
  assert.ok(audit.includes('if (c == 4427 && !drawUsed) continue;'), 'declared-but-unused DrawParameters is exempted');
  assert.match(audit, /w\[i \+ 3\] == 4\) cullUsed = true/, 'OpDecorate BuiltIn CullDistance sets cullUsed');
  assert.match(audit, /w\[i \+ 4\] == 4\) cullUsed = true/, 'OpMemberDecorate BuiltIn CullDistance sets cullUsed');
  assert.match(audit, /w\[i \+ 3\] >= 4424 && w\[i \+ 3\] <= 4426\) drawUsed = true/, 'OpDecorate BuiltIn BaseVertex/BaseInstance/DrawIndex sets drawUsed');
  assert.match(audit, /w\[i \+ 4\] >= 4424 && w\[i \+ 4\] <= 4426\) drawUsed = true/, 'OpMemberDecorate draw builtins set drawUsed');
});

// Translate-side proof for the whitelist: the captured DXVK modules carrying
// the whitelisted capabilities, plus the hand-assembled declared-but-unused
// CullDistance / DrawParameters modules the audit now admits, must all
// convert through the same spirvfix + naga path the page tier uses.
test('whitelisted-capability modules convert through spirvfix + naga', () => {
  const cases = [
    'testdata/spirv/dxvk-live/tri9.vert.spv',   // caps 32, 50
    'testdata/spirv/dxvk-live/tri9.frag.spv',   // caps 50, 51, 5379
    'testdata/spirv/dxvk-live/present.frag.spv', // cap 43
    'testdata/spirv/dxvk-live/present.vert.spv',
    'testdata/spirv/p9-cull-unused.vert.spv',   // cap 33 declared, unused
    'testdata/spirv/p10-drawparams-unused.vert.spv', // cap 4427 declared, unused
    'testdata/spirv/dxvk-live/tropics-dxbc.vert.spv', // caps 4465 + 5345 (DXBC vertex shader, real capture)
    'testdata/spirv/dxvk-live/dxvk-meta-copy-color-1d.frag.spv', // caps 2, 43 (DXVK meta copy, build-header extraction)
    'testdata/spirv/dxvk-live/dxvk-meta-copy-color-2d.frag.spv', // cap 2
    'testdata/spirv/dxvk-live/dxvk-meta-copy-color-ms.frag.spv', // caps 2, 35
    'testdata/spirv/dxvk-live/dxvk-meta-blit-2d.frag.spv',       // cap 2
    'testdata/spirv/dxvk-live/dxvk-meta-resolve-f.frag.spv',     // cap 2
    'testdata/spirv/dxvk-live/dxvk-fullscreen.vert.spv',         // vertex stage of the same meta pipelines
  ];
  for (const rel of cases) {
    const bytes = new Uint8Array(readFileSync(new URL(`../../../../${rel}`, import.meta.url)));
    const { bytes: fixed } = applySpirvFix(bytes);
    const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: fixed }));
    assert.ok(wgsl.length > 0, `${rel}: empty WGSL`);
  }
});

// The float-controls family (4464-4468) and VulkanMemoryModel (5345) are
// module-level declarations, not constructs: the DXBC compiler puts 5345 on
// every module it emits and adds 4465 (+ its execution mode) to state D3D FP
// semantics whenever the device float-controls features allow it. The bridge
// admits all six; the page tier strips the float-controls family for naga
// (spirvfix R7) and ingests the Vulkan memory model natively. The genuine
// ShaderViewportIndexLayerEXT capability is 5254 -- the table carried it
// against 5345 (VulkanMemoryModel) until 2026-10-26, which made the audit
// reject every DXBC module while reporting a viewport feature no shader had
// declared; 5254 stays a named rejection under its correct number.
test('float-controls and VulkanMemoryModel are whitelisted; 5254 is the real viewport-index-layer cap', () => {
  const table = body(bridge, 'static const char* vk64SpirvCapabilityName(');
  for (const cap of [4464, 4465, 4466, 4467, 4468, 5345]) {
    assert.ok(table.includes(`case ${cap}: return nullptr;`), `cap ${cap} must be whitelisted`);
  }
  assert.ok(table.includes('case 5254: return "ShaderViewportIndexLayerEXT"'),
    'ShaderViewportIndexLayerEXT is capability 5254');
  assert.ok(!table.includes('case 5345: return "ShaderViewportIndexLayerEXT"'),
    'capability 5345 (VulkanMemoryModel) must not be mislabeled ShaderViewportIndexLayerEXT');
});

// The used forms stay rejected at the bridge because the page tier cannot
// translate them today (testdata/spirv/rejected/README.md). If a future
// spirvfix/naga fix makes either module convert, this test goes red on
// purpose: the audit's used-form rejection must then be re-dispositioned in
// docs/known-gaps/vulkan.md instead of silently staying rejected.
test('used CullDistance / DrawParameters shapes remain untranslatable', () => {
  const cull = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/rejected/cull-used.vert.spv', import.meta.url)));
  assert.throws(
    () => naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(cull).bytes }),
    /CullDistance/,
    'a module that writes BuiltIn CullDistance must still fail translation');
  const draw = new Uint8Array(readFileSync(new URL('../../../../testdata/spirv/rejected/draw-used.vert.spv', import.meta.url)));
  assert.throws(
    () => naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(draw).bytes }),
    undefined,
    'a module that loads the draw builtins must still fail translation (spirvfix R5 replace emits invalid SPIR-V for this layout)');
});
