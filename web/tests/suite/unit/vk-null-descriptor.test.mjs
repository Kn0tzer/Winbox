// Null descriptors (VK_EXT_robustness2 nullDescriptor): the descriptor half
// of the advertised feature, audited end to end (nulldesc lane, 2026-10-25).
// The vertex-buffer half is pinned by vk-vertex-buffer-rejection.test.mjs.
//
// The bridge advertises nullDescriptor = VK_TRUE (a HARD requirement --
// DXVK refuses to run without it, dxvk_adapter.cpp CHECK_FEATURE_NEED) and
// DXVK writes VK_NULL_HANDLE descriptors by design for every shader slot
// the app never bound (dxvk_context.cpp updateDescriptorResources: null
// image view, null texel buffer view, null buffer with range WHOLE_SIZE).
// A captured Pong stream shows null image views (x9) and null uniform
// buffers (x2) in real traffic. Under the feature, reads from a null
// descriptor return zero and writes are discarded -- the tiers must SERVE
// that, not drop or misread. Verdicts per descriptor type:
//
//   UNIFORM/STORAGE_BUFFER (+_DYNAMIC)  zero buffer serves reads; writes
//                                       land in the page-owned zero buffer
//                                       and are discarded.        SERVED
//   SAMPLED_IMAGE (null view)           zero-filled blank texture. SERVED
//   COMBINED_IMAGE_SAMPLER (null view)  zero-filled blank texture. SERVED
//   COMBINED (null sampler, real view)  texture blanked: sampling via a
//                                       null sampler returns zero. SERVED
//   SAMPLER (null sampler)              default-sampler substitution, the
//                                       same serve as an unwritten sampler
//                                       binding; DXVK never emits it (it
//                                       substitutes a dummy sampler), and
//                                       the WGSL separated texture/sampler
//                                       model gives the page tier no pairing
//                                       to zero against.      SUBSTITUTED
//   STORAGE_IMAGE (null view)           the texture_storage path binds the
//                                       blank texture unconditionally, so
//                                       writes are discarded and reads are
//                                       zero like the class stub.  SERVED
//   UNIFORM_TEXEL_BUFFER (null view)    zero texel texture (was: the whole
//                                       bind group dropped the draw). SERVED
//   STORAGE_TEXEL_BUFFER                rejected loudly as a class at write
//                                       time (texel-buffer-storage); null
//                                       adds nothing.     CLASS-REJECTED
//   INPUT_ATTACHMENT                    rejected loudly as a class at write
//                                       time (subpass-input-descriptor).
//                                                         CLASS-REJECTED
//   INLINE_UNIFORM_BLOCK                carries bytes, no handle.  N/A
//
// A NONZERO handle that names no live object is a genuinely lost resource
// and every loud path for it below must stay loud.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');
const caps = readFileSync(new URL('../../../../source/vulkan/vk-capabilities.json', import.meta.url), 'utf8');

const applyWrite = bridge.match(/static void applyDescWrite\([\s\S]*?\n\}\n/)?.[0] ?? '';
const emitSet = (() => {
  const start = bridge.indexOf('auto emitDescSet =');
  return bridge.slice(start, bridge.indexOf('U64 activePipe = 0;', start));
})();
const entries = (() => {
  const start = page.indexOf('function bindGroupEntries');
  return page.slice(start, page.indexOf('async function render', start));
})();
const fingerprint = (() => {
  const start = page.indexOf('function bindGroupFingerprint');
  return page.slice(start, page.indexOf('function bindGroupEntries', start));
})();

// The census: every descriptor type the shared write path handles. A new
// case in applyDescWrite (or a removed one) fails here until the type has
// a null-handle disposition in the table above and the pins below.
const CENSUS = {
  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: 'zero-served',
  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: 'zero-served',
  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: 'zero-served',
  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: 'zero-served',
  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: 'zero-served',
  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: 'zero-served',
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: 'zero-served',
  VK_DESCRIPTOR_TYPE_SAMPLER: 'substituted',
  VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: 'zero-served',
  VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: 'class-rejected',
  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: 'class-rejected',
  VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: 'not-a-handle',
};

