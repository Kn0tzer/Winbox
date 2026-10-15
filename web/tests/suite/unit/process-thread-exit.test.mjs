import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('concurrent final thread exits release process memory exactly once', () => {
  const source = readFileSync('source/kernel/kprocess.cpp', 'utf8');
  const start = source.indexOf('void KProcess::deleteThread(KThread* thread)');
  const end = source.indexOf('\nvoid KProcess::setupCommandlineNode()', start);
  assert.ok(start >= 0 && end > start);
  const platform = readFileSync('source/emulation/cpu/normal/normalPlatformMultiThreaded.cpp', 'utf8');
  const exitStart = platform.indexOf('        ripSamplerReleaseSlot(ripSlot);');
  const exitEnd = platform.indexOf('        return;', exitStart);
  assert.ok(exitStart >= 0 && exitEnd > exitStart);
  const groupStart = source.indexOf('U32 KProcess::exitgroup(');
  const groupCleanup = source.indexOf('\n\n', source.indexOf('this->terminated = true;', groupStart)) + 2;
  const groupEnd = source.indexOf('    this->exitCode = code;', groupCleanup);
  assert.ok(groupStart >= 0 && groupEnd > groupCleanup);
  const ordinaryStart = source.indexOf('U32 KProcess::exit(KThread* thread, U32 code)');
  const ordinaryEnd = source.indexOf('\nKFileDescriptorPtr KProcess::getFileDescriptor(', ordinaryStart);
  assert.ok(ordinaryStart >= 0 && ordinaryEnd > ordinaryStart);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-thread-exit-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
using U32 = unsigned;
#define BOXEDWINE_MULTI_THREADED
#define KProcessPtr std::shared_ptr<KProcess>
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::recursive_mutex> lock(m)
struct KProcess;
struct KSystem {
  static inline bool shutingDown = false;
  static KProcessPtr getProcess(U32) { return {}; }
  static void eraseProcess(U32) {}
};
std::atomic<int> platformThreadCount{2}, quits{0};
struct KNativeSystem { static void postQuit() { ++quits; } };
void ripSamplerReleaseSlot(int) {}
struct Memory {
  static inline std::atomic<int> releases{0};
  ~Memory() { ++releases; }
  void execvReset(bool) {}
  // Retain storage so an erroneous duplicate release is measured safely.
  static void operator delete(void*) {}
};
struct KThread {
  KProcess* process;
  bool removed = false;
  std::atomic<bool>* removalSignal = nullptr;
  struct CpuFlags { bool yield = false; } flags;
  CpuFlags* cpu = &flags;
  void cleanup();
};
void terminateCurrentThread(KThread*) {}
struct KProcess {
  std::recursive_mutex threadsMutex, threadExitMutex;
  std::atomic<int> count{2}, cleanups{0};
  struct Table { std::atomic<int>* count; void clear() { *count = 0; } } threads{&count};
  bool terminated = true;
  bool cloneVM = false;
  U32 exitCode = 0;
  U32 id = 1, parentId = 0;
  Memory* memory = new Memory;
  U32 getThreadCount() { return count.load(); }
  void cleanupProcess() {
    assert(memory != nullptr && Memory::releases == 0);
    ++cleanups;
  }
  void deleteThread(KThread*);
  void exitGroupCleanup(KThread*);
  U32 exit(KThread*, U32);
  U32 exitgroup(KThread* thread, U32) { terminated = true; exitGroupCleanup(thread); return 0; }
};
void KThread::cleanup() {
  if (removed) return;
  removed = true;
  --process->count;
  if (removalSignal) *removalSignal = true;
  // Let a sibling finish its cleanup before the final-thread check. This
  // models the blocking wakeups and filesystem work in real thread cleanup.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
${source.slice(start, end)}
${source.slice(ordinaryStart, ordinaryEnd)}
void KProcess::exitGroupCleanup(KThread* thread) {
${source.slice(groupCleanup, groupEnd)}
}
struct CPU { KThread* thread; };
void finishThread(CPU* cpu, KProcessPtr process) {
  int ripSlot = 0;
${platform.slice(exitStart, exitEnd)}
}
int main() {
  auto process = std::make_shared<KProcess>();
  Memory* storage = process->memory;
  std::barrier start(3);
  auto exit = [&] {
    auto* thread = new KThread{process.get()};
    CPU cpu{thread};
    start.arrive_and_wait();
    finishThread(&cpu, process);
  };
  std::thread first(exit), second(exit);
  start.arrive_and_wait();
  first.join(); second.join();
  assert(process->cleanups == 1);
  assert(Memory::releases == 1);
  assert(process->memory == nullptr);
  assert(platformThreadCount == 0 && quits == 1);
  ::operator delete(storage);

  Memory::releases = 0;
  platformThreadCount = 2; quits = 0;
  process = std::make_shared<KProcess>();
  process->terminated = false;
  storage = process->memory;
  std::barrier ordinaryStart(3);
  auto ordinaryExit = [&] {
    auto* thread = new KThread{process.get()};
    CPU cpu{thread};
    ordinaryStart.arrive_and_wait();
    process->exit(thread, 42);
    finishThread(&cpu, process);
  };
  std::thread third(ordinaryExit), fourth(ordinaryExit);
  ordinaryStart.arrive_and_wait();
  third.join(); fourth.join();
  assert(process->terminated && process->exitCode == 42);
  assert(Memory::releases == 1 && process->memory == nullptr);
  assert(platformThreadCount == 0 && quits == 1);
  ::operator delete(storage);

  Memory::releases = 0;
  platformThreadCount = 2; quits = 0;
  process = std::make_shared<KProcess>();
  storage = process->memory;
  std::atomic<bool> siblingRemoved{false};
  auto* mainThread = new KThread{process.get()};
  std::thread sibling([&] {
    auto* thread = new KThread{process.get(), false, &siblingRemoved};
    CPU cpu{thread};
    finishThread(&cpu, process);
  });
  // killAllThreads observes the sibling removed from the table before its
  // destructor and final-thread check have finished.
  while (!siblingRemoved) std::this_thread::yield();
  process->exitGroupCleanup(mainThread);
  CPU cpu{mainThread};
  finishThread(&cpu, process);
  sibling.join();
  assert(process->cleanups == 2); // early exit_group cleanup, then final release
  assert(Memory::releases == 1 && process->memory == nullptr);
  assert(platformThreadCount == 0 && quits == 1);
  ::operator delete(storage);
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000 });
    assert.equal(run.status, 0, run.stderr || String(run.error || run.signal));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
