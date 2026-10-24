import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('unsupported texel-buffer, input-attachment and unknown descriptors warn in the one shared write path', () => {
  // Direct updates (both ABIs), templates (both ABIs) and push descriptors all end in applyDescWrite.
  const write = bridge.match(/static void applyDescWrite\([\s\S]*?\n\}\n/)?.[0] ?? '';
  assert.ok(write, 'applyDescWrite exists');
  for (const f of ['texel-buffer-view', 'subpass-input-descriptor', 'descriptor-type'])
    assert.equal((write.match(new RegExp('vk64Unsupported\\("' + f + '"', 'g')) || []).length, 1, f + ' warns in applyDescWrite');
  assert.ok((bridge.match(/vk64Unsupported\("texel-buffer-view"/g) || []).length >= 3,
    'buffer view creation (both ABIs) also warns');
  assert.equal((bridge.match(/applyDescWrite\(/g) || []).length, 4, 'definition + applyWrite64/32 + template; no private write loops');
  assert.equal((bridge.match(/applyDescTemplate\(\*s/g) || []).length, 4, 'update + push template, both ABIs');
});