test('census: applyDescWrite handles exactly the audited descriptor types', () => {
  assert.ok(applyWrite, 'applyDescWrite exists');
  const found = new Set([...applyWrite.matchAll(/VK_DESCRIPTOR_TYPE_[A-Z_]+/g)].map(m => m[0]));
  assert.deepEqual([...found].sort(), Object.keys(CENSUS).sort(),
    'a descriptor type entered or left the write path without a null-handle disposition');
});

test('the advertisement this audit holds the tiers to is real', () => {
  assert.match(bridge, /f\.nullDescriptor = VK_TRUE;/);
  assert.match(bridge, /"VK_EXT_robustness2",\s*\/\/ HARD requirement/);
  assert.match(caps, /"fillRobustness2\.nullDescriptor": \{\s*"status": "implemented"/);
});

test('null uniform texel view: page tier serves a zero texel texture, lost views still drop', () => {
  const zeroAt = entries.indexOf("if (b.obj === '0000000000000000') { views.push(zeroTexelTexture(t).createView()); continue; }");
  assert.ok(zeroAt >= 0, 'null texel view must be served from the zero texel texture');
  const dropAt = entries.indexOf("noteDrop('bind-group-texel-unavailable', b.obj); return null;");
  assert.ok(dropAt > zeroAt, 'the lost-view loud drop must remain, after the null serve');
  assert.match(page, /const zeroTexelTexture = t => \{/);
  assert.match(page, /elem === 'u32' \? 'rgba32uint' : elem === 'i32' \? 'rgba32sint' : 'rgba8unorm'/,
    'the zero texel format follows the decl element type so the sample type matches');
  assert.match(page, /new Uint8Array\(V2_TEXEL_WRAP \* bpp\)/, 'the zero texel texture is zero-filled');
});

test('null uniform texel view: the bridge does not log it as a missing view', () => {
  assert.match(emitSet, /if \(b\.obj\) vk64Unsupported\("texel-buffer-view-missing"/,
    'the missing-view log must be gated on a nonzero id');
  assert.equal((emitSet.match(/texel-buffer-view-missing/g) || []).length, 1,
    'the loud path for stale nonzero views stays, exactly once');
});

test('combined binding with a null sampler blanks the texture in entries AND fingerprint', () => {
  assert.match(page, /function nullSamplerCombined\(b\) \{\s*return !!b && b\.kind === 2 && b\.type === 1 && b\.range === '0000000000000000';\s*\}/);
  assert.match(entries, /let texture = nullSamplerCombined\(b\) \? blankTex\(/,
    'bind-group construction must blank the texture for a null-sampler combined binding');
  assert.match(fingerprint, /if \(nullSamplerCombined\(b\)\) \{ parts\.push\('t' \+ d\.binding \+ ':' \+ k \+ ':nullsamp'\); continue; \}/,
    'the fingerprint must key the blanked resolution, or the persistent cache collides with the real-sampler group');
});

test('null buffer descriptors reach the zero-buffer serve; null image views reach the blank texture', () => {
  // Uniform/storage branch: a binding whose bytes never resolved (a null
  // buffer emits no BUFFER_DATA at all) substitutes the zero buffer.
  const uniformBranch = entries.slice(entries.indexOf("} else if (d.addr === 'uniform'"));
  assert.match(uniformBranch, /if \(!bytes && !preHash\) \{ bytes = ZERO4096;/,
    'null/unresolved buffer bindings must be served the zero buffer');
  // Texture branch: a view with no image record resolves to the blank
  // (zero-filled) texture rather than dropping.
  assert.match(entries, /img \? gpuTextureFor\(imgHex, img, cube, dimension\) : blankTex\(/,
    'null image views must resolve to the blank texture');
  assert.match(entries, /noteDrop\('bind-group-texture-unavailable', imgHex\); return null;/,
    'a real texture that fails to materialise still drops loudly');
});

test('null sampler-only binding takes the deliberate default-sampler substitution', () => {
  assert.match(entries, /resource: s \? gpuSamplerFor\(sHex, s\) : device\.createSampler\(/,
    'a missing/null sampler binding substitutes the default sampler (DXVK never emits a null sampler: it substitutes a dummy handle)');
});
