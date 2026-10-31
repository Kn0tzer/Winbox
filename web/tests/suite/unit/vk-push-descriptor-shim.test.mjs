// PE-shim push-descriptor coverage. vkd3d-proton's meta clear/copy
// pipelines bind their descriptors with vkCmdPushDescriptorSetKHR; the
// guest resolves it through the bw64 PE shim's vkGetDeviceProcAddr,
// which answers any name missing from exports.txt with a do-nothing
// benign stub. The bridge has implemented the push traps (ABI 914/915)
// since the push-descriptor work, but exports.txt carried only a
// fnId-1 stub for the core name and no KHR entries at all, so every
// meta bind was silently swallowed in the guest and the dispatch ran
// unbound (cs12 BUF/BUF2 bad=64; campaign section 4.31). These tests
// pin the shim side of the path: export table, ABI agreement, the
// built DLLs, and the staging pins.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const ROOT = new URL('../../../../', import.meta.url);
const rd = (p) => readFileSync(new URL(p, ROOT));
const txt = (p) => rd(p).toString('latin1');

function exportEntry(name) {
  for (const line of txt('tools/vkd3d/probe/exports.txt').split('\n')) {
    const parts = line.trim().split(/\s+/);
    if (parts[0] === name) return { fnid: Number(parts[1]), argc: Number(parts[2]) };
  }
  return null;
}

test('exports.txt traps the whole push-descriptor family at the ABI ids', () => {
  // ABI ids from source/vulkan/vk64bridge_abi.h (the bridge trap cases
  // and the unix shim agree on these numbers).
  const abi = txt('source/vulkan/vk64bridge_abi.h');
  assert.match(abi, /VK64_fn_vkCmdPushDescriptorSetKHR = 914/);
  assert.match(abi, /VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR = 915/);
  // (name, fnid, argc): fnId 1 would be the do-nothing unimplemented stub.
  for (const [name, fnid, argc] of [
    ['vkCmdPushDescriptorSetKHR', 914, 6],
    ['vkCmdPushDescriptorSet', 914, 6],
    ['vkCmdPushDescriptorSetWithTemplateKHR', 915, 5],
    ['vkCmdPushDescriptorSetWithTemplate', 915, 5],
  ]) {
    const e = exportEntry(name);
    assert.ok(e, `${name} missing from exports.txt: vkGetDeviceProcAddr would benign-stub it`);
    assert.equal(e.fnid, fnid, `${name} must trap ${fnid}, not the fnId-1 stub`);
    assert.equal(e.argc, argc, `${name} arg count`);
  }
});

test('the built PE shim DLLs export the push family and stay byte-identical', () => {
  const names = ['bw64vulkan.dll', 'vulkan-1.dll', 'winevulkan.dll']
    .map((n) => rd(`tools/vkd3d/probe/${n}`));
  assert.ok(names[0].equals(names[1]) && names[0].equals(names[2]),
    'the three staged shim copies must stay byte-identical (one handle domain)');
  const dll = names[0].toString('latin1');
  // PE export names are NUL-terminated strings in the export table:
  // match them whole. A bare substring check is vacuous for the core
  // names -- 'vkCmdPushDescriptorSet' is a prefix of the KHR name, and
  // the old DLL already contained the core string as part of it, so
  // the check could not fail (frontier review finding 10, metabindfix
  // 4.32). The matcher is pinned against exactly that trap below.
  const hasExport = (text, name) => text.includes(name + '\0');
  for (const name of ['vkCmdPushDescriptorSetKHR', 'vkCmdPushDescriptorSet',
    'vkCmdPushDescriptorSetWithTemplateKHR', 'vkCmdPushDescriptorSetWithTemplate']) {
    assert.ok(hasExport(dll, name), `${name} not exported by the built shim`);
  }
  const khrOnly = 'xxvkCmdPushDescriptorSetKHR\0yy';
  assert.ok(hasExport(khrOnly, 'vkCmdPushDescriptorSetKHR'), 'the matcher finds the KHR export');
  assert.ok(!hasExport(khrOnly, 'vkCmdPushDescriptorSet'),
    'a buffer carrying only the KHR name must not satisfy the core export name');
});

test('the runtime completeness pins match the built shim sha256', () => {
  const sha = createHash('sha256').update(rd('tools/vkd3d/probe/bw64vulkan.dll')).digest('hex');
  const pins = txt('tools/rootfs64/check_runtime_completeness.py');
  for (const name of ['bw64vulkan.dll', 'vulkan-1.dll', 'winevulkan.dll']) {
    assert.ok(pins.includes(`'${name}': '${sha}'`),
      `${name} pin drifted from the built shim (re-pin deliberately)`);
  }
});

test('exports.txt traps the query, event, dynamic-state and pipeline-cache families at the ABI ids', () => {
  // 2026-10-30 (firstframe lane): exports.txt carried the fnId-1
  // unimplemented stub for 23 names whose bridge traps and unix-shim
  // wrappers were implemented all along -- the whole query family (Godot's
  // D3D12 query heaps died at vkCreateQueryPool through the PE shim), the
  // whole event family, three dynamic-state commands, descriptor-set
  // free/layout-support, and the pipeline-cache pair (bridge handlers
  // added the same day; the unix shim answers those two locally).
  const abi = txt('source/vulkan/vk64bridge_abi.h');
  assert.match(abi, /VK64_fn_vkCreateQueryPool = 849|VK64_fn_vkCreateQueryPool,/);
  assert.match(abi, /VK64_fn_vkGetPipelineCacheData = 921/);
  assert.match(abi, /VK64_fn_vkMergePipelineCaches = 922/);
  for (const [name, fnid, argc] of [
    ['vkCreateQueryPool', 849, 4], ['vkDestroyQueryPool', 850, 3],
    ['vkGetQueryPoolResults', 852, 8], ['vkCmdResetQueryPool', 853, 4],
    ['vkCmdBeginQuery', 854, 4], ['vkCmdEndQuery', 855, 3],
    ['vkCreateEvent', 841, 4], ['vkDestroyEvent', 842, 3],
    ['vkSetEvent', 843, 2], ['vkResetEvent', 844, 2], ['vkGetEventStatus', 845, 2],
    ['vkCmdSetEvent', 846, 3], ['vkCmdResetEvent', 847, 3], ['vkCmdWaitEvents', 848, 11],
    ['vkCmdSetStencilReference', 903, 3], ['vkCmdSetDepthBias', 905, 4],
    ['vkCmdWriteTimestamp', 910, 4], ['vkCmdSetDepthBounds', 911, 3],
    ['vkFreeDescriptorSets', 913, 4], ['vkGetDescriptorSetLayoutSupport', 916, 3],
    ['vkCmdNextSubpass', 920, 2],
    ['vkGetPipelineCacheData', 921, 4], ['vkMergePipelineCaches', 922, 4],
  ]) {
    const e = exportEntry(name);
    assert.ok(e, `${name} missing from exports.txt: vkGetDeviceProcAddr would benign-stub it`);
    assert.equal(e.fnid, fnid, `${name} must trap ${fnid}, not the fnId-1 stub`);
    assert.equal(e.argc, argc, `${name} arg count`);
  }
});
