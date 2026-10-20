// web/tests/suite/unit/probe-slots.test.mjs — Probe slot process coordination.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { EventEmitter } from 'node:events';
import { mkdtemp, mkdir, readdir, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const probeModule = pathToFileURL(join(repoRoot, 'web/tests/probe-slots.mjs')).href;
const slotRoot = '/tmp/winbox-finish/work-PS1';
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

async function freshSlotDir(t) {
  await mkdir(slotRoot, { recursive: true });
  const dir = await mkdtemp(join(slotRoot, 'probe-slots-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  return dir;
}

function startAcquire(dir, { slots = 2, lightSlots = 2, kind = 'heavy', timeoutMs, mode = 'hold', demo = 'unit-probe' } = {}) {
  const source = `
    import { acquireProbeSlot } from ${JSON.stringify(probeModule)};
    const mode = ${JSON.stringify(mode)};
    try {
      const slot = await acquireProbeSlot(${JSON.stringify({ timeoutMs, kind })});
      console.log(JSON.stringify({ event: 'acquired', pid: process.pid, index: slot.index, argv: process.argv.slice(1) }));
      if (mode === 'hold') {
        process.stdin.setEncoding('utf8');
        for await (const chunk of process.stdin) {
          if (chunk.includes('release')) {
            await slot.release();
            console.log(JSON.stringify({ event: 'released' }));
            break;
          }
        }
      } else {
        await slot.release();
      }
    } catch (error) {
      console.log(JSON.stringify({ event: 'error', message: error.message }));
    }
  `;
  const child = spawn(process.execPath, ['--input-type=module', '-e', source], {
    cwd: repoRoot,
    env: {
      ...process.env,
      PROBE_SLOT_DIR: dir,
      PROBE_SLOTS: String(slots),
      PROBE_LIGHT_SLOTS: String(lightSlots),
      PROBE_SLOT_TIMEOUT_MS: '10000',
      PROBE_KIND: kind,
      DEMO: demo,
    },
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  const events = new EventEmitter();
  const records = [];
  let stdoutBuffer = '';
  let stderr = '';
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', chunk => {
    stdoutBuffer += chunk;
    for (;;) {
      const newline = stdoutBuffer.indexOf('\n');
      if (newline < 0) break;
      const line = stdoutBuffer.slice(0, newline);
      stdoutBuffer = stdoutBuffer.slice(newline + 1);
      try {
        const record = JSON.parse(line);
        records.push(record);
        events.emit('event', record);
      } catch {
        events.emit('event', { event: 'invalid-output', line });
      }
    }
  });
  child.stderr.on('data', chunk => { stderr += chunk; });

  function waitFor(predicate, timeoutMs = 10000) {
    const found = records.find(predicate);
    if (found) return Promise.resolve(found);
    const failure = records.find(event => event.event === 'error' && !predicate(event));
    if (failure) return Promise.reject(new Error(`child acquire failed: ${failure.message}`));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        events.off('event', onEvent);
        reject(new Error(`child event timed out; stderr=${stderr}`));
      }, timeoutMs);
      const onEvent = event => {
        if (event.event === 'error' && !predicate(event)) {
          clearTimeout(timer);
          events.off('event', onEvent);
          reject(new Error(`child acquire failed: ${event.message}`));
          return;
        }
        if (!predicate(event)) return;
        clearTimeout(timer);
        events.off('event', onEvent);
        resolve(event);
      };
      events.on('event', onEvent);
    });
  }

  return {
    child,
    records,
    waitFor,
    release: () => { if (child.exitCode === null && child.signalCode === null) child.stdin.write('release\n'); },
    async stop() {
      if (child.exitCode !== null || child.signalCode !== null) return;
      this.release();
      await new Promise(resolve => {
        const timer = setTimeout(() => {
          if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
          resolve();
        }, 5000);
        child.once('close', () => {
          clearTimeout(timer);
          resolve();
        });
      });
    },
  };
}

async function waitForWaiters(dir, kind, count) {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const names = (await readdir(dir)).filter(name => name.startsWith(`wait-${kind}-`));
    if (names.length >= count) return names.sort();
    await delay(25);
  }
  throw new Error(`waited for ${count} ${kind} waiters in ${dir}`);
}

test('two heavy slots admit two probes and the third waits for a release', async t => {
  const dir = await freshSlotDir(t);
  const children = [];
  t.after(async () => Promise.all(children.map(child => child.stop())));
  const first = startAcquire(dir, { slots: 2 }); children.push(first);
  const second = startAcquire(dir, { slots: 2 }); children.push(second);
  const firstAcquired = await first.waitFor(event => event.event === 'acquired');
  const secondAcquired = await second.waitFor(event => event.event === 'acquired');
  assert.deepEqual([firstAcquired.index, secondAcquired.index].sort(), [0, 1]);

  const third = startAcquire(dir, { slots: 2 }); children.push(third);
  await waitForWaiters(dir, 'heavy', 1);
  await delay(250);
  assert.equal(third.records.some(event => event.event === 'acquired'), false);
  first.release();
  assert.equal((await third.waitFor(event => event.event === 'acquired', 5000)).index, firstAcquired.index);
});

