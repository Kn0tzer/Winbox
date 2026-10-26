import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridge = readFileSync(resolve(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('secondary command buffers merge all recorded commands and bound state for both ABIs', () => {
  const first = bridge.indexOf('case VK64_fn_vkCmdExecuteCommands:');
  const second = bridge.indexOf('case VK64_fn_vkCmdExecuteCommands:', first + 1);
  const end64 = bridge.indexOf('case VK64_fn_vkCmdDispatch:', second);
  const end32 = bridge.indexOf('case VK64_fn_vkDispatch:', end64);
  const path64 = bridge.slice(first, second);
  const path32 = bridge.slice(second, end64);
  assert.match(path64, /for \(U32 i = 0; i < count; i\+\+\)/);
  assert.doesNotMatch(path64, /count > 16/);
  assert.match(path64, /pushCmd\(primary, c\)/);
  assert.match(path32, /pushCmd\(cb, c\)/);
  assert.match(path32, /cb->nvb = secondary->nvb/);
  assert.match(path32, /cb->pipe = secondary->pipe/);
  assert.ok(end32 > end64 || end32 === -1);
});
