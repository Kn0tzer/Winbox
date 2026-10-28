// web/tests/suite/unit/vk-descriptor-group-remap.test.mjs — page-tier
// descriptor-group remap, campaign section 4.25.
//
// Guest Vulkan set numbers are virtual (vkd3d heap-lowering tables,
// bindless sets): uav12's EXEC shaders declare set 5, and the old page
// tier rejected every such pipeline at build time ("group-out-of-range")
// because it only served groups 0-2 plus push group 3. The tier now
// compacts a pipeline's non-push groups into the device's bind-group
// budget, rewrites the WGSL @group annotations to match, and keeps a
// remapped->original map so draw-time set lookups still resolve the
// guest's original set. Over-budget pipelines still fail loudly.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
const utils = readFileSync(new URL('../../../wgsl-utils.mjs', import.meta.url), 'utf8');

test('descriptor groups are remapped into the device budget, not rejected', () => {
  assert.ok(!tier.includes('group-out-of-range'), 'the old group gate must be gone');
  assert.ok(tier.includes('const groupMap = plan.groupMap;'), 'the remap table is recorded');
  assert.ok(tier.includes('planGroupRemap(decls,'), 'the builder plans through the shared helper');
  assert.ok(utils.includes('groupMap[V2_PUSH_GROUP] = V2_PUSH_GROUP;'), 'push group stays put');
  assert.ok(utils.includes('while (next === V2_PUSH_GROUP) next++;'), 'non-push groups skip the push slot');
  assert.ok(tier.includes('code.replace(/@group\\((\\d+)\\)/g'), 'WGSL @group annotations are rewritten to the remapped groups');
  assert.ok(tier.includes('d.sourceGroup = d.group;'), 'decls keep their original set for diagnostics');
});

test('draw-time set lookups use the original set index', () => {
  assert.ok(tier.includes('groupMap,'), 'the pipe record carries the remap');
  const builders = tier.match(/const srcGroup = prec\.groupMap \? \(prec\.groupMap\[group\] \?\? group\) : group;/g) || [];
  assert.equal(builders.length, 2, 'both bind-group builders translate remapped -> original');
  const samp = tier.match(/samplerIdForDecl\(sets, srcGroup, d, st\)/g) || [];
  assert.equal(samp.length, 2, 'both sampler lookups use the translated set');
});

test('over-budget pipelines and push-group collisions still fail loudly', () => {
  assert.ok(tier.includes("noteUnsupported('group-count-limit'"), 'too many groups fails loudly');
  assert.ok(tier.includes("return fail('group-count-limit')"), 'too many groups fails the pipeline');
  assert.ok(tier.includes("noteUnsupported('group-3-non-push'"), 'a non-push declaration at the push group fails loudly');
  assert.ok(tier.includes("return fail('group-3-non-push')"), 'a push-group collision fails the pipeline');
});
