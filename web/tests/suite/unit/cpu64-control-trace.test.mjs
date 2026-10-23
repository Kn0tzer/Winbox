import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const launcher = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const cpuHeader = readFileSync(new URL('../../../../include/cpu64.h', import.meta.url), 'utf8');
const cpuSource = readFileSync(new URL('../../../../source/emulation/cpu/cpu64.cpp', import.meta.url), 'utf8');

test('?ctrace=1 enables CPU64 tracing before the initial guest starts', () => {
  const callback = launcher.slice(launcher.indexOf('onRuntimeInitialized: function () {'),
    launcher.indexOf('totalDependencies:', launcher.indexOf('onRuntimeInitialized: function () {')));
  assert.match(callback, /param\("ctrace"\) === "1"/);
  assert.match(callback, /MC\.ccall\("bw64_set_control_trace", null, \["number"\], \[1\]\)/);
  const body = callback.slice(callback.indexOf('{') + 1, callback.lastIndexOf('}'));
  const calls = [];
  const state = {
    runtimeInitialized: false,
    param: name => name === 'ctrace' ? '1' : null,
    liveModule: { ccall: (...args) => calls.push(args) },
    window: {},
    console: { warn() {} },
  };
  runInNewContext(body, state);
  assert.equal(state.runtimeInitialized, true);
  assert.equal(calls[0][0], 'bw64_set_control_trace');
  assert.equal(calls[0][1], null);
  assert.deepEqual(Array.from(calls[0][2]), ['number']);
  assert.deepEqual(Array.from(calls[0][3]), [1]);
});

test('CPU64 holds a 16-branch ring and reports the full stack at bad RIPs', () => {
  assert.match(cpuHeader, /ControlBranch controlBranches\[16\]/);
  assert.match(cpuSource, /controlBranchNext = \(U8\)\(\(controlBranchNext \+ 1\) & 15\)/);
  assert.match(cpuSource, /for \(U32 i = 0; i < 16; i\+\+\) \{\s*const U64 addr = rsp \+ \(U64\)i \* 8;/);
  assert.match(cpuSource, /reportControlTrace\("instruction-fetch-fault", rip, false\)/);
  assert.match(cpuSource, /"unimplemented-opcode", ipStart/);
  assert.match(cpuSource, /controlTrace && !\(jitReady && fastJitDispatch\) && rip != stepRip \+ n/);
});
