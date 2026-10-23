import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));

test('formatted texel buffer views are rejected and format properties do not advertise them', () => {
  const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');
  assert.doesNotMatch(bridge, /flags\s*\|=\s*VK_FORMAT_FEATURE_(?:UNIFORM|STORAGE)_TEXEL_BUFFER/);
  assert.equal((bridge.match(/"texel-buffer-view", "formatted texel-buffer descriptors are not implemented/g) || []).length, 2,
    'both guest ABIs reject buffer-view creation loudly');
  assert.equal((bridge.match(/return VK_ERROR_FORMAT_NOT_SUPPORTED;/g) || []).length >= 2, true);
});
