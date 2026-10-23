import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('indirect draw variants preserve command fields across both ABIs and match the advertised cap', () => {
  assert.match(bridge, /L\.maxDrawIndirectCount = VK64_MAX_INDIRECT_DRAWS/);
  assert.match(bridge, /const U32 VK64_MAX_INDIRECT_DRAWS = 1024/);
  for (const field of ['vertexCount', 'instanceCount', 'firstVertex', 'firstInstance']) assert.ok(bridge.includes(`cmd.${field}`), field);
  for (const field of ['indexCount', 'firstIndex', 'vertexOffset']) assert.ok(bridge.includes(`cmd.${field}`), field);
  assert.ok((bridge.match(/drawCount > VK64_MAX_INDIRECT_DRAWS/g) || []).length >= 4,
    'both ABIs bound non-indexed/indexed indirect draw counts to the advertised limit');
  assert.ok((bridge.match(/actualCount > VK64_MAX_INDIRECT_DRAWS/g) || []).length >= 2,
    '64-bit indirect-count variants respect the same limit');
  assert.match(bridge, /if \(actualCount > maxDraws\) actualCount = maxDraws/);
  assert.match(bridge, /if \(drawCount > maxDraws\) drawCount = maxDraws/);
});
