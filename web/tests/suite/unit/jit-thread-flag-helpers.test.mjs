import test from 'node:test';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('threads entering an already compiled block receive the process flag helpers', () => {
  const source = readFileSync(new URL('../../../../source/emulation/cpu/jit/jitCodeGen.cpp', import.meta.url), 'utf8');
  const method = (signature, nextSignature) => {
    const start = source.indexOf(signature);
    if (start < 0) throw new Error(`Missing method: ${signature}`);
    const end = source.indexOf(nextSignature, start + signature.length);
    if (end < 0) throw new Error(`Missing next method: ${nextSignature}`);
    return source.slice(start, end);
  };
  const dir = mkdtempSync(join(tmpdir(), 'bw64-jit-thread-helpers-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstring>
#include <mutex>
#include <cstdlib>
using U32 = unsigned; using U8 = unsigned char; using LazyFlagType = unsigned;
#define OPCALL
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::mutex> lock(m)
constexpr U32 OP_FLAG_JIT = 1, JIT_RUN_COUNT = 3, Done = 99;
constexpr U32 OP_FLAG2_WASM_JIT_PENDING = 1;
constexpr U32 FLAGS_NULL = 4, FLAGS_ADD64 = 5, FLAGS_NEG64 = 6, CS = 0;
struct CPU; struct DecodedOp;
using OpCallback = void (*)(CPU*, DecodedOp*);
struct Process {
  void *syncToHost{}, *syncFromHost{}, *blockExit{}, *emulateSingleOp{};
  OpCallback startJITOp{}; void* calculateCF[FLAGS_NULL]{};
};
struct Thread { Process* process; };
struct Memory { std::mutex mutex; };
struct CPU {
  Thread* thread; Memory* memory; void* calculateCF[FLAGS_NULL]{};
  U32 getEipAddress() { return 42; }
};
struct DecodedOp { U32 flags{}, runCount{}, inst{}; OpCallback pfn{}; U32 flags2{}; };
int attempts = 0, dispatches = 0;
void kpanic(const char*) { std::abort(); }
void dispatch(CPU*, DecodedOp*) { dispatches++; }
struct JitCodeGen {
  CPU* cpu; bool disableTmps{}; U32 currentEip{}, startingEip{};
  DecodedOp* firstOp{};
  void doJIT(U32, DecodedOp*);
  U8* createSyncToHost() { return nullptr; }
  U8* createSyncFromHost() { return nullptr; }
  U8* createBlockExit() { return nullptr; }
  U8* createEmulateSingleOp() { return nullptr; }
  U8* createStartJITCode() { return nullptr; }
  U8* createCalculationCF(LazyFlagType) { return nullptr; }
  void createHelpers() {} void initDynamicOps() {}
  bool calculateLongestBlock(DecodedOp*) { attempts++; return false; }
  bool compileOps(DecodedOp*) { return false; }
  void onBlockPreCommit(DecodedOp*) {} void commitJIT(DecodedOp*) {}
};
JitCodeGen* startNewJIT(CPU* cpu) { return new JitCodeGen{cpu}; }
void startNewJIT(CPU* cpu, U32 address, DecodedOp* op) {
  JitCodeGen jit{cpu}; jit.doJIT(address, op);
}
${method('void JitCodeGen::doJIT(', 'void OPCALL firstDynamicOp(')}
${method('void OPCALL firstDynamicOp(', '#define CPU_OFFSET_OF(x)')}
int main() {
  Process process; process.startJITOp = dispatch;
  for (U32 i = 0; i < FLAGS_NULL; i++) process.calculateCF[i] = reinterpret_cast<void*>(i + 1);
  Thread thread{&process}; Memory memory; CPU cpu{&thread, &memory};
  DecodedOp op{OP_FLAG_JIT, JIT_RUN_COUNT + 1, 0, dispatch};
  // A sibling compiled the shared block before this CPU entered it.
  JitCodeGen jit{&cpu}; jit.doJIT(42, &op);
  assert(std::memcmp(cpu.calculateCF, process.calculateCF, sizeof(cpu.calculateCF)) == 0);
  assert(attempts == 0);
  // A newly scheduled CPU must initialize even when the shared run count
  // is already above the compilation threshold.
  std::memset(cpu.calculateCF, 0, sizeof(cpu.calculateCF));
  firstDynamicOp(&cpu, &op);
  assert(std::memcmp(cpu.calculateCF, process.calculateCF, sizeof(cpu.calculateCF)) == 0);
  assert(attempts == 0 && dispatches == 1);
  // Pending shared blocks need the same per-CPU initialization before the
  // compiler's early return, without compiling the block a second time.
  std::memset(cpu.calculateCF, 0, sizeof(cpu.calculateCF));
  op.flags = 0; op.flags2 = OP_FLAG2_WASM_JIT_PENDING;
  jit.doJIT(42, &op);
  assert(std::memcmp(cpu.calculateCF, process.calculateCF, sizeof(cpu.calculateCF)) == 0);
  assert(attempts == 0);
  // An uncompiled cold block must keep warming up. Missing helpers alone
  // must not bypass the threshold or repeatedly enter a disabled compiler.
  std::memset(cpu.calculateCF, 0, sizeof(cpu.calculateCF));
  op.flags = 0; op.flags2 = 0; op.runCount = 1;
  firstDynamicOp(&cpu, &op);
  assert(attempts == 0 && dispatches == 2);
  assert(cpu.calculateCF[0] == nullptr);
}
`);
    for (const defines of [[], ['-D__TEST']]) {
      execFileSync('c++', ['-std=c++20', ...defines, '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { stdio: 'pipe', timeout: 30000 });
      execFileSync(join(dir, 'check'), [], { stdio: 'pipe', timeout: 10000 });
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
