// GUARD (unsupported pipeline state): Vulkan must not build an approximation
// when the corresponding feature or state has no WebGPU replay implementation.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const page = readFileSync(new URL('web/vkwebgpu.mjs', root), 'utf8');
const manifest = JSON.parse(readFileSync(new URL('source/vulkan/vk-capabilities.json', root), 'utf8'));

function functionBody(source, header) {
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

test('unsupported graphics pipeline state is warned and rejected in the shared audit', () => {
  const body = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  const unsupported = [
    ['polygonMode', 'polygon-mode-line'], ['logicOp', 'logic-op'], ['depthBounds', 'depth-bounds-test'],
    ['sampleShading', 'sample-shading'], ['alphaToOne', 'alpha-to-one'], ['viewportCount', 'multi-viewport'],
    ['vertexDivisor', 'vertex-attribute-divisor'], ['sampleCount', 'sample-count'],
  ];
  for (const [field, feature] of unsupported) {
    assert.match(body, new RegExp(`a\\.${field}[^\\n]*vk64Unsupported\\([^\\n]*"${feature}"[^\\n]*reject = true`), feature);
  }
  for (const feature of [
    'tessellation-control-stage', 'tessellation-evaluation-stage', 'geometry-stage', 'task-stage', 'mesh-stage',
    'unknown-graphics-stage', 'patch-list-topology', 'adjacency-topology',
  ]) assert.match(body, new RegExp(`vk64Unsupported\\("${feature}"[^\\n]*reject = true`), feature);
  assert.match(body, /return reject;/);
  assert.match(bridge, /if \(vk64AuditGraphicsPipeline\(audit\)(?: \|\| dynamicRejected)?\) \{[\s\S]{0,180}VK_ERROR_FEATURE_NOT_PRESENT/);
  for (const capability of [
    'fillCoreFeatures.fillModeNonSolid', 'fillCoreFeatures.logicOp', 'fillCoreFeatures.depthBounds',
    'fillCoreFeatures.sampleRateShading', 'fillCoreFeatures.alphaToOne', 'fillCoreFeatures.multiViewport',
  ]) assert.ok(manifest.notAdvertised.includes(capability), `${capability} must remain disabled`);
  for (const extension of ['VK_EXT_mesh_shader']) assert.ok(manifest.notAdvertisedExtensions.includes(extension), `${extension} must remain absent`);
});

test('unsupported vertex formats fail pipeline creation in both guest ABIs', () => {
  const body = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  assert.match(body, /if \(a\.unsupportedVertexFormat\) \{ vk64Unsupported\("vertex-format", "pipeline rejected"\); reject = true; \}/);
  assert.equal((bridge.match(/audit\.unsupportedVertexFormat = true/g) || []).length, 2,
    '64-bit and 32-bit pipeline creation must reject vertex formats outside VkFormatFeatureFlags');
  assert.equal((bridge.match(/v2FormatIsSupportedVertex\(p->vaFormat\[a\]\)/g) || []).length, 2);
  assert.match(bridge, /static bool v2FormatIsSupportedVertex\(U32 format\)[\s\S]*?return v2FormatIn\(format, kV2SupportedVertexFormats\);/);
  assert.match(bridge, /v2FormatBufferFeatureFlags\(U32 format\)\s*\{\s*VkFormatFeatureFlags flags = v2FormatIsSupportedVertex\(format\)/);
});

test('vertex binding limits follow WebGPU and pipeline creation rejects excess bindings', () => {
  const audit = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  assert.match(audit, /a\.unsupportedVertexBindingCount[^\n]*vk64Unsupported\("vertex-binding-limit"[^\n]*reject = true/);
  assert.equal((bridge.match(/audit\.unsupportedVertexBindingCount = p->nvb > v2WebGpuMaxVertexBuffers\(\)/g) || []).length, 2);
  assert.match(bridge, /maxVertexBuffers = \(U32\)MAIN_THREAD_EM_ASM_INT\([\s\S]*?L\.maxVertexInputBindings = maxVertexBuffers > 16 \? 16 : maxVertexBuffers/);
  assert.match(bridge, /static U32 v2WebGpuMaxVertexBuffers\(\)[\s\S]*?bwVkMaxVertexBuffers[\s\S]*?return 8;/);
  assert.equal((page.match(/globalThis\.bwVkMaxVertexBuffers = Math\.min\(16, device\.limits\?\.maxVertexBuffers \|\| 8\)/g) || []).length, 2);
});

test('unsupported blend enums and mixed constants fail pipeline creation in both guest ABIs', () => {
  const audit = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  assert.match(audit, /a\.unsupportedBlendEnum[^\n]*vk64Unsupported\("blend-enum"[^\n]*reject = true/);
  assert.match(audit, /a\.mixedBlendConstants[^\n]*vk64Unsupported\("mixed-blend-constants"[^\n]*reject = true/);
  const blendAudit = functionBody(bridge, 'static void vk64AuditBlend(');
  assert.match(blendAudit, /b\.colorOp > 4 \|\| b\.alphaOp > 4[\s\S]*?b\.colorOp < 3 && \(b\.srcColor > 14 \|\| b\.dstColor > 14\)[\s\S]*?b\.alphaOp < 3 && \(b\.srcAlpha > 14 \|\| b\.dstAlpha > 14\)[\s\S]*?a\.unsupportedBlendEnum = true/,
    'factor validation must follow the Vulkan rule that MIN/MAX ignore factors');
  assert.match(blendAudit, /a\.mixedBlendConstants = colorConstant && alphaConstant/);
  assert.equal((bridge.match(/vk64AuditBlend\(\*p, audit\)/g) || []).length, 2,
    'both guest ABIs must reject unsupported blend state before emitting a pipeline');
  assert.match(bridge, /result = VK_ERROR_FEATURE_NOT_PRESENT;[\s\S]{0,120}continue;/);
});

test('unsupported stencil enums and two-sided static masks fail both guest pipeline creates', () => {
  const audit = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  assert.match(audit, /a\.unsupportedStencilEnum[^\n]*vk64Unsupported\("stencil-enum"[^\n]*reject = true/);
  assert.match(audit, /a\.incompatibleStencilFaces[^\n]*vk64Unsupported\("stencil-face-state"[^\n]*reject = true/);
  const stencil = functionBody(bridge, 'static void vk64AuditStencil(');
  assert.match(stencil, /s\.failOp <= 7 && s\.passOp <= 7 && s\.depthFailOp <= 7 && s\.compareOp <= 7/);
  assert.match(stencil, /f\.compareMask != b\.compareMask/);
  assert.match(stencil, /f\.writeMask != b\.writeMask/);
  assert.match(stencil, /f\.reference != b\.reference/);
  assert.equal((bridge.match(/vk64AuditStencil\(\*p, audit\)/g) || []).length, 2);
});

test('resource declarations cover Vulkan descriptor kinds and reject incompatible pipeline interfaces', () => {
  const layout = functionBody(page, 'function layoutEntryFor(decl) {');
  assert.match(layout, /t\.startsWith\('texture_'\)/);
  assert.match(layout, /v2SamplerLayoutType\(t\)/);
  assert.match(layout, /decl\.addr === 'uniform'/);
  assert.match(layout, /decl\.addr\.startsWith\('storage'\)/);
  assert.match(layout, /texture_storage_/);
  const pipeline = functionBody(page, 'async function getPipelineFor(');
  assert.match(pipeline, /prior && JSON\.stringify\(layoutEntryFor\(prior\)\) !== JSON\.stringify\(layoutEntryFor\(decl\)\)/);
  assert.match(pipeline, /noteUnsupported\('incompatible-shared-binding'/);
  assert.match(pipeline, /return fail\('incompatible-shared-binding:/); // routed through fail(): registry + PIPELINE-FAILED log, still returns the named skip
});
