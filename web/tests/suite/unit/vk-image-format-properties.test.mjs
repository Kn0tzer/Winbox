// GUARD (advertised image limits): Vulkan image property queries must match
// the supported format table and the WebGPU tier's per-format sample counts.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

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

test('image properties advertise only supported formats and valid sample counts', () => {
  const counts = body(bridge, 'static VkSampleCountFlags v2FormatSampleCounts(U32 format) {');
  assert.match(counts, /if \(!v2FormatIsSupportedImage\(format\)\) return 0/);
  assert.match(counts, /samples = VK_SAMPLE_COUNT_1_BIT/);
  assert.match(counts, /samples \|= VK_SAMPLE_COUNT_4_BIT/);
  assert.doesNotMatch(counts, /VK_SAMPLE_COUNT_2_BIT|VK_SAMPLE_COUNT_8_BIT/);
  assert.match(counts, /!v2FormatIsBC\(format\) && \(v2FormatIsColorAttachment\(format\) \|\| v2FormatIsDepthStencil\(format\)\)/);
  const features = body(bridge, 'static VkFormatFeatureFlags v2FormatFeatureFlags(U32 format) {');
  const features2 = body(bridge, 'static VkFormatFeatureFlags2 v2FormatFeatureFlags2(U32 format) {');
  for (const flags of [features, features2])
    assert.doesNotMatch(flags, /VK_FORMAT_FEATURE_(?:BLIT_SRC|BLIT_DST|SAMPLED_IMAGE_FILTER_LINEAR)_BIT/,
      'blit and linear-filter blit paths must remain unadvertised until replay supports them');

  const props = body(bridge, 'void fillImageFormatProps(');
  assert.match(props, /VkExtent3D\{8192, 8192, 2048\}/);
  assert.match(props, /maxArrayLayers = 256/);
  assert.match(props, /256ull \* 1024 \* 1024/);

  for (const signature of [
    'case VK64_fn_vkGetPhysicalDeviceImageFormatProperties:',
    'case VK64_fn_vkGetPhysicalDeviceImageFormatProperties2:',
  ]) {
    const start = bridge.indexOf(signature);
    const end = bridge.indexOf('\n    case VK64_fn_', start + signature.length);
    const handler = bridge.slice(start, end);
    assert.match(handler, /v2FormatIsSupportedImage\(/);
    assert.match(handler, /VK_ERROR_FORMAT_NOT_SUPPORTED/);
    assert.match(handler, /fillImageFormatProps\(/);
  }

  const imageCases = [...bridge.matchAll(/case VK64_fn_vkCreateImage: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(imageCases.length, 2);
  for (const handler of imageCases) {
    assert.match(handler, /v2FormatSampleCounts\(ci\.format\)/);
    assert.match(handler, /"image-sample-count"/);
    assert.match(handler, /VK_ERROR_FORMAT_NOT_SUPPORTED/);
  }
});
