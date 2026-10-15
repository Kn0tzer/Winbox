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
  // Retain storage so an erroneous duplicate release is measured safely.
  static void operator delete(void*) {}
};
struct KThread {
  KProcess* process;
  bool removed = false;
  void cleanup();
};
struct KProcess {
  std::recursive_mutex threadsMutex, threadExitMutex;
  std::atomic<int> count{2}, cleanups{0};
  bool terminated = true;
  U32 id = 1, parentId = 0;
  Memory* memory = new Memory;
  U32 getThreadCount() { return count.load(); }
  void cleanupProcess() { ++cleanups; }
  void deleteThread(KThread*);
};
void KThread::cleanup() {
  if (removed) return;
  removed = true;
  --process->count;
  // Let a sibling finish its cleanup before the final-thread check. This
  // models the blocking wakeups and filesystem work in real thread cleanup.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
${source.slice(start, end)}
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
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000 });
    assert.equal(run.status, 0, run.stderr || String(run.error || run.signal));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
