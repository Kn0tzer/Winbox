// Defense branches in replay must either be reachable for valid commands or
// demonstrably be unreachable under Vulkan command-buffer valid usage.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const page = readFileSync(new URL('../../../../web/vkwebgpu.mjs', import.meta.url), 'utf8');
const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
const protocol = readFileSync(new URL('../../../../web/vk-protocol.mjs', import.meta.url), 'utf8');

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

test('dynamic stencil values must be set for every face used by the pipeline', () => {
  const start = page.indexOf('function v2StencilPipeline(');
  const end = page.indexOf('\n}', start) + 2;
  const stencil = page.slice(start, end);
  assert.match(stencil, /if \(\(index === 0 && pipe\.cull === 1\) \|\| \(index === 1 && pipe\.cull === 2\)\) continue/);
  assert.match(stencil, /if \(dynamic\[kind\]\[index\] === undefined\) throw new Error\('missing dynamic stencil '/);
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

test('valid sampled-image descriptors always carry view and backing-image metadata', () => {
  const emitImage = bridge.slice(bridge.indexOf('auto emitImageDataInner ='), bridge.indexOf('auto emitImageData =', bridge.indexOf('auto emitImageDataInner =')));
  assert.match(emitImage, /ImageView\* view = objOf<ImageView>\(viewId[\s\S]*VK64_DROP_IF\(return, !view \|\| !view->image\)/);
  assert.match(emitImage, /Image\* tex = objOf<Image>\(view->image[\s\S]*VK64_DROP_IF\(return, !tex\)/);
  assert.match(emitImage, /emit\(V2_IMAGE_DATA[\s\S]*p\.u64\(viewId\); p\.u64\(view->image\)/);
  assert.match(bridge, /emitImageData\(b\.obj\);\s*\/\/ b\.obj = image view id/);
  assert.match(protocol, /case V2_REC\.IMAGE_DATA:[\s\S]*frame\.views\.set\(viewId, imageId\);[\s\S]*if \(!frame\.images\.has\(imageId\)\)\s*frame\.images\.set\(imageId, \{ w, h, d, layers, mips, format, hash, pixels:/);
});

test('valid buffer dedup records follow a full payload and retain a persistent page resource', () => {
  const emitBuffer = bridge.slice(bridge.indexOf('auto emitBufferDataInner ='), bridge.indexOf('auto emitBufferData =', bridge.indexOf('auto emitBufferDataInner =')));
  assert.match(emitBuffer, /g_v2SubmitFrames <= 3 \|\| \(g_v2SubmitFrames % V2_BUF_RESEND_PERIOD\) == 0/);
  assert.match(emitBuffer, /p\.u32\(sendBytes \? \(U32\)bytes\.size\(\) : 0\)[\s\S]*if \(sendBytes\) p\.bytes\(bytes\)/);
  assert.match(page, /const persistBufCache = new Map\(\)/);
  assert.match(page, /persistBufByHash\.set\(preHash \+ ':' \+ usage, b\)/);
  assert.match(page, /NOTE: persistTexCache\/persistBufCache\/persistSampCache are NOT cleared/);
});
