// GUARD (format advertisement): BC formats cannot be advertised until the
// WebGPU adapter feature is negotiated back to Vulkan guests.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const read = (p) => readFileSync(new URL(p, root), 'utf8');
const bridge = read('source/vulkan/vk64bridge.cpp');
const web = read('web/vkwebgpu.mjs');
const manifest = JSON.parse(read('source/vulkan/vk-capabilities.json'));

test('BC image formats are omitted and rejected in both guest ABIs', () => {
  const cppFormats = bridge.match(/kV2SupportedTextureFormats\[\] = \{([^}]+)\}/)?.[1];
  const webFormats = web.match(/V2_SUPPORTED_TEXTURE_FORMATS = Object\.freeze\(\[([^\]]+)\]/)?.[1];
  assert.ok(cppFormats);
  assert.ok(webFormats);
  for (const format of Array.from({length: 16}, (_, i) => 131 + i)) {
    assert.doesNotMatch(cppFormats, new RegExp(`\\b${format}\\b`));
    assert.doesNotMatch(webFormats, new RegExp(`\\b${format}\\b`));
  }
  const createCases = [...bridge.matchAll(/case VK64_fn_vkCreateImage: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(createCases.length, 2);
  for (const body of createCases) {
    assert.match(body, /v2FormatIn\(ci\.format, kV2SupportedTextureFormats\)/);
    assert.match(body, /vk64Unsupported\("image-format"/);
    assert.match(body, /VK_ERROR_FORMAT_NOT_SUPPORTED/);
  }
  assert.ok(manifest.notAdvertised.includes('fillCoreFeatures.textureCompressionBC'));
  assert.doesNotMatch(bridge, /f->textureCompressionBC\s*=\s*VK_TRUE/);
});
