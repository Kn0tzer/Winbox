import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { callThreadDump, threadDumpDriverLine, threadDumpStatus, THREAD_DUMP_RC } from '../../thread-dump-driver.mjs';

const syscalls = readFileSync(new URL('../../../../source/kernel/syscall64.cpp', import.meta.url), 'utf8');
const kunixsocket = readFileSync(new URL('../../../../source/kernel/kunixsocket.cpp', import.meta.url), 'utf8');
const kepoll = readFileSync(new URL('../../../../source/kernel/kepoll.cpp', import.meta.url), 'utf8');

test('contended dump returns a labeled unavailable instead of hanging', () => {
  // Stand-in for the C++ side under contention: a guest thread holds the
  // log lock across a browser-proxied call, so the dump export returns
  // UNAVAILABLE_LOCK_HELD immediately rather than blocking the main
  // thread (a real guest thread cannot be driven into the proxied state
  // from a unit test; the C++ return contract is what the rig consumes).
  let lockHeld = true;
  const fakeModule = { _bw64_dump_guest_threads: () => (lockHeld ? THREAD_DUMP_RC.UNAVAILABLE_LOCK_HELD : THREAD_DUMP_RC.DUMPED) };

  const started = Date.now();
  const contended = callThreadDump(fakeModule);
  assert.ok(Date.now() - started < 1000, 'contended dump call must return promptly');
  assert.equal(contended.status, 'unavailable-lock-held');
  assert.equal(contended.via, 'export');
  assert.match(threadDumpDriverLine(contended.rc, contended.via, 3), /BW_THREAD_DUMP_DRIVER unavailable: dump not taken/);

  lockHeld = false;
  const taken = callThreadDump(fakeModule);
  assert.equal(taken.status, 'dumped');
  assert.match(threadDumpDriverLine(taken.rc, taken.via, 4), /BW_THREAD_DUMP_DRIVER invoked via export \(#4\)/);
});

test('ccall path asks for the numeric return code; missing exports are labeled', () => {
  const seen = [];
  const fakeModule = {
    ccall: (name, returnType, argTypes, args) => {
      seen.push([name, returnType, argTypes, args]);
      return THREAD_DUMP_RC.UNAVAILABLE_LOCK_HELD;
    },
  };
  const result = callThreadDump(fakeModule);
  assert.equal(result.status, 'unavailable-lock-held');
  assert.deepEqual(seen, [['bw64_dump_guest_threads', 'number', [], []]]);

  assert.equal(callThreadDump({}).status, 'no-export');
  assert.equal(callThreadDump(null).status, 'no-module');
  assert.equal(threadDumpStatus(THREAD_DUMP_RC.DISABLED), 'disabled');
  assert.equal(threadDumpStatus(undefined), 'unknown');
});

test('the C++ dump never blocks on an emulator lock (source pins)', () => {
  const start = syscalls.indexOf('extern "C" EMSCRIPTEN_KEEPALIVE int bw64_dump_guest_threads()');
  assert.ok(start >= 0, 'dump export must return its status code');
  const body = syscalls.slice(start, syscalls.indexOf('void ksyscall64(CPU64* cpu)', start));

  // The log lock gates the whole dump, by try-lock only.
  assert.match(body, /bw64TryLockLog\(\)/);
  assert.match(body, /bw64UnlockLog\(\)/);
  assert.match(body, /return 2;/);
  // Thread and fd lookups go through the try variants only.
  assert.match(body, /KSystem::tryGetThreadById/);
  assert.match(body, /tryGetFileDescriptor/);
  assert.ok(!/KSystem::getThreadById\(/.test(body), 'dump must not use the blocking thread lookup');
  assert.ok(!/[^y]getFileDescriptor\(/.test(body.replaceAll('tryGetFileDescriptor', '')), 'dump must not use the blocking fd lookup');
  // Skips are labeled in the output, never silent.
  assert.match(body, /BW_THREAD_DUMP unavailable tid=%u lock=processes\/threads/);
  assert.match(body, /BW_THREAD_DUMP fd-detail-unavailable/);
  assert.match(body, /BW_THREAD_DUMP end dumped=%u skipped-lock=%u/);

  // The stack walk holds the page lock by try-lock only.
  const stackStart = syscalls.indexOf('static void bwDumpGuestStack(CPU64* c, U32 pid, U32 tid)');
  const stackBody = syscalls.slice(stackStart, syscalls.indexOf('mem->unlockPages();', stackStart) + 20);
  assert.match(stackBody, /tryLockPages\(\)/);
  assert.match(stackBody, /BW_THREAD_DUMP stack-unavailable/);
  assert.match(stackBody, /unlockPages\(\)/);

  // The socket and epoll sub-dumps try-lock and label as well.
  assert.match(kunixsocket, /BW_THREAD_DUMP socket-unavailable/);
  assert.match(kunixsocket, /lockCond->tryLock\(\)/);
  assert.match(kepoll, /BW_THREAD_DUMP epoll-unavailable/);
  assert.match(kepoll, /tryGetFileDescriptor/);
});
