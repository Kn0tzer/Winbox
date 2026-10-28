// web/tests/suite/unit/probe-guest-death.test.mjs — guest-death detection
// + METRICS present extraction for Playwright probes.
//
// Fixtures below are verbatim excerpts from the 2026-10-27 boundary window:
//  - Doom A (shipped build): session teardown at ~162s, then persist-only
//    traffic until the 300s mapping timeout. The probe must call GUEST-DIED
//    instead of waiting out the timeout.
//  - Gang Beasts: services.exe exits at 116-128s while the game lives on
//    (crash-handler + file activity follow) — daemon exit alone must NOT
//    fire. Only the final silence after the last guest line is death.
//  - Doom B: wasm worker RuntimeError (memory access out of bounds) —
//    immediate death, no silence wait.
// Logic under test: web/tests/probe-guest-death.mjs (pure, no Playwright).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { appendFileSync, readFileSync, writeFileSync, unlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  createGuestDeathWatch, extractMetricsPresent, isGuestActivityLine,
  isTeardownLine, isWorkerDeathText, SILENCE_AFTER_TEARDOWN_MS,
} from '../../probe-guest-death.mjs';

const L = (t, text, type = 'log') => ({ t, type, text });

// --- Doom A: teardown + persist-only tail ---------------------------------
const DOOM_A_TAIL = [
  L(156700, 'CPU64: exit_group trace tid=39 rip=0x7000d4409 rsp=0x7fffffffd7f8 ra=[0x70003e522,0x1010103010082008,0x1810202808083000,0x2028103020202020]'),
  L(156701, 'CPU64: exit_group rip=0x7000d4409 -> imm32.dll+0x36c9a4409'),
  L(161913, "CPU64: exit syscall, status=0  pid=22 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64C:\\\\windows\\\\system32\\\\services.exe'"),
  L(169517, 'persist: synced 1 changed, 0 removed'),
  L(174580, 'persist: synced 1 changed, 0 removed'),
  L(179572, 'persist: synced 1 changed, 0 removed'),
  L(184424, 'persist: synced 1 changed, 0 removed'),
  L(189488, 'persist: synced 1 changed, 1 removed'),
];

test('Doom A: teardown + persist-only tail becomes GUEST-DIED after the silence window', () => {
  const w = createGuestDeathWatch();
  for (const line of DOOM_A_TAIL) w.feed(line);
  // 10s after the last line: persist traffic must not count as life, but the
  // silence window has not elapsed yet.
  assert.equal(w.check(189488 + 10000), null);
  const death = w.check(189488 + SILENCE_AFTER_TEARDOWN_MS);
  assert.ok(death, 'expected a death verdict');
  assert.equal(death.verdict, 'GUEST-DIED');
  assert.equal(death.kind, 'teardown-silence');
  assert.match(death.signature, /services\.exe/);
});

test('persist heartbeats are not guest activity; loader and metrics lines are', () => {
  assert.equal(isGuestActivityLine('persist: synced 1 changed, 0 removed'), false);
  assert.equal(isGuestActivityLine('FsZip: 384 entry streams opened'), true);
  assert.equal(isGuestActivityLine('bw64: METRICS {"v":1}'), true);
  assert.equal(isGuestActivityLine(''), false);
  assert.equal(isGuestActivityLine(null), false);
});

// --- Gang Beasts: daemon exit is not death while the game lives on -------
const GB_HEALTHY_SPAN = [
  L(116600, "CPU64: exit_group syscall, status=0  pid=25 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64C:\\windows\\system32\\services.exe'"),
  L(128400, "CPU64: exit syscall, status=0  pid=22 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64C:\\windows\\system32\\services.exe'"),
  L(259300, "CPU64: exit_group syscall, status=0  pid=38 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64Z:\\home\\username\\apps\\UnityCrashHandler64.exe--attach32563351552'"),
  L(300000, 'FsZip: 384 entry streams opened, 223 MB decompressed'),
  L(400000, 'XWire SDL: presented'),
];

test('Gang Beasts: services.exe exits early do not fire while guest activity continues', () => {
  const w = createGuestDeathWatch();
  for (const line of GB_HEALTHY_SPAN) w.feed(line);
  assert.equal(w.check(400000 + 10000), null);
  assert.equal(w.state().teardownAtMs, 116600);
});

test('verdict cites the final teardown, not the first daemon exit', () => {
  const w = createGuestDeathWatch();
  w.feed(L(41000, "CPU64: exit_group syscall, status=0  pid=25 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64C:\\windows\\system32\\services.exe'"));
  w.feed(L(100000, 'FsZip: 384 entry streams opened'));
  w.feed(L(169081, "CPU64: exit syscall, status=0  pid=38 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64C:\\windows\\system32\\explorer.exe/desktop'"));
  w.feed(L(174221, "CPU64: exit syscall, status=0  pid=22 exe='/usr/lib/wine/wine64' cmd='/usr/lib/wine/wine64C:\\windows\\system32\\services.exe'"));
  const death = w.check(174221 + SILENCE_AFTER_TEARDOWN_MS);
  assert.ok(death);
  assert.equal(death.kind, 'teardown-silence');
  assert.match(death.signature, /services\.exe/);
  assert.equal(death.atMs, 174221);
  assert.equal(w.state().teardownAtMs, 41000);
});

