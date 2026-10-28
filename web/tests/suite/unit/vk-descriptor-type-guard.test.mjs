import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('unsupported descriptor writes warn in the one shared write path; uniform texel is served', () => {
  // Direct updates (both ABIs), templates (both ABIs) and push descriptors all end in applyDescWrite.
  const write = bridge.match(/static void applyDescWrite\([\s\S]*?\n\}\n/)?.[0] ?? '';
  assert.ok(write, 'applyDescWrite exists');
  for (const f of ['subpass-input-descriptor', 'descriptor-type'])
    assert.equal((write.match(new RegExp('vk64Unsupported\\("' + f + '"', 'g')) || []).length, 1, f + ' warns in applyDescWrite');
  // UNIFORM texel buffers are implemented (wire kind 5) and STORAGE texel
  // buffers are implemented (wire kind 6, metabind lane): both writes record
  // the view and must NOT warn; the texel-buffer-storage rejection is retired.
  const uni = write.slice(write.indexOf('case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:'), write.indexOf('case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:'));
  assert.match(uni, /b\.obj = rdq\(w\.pTexel \+ at\)/);
  assert.ok(!uni.includes('vk64Unsupported'), 'uniform texel writes are served, not warned');
  assert.equal((bridge.match(/vk64Unsupported\("texel-buffer-storage"/g) || []).length, 0, 'storage texel no longer warns anywhere in the bridge');
  // Buffer view creation: both ABIs validate through the one shared helper,
  // whose unservable-format rejection warns once.
  assert.equal((bridge.match(/vk64BufferViewRange\(buf->size/g) || []).length, 2, 'both vkCreateBufferView paths share vk64BufferViewRange');
  assert.equal((bridge.match(/vk64Unsupported\("texel-buffer-view-format"/g) || []).length, 1, 'the format rejection warns once, in the shared helper');
  assert.equal((bridge.match(/applyDescWrite\(/g) || []).length, 4, 'definition + applyWrite64/32 + template; no private write loops');
  assert.equal((bridge.match(/applyDescTemplate\(\*s/g) || []).length, 4, 'update + push template, both ABIs');
});

test('V2 descriptor wire rejects an unmodelled kind loudly before writing kind zero', () => {
  const start = bridge.indexOf('auto emitDescSet =');
  const end = bridge.indexOf('U64 activePipe = 0;', start);
  const emit = bridge.slice(start, end);
  assert.match(emit, /default:\s*vk64Unsupported\("descriptor-wire-kind"/);
  assert.match(emit, /unsupported descriptor type=.*has no V2 binding kind/);
});
