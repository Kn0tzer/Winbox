import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const launcher = readFileSync(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url), 'utf8');
const syscalls = readFileSync(new URL('../../../../source/kernel/syscall64.cpp', import.meta.url), 'utf8');
const contains = (source, pattern, label) => assert.ok(pattern.test(source), label);

test('?guestdiag=N enables periodic thread, RIP, and module-profile diagnostics', () => {
  for (const pattern of [/param\("guestdiag"\)/, /BW64_MEMSTATS_INTERVAL_MS/, /BW_THREAD_DUMP/,
    /BW_GUEST_PROFILE/, /BW64_RIPSAMPLE_HB/]) {
    contains(launcher, pattern, `launcher is missing ${pattern}`);
  }

  const envStart = launcher.indexOf('var guestDiagSeconds = Number(param("guestdiag"));');
  const envEnd = launcher.indexOf('if (param("wildjump")', envStart);
  assert.ok(envStart >= 0 && envEnd > envStart, 'guestdiag environment setup is missing');
  const envBody = launcher.slice(envStart, envEnd);
  const envFor = value => {
    const ENV = {};
    runInNewContext(envBody, { ENV, param: name => name === 'guestdiag' ? value : null });
    return ENV;
  };
  assert.equal(envFor('10').BW64_MEMSTATS_INTERVAL_MS, '10000');
  assert.equal(envFor(null).BW_THREAD_DUMP, undefined);
  assert.equal(envFor('301').BW_GUEST_PROFILE, undefined);

  const runtimeInit = launcher.indexOf('onRuntimeInitialized: function () {');
  const start = launcher.indexOf('var guestDiagSeconds = Number(param("guestdiag"));', runtimeInit);
  const end = launcher.indexOf('// BW64_VKPROF', start);
  assert.ok(runtimeInit >= 0 && start > runtimeInit && end > start, 'runtime guestdiag timer is missing');
  const body = launcher.slice(start, end);
  const run = value => {
    const calls = [];
    const timers = [];
    const state = {
      param: name => name === 'guestdiag' ? value : null,
      liveModule: { ccall: (...args) => { calls.push(args); return '{"totalSamples":0}'; } },
      window: { setInterval: (callback, ms) => timers.push({ callback, ms }) },
      console: { log() {}, warn() {} },
    };
    runInNewContext(body, state);
    return { calls, timers };
  };
  const { calls, timers } = run('10');
  assert.equal(timers.length, 1);
  assert.equal(timers[0].ms, 10000);
  timers[0].callback();
  assert.deepEqual(calls.map(call => call[0]), [
    'bw64_dump_guest_threads', 'bw64_guest_profile_json', 'bw64_guest_profile_reset',
  ]);
  for (const value of [null, '0', '301', 'invalid']) {
    assert.equal(run(value).timers.length, 0, `guestdiag=${value} should remain disabled`);
  }
});

test('MEMSTATS reports per-process top syscall counts for each interval', () => {
  for (const pattern of [/syscallCounts\[256\]\[515\]/, /syscallCounts\[slot\]\[syscallBucket\]\.fetch_add/,
    /MEMSTATS_SYSCALLS/, /BW64_MEMSTATS_INTERVAL_MS/, /top1=/]) {
    contains(syscalls, pattern, `syscall diagnostics are missing ${pattern}`);
  }
});
