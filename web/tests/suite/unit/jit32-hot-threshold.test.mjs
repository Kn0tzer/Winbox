// web/tests/suite/unit/jit32-hot-threshold.test.mjs
//
// The 32-bit guest WASM JIT counts block entries in firstDynamicOp and starts
// the generator when an op sits exactly at the hot threshold. run() only sends
// an op to that probe while runCount <= threshold, and the direct-dispatch
// chain in normalCPU.cpp only skips the probe for an op strictly past it.
//
// Those three comparisons have to agree. When the probe gate used `<` while the
// trigger used `==`, every block stopped one entry short of the trigger and was
// then chained past the probe: jitbench32 went from 23,002 emitted blocks and
// 139M generated-code entries to 26 and 10.7k, and the JIT lost its entire
// benefit (measured 2026-10-22). These assertions pin the inclusive probe gate
// and the strict chain rule together with the trigger they must serve.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const source = path => readFileSync(join(repoRoot, path), 'utf8');

test('the 32-bit interpreter probe gate, chain rule and generator trigger agree', () => {
  const normalCPU = source('source/emulation/cpu/normal/normalCPU.cpp');
  const jitCodeGen = source('source/emulation/cpu/jit/jitCodeGen.cpp');

  assert.match(normalCPU, /nextOp->runCount <= wasmJit32HotThreshold\(\) \|\| !calculateCF\[0\]/,
    "run() must probe an op sitting exactly at the threshold: that call is where the generator starts");
  assert.match(jitCodeGen, /#ifdef BOXEDWINE_WASM_JIT\s+const U32 hotThreshold = wasmJit32HotThreshold\(\);\s+#else[\s\S]*?const U32 hotThreshold = JIT_RUN_COUNT;\s+#endif/,
    'WASM uses the latched threshold while native configurations use the compiled threshold');
  assert.match(jitCodeGen, /op->runCount == hotThreshold && op->inst != Done/,
    'firstDynamicOp must start the generator when the latched threshold is reached');
  assert.match(normalCPU, /op->runCount > wasmJit32HotThreshold\(\)/,
    'the direct-dispatch chain must only skip the probe for an op strictly past the threshold');
  assert.match(normalCPU, /op->pfn == NormalCPU::getFunctionForOp\(op\)/,
    'the chain must only skip the probe for a plain interpreter op (not a trampoline or compiler entry)');
  assert.match(normalCPU, /!\(op->flags2 & OP_FLAG2_WASM_JIT_PENDING\)/,
    'a compiled block in flight still needs the pending-hit bookkeeping in the probe');
});

test('the decided runCount the JIT writes stays above the latched threshold', () => {
  const runtime = source('source/emulation/cpu/wasm/wasmJitRuntime.cpp');
  assert.match(runtime, /U32 wasmJit32DecidedRunCount\(\) \{/,
    'the decided marker must derive from the same latched threshold');
  assert.match(runtime, /threshold >= 254 \? 0xff : threshold \+ 1/,
    'the decided marker must stay strictly above the threshold and inside U8');
  const jitWasm = source('source/emulation/cpu/wasm/jitWasmCodeGen.cpp');
  assert.equal((jitWasm.match(/wasmJit32DecidedRunCount\(\)/g) || []).length, 3,
    'every ruled-out block path (slot miss, ST failure, MT failure) must write the decided marker');
  assert.equal((jitWasm.match(/pending\.op->runCount = \(U8\)wasmJit32HotThreshold\(\);/g) || []).length, 2,
    'a block waiting for installation sits at the threshold, so run() still reaches the pending probe');
});