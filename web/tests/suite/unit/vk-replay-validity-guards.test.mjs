// Defense branches in replay must either be reachable for valid commands or
// demonstrably be unreachable under Vulkan command-buffer valid usage.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');

test('render replay skip flag is never set, and draw prerequisites are checked', () => {
  const passAt = page.indexOf('let skipped = null;');
  const switchAt = page.indexOf("case 'draw': {", passAt);
  const drawEnd = page.indexOf("default: noteUnsupported('unknown-replay-op'", switchAt);
  const frame = page.slice(passAt, drawEnd);
  assert.equal((frame.match(/\bskipped\s*=/g) || []).length, 1,
    'draw-pass-skipped has no runtime producer and is only a defensive branch');
  assert.match(frame, /if \(!passEnc \|\| !st\.pipeDesc\)[\s\S]*draw-no-encoder[\s\S]*draw-no-pipeline-bound/,
    'Vulkan draw requires an active render pass and bound graphics pipeline');
  assert.match(frame, /if \(skipped\) \{ noteSkippedDraw\('draw-pass-skipped'/);
});

test('strip-index pipeline retry uses the exact index type already used for selection', () => {
  const start = page.indexOf("case 'draw': {");
  const end = page.indexOf("default: noteUnsupported('unknown-replay-op'", start);
  const draw = page.slice(start, end);
  const calls = [...draw.matchAll(/getPipelineFor\(drawPipe,passSamples,st\.pipeNeg,!!depthView,st\.ibind\?\.indexType\|\|0/g)];
  assert.equal(calls.length, 1, 'initial pipeline lookup uses the current Vulkan index type');
  assert.match(draw, /st\.pipeRec\.stripIndexType !== \(st\.ibind\?\.indexType \|\| 0\)[\s\S]*getPipelineFor\(drawPipe, passSamples, st\.pipeNeg, \!\!depthView, st\.ibind\?\.indexType \|\| 0/,
    'the later lookup is defensive and cannot differ without an intervening index-bind command');
});
