// Vulkan panic-stub regressions. Guest calls must return a Vulkan result or
// preserve the command semantics; unsupported optional metadata must not panic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const host = readFileSync(new URL('source/vulkan/vk_host.cpp', root), 'utf8');
function body(name) {
  const start = host.indexOf(`void ${name}(CPU* cpu) {`);
  assert.notEqual(start, -1, `missing ${name}`);
  const open = host.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < host.length; i++) {
    if (host[i] === '{') depth++;
    if (host[i] === '}' && --depth === 0) return host.slice(open + 1, i);
  }
  throw new Error(`unclosed ${name}`);
}

const rejectedResults = [
  'vk_GetMemoryHostPointerPropertiesEXT',
  'vk_GetBufferOpaqueCaptureDescriptorDataEXT',
  'vk_GetImageOpaqueCaptureDescriptorDataEXT',
  'vk_GetImageViewOpaqueCaptureDescriptorDataEXT',
  'vk_GetSamplerOpaqueCaptureDescriptorDataEXT',
  'vk_GetAccelerationStructureOpaqueCaptureDescriptorDataEXT',
];
for (const name of rejectedResults) {
  test(`${name} rejects unsupported external/capture data without panicking`, () => {
    const src = body(name);
    assert.doesNotMatch(src, /kpanic\s*\(/);
    assert.match(src, /static\s+bool\s+shown/);
    assert.match(src, /klog\s*\(/);
    assert.match(src, /EAX\s*=\s*\(U32\)VK_ERROR_(?:FEATURE_NOT_PRESENT|INVALID_EXTERNAL_HANDLE)/);
  });
}
for (const name of ['vk_CmdPushDescriptorSetWithTemplate', 'vk_CmdPushDescriptorSetWithTemplateKHR']) {
  test(`${name} marshals template data and forwards the command without panicking`, () => {
    const src = body(name);
    assert.doesNotMatch(src, /kpanic\s*\(/);
    assert.match(src, /calculateUpdateDescriptorSetWithTemplateDataSize/);
    assert.match(src, /lockReadOnlyMemory\(ARG5/);
    assert.match(src, new RegExp(`pvk${name.slice(3)}\\(`));
    assert.match(src, /unlockMemory/);
  });
}
test('vk_CmdSetCheckpointNV treats the checkpoint as optional metadata without panicking', () => {
  const src = body('vk_CmdSetCheckpointNV');
  assert.doesNotMatch(src, /kpanic\s*\(/);
  assert.match(src, /static\s+bool\s+shown/);
  assert.match(src, /klog\s*\(/);
});
test('vk_GetDescriptorSetHostMappingVALVE clears the optional output and does not panic', () => {
  const src = body('vk_GetDescriptorSetHostMappingVALVE');
  assert.doesNotMatch(src, /kpanic\s*\(/);
  assert.match(src, /static\s+bool\s+shown/);
  assert.match(src, /klog\s*\(/);
  assert.match(src, /writeq\(ARG3,\s*0\)/);
});

test('unknown Vulkan pNext structures are logged and skipped instead of panicking', () => {
  for (const name of ['vulkanGetNextPtr', 'vulkanDeleteNextPtr', 'vulkanWriteNextPtr']) {
    const marker = `${name}(`;
    const start = host.indexOf(marker);
    assert.notEqual(start, -1, `missing ${name}`);
    const open = host.indexOf('{', start);
    let depth = 0, end = -1;
    for (let i = open; i < host.length; i++) {
      if (host[i] === '{') depth++;
      if (host[i] === '}' && --depth === 0) { end = i; break; }
    }
    assert.notEqual(end, -1, `unclosed ${name}`);
    const src = host.slice(open + 1, end);
    assert.doesNotMatch(src, /kpanic(?:_fmt)?\s*\(/, `${name} must not panic on unknown pNext`);
    assert.match(src, /klog(?:_fmt)?\s*\(/, `${name} must report skipped unknown pNext`);
  }
});

test('host Vulkan has no not-implemented panic stubs', () => {
  assert.doesNotMatch(host, /kpanic(?:_fmt)?\s*\([^\n]*not implemented/i);
});

// ---- vulkancommon.cpp: map/unmap, template sizing, loader and dispatcher ----
const common = readFileSync(new URL('source/vulkan/vulkancommon.cpp', root), 'utf8');
function commonBody(signature) {
  const start = common.indexOf(signature);
  assert.notEqual(start, -1, `missing ${signature}`);
  const open = common.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < common.length; i++) {
    if (common[i] === '{') depth++;
    if (common[i] === '}' && --depth === 0) return common.slice(open + 1, i);
  }
  throw new Error(`unclosed ${signature}`);
}

test('vulkancommon.cpp contains no kpanic', () => {
  assert.doesNotMatch(common, /kpanic(?:_fmt)?\s*\(/);
});
test('mapVkMemory reports bad requests as 0 (VK_ERROR_MEMORY_MAP_FAILED), logged once', () => {
  const src = commonBody('U32 mapVkMemory(');
  assert.match(src, /static\s+bool\s+shown/);
  assert.match(src, /VK_ERROR_MEMORY_MAP_FAILED/);
  assert.match(src, /return 0;/);
  for (const name of ['vk_MapMemory', 'vk_MapMemory2', 'vk_MapMemory2KHR']) {
    assert.match(body(name), /VK_ERROR_MEMORY_MAP_FAILED/, `${name} must surface the failure`);
  }
});
test('unmapVkMemory ignores unknown memory instead of panicking', () => {
  const src = commonBody('void unmapVkMemory(');
  assert.match(src, /static\s+bool\s+shown/);
  assert.match(src, /return;/);
});
test('template data size covers the last element and tolerates missing create info', () => {
  const src = commonBody('U32 calculateUpdateDescriptorSetWithTemplateDataSize(');
  assert.match(src, /descriptorUpdateTemplateCreateInfo\.find/);
  assert.match(src, /return 0;/);
  assert.match(src, /\(e\.descriptorCount - 1\) \* e\.stride \+ payload/);
});
test('missing host Vulkan loader fails instance creation with VK_ERROR_INITIALIZATION_FAILED', () => {
  assert.match(commonBody('bool initVulkan() {'), /return false;/);
  assert.match(commonBody('void vk_CreateInstance(CPU* cpu)'), /VK_ERROR_INITIALIZATION_FAILED/);
  assert.match(commonBody('void vk_EnumerateInstanceExtensionProperties(CPU* cpu)'), /VK_ERROR_INITIALIZATION_FAILED/);
  assert.match(body('vk_EnumerateInstanceVersion'), /VK_ERROR_INITIALIZATION_FAILED/);
  assert.match(body('vk_EnumerateInstanceLayerProperties'), /VK_ERROR_INITIALIZATION_FAILED/);
});
test('callVulkan returns VK_ERROR_FEATURE_NOT_PRESENT for a missing function and logs it once per id', () => {
  const src = commonBody('void callVulkan(CPU* cpu, U32 index)');
  assert.match(src, /reportMissingVulkanFunction/);
  const reporter = commonBody('static void reportMissingVulkanFunction(');
  assert.match(reporter, /reported\.insert\(index\)\.second/);
  assert.match(reporter, /EAX = \(U32\)-8/);
});

// ---- generated host marshaller: gated off, guarded so enabling it forces review ----
test('the native passthrough marshaller (kpanic-bearing, generated) is compiled by no build target', () => {
  const files = ['project/emscripten/makefile', 'project/linux/makefile'];
  for (const f of files) {
    const text = readFileSync(new URL(f, root), 'utf8');
    assert.doesNotMatch(text, /-DBOXEDWINE_VULKAN(?![0-9A-Za-z_])/, `${f} enables the native passthrough path; its generated kpanics must be removed first`);
  }
});
