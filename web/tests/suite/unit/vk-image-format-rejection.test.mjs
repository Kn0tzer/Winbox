// GUARD (unadvertised image formats): zero-feature ETC2/ASTC/unknown formats
// cannot become fake image objects that the WebGPU tier skips later.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const manifest = JSON.parse(readFileSync(new URL('source/vulkan/vk-capabilities.json', root), 'utf8'));

test('unsupported ETC2, ASTC, and unknown images fail creation while BC remains conditionally supported', () => {
  const textureFormats = bridge.match(/kV2SupportedTextureFormats\[\] = \{([^}]+)\}/)?.[1];
  assert.ok(textureFormats);
  for (const format of [147, 148, 149, 150, 151, 157, 158, 159, 184, 0x7fffffff]) {
    assert.doesNotMatch(textureFormats, new RegExp(`\\b${format}\\b`), `format ${format} must not be advertised`);
  }
  assert.match(bridge, /static bool v2FormatIsBC\(U32 format\) \{ return format >= 131 && format <= 146; \}/);
  const handlers = [...bridge.matchAll(/case VK64_fn_vkCreateImage: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(handlers.length, 2);
  for (const handler of handlers) {
    assert.match(handler, /!v2FormatIsSupportedImage\(ci\.format\)/);
    assert.match(handler, /vk64Unsupported\("image-format"/);
    assert.match(handler, /VK_ERROR_FORMAT_NOT_SUPPORTED/);
  }
  assert.ok(manifest.notAdvertised.includes('fillCoreFeatures.textureCompressionETC2'));
  assert.ok(manifest.notAdvertised.includes('fillCoreFeatures.textureCompressionASTC_LDR'));
});
