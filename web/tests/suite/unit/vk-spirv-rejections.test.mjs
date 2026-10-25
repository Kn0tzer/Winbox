// GUARD (shader capability): a shader the renderer cannot execute must fail at
// VkCreateShaderModule with a greppable reason and a zero output handle.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { rewritePushConstantsWGSL } from '../../../vkwebgpu.mjs';

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
  for (const token of ['spirv-malformed', 'spirv-capability:', 'spirv-capability-unknown:', 'spirv-compute-entry', 'spirv-storage-image', 'spirv-input-attachment', 'spirv-storage-buffer-write']) {
    assert.ok(audit.includes(token), `${token} must be logged`);
  }
  assert.match(audit, /vk64Unsupported\(\("spirv-capability:"/);
  assert.match(audit, /VK_ERROR_INVALID_SHADER_NV/);
  assert.match(audit, /w\[i \+ 3\] == 6\) inputAttachment = true/);
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
