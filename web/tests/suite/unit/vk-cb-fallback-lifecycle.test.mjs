import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../../../', import.meta.url));

// GUARD (crash): the vkQueueSubmit/vkQueueSubmit2 fallback scans live objects
// for command buffers the guest recorded but never submitted. Two defects
// compounded: (1) its type guard was VK64_DROP_IF(continue, ...) — a
// do{}while(0) macro whose loop swallows the continue, so the scan read
// CmdBuf fields in EVERY live object and the D3D9 Pong workload probe read
// garbage command counts in the billions from images/buffers and trapped
// the WASM worker (2026-10-25); (2) among genuine CmdBufs it could not tell
// a mid-record buffer (command vector mutating on a DXVK worker thread)
// from a finished one. The scan now uses an explicit kind guard, and picks
// only buffers the recording lifecycle in source/vulkan/vk_cb_lifecycle.h
// marks eligible: ended at vkEndCommandBuffer, not submitted since.

test('command-buffer lifecycle gates the submit fallback', () => {
  const dir = mkdtempSync(join(tmpdir(), 'vk-cb-lifecycle-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include "source/vulkan/vk_cb_lifecycle.h"
#include <cassert>
using vkcb::CbLifecycle;
int main() {
  // A freshly allocated buffer has nothing to submit.
  { CbLifecycle l; assert(!l.fallbackEligible(true)); assert(!l.fallbackEligible(false)); }
  // THE pin: mid-record buffers are never eligible, however many commands
  // the recording thread has appended so far.
  { CbLifecycle l; l.onBegin(); assert(!l.fallbackEligible(true)); assert(!l.fallbackEligible(false)); }
  // Ended with commands: eligible. Ended empty: nothing to pick up.
  { CbLifecycle l; l.onBegin(); l.onEnd(); assert(l.fallbackEligible(true)); assert(!l.fallbackEligible(false)); }
  // Selected once: after any submit the buffer is consumed until re-recorded.
  { CbLifecycle l; l.onBegin(); l.onEnd(); assert(l.fallbackEligible(true)); l.onSubmitted(); assert(!l.fallbackEligible(true)); l.onSubmitted(); assert(!l.fallbackEligible(true)); }
  // Executed into a primary (vkCmdExecuteCommands consumes the same way).
  { CbLifecycle l; l.onBegin(); l.onEnd(); l.onSubmitted(); assert(!l.fallbackEligible(true)); }
  // Re-record after submit re-arms: begin excludes again, end re-admits.
  { CbLifecycle l; l.onBegin(); l.onEnd(); l.onSubmitted(); l.onBegin(); assert(!l.fallbackEligible(true)); l.onEnd(); assert(l.fallbackEligible(true)); }
  // Reset after end clears eligibility (vkResetCommandBuffer / pool reset).
  { CbLifecycle l; l.onBegin(); l.onEnd(); assert(l.fallbackEligible(true)); l.onReset(); assert(!l.fallbackEligible(true)); }
  // Reset mid-record clears too, and a stray end without a begin (guest
  // error) cannot manufacture eligibility.
  { CbLifecycle l; l.onBegin(); l.onReset(); assert(!l.fallbackEligible(true)); l.onEnd(); assert(!l.fallbackEligible(true)); }
  { CbLifecycle l; l.onEnd(); assert(!l.fallbackEligible(true)); }
  // A consume landing mid-record (no valid submit can do this) must not
  // poison the buffer: the following end still makes it eligible.
  { CbLifecycle l; l.onBegin(); l.onSubmitted(); l.onEnd(); assert(l.fallbackEligible(true)); }
  // A second begin/end cycle after a fallback pickup works end to end.
  { CbLifecycle l; l.onBegin(); l.onEnd(); assert(l.fallbackEligible(true)); l.onSubmitted();
    l.onBegin(); l.onEnd(); assert(l.fallbackEligible(true)); l.onReset(); assert(!l.fallbackEligible(true)); }
}
`);
    execFileSync('c++', ['-std=c++17', '-I', root, join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('vk64bridge wires the lifecycle into recording, execute and submit paths', () => {
  const source = readFileSync(join(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');
  assert.match(source, /#include "vk_cb_lifecycle\.h"/);
  const structAt = source.indexOf('struct CmdBuf : Obj {');
  assert.ok(structAt >= 0);
  assert.match(source.slice(structAt, source.indexOf('\n};\n', structAt)), /vkcb::CbLifecycle lifecycle;/);
  const resetAt = source.indexOf('void resetCmdBuf(CmdBuf* cb) {');
  assert.ok(resetAt >= 0);
  assert.match(source.slice(resetAt, source.indexOf('\n}\n', resetAt)), /cb->lifecycle\.onReset\(\);/);
  // Both ABIs maintain the state on begin/end.
  assert.equal((source.match(/lifecycle\.onBegin\(\)/g) || []).length, 2);
  assert.equal((source.match(/lifecycle\.onEnd\(\)/g) || []).length, 2);
  // Consumption: both 64-bit submits, the 32-bit submit, both execute paths.
  assert.equal((source.match(/lifecycle\.onSubmitted\(\)/g) || []).length, 5);
  // Both fallback scans gate on the lifecycle, never on the raw command list.
  assert.equal((source.match(/lifecycle\.fallbackEligible\(!cb->cmds\.empty\(\)\)/g) || []).length, 2);
  for (const marker of ['vkQueueSubmit(%u batches', 'vkQueueSubmit2(%u batches']) {
    const at = source.indexOf(marker);
    assert.ok(at >= 0, marker);
    const block = source.slice(at, source.indexOf('serializeSubmit(frames);', at));
    assert.match(block, /fallbackEligible/);
    assert.doesNotMatch(block, /if \(!cb->cmds\.empty\(\)\)/);
    // The scan's type guard must be the explicit form: VK64_DROP_IF wraps
    // its action in do{}while(0), whose loop swallows a continue, so the
    // macro form lets the scan read CmdBuf fields in every live object.
    assert.match(block, /if \(!o \|\| o->kind != K_CMDBUF\) continue;/);
    assert.doesNotMatch(block, /VK64_DROP_IF\(continue, !o/);
  }
});
