// GUARD (unknown Vulkan call): generated calls lacking bridge handlers must
// produce a greppable feature diagnostic and a Vulkan error in both ABIs.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');

test('unimplemented Vulkan dispatch IDs reject loudly for 32-bit and 64-bit guests', () => {
  for (const [fn, abi] of [['vk64Dispatch', '64'], ['vk32Dispatch', '32']]) {
    const at = bridge.indexOf(`if (fnId == VK64_fn_unimplemented)`, bridge.indexOf(fn));
    assert.notEqual(at, -1, `${abi}-bit unimplemented dispatch branch exists`);
    const body = bridge.slice(at, bridge.indexOf('\n    }', at) + 6);
    assert.match(body, /vk64Unsupported\(/, `${abi}-bit branch logs a feature`);
    assert.match(body, /VK_ERROR_FEATURE_NOT_PRESENT/, `${abi}-bit branch returns a Vulkan error`);
  }
});
