// web/tests/suite/unit/vk-texel-buffer-binding.test.mjs — V2 storage
// texel buffer views bound as BUFFERS (FD-62, spirvfix R12A).
//
// When a shader's texel-pointer variable is rewritten to a storage
// buffer of texels, its descriptor (wire kind 6, a storage texel
// buffer view) arrives at a binding the shader declares in the
// storage-buffer address space. Before this lane the bind-group code
// resolved only buffer records (kind 1) and inline records (kind 4)
// for such declarations, so the view fell through to a zero dummy
// buffer: the shader's atomic writes would have vanished silently --
// the worst outcome the tier has. The view's buffer range now IS the
// binding (texelBufferBind), created through the content-keyed buffer
// cache and registered for V2_BUFFER_READBACK with its binding batch,
// so the shader's writes reach guest memory like any storage
// buffer's. These tests pin the source-level shape of every hop, in
// the style of vk-storage-texel.test.mjs (the R12S texture shape);
// end-to-end behaviour is covered by the demo-path probes.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const page = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');

function bodyOf(name) {
  const start = page.indexOf('function ' + name);
  assert.ok(start >= 0, name + ' must exist');
  const next = page.indexOf('\n  function ', start + 10);
  return page.slice(start, next < 0 ? undefined : next);
}

test('texelBufferBind: serves the view range as a buffer, honesty-checked and registered', () => {
  const fn = bodyOf('texelBufferBind');
  assert.match(fn, /F\.bufferViews\?\.get\(viewHex\)/, 'the view record is the binding source');
  assert.match(fn, /bytes\.length < viewLen\) return null/, 'bytes that do not cover the whole view refuse');
  assert.match(fn, /const bufId0 = 'texview:' \+ viewHex \+ ':' \+ viewLen;/, 'the cache identity carries the view id and length');
  assert.match(fn, /gpuBufferFor\(bufId0, bytes\.subarray\(0, viewLen\), usage, preHash\)/, 'the buffer comes from the content-keyed cache, seeded with exactly the view bytes');
  assert.match(fn, /storageBufferRegistry\.set\('texview:' \+ viewHex, \{ buffer: gpu, bufId: view\.bufferId, baseOff: BigInt\(view\.absOff\), texelView: viewHex, boundSeq: stexelBatchSeq \}\)/,
    'the registration carries the guest range, the view identity and the binding batch for readback arbitration');
  assert.match(fn, /bufferKey\(bufId0/, 'the returned key is the buffer cache identity, shared with bindGroupEntries');
});

test('bindGroupEntries: a kind-6 record under a storage declaration binds the view buffer, never a dummy', () => {
  const fn = bodyOf('bindGroupEntries');
  const arm = fn.slice(fn.indexOf('else if (b && b.kind === 6)'));
  assert.match(arm, /texelBufferBind\(b\.obj\)/, 'the arm resolves the view as a buffer');
  assert.match(arm, /noteDrop\('bind-group-texel-buffer-unavailable', b\.obj\); return BIND_DROP/,
    'an unservable real view drops the bind group loudly');
  assert.match(arm, /noteDrop\('bind-group-texel-buffer-uniform-decl', b\.obj\); return BIND_DROP/,
    'a view record under a uniform declaration (no rewrite produces one) drops loudly');
  assert.ok(arm.indexOf("b.obj !== '0000000000000000'") >= 0 && arm.indexOf("b.obj !== '0000000000000000'") < arm.indexOf('texelBufferBind(b.obj)'),
    'a VK_NULL_HANDLE view is checked before binding (it falls through to the zero fallback)');
  assert.match(fn, /if \(r === BIND_DROP\) return null/, 'scalar binds honour the drop sentinel');
  assert.match(fn, /if \(r === BIND_DROP\) return null; arr\.push\(r\)/, 'array binds honour the drop sentinel');
  // The texture branch keeps the R12S shape: kind 6 there still
  // materialises a storage texture, untouched by this lane.
  const texArm = fn.slice(fn.indexOf("t.startsWith('texture_storage')"));
  assert.match(texArm, /storageTexelBind\(b\.obj\)/, 'the storage-texture branch still serves kind 6 as a texture');
});

test('bindGroupFingerprint: kind 6 resolves through texelBufferBind so cache-hit binds stay registered', () => {
  const fn = bodyOf('bindGroupFingerprint');
  const arm = fn.slice(fn.indexOf('else if (b && b.kind === 6)'), fn.indexOf('if (!bytes && !preHash)'));
  assert.match(arm, /texelBufferBind\(b\.obj\)/, 'the fingerprint resolves (and thereby registers) the view');
  assert.match(arm, /tb \? tb\.key : 'texbuf-missing:' \+ b\.obj/, 'the key carries the buffer identity or the loud miss marker');
  assert.match(arm, /:texbuf-uniform:' \+ b\.obj/, 'a uniform declaration keys the mismatch instead of resolving it');
});

test('readback: a view bound this batch as a buffer outranks stale sources; split representations decline', () => {
  const at = page.indexOf("const freshPick = pickTexelReadback(brb, true);");
  assert.ok(at >= 0);
  const block = page.slice(at, page.indexOf('const stalePick', at));
  assert.match(block, /cand\.texelView \|\| cand\.boundSeq !== stexelBatchSeq\) continue/,
    'only view registrations bound in this batch count as fresh buffer sources');
  assert.match(block, /noteUnsupported\('buffer-readback-representation-ambiguous'/,
    'a range bound both as a texture and as a buffer this batch declines by name');
  assert.ok(block.indexOf('freshBuf && freshPick') < block.indexOf('serveTexel(freshPick)'),
    'the split-representation decline is checked before the fresh texture is served');
  assert.match(block, /let ent = freshBuf;/, 'the fresh buffer registration feeds the standard buffer copy path');
});
