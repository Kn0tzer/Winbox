import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('unsupported texel-buffer, input-attachment and unknown descriptors warn in update and template paths', () => {
  assert.ok((bridge.match(/vk64Unsupported\("texel-buffer-view"/g) || []).length >= 5,
    'buffer view create and both ABIs update/direct-template paths warn');
  assert.equal((bridge.match(/vk64Unsupported\("subpass-input-descriptor"/g) || []).length, 4,
    'both ABIs warn for direct and template input-attachment writes');
  assert.equal((bridge.match(/vk64Unsupported\("descriptor-type"/g) || []).length, 4,
    'unknown direct and template descriptor writes warn for both ABIs');
});
