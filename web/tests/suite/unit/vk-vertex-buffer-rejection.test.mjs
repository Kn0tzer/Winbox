// A lost Vulkan vertex buffer must cancel its draw; substituting a dummy for a
// bound resource changes guest-visible rendering without reporting the loss.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');

test('missing or unallocatable bound vertex buffers stop the draw with diagnostics', () => {
  const start = page.indexOf("case 'draw': {");
  const end = page.indexOf("default: noteUnsupported('unknown-replay-op'", start);
  const draw = page.slice(start, end);
  assert.match(draw, /let invalidVertexBuffer = false/);
  assert.match(draw, /draw-vertex-buffer-missing[^\n]*invalidVertexBuffer = true; break/);
  assert.match(draw, /draw-vertex-buffer-gpu-failed[^\n]*invalidVertexBuffer = true; break/);
  assert.match(draw, /if \(invalidVertexBuffer\) \{ noteSkippedDraw\('draw-vertex-buffer-unavailable'\); break; \}/);
  assert.ok(draw.indexOf('if (invalidVertexBuffer)') < draw.indexOf('passEnc.draw('),
    'the guard must run before issuing the non-indexed draw');
  assert.ok(draw.indexOf('if (invalidVertexBuffer)') < draw.indexOf('passEnc.drawIndexed('),
    'the guard must run before issuing the indexed draw');
});
