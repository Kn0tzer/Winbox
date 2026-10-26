// The vkd3d probe denylists extension entry points that make vkd3d spin when
// stubbed (tools/vkd3d/probe/gen_vk64pe.py). That is only safe if the matching
// extensions are NOT advertised by vkEnumerate{Instance,Device}ExtensionProperties:
// a guest that sees the extension would call the missing function.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const read = (p) => readFileSync(new URL(p, root), 'utf8');
const bridge = read('source/vulkan/vk64bridge.cpp');
const manifest = JSON.parse(read('source/vulkan/vk-capabilities.json'));
const exts = (array) => new Set([...bridge.match(new RegExp(`const char\\* const ${array}\\[\\] = \\{([\\s\\S]*?)\\};`))[1].matchAll(/"(VK_\w+)"/g)].map((m) => m[1]));

// entry point -> the extension(s) that expose it
const OWNERS = [
  [/Descriptor(Set)?(Layout)?(Size|BindingOffset|EXT|BufferEmbedded)|DescriptorBuffer|OpaqueCaptureDescriptorData/, ['VK_EXT_descriptor_buffer', 'VK_EXT_descriptor_heap']],
  [/CooperativeMatrixProperties(KHR)$/, ['VK_KHR_cooperative_matrix']],
  [/CooperativeMatrixProperties(NV)$/, ['VK_NV_cooperative_matrix']],
  [/AccelerationStructureOpaqueCaptureDescriptorData/, ['VK_KHR_acceleration_structure']],
  [/GetPhysicalDeviceDescriptorSizeEXT/, ['VK_EXT_descriptor_heap']],
];

test('every denylisted probe entry point belongs to an extension that is not advertised', () => {
  const py = read('tools/vkd3d/probe/gen_vk64pe.py');
  const list = py.match(/g_denylist\[\] = \{([\s\S]*?)\};/)[1];
  const names = [...list.matchAll(/"(vk\w+)"/g)].map((m) => m[1]);
  assert.ok(names.length >= 10);
  assert.equal(Number(py.match(/g_ndenylist = (\d+)/)[1]), names.length, 'g_ndenylist matches the array');
  const advertised = new Set([...exts('g_dev_exts'), ...exts('g_inst_exts')]);
  for (const fn of names) {
    const owner = OWNERS.find(([re]) => re.test(fn));
    assert.ok(owner, `${fn}: no owning extension recorded in this test`);
    for (const ext of owner[1]) {
      assert.ok(!advertised.has(ext), `${fn} is denylisted but ${ext} is advertised`);
      assert.ok(manifest.notAdvertisedExtensions.includes(ext), `${ext} must be pinned in vk-capabilities.json notAdvertisedExtensions`);
    }
  }
});

test('unimplemented-entry-point log is once per name', () => {
  const log = bridge.match(/static void logAskedFor\([\s\S]*?\n\}/)[0];
  assert.match(log, /seen\.insert/);
  assert.equal((bridge.match(/asked-for \(unimplemented\)/g) || []).length, 1, 'only the rate-limited helper logs it');
  assert.equal((bridge.match(/logAskedFor\("vk(64|32)"/g) || []).length, 2, 'both ABIs use the helper');
});
