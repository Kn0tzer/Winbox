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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kerror.h"
#include "kmemory64.h"
#include "ksignal.h"
#include "../cpu/testCPU.h"

#include <chrono>
#include <thread>

void runTimers();
S32 ksyscall64(CPU64* cpu);

// x64 setitimer/getitimer must be real syscalls (they were ENOSYS stubs):
// ITIMER_REAL arms, queues SIGALRM on expiry, round-trips through
// getitimer, disarms, and rejects ITIMER_VIRTUAL/PROF with -EINVAL.

void testSetitimer64Syscalls() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x77000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 setitimer test could not map its fixture page");
        return;
    }
    constexpr U64 value = page + 0x100;
    constexpr U64 oldValue = page + 0x140;

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto writeItimerval64 = [&](U64 address, U64 valueSec, U64 valueUsec, U64 intervalSec, U64 intervalUsec) {
        memory->writeq(address, intervalSec);
        memory->writeq(address + 8, intervalUsec);
        memory->writeq(address + 16, valueSec);
        memory->writeq(address + 24, valueUsec);
    };

    context.process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));
    // Block SIGALRM so expiry queues in the process pending mask instead
    // of taking the default-terminate path (see testSetitimerProbes).
    context.thread->sigMask |= (1ULL << (K_SIGALRM - 1));

    // __NR_setitimer = 38, __NR_getitimer = 36
    writeItimerval64(value, 0, 30 * 1000, 0, 0);
    S64 result = invoke(38, 0 /* ITIMER_REAL */, value, oldValue);
    if (result != 0) {
        testFail("x64 setitimer(ITIMER_REAL) returned %lld, expected 0", (long long)result);
        return;
    }
    result = invoke(36, 0 /* ITIMER_REAL */, oldValue, 0);
    U64 remain = memory->readq(oldValue + 16) * 1000000 + memory->readq(oldValue + 24);
    if (result != 0 || remain == 0 || remain > 30000) {
        testFail("x64 getitimer(ITIMER_REAL) wrong: result=%lld remain=%llu us",
                 (long long)result, (unsigned long long)remain);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    runTimers();
    if (!(context.process->pendingSignals & (1ULL << (K_SIGALRM - 1)))) {
        testFail("x64 setitimer never queued SIGALRM");
    }
    context.process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));

    // Periodic interval round-trips.
    writeItimerval64(value, 0, 25 * 1000, 0, 25 * 1000);
    result = invoke(38, 0 /* ITIMER_REAL */, value, 0);
    if (result != 0) {
        testFail("x64 setitimer(periodic) returned %lld", (long long)result);
    }
    result = invoke(36, 0 /* ITIMER_REAL */, oldValue, 0);
    if (result != 0 || memory->readq(oldValue) != 0 || memory->readq(oldValue + 8) != 25000) {
        testFail("x64 getitimer did not round-trip the interval");
    }

    // Disarm.
    writeItimerval64(value, 0, 0, 0, 0);
    result = invoke(38, 0 /* ITIMER_REAL */, value, 0);
    if (result != 0) {
        testFail("x64 setitimer(disarm) returned %lld", (long long)result);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runTimers();
    if (context.process->pendingSignals & (1ULL << (K_SIGALRM - 1))) {
        testFail("x64 disarmed setitimer still queued SIGALRM");
    }

    // CPU-time timers: -EINVAL, never ENOSYS, never a crash.
    writeItimerval64(value, 0, 1000, 0, 0);
    if (invoke(38, 1 /* ITIMER_VIRTUAL */, value, 0) != -K_EINVAL) {
        testFail("x64 setitimer(ITIMER_VIRTUAL) was not -EINVAL");
    }
    if (invoke(38, 2 /* ITIMER_PROF */, value, 0) != -K_EINVAL) {
        testFail("x64 setitimer(ITIMER_PROF) was not -EINVAL");
    }
    if (invoke(36, 1 /* ITIMER_VIRTUAL */, oldValue, 0) != -K_EINVAL) {
        testFail("x64 getitimer(ITIMER_VIRTUAL) was not -EINVAL");
    }
    if (invoke(38, 99, value, 0) != -K_EINVAL) {
        testFail("x64 setitimer(bad which) was not -EINVAL");
    }
    context.thread->sigMask &= ~(1ULL << (K_SIGALRM - 1));
    context.process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));
}

#endif
#endif
