// The bridge's texel-buffer servable predicate (vk64bridge.cpp
// v2TexelBufferServable) and the page's format table (vk-protocol.mjs
// V2_TEXEL_FORMAT) are two hand-maintained copies of one contract: a format
// the bridge accepts but the page cannot materialise would fail at bind time,
// and a format the page supports but the bridge rejects would fail device
// creation. This test pins them to exact set equality, resolving the C++
// enum names through vulkan_core.h so no number is trusted from either side.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { V2_TEXEL_FORMAT } from '../../../vk-protocol.mjs';

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const header = readFileSync(new URL('source/vulkan/vk/vulkan_core.h', root), 'utf8');

test('bridge servable set == page V2_TEXEL_FORMAT set', () => {
  const numByName = new Map();
  for (const m of header.matchAll(/VK_FORMAT_(\w+) = (\d+),/g)) numByName.set(m[1], Number(m[2]));
  const at = bridge.indexOf('static bool v2TexelBufferServable');
  assert.ok(at >= 0);
  const body = bridge.slice(at, bridge.indexOf('\n}\n', at));
  const bridgeSet = new Set();
  for (const m of body.matchAll(/case VK_FORMAT_(\w+):/g)) {
    assert.ok(numByName.has(m[1]), 'unknown format name ' + m[1]);
    bridgeSet.add(numByName.get(m[1]));
  }
  assert.ok(bridgeSet.size >= 40, 'predicate should list the full servable set');
  const pageSet = new Set(Object.keys(V2_TEXEL_FORMAT).map(Number));
  assert.deepEqual([...bridgeSet].sort((a, b) => a - b), [...pageSet].sort((a, b) => a - b));
});
