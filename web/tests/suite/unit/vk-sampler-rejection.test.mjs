// Unsupported sampler modes must not silently create a sampler with changed
// addressing or LOD behavior.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');

function body(source, header) {
  const start = source.indexOf(header);
  assert.notEqual(start, -1, `missing ${header}`);
  const open = source.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}' && --depth === 0) return source.slice(open, i + 1);
  }
  throw new Error(`unbalanced function ${header}`);
}

test('mirror clamp, border addressing, and mip LOD bias reject sampler creation for both ABIs', () => {
  const audit = body(bridge, 'static bool vk64AuditSampler(');
  for (const token of ['mirror-clamp-to-edge', 'sampler-border-colour', 'sampler-mip-lod-bias']) {
    assert.ok(audit.includes(`vk64Unsupported("${token}"`), `${token} has a greppable diagnostic`);
  }
  assert.match(audit, /return reject;/);
  const cases = [...bridge.matchAll(/case VK64_fn_vkCreateSampler: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(cases.length, 2);
  for (const handler of cases) {
    assert.match(handler, /vk64AuditSampler\(/);
    assert.match(handler, /VK_ERROR_FEATURE_NOT_PRESENT/);
    assert.ok(handler.indexOf('vk64AuditSampler(') < handler.indexOf('createObj'), 'reject before creating a sampler');
  }
});
