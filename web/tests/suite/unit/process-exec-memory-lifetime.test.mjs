import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('exec waits for sibling execution and teardown before resetting either address space', () => {
  const source = readFileSync('source/kernel/kprocess.cpp', 'utf8');
  const start = source.indexOf('    // reset memory must come after we grab the args and env');
  const end = source.indexOf('    // not sure why x64', start);
  assert.ok(start >= 0 && end > start);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-exec-memory-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>
using U32 = unsigned;
#define BOXEDWINE_MULTI_THREADED
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::recursive_mutex> lock(m)
std::atomic<bool> stopped{false}, removed{false}, destroyed{false};
void requireQuiescent() {
  if (!stopped || !destroyed) throw std::runtime_error("exec reset memory while a sibling can still access it");
}
struct KThread { void reset() { requireQuiescent(); } };
struct Memory { void execvReset(bool) { requireQuiescent(); } };
struct Heap { void freeAll(Memory*) { requireQuiescent(); } };
struct KMemory64 {
  explicit KMemory64(void*) { requireQuiescent(); }
  ~KMemory64() { requireQuiescent(); }
};
struct Reg { void setU64(int) {} };
constexpr int X64_REG_COUNT = 16;
struct CPU64 {
  KMemory64* memory = nullptr;
  Reg reg[16];
  int rflags=0, fsbase=0, gsbase=0, mmapNext=0, sigMask=0;
  std::vector<int> futexWaiters;
  struct SigAction {};
  SigAction sigActions[65];
  void invalidateFetchCache() {}
};
void bw64InvalidateScratchForThread(unsigned) {}
struct KProcess {
  std::recursive_mutex threadExitMutex;
  Heap heap;
  Memory storage;
  Memory* memory = &storage;
  KMemory64* memory64 = nullptr;
  CPU64* cpu64 = nullptr;
  bool cloneVM = false, is64Bit = false;
  struct Entry { unsigned key; };
  std::vector<Entry> threads{{1},{2}};
  std::vector<int> startupArgs64, startupEnv64;
  void killAllThreads(KThread*) {
    stopped = true;
    while (!removed) std::this_thread::yield();
  }
  void onExec(KThread* t) { killAllThreads(t); }
  void replaceImage(KThread* thread) {
    std::vector<int> args, envs;
${source.slice(start, end)}
  }
};
int main() {
  for (bool x64 : {false,true}) {
    stopped=false; removed=false; destroyed=false;
    KProcess process; process.is64Bit=x64;
    KThread survivor;
    // Table removal wakes terminateOtherThread before the sibling destructor
    // finishes. That destructor still clears guest futex/robust-list state.
    std::thread sibling([&] {
      while (!stopped) std::this_thread::yield();
      std::lock_guard<std::recursive_mutex> lock(process.threadExitMutex);
      removed=true;
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      destroyed=true;
    });
    bool passed=true;
    try { process.replaceImage(&survivor); }
    catch (const std::exception&) { passed=false; stopped=true; }
    sibling.join();
    if (!passed) return 1;
    delete process.memory64;
  }
}
`);
    for (const defines of [[], ['-DBOXEDWINE_GUEST_X64']]) {
      const build = spawnSync('c++', ['-std=c++17', '-pthread', ...defines,
        join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8', timeout: 30000 });
      assert.equal(build.status, 0, build.stderr);
      const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000 });
      assert.equal(run.status, 0, run.stderr || String(run.error || 'exec reset memory before sibling teardown finished'));
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
