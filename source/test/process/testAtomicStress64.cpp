/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"
#include "../cpu/testCPU.h"

#include <functional>
#include <thread>
#include <vector>

// K-M1 concurrent atomicity stress (see docs/known-gaps/kernel.md).
//
// The Mindustry stage-C audit cleared aligned LOCK-vs-LOCK (page-shard
// mutexes) but convicted lock-class divergence: the lock class is chosen
// per operand from host alignment, so overlapping operands of different
// classes (aligned vs unaligned/cross-page) used to share NO lock and an
// increment could be silently lost. These scenarios run several CPU64
// instances on host threads over one shared KMemory64 — the production
// threading model — with exact integer oracles: any lost RMW fails.
//
// S1 aligned LOCK XADD: counter == T*I and the sum of returned old values
//    == (N-1)N/2 for N = T*I (dual oracle).
// S2 LOCK CMPXCHG retry loop: counter == T*I.
// S3 split/shard overlap: qword adds at [A] (aligned) racing qword adds
//    at [A+4] (split). Exact oracle on the 12-byte region (see below).
// S3b cross-page overlap: qword adds at [P+0xFFC] (spanning, split)
//    racing dword adds at [P+0x1000] (aligned, next page's shard).
// S4 split-only contention: qword adds at [A+4] == T*I.
// Run the x64 suite also with BW64_JIT=1: LOCK blocks are declined by the
// JIT, so the same oracles must hold through the interpreter fallback.

namespace {

constexpr U64 kCodePage = 0x60000000ULL;
constexpr U64 kStackBase = 0x60010000ULL;
constexpr U64 kDataPage = 0x60040000ULL;
constexpr U64 kCrossPage = 0x60050000ULL;
constexpr U32 kThreads = 4;
constexpr U64 kIters = 50000;

// lock add qword [rax], rcx(=1) loop; r8 = remaining iterations.
//   mov rcx, 1        48 C7 C1 01 00 00 00
//   lock add [rax],rcx F0 48 01 08
//   dec r8            49 FF C8
//   jnz loop          75 F0  (loop includes the mov: 16 bytes total,
//                      jnz at 14, rel = -16)
const U8 kLockAddLoop[] = {
    0x48, 0xC7, 0xC1, 0x01, 0x00, 0x00, 0x00,
    0xF0, 0x48, 0x01, 0x08,
    0x49, 0xFF, 0xC8,
    0x75, 0xF0,
};

// lock xadd qword [rax], rcx loop with old-value accumulation in rdx.
//   mov rcx, 1         48 C7 C1 01 00 00 00
//   lock xadd [rax],rcx F0 48 0F C1 08
//   add rdx, rcx       48 01 CA
//   dec r8             49 FF C8
//   jnz loop           75 EC  (5 instructions, 20 bytes, rel = -20;
//                      the mov is inside the loop because XADD
//                      overwrites rcx with the old memory value)
const U8 kLockXaddLoop[] = {
    0x48, 0xC7, 0xC1, 0x01, 0x00, 0x00, 0x00,
    0xF0, 0x48, 0x0F, 0xC1, 0x08,
    0x48, 0x01, 0xCA,
    0x49, 0xFF, 0xC8,
    0x75, 0xEC,
};

// lock cmpxchg qword [rbx], rcx retry loop; r8 = successful increments left.
//   retry: mov rax, [rbx]        48 8B 03
//          lea rcx, [rax+1]      48 8D 48 01
//          lock cmpxchg [rbx],rcx F0 48 0F B1 0B
//          jnz retry             75 F2  (at 12, rel = -14)
//          dec r8                49 FF C8
//          jnz retry             75 ED  (at 17, rel = -19)
const U8 kLockCmpxchgLoop[] = {
    0x48, 0x8B, 0x03,
    0x48, 0x8D, 0x48, 0x01,
    0xF0, 0x48, 0x0F, 0xB1, 0x0B,
    0x75, 0xF2,
    0x49, 0xFF, 0xC8,
    0x75, 0xED,
};

// lock add dword [rax], ecx(=1) loop (cross-page partner in S3b).
//   mov ecx, 1         B9 01 00 00 00
//   lock add [rax],ecx F0 01 08
//   dec r8             49 FF C8
//   jnz loop           75 F3  (loop includes the mov: 13 bytes,
//                      jnz at 11, rel = -13)
const U8 kLockAddDwordLoop[] = {
    0xB9, 0x01, 0x00, 0x00, 0x00,
    0xF0, 0x01, 0x08,
    0x49, 0xFF, 0xC8,
    0x75, 0xF3,
};

bool ensureMapped(KMemory64* memory, U64 addr, U64 len, U32 prot) {
    if (memory->getPageFlags(addr >> K64_PAGE_SHIFT)) return true;
    return memory->mmapAnonymousFixed(addr, len, prot) == addr;
}

struct Worker {
    CPU64* cpu = nullptr;
    U64 sum = 0;       // S1: accumulated XADD old values (guest rdx)
    bool done = false; // guest r8 reached 0
};

// Runs one guest program on `threadCount` host threads. Each thread gets
// its own CPU64 (own register state) over the shared memory, its own
// stack, and a data address chosen by addrFor(threadIndex). fixedBudget
// > 0 runs exactly that many instructions (programs whose instruction
// count is exact); otherwise runs in chunks until r8 == 0.
bool runWorkers(KMemory64* memory, const U8* code, U32 codeLen,
                U32 threadCount, U64 iters,
                const std::function<U64(U32)>& addrFor,
                U64 fixedBudget, std::vector<Worker>& workers) {
    if (!ensureMapped(memory, kCodePage, K64_PAGE_SIZE, 7)) return false;
    memory->memcpyToGuest(kCodePage, code, codeLen);
    workers.assign(threadCount, Worker());
    std::vector<std::thread> threads;
    for (U32 i = 0; i < threadCount; i++) {
        U64 stackTop = kStackBase + (U64)(i + 1) * K64_PAGE_SIZE * 4 - 16;
        if (!ensureMapped(memory, kStackBase + (U64)i * K64_PAGE_SIZE * 4, K64_PAGE_SIZE * 4, 3)) return false;
        threads.emplace_back([&, i, stackTop]() {
            CPU64 cpu(memory);
            cpu.thread = nullptr;
            cpu.rip = kCodePage;
            cpu.reg[X64_RSP].setU64(stackTop);
            U64 addr = addrFor(i);
            cpu.reg[X64_RAX].setU64(addr);
            cpu.reg[X64_RBX].setU64(addr);
            cpu.reg[X64_R8].setU64(iters);
            if (fixedBudget) {
                cpu.runBounded(fixedBudget);
            } else {
                U64 guard = 0;
                while (cpu.reg[X64_R8].u64 != 0 && guard < 100000) {
                    cpu.runBounded(100000);
                    guard++;
                }
            }
            workers[i].cpu = nullptr;
            workers[i].sum = cpu.reg[X64_RDX].u64;
            workers[i].done = cpu.reg[X64_R8].u64 == 0;
        });
    }
    for (auto& t : threads) t.join();
    return true;
}

U64 readQ(KMemory64* memory, U64 addr) {
    U64 v = 0;
    memory->memcpyFromGuest(&v, addr, 8);
    return v;
}
U32 readD(KMemory64* memory, U64 addr) {
    U32 v = 0;
    memory->memcpyFromGuest(&v, addr, 4);
    return v;
}
void zeroRange(KMemory64* memory, U64 addr, U32 len) {
    std::vector<U8> zeros(len, 0);
    memory->memcpyToGuest(addr, zeros.data(), len);
}

} // namespace

