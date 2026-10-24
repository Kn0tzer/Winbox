// GUARD (unsupported pipeline state): Vulkan must not build an approximation
// when the corresponding feature or state has no WebGPU replay implementation.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
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
  assert.match(bridge, /if \(vk64AuditGraphicsPipeline\(audit\)\) \{[\s\S]{0,180}VK_ERROR_FEATURE_NOT_PRESENT/);
  for (const capability of [
    'fillCoreFeatures.fillModeNonSolid', 'fillCoreFeatures.logicOp', 'fillCoreFeatures.depthBounds',
    'fillCoreFeatures.sampleRateShading', 'fillCoreFeatures.alphaToOne', 'fillCoreFeatures.multiViewport',
  ]) assert.ok(manifest.notAdvertised.includes(capability), `${capability} must remain disabled`);
  for (const extension of ['VK_EXT_mesh_shader']) assert.ok(manifest.notAdvertisedExtensions.includes(extension), `${extension} must remain absent`);
});