test('light slot acquisition proceeds while all heavy slots are held', async t => {
  const dir = await freshSlotDir(t);
  const children = [];
  t.after(async () => Promise.all(children.map(child => child.stop())));
  for (let i = 0; i < 2; i++) {
    const heavy = startAcquire(dir, { slots: 2 });
    children.push(heavy);
    await heavy.waitFor(event => event.event === 'acquired');
  }
  const light = startAcquire(dir, { slots: 2, kind: 'light' }); children.push(light);
  assert.equal((await light.waitFor(event => event.event === 'acquired')).index, 0);
  assert.ok((await readdir(dir)).includes('light-0'));
});

test('waiters acquire in FIFO order within a kind', async t => {
  const dir = await freshSlotDir(t);
  const children = [];
  t.after(async () => Promise.all(children.map(child => child.stop())));
  const holder = startAcquire(dir, { slots: 1 }); children.push(holder);
  await holder.waitFor(event => event.event === 'acquired');
  const first = startAcquire(dir, { slots: 1 }); children.push(first);
  await waitForWaiters(dir, 'heavy', 1);
  await delay(500);
  const second = startAcquire(dir, { slots: 1 }); children.push(second);
  await waitForWaiters(dir, 'heavy', 2);

  holder.release();
  assert.equal((await first.waitFor(event => event.event === 'acquired', 5000)).index, 0);
  assert.equal(second.records.some(event => event.event === 'acquired'), false);
  first.release();
  assert.equal((await second.waitFor(event => event.event === 'acquired', 5000)).index, 0);
});

test('dead pid slot and its sidecar are reclaimed', async t => {
  const dir = await freshSlotDir(t);
  const deadPid = 1_000_000_000;
  await writeFile(join(dir, 'slot-0'), `${deadPid}\n`);
  await writeFile(join(dir, 'info-slot-0.json'), JSON.stringify({ pid: deadPid, kind: 'heavy' }));
  const child = startAcquire(dir, { slots: 1 });
  t.after(() => child.stop());
  const acquired = await child.waitFor(event => event.event === 'acquired');
  assert.ok(acquired.pid > 0);
  assert.equal((await readFile(join(dir, 'slot-0'), 'utf8')), `${acquired.pid}\n`);
  const info = JSON.parse(await readFile(join(dir, 'info-slot-0.json'), 'utf8'));
  assert.equal(info.pid, acquired.pid);
  child.release();
});

test('slot content stays pid-only and sidecar metadata is removed on release', async t => {
  const dir = await freshSlotDir(t);
  const child = startAcquire(dir, { slots: 1 });
  t.after(() => child.stop());
  const acquired = await child.waitFor(event => event.event === 'acquired');
  assert.equal(await readFile(join(dir, 'slot-0'), 'utf8'), `${acquired.pid}\n`);
  const info = JSON.parse(await readFile(join(dir, 'info-slot-0.json'), 'utf8'));
  assert.deepEqual(Object.keys(info).sort(), ['argv', 'cwd', 'demo', 'jit', 'kind', 'pid', 'startedAt'].sort());
  assert.equal(info.pid, acquired.pid);
  assert.equal(info.kind, 'heavy');
  assert.equal(info.cwd, repoRoot);
  assert.deepEqual(info.argv, acquired.argv);
  assert.equal(info.demo, 'unit-probe');
  assert.equal(info.jit, process.env.JIT ?? null);
  assert.ok(Number.isFinite(info.startedAt));

  child.release();
  await child.waitFor(event => event.event === 'released');
  assert.equal((await readdir(dir)).some(name => name === 'slot-0' || name === 'info-slot-0.json'), false);
});

test('timeout keeps the retry-compatible prefix and removes the waiter', async t => {
  const dir = await freshSlotDir(t);
  await writeFile(join(dir, 'slot-0'), `${process.pid}\n`);
  const child = startAcquire(dir, { slots: 1, timeoutMs: 3000, mode: 'release' });
  t.after(() => child.stop());
  const error = await child.waitFor(event => event.event === 'error', 8000);
  assert.match(error.message, /^timed out waiting for a probe slot.*kind=heavy/);
  assert.equal((await readdir(dir)).some(name => name.startsWith('wait-')), false);
});

test('invalid slot kind is rejected', async t => {
  const dir = await freshSlotDir(t);
  const child = startAcquire(dir, { kind: 'medium', mode: 'release' });
  t.after(() => child.stop());
  const error = await child.waitFor(event => event.event === 'error');
  assert.match(error.message, /kind must be 'heavy' or 'light'/);
});
