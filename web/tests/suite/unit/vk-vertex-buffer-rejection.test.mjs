// A lost Vulkan vertex buffer must cancel its draw; substituting a dummy for a
// bound resource changes guest-visible rendering without reporting the loss.
// Scope: a LOST buffer is a nonzero buffer id whose bytes/hash never arrived.
// A ZERO buffer id is not lost data -- it is a guest VK_NULL_HANDLE bind,
// which the bridge-advertised VK_EXT_robustness2 nullDescriptor feature makes
// spec-legal with attribute reads defined to return zero; that bind is served
// by the zero-filled dummy through the unbound-slot fill, never dropped.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');

const drawCase = () => {
  const start = page.indexOf("case 'draw': {");
  const end = page.indexOf("default: noteUnsupported('unknown-replay-op'", start);
  return page.slice(start, end);
};

test('missing or unallocatable bound vertex buffers stop the draw with diagnostics', () => {
  const draw = drawCase();
  assert.match(draw, /let invalidVertexBuffer = false/);
  assert.match(draw, /draw-vertex-buffer-missing[^\n]*invalidVertexBuffer = true; break/);
  assert.match(draw, /draw-vertex-buffer-gpu-failed[^\n]*invalidVertexBuffer = true; break/);
  assert.match(draw, /if \(invalidVertexBuffer\) \{ noteSkippedDraw\('draw-vertex-buffer-unavailable'\); break; \}/);
  assert.ok(draw.indexOf('if (invalidVertexBuffer)') < draw.indexOf('passEnc.draw('),
    'the guard must run before issuing the non-indexed draw');
  assert.ok(draw.indexOf('if (invalidVertexBuffer)') < draw.indexOf('passEnc.drawIndexed('),
    'the guard must run before issuing the indexed draw');
});

test('VK_NULL_HANDLE vertex binds are zero-filled, never dropped as missing', () => {
  const draw = drawCase();
  // The null-id skip exists, is a bare continue (it must not set the
  // invalid flag), and runs BEFORE the missing-bytes check so a null bind
  // can never reach the loud drop.
  const nullSkip = draw.indexOf("if (vb.buf === '0000000000000000') continue;");
  assert.ok(nullSkip >= 0, 'null vertex binds must be skipped before byte resolution');
  assert.ok(nullSkip < draw.indexOf('draw-vertex-buffer-missing'),
    'the null check must precede the missing-buffer drop');
  // The skipped binding is served by the zero-filled dummy through the
  // unbound-slot fill that follows the bind loop.
  assert.match(draw, /if \(!vbSet\.has\(slot\)\) passEnc\.setVertexBuffer\(slot, dummyVb, 0\)/);
  // The fill must come after the bind loop so null-bound slots stay unset
  // in vbSet and are picked up by it.
  assert.ok(draw.indexOf('if (!vbSet.has(slot))') > draw.indexOf('if (invalidVertexBuffer)'),
    'the dummy fill must follow the bind loop');
});