test('Gang Beasts: final silence after the last guest line is death', () => {
  const w = createGuestDeathWatch();
  for (const line of GB_HEALTHY_SPAN) w.feed(line);
  w.feed(L(621800, "CPU64: exit_group rip=0x7000d4409 -> imm32.dll+0x36c9a4409"));
  const death = w.check(621800 + SILENCE_AFTER_TEARDOWN_MS);
  assert.ok(death);
  assert.equal(death.kind, 'teardown-silence');
});

// --- Doom B: worker death is immediate ------------------------------------
const DOOM_B_ERRORS = [
  L(345888, 'worker sent an error! http://127.0.0.1:40901/runtime/boxedwine64.js?v=7256fd257456:1: Uncaught RuntimeError: memory access out of bounds', 'error'),
  L(345890, 'wine64-launcher window.onerror: Uncaught [object ErrorEvent] @ http://127.0.0.1:40901/runtime/boxedwine64.js?v=7256fd257456:1:15105', 'error'),
];

test('Doom B: worker RuntimeError is immediate GUEST-DIED, no silence wait', () => {
  const w = createGuestDeathWatch();
  w.feed(L(345836, 'bw64: METRICS {"v":1,"present":{"vk":0,"vk32":0,"sdl":1,"count":1,"path":"sdl"}}'));
  for (const line of DOOM_B_ERRORS) w.feed(line);
  const death = w.check(345890);
  assert.ok(death);
  assert.equal(death.kind, 'worker');
  assert.match(death.signature, /memory access out of bounds/);
});

test('pageerror text classifies worker death; trace lines do not count as teardown', () => {
  assert.equal(isWorkerDeathText('memory access out of bounds'), true);
  assert.equal(isWorkerDeathText('worker sent an error!'), true);
  assert.equal(isWorkerDeathText('plain log line'), false);
  assert.equal(isTeardownLine("CPU64: exit syscall, status=0  pid=22 exe='/usr/lib/wine/wine64' cmd='services.exe'"), true);
  assert.equal(isTeardownLine('CPU64: exit_group trace tid=39 rip=0x7000d4409'), false);
  const w = createGuestDeathWatch();
  w.feedPageError('memory access out of bounds', 345890);
  assert.equal(w.check(345890).kind, 'worker');
  const w2 = createGuestDeathWatch();
  w2.feedPageError('some unrelated page error', 1000);
  assert.equal(w2.check(1000), null);
});

// --- METRICS present extraction (truncation-proof) ------------------------
const FULL_METRICS = 'bw64: METRICS {"v":1,"uptimeMs":1793136406248,"winMs":501.0,"arch":"x86-64","enabled":true,"present":{"vk":0,"vk32":0,"sdl":1,"count":1,"fps":2.0,"frameMs":500.95,"path":"sdl"},"vk":{"supported":true,"frames":0}}';
// The play-phase form: same line cut at ~832 payload chars (after present).
const TRUNCATED_METRICS = FULL_METRICS.slice(0, 200);
const CUT_INSIDE_PRESENT = 'bw64: METRICS {"v":1,"present":{"vk":0,"vk32":0,"sdl":1,"cou';

test('present section extracts from full and truncated METRICS lines', () => {
  assert.deepEqual(extractMetricsPresent(FULL_METRICS), { vk: 0, vk32: 0, sdl: 1, count: 1, path: 'sdl' });
  assert.deepEqual(extractMetricsPresent(TRUNCATED_METRICS), { vk: 0, vk32: 0, sdl: 1, count: 1, path: 'sdl' });
  assert.equal(extractMetricsPresent(CUT_INSIDE_PRESENT), null);
  assert.equal(extractMetricsPresent('some other log line'), null);
  assert.equal(extractMetricsPresent(null), null);
});

test('a realistic long line cut at the console channel limit still yields present', () => {
  const longLine = 'bw64: METRICS {"v":1,"present":{"vk":0,"vk32":0,"sdl":3,"count":3,"fps":2.0,"frameMs":500.1,"path":"sdl"},"vk":{"supported":true,"frames":0,"wallMs":0.00,' + 'x'.repeat(700) + '},"jit":{"blocksEntered":123}}';
  const cut = longLine.slice(0, 14 + 832);
  assert.throws(() => JSON.parse(cut.slice(14)));
  assert.deepEqual(extractMetricsPresent(cut), { vk: 0, vk32: 0, sdl: 3, count: 3, path: 'sdl' });
});

// --- Progress logging is synchronous (defect 4) ---------------------------
test('appendFileSync progress writes are visible to an immediate re-read', () => {
  const path = join(tmpdir(), `probe-progress-${process.pid}.log`);
  try {
    writeFileSync(path, '');
    appendFileSync(path, '[1.0s] MAPPED\n');
    assert.equal(readFileSync(path, 'utf8'), '[1.0s] MAPPED\n');
    appendFileSync(path, '[2.0s] RECORDING\n');
    assert.match(readFileSync(path, 'utf8'), /RECORDING/);
  } finally {
    try { unlinkSync(path); } catch {}
  }
});
