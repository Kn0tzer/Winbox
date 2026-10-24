import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const renderer = readFileSync(new URL('web/vkwebgpu.mjs', root), 'utf8');

test('BC compression and BC format properties follow the active WebGPU device feature', () => {
  assert.match(renderer, /globalThis\.bwVkTextureCompressionBC\s*=\s*device\.features\?\.has\('texture-compression-bc'\)\s*===\s*true/);
  assert.match(bridge, /static bool v2WebGpuBcSupported\(\)[\s\S]*?MAIN_THREAD_EM_ASM_INT\([\s\S]*?bwVkTextureCompressionBC/);
  assert.match(bridge, /f->textureCompressionBC\s*=\s*v2WebGpuBcSupported\(\)\s*\?\s*VK_TRUE\s*:\s*VK_FALSE/);
  assert.match(bridge, /static bool v2FormatIsSupportedImage\(U32 format\)\s*\{\s*return v2FormatIsBC\(format\)\s*\?\s*v2WebGpuBcSupported\(\)\s*:\s*v2FormatIn\(format, kV2SupportedTextureFormats\);/);
  assert.match(bridge, /v2FormatFeatureFlags\(U32 format\)\s*\{\s*if \(!v2FormatIsSupportedImage\(format\)\) return 0;/);
});
