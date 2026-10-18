import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { Worker } from 'node:worker_threads';

test('the guest32 emitter increments the counter visible to another worker', async () => {
  const root = resolve(import.meta.dirname, '../../../..');
  const dir = mkdtempSync(join(tmpdir(), 'bw32-counter-'));
  try {
    const exe = join(dir, 'fixture');
    execFileSync('g++', ['-std=c++20', '-O1', '-DBOXEDWINE_WASM_JIT', '-DBOXEDWINE_MULTI_THREADED',
      '-Iinclude', '-Ilib/simde', 'source/emulation/cpu/wasm/wasmEmitter.cpp',
      'source/emulation/cpu/tests/wasm32_counter_fixture.cpp', '-o', exe], { cwd: root, stdio: 'pipe' });
    const module = new WebAssembly.Module(execFileSync(exe));
    const memory = new WebAssembly.Memory({ initial: 256, maximum: 256, shared: true });
    const counters = new BigUint64Array(memory.buffer);
    Atomics.store(counters, 1, 0xffffffffn);
    await Promise.all([0, 1].map(() => new Promise((resolve, reject) => {
      const worker = new Worker(`
        const { workerData, parentPort } = require('node:worker_threads');
        const instance = new WebAssembly.Instance(workerData.module, { env: { memory: workerData.memory } });
        for (let i = 0; i < 10000; i++) instance.exports.increment(0);
        parentPort.postMessage('done');
      `, { eval: true, workerData: { module, memory } });
      worker.once('error', reject);
      worker.once('exit', code => code === 0 ? resolve() : reject(Error('counter worker exited ' + code)));
    })));
    assert.equal(Atomics.load(counters, 1), 0xffffffffn + 20000n, 'concurrent increments preserve carry and lose no updates');
    assert.equal(Atomics.load(counters, 0), 0n, 'the adjacent CPU field is untouched');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