void testAtomicStress64() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    if (!ensureMapped(memory, kDataPage, K64_PAGE_SIZE, 3) ||
        !ensureMapped(memory, kCrossPage, K64_PAGE_SIZE * 2, 3)) {
        testFail("atomic stress test could not map its data pages");
        return;
    }
    const U64 total = (U64)kThreads * kIters;
    std::vector<Worker> workers;

    // S1: aligned LOCK XADD, dual oracle. 5 instructions per iteration.
    zeroRange(memory, kDataPage, 64);
    if (!runWorkers(memory, kLockXaddLoop, sizeof(kLockXaddLoop), kThreads, kIters,
                    [](U32) { return kDataPage; }, kIters * 5, workers)) {
        testFail("S1: could not run workers");
        return;
    }
    {
        U64 counter = readQ(memory, kDataPage);
        U64 sum = 0;
        for (const Worker& w : workers) {
            sum += w.sum;
            if (!w.done) testFail("S1: a worker did not finish its iterations");
        }
        U64 expectedSum = total * (total - 1) / 2;
        if (counter != total)
            testFail("S1: LOCK XADD counter is %llu, expected %llu (lost RMW)",
                     (unsigned long long)counter, (unsigned long long)total);
        if (sum != expectedSum)
            testFail("S1: LOCK XADD old-value sum is %llu, expected %llu",
                     (unsigned long long)sum, (unsigned long long)expectedSum);
    }

    // S2: LOCK CMPXCHG retry loop.
    zeroRange(memory, kDataPage, 64);
    if (!runWorkers(memory, kLockCmpxchgLoop, sizeof(kLockCmpxchgLoop), kThreads, kIters,
                    [](U32) { return kDataPage; }, 0, workers)) {
        testFail("S2: could not run workers");
        return;
    }
    {
        U64 counter = readQ(memory, kDataPage);
        for (const Worker& w : workers)
            if (!w.done) testFail("S2: a worker did not finish its iterations");
        if (counter != total)
            testFail("S2: LOCK CMPXCHG counter is %llu, expected %llu (lost RMW)",
                     (unsigned long long)counter, (unsigned long long)total);
    }

    // S3: split/shard overlap. Half the threads add at [A] (aligned qword),
    // half at [A+4] (unaligned qword -> split path). As integers over the
    // 12-byte region: X = a*1 + b*2^32, so u32@[A] == a, u32@[A+4] == b,
    // u32@[A+8] == 0, where a == b == total/2.
    zeroRange(memory, kDataPage, 64);
    if (!runWorkers(memory, kLockAddLoop, sizeof(kLockAddLoop), kThreads, kIters,
                    [](U32 i) { return kDataPage + (i % 2) * 4; }, kIters * 4, workers)) {
        testFail("S3: could not run workers");
        return;
    }
    {
        U64 half = total / 2;
        U32 lo = readD(memory, kDataPage);
        U32 mid = readD(memory, kDataPage + 4);
        U32 hi = readD(memory, kDataPage + 8);
        if (lo != half || mid != half || hi != 0)
            testFail("S3: overlap region is lo=%u mid=%u hi=%u, expected %llu/%llu/0 (lock-class divergence)",
                     lo, mid, hi, (unsigned long long)half, (unsigned long long)half);
    }

    // S3b: cross-page overlap. Half the threads add qword at [P+0xFFC]
    // (spans into the next page -> split path), half add dword at
    // [P+0x1000] (aligned, next page's shard). u32@[P+0xFFC] == a,
    // u32@[P+0x1000] == b, u32@[P+0x1004] == 0.
    zeroRange(memory, kCrossPage, K64_PAGE_SIZE * 2);
    {
        const U64 spanAddr = kCrossPage + K64_PAGE_SIZE - 4;
        std::vector<Worker> spanWorkers, dwordWorkers;
        // Two runs with different programs share the join logic by running
        // them as two sequential worker batches is NOT concurrent; instead
        // encode both loops on one code page pair and launch manually.
        if (!ensureMapped(memory, kCodePage, K64_PAGE_SIZE, 7)) {
            testFail("S3b: could not map code page");
            return;
        }
        memory->memcpyToGuest(kCodePage, kLockAddLoop, sizeof(kLockAddLoop));
        memory->memcpyToGuest(kCodePage + 0x100, kLockAddDwordLoop, sizeof(kLockAddDwordLoop));
        std::vector<std::thread> threads;
        for (U32 i = 0; i < kThreads; i++) {
            U64 stackTop = kStackBase + (U64)(i + 1) * K64_PAGE_SIZE * 4 - 16;
            bool span = (i % 2) == 0;
            threads.emplace_back([&, i, stackTop, span, spanAddr]() {
                CPU64 cpu(memory);
                cpu.thread = nullptr;
                cpu.rip = span ? kCodePage : kCodePage + 0x100;
                cpu.reg[X64_RSP].setU64(stackTop);
                cpu.reg[X64_RAX].setU64(span ? spanAddr : spanAddr + 4);
                cpu.reg[8].setU64(kIters);
                cpu.runBounded(kIters * 4);
            });
        }
        for (auto& t : threads) t.join();
        U64 half = total / 2;
        U32 lo = readD(memory, spanAddr);
        U32 mid = readD(memory, spanAddr + 4);
        U32 hi = readD(memory, spanAddr + 8);
        if (lo != half || mid != half || hi != 0)
            testFail("S3b: cross-page region is lo=%u mid=%u hi=%u, expected %llu/%llu/0 (lock-class divergence)",
                     lo, mid, hi, (unsigned long long)half, (unsigned long long)half);
    }

    // S4: split-only contention (all threads on the unaligned address).
    zeroRange(memory, kDataPage, 64);
    if (!runWorkers(memory, kLockAddLoop, sizeof(kLockAddLoop), kThreads, kIters,
                    [](U32) { return kDataPage + 4; }, kIters * 4, workers)) {
        testFail("S4: could not run workers");
        return;
    }
    {
        U64 counter = readQ(memory, kDataPage + 4);
        if (counter != total)
            testFail("S4: split-path counter is %llu, expected %llu (lost RMW)",
                     (unsigned long long)counter, (unsigned long long)total);
    }
}

#endif
#endif
