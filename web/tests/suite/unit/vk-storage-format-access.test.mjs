// web/tests/suite/unit/vk-storage-format-access.test.mjs — page-tier
// storage-format access rejection, campaign section 4.26.
//
// Dawn reports a format it will not serve as a storage texture at
// bind-group-layout creation. The read-write upgrade was PROVEN USELESS
// for r16float by micro-probe (Dawn rejects read-only AND read-write
// there), so the page tier does not rewrite the access: it names the
// rejection and fails the pipeline loudly. The serving path for
// read-path storage in such formats is a sampled-texture rewrite,
// queued separately. The live behavioral proof is the uav12 R16_FLOAT
// probe case failing with the named reason and no "uncaptured" warning
// for the rejection; these guards pin the parsing and the wiring.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseStorageTextureAccessRejection } from '../../../wgsl-utils.mjs';

test('Dawn storage-access rejections parse to format and access', () => {
  assert.deepEqual(
    parseStorageTextureAccessRejection('Texture format TextureFormat::R16Float does not support storage texture access StorageTextureAccess::ReadOnly.'),
    { format: 'R16Float', access: 'ReadOnly' });
  assert.deepEqual(
    parseStorageTextureAccessRejection('Texture format TextureFormat::R16Float does not support storage texture access StorageTextureAccess::ReadWrite.'),
    { format: 'R16Float', access: 'ReadWrite' });
  assert.deepEqual(
    parseStorageTextureAccessRejection('On entries[0]: binding number (4096) exceeds the maxBindingsPerBindGroup limit (1000).'),
    null);
  assert.equal(parseStorageTextureAccessRejection(''), null);
  assert.equal(parseStorageTextureAccessRejection(null), null);
});

test('the pipeline builder scopes layout creation and names the rejection', () => {
  const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.ok(!tier.includes('upgradeReadonlyStorageAccess'), 'the refuted read-write upgrade must not come back');
  const scopeAt = tier.indexOf("device.pushErrorScope('validation');");
  const layoutsAt = tier.indexOf('const layouts = [];', scopeAt);
  assert.ok(scopeAt !== -1 && layoutsAt !== -1 && layoutsAt > scopeAt, 'the validation scope must open before layout creation so the rejection is captured, not uncaptured');
  assert.ok(tier.includes('const accessRejection = err ? parseStorageTextureAccessRejection(err.message) : null;'), 'the popped error is checked for a storage-access rejection');
  assert.ok(tier.includes("return fail('storage-format-access:' + accessRejection.format + ':' + accessRejection.access + where);"), 'the rejection fails loudly with a named reason');
  assert.ok(tier.includes("noteUnsupported('storage-format-access', accessRejection.format + ':' + accessRejection.access);"), 'the rejection is counted, not just returned');
});
