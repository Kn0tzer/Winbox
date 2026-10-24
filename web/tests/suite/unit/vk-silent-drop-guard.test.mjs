// Source guard: new Vulkan dispatch/replay drop paths must remain observable.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');
const replay = readFileSync(new URL('web/vkwebgpu.mjs', root), 'utf8');

function body(src, anchor) {
  const at = src.indexOf(anchor);
  assert.ok(at >= 0, `missing ${anchor}`);
  const open = src.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === '{') depth++;
    if (src[i] === '}' && --depth === 0) return src.slice(open, i + 1);
  }
  throw new Error(`unclosed ${anchor}`);
}

test('bridge drop guards always log before discarding work', () => {
  assert.match(bridge, /static void vk64DropLog\([\s\S]*?\n\}/);
  assert.match(bridge, /#define VK64_DROP_IF\(action, \.\.\.\) do \{ if \(__VA_ARGS__\) \{ vk64DropLog\(#__VA_ARGS__, __LINE__\); action; \} \} while \(0\)/);
  assert.ok((bridge.match(/VK64_DROP_IF\(/g) || []).length > 100, 'bridge drop sites must use the auditable macro');
});

test('render submit command default paths report unsupported commands', () => {
  const commandSwitch = body(bridge.slice(bridge.lastIndexOf('switch (k.kind)')), 'switch (k.kind)');
  assert.match(commandSwitch, /case CMD_DISPATCH:[\s\S]*?vk64Unsupported\("compute-dispatch"/);
  assert.match(commandSwitch, /default:[\s\S]*?vk64Unsupported\("unknown-cmd-kind"/);
});

test('page replay counts and logs unsupported operations and skipped draws', () => {
  assert.match(replay, /function noteDrop\(reason, detail = ''\)[\s\S]*?bwLog\(1, 'vulkan', 'v2 dropped '/);
  assert.match(replay, /const noteUnsupported = \(reason, detail\) => \{ stats\.unsupported\+\+; return noteDrop\(reason, detail\); \}/);
  assert.match(replay, /const noteSkippedDraw = \(reason, detail\) => \{ stats\.skippedDraws[\s\S]*?return noteDrop\(reason, detail\); \}/);
  assert.match(replay, /default: noteUnsupported\('unknown-replay-op'/);
});

test('Vulkan transport hook installation does not swallow assignment failures', () => {
  assert.match(bridge, /MAIN_THREAD_EM_ASM\(\{\s*globalThis\.bwVkHeapBuffer = HEAPU8\.buffer;\s*\}\)/);
  assert.match(replay, /globalThis\.bwVkChunk = acceptChunk;/);
  assert.doesNotMatch(bridge, /try\s*\{\s*globalThis\.bwVkHeapBuffer[\s\S]{0,120}?catch\s*\([^)]*\)\s*\{\s*\}/);
  assert.doesNotMatch(replay, /try\s*\{\s*globalThis\.bwVkChunk[\s\S]{0,120}?catch\s*\([^)]*\)\s*\{\s*\}/);
});

test('chunk-handler and readback failures keep rate-limited diagnostics', () => {
  assert.match(bridge, /catch \(e\) \{\s*globalThis\.__bwVkChunkErrors = \(globalThis\.__bwVkChunkErrors \|\| 0\) \+ 1;[\s\S]*?console\.warn\('vk64: UNSUPPORTED v2-chunk-handler-threw x'/);
  assert.match(replay, /catch \(e\) \{ noteUnsupported\('readback-failed'/);
  assert.match(replay, /catch \(e\) \{ noteUnsupported\('readback-notify-failed'/);
  assert.match(replay, /if \(buffer\) buffer\.destroy\(\);\s*\}\s*catch \(e\) \{\}/);
});
