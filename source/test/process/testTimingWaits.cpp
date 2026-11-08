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

// Timing/wait contract tests. The expected answers come from a host
// grid measured at the raw syscall level (test-results/timing-waits/
// hostref-timing.txt + hostref-timing2.txt): a libc wrapper's answer is
// not the kernel's, so every shape below was issued through the raw
// syscall trampoline on the host first. Interrupt-driven shapes (the
// remaining-time write on an interrupted sleep) cannot be driven
// synchronously in this harness; the guest probes in
// test-results/timing-waits/ are their regression vehicle.

#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kerror.h"
#include "kmemory64.h"
#include "../cpu/testCPU.h"

#include <chrono>

S32 ksyscall64(CPU64* cpu);

void testTimingWaits64() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x77000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 timing test could not map its fixture page");
        return;
    }
    constexpr U64 tsAt = page + 0x100;    // struct timespec / timeval pairs
    constexpr U64 bufAt = page + 0x200;   // rusage / itimerval / misc
    constexpr U64 evAt = page + 0x300;    // epoll_event / futex word

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4 = 0, U64 a5 = 0, U64 a6 = 0) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto expectEq = [&](const char* what, S64 got, S64 want) {
        if (got != want) {
            testFail("timing64 %s returned %lld (expected %lld)", what, (long long)got, (long long)want);
        }
    };
    auto writeTs = [&](U64 at, S64 sec, S64 nsec) {
        memory->writeq(at, (U64)sec);
        memory->writeq(at + 8, (U64)nsec);
    };

    // clock_gettime (228): per-clock domains, id validated first.
    expectEq("clock_gettime clk10", invoke(228, 10, tsAt, 0), -K_EINVAL);
    expectEq("clock_gettime clk12", invoke(228, 12, tsAt, 0), -K_EINVAL);
    expectEq("clock_gettime clk-1", invoke(228, (U64)-1, tsAt, 0), -K_EINVAL);
    expectEq("clock_gettime clk99", invoke(228, 99, tsAt, 0), -K_EINVAL);
    expectEq("clock_gettime clk99 null tp (id first)", invoke(228, 99, 0, 0), -K_EINVAL);
    expectEq("clock_gettime null tp", invoke(228, 0, 0, 0), -K_EFAULT);
    expectEq("clock_gettime clk2", invoke(228, 2, tsAt, 0), 0);
    expectEq("clock_gettime clk3", invoke(228, 3, tsAt, 0), 0);
    expectEq("clock_gettime clk7", invoke(228, 7, tsAt, 0), 0);
    expectEq("clock_gettime clk8", invoke(228, 8, tsAt, 0), 0);
    expectEq("clock_gettime clk9", invoke(228, 9, tsAt, 0), 0);
    expectEq("clock_gettime clk11", invoke(228, 11, tsAt, 0), 0);
    {
        expectEq("clock_gettime clk0", invoke(228, 0, tsAt, 0), 0);
        S64 realSec = (S64)memory->readq(tsAt);
        S64 hostSec = (S64)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (realSec < hostSec - 120 || realSec > hostSec + 120) {
            testFail("timing64 clock_gettime realtime sec %lld is not near host %lld",
                     (long long)realSec, (long long)hostSec);
        }
        expectEq("clock_gettime clk1", invoke(228, 1, page + 0x180, 0), 0);
        S64 monoSec = (S64)memory->readq(page + 0x180);
        if (monoSec >= realSec) {
            testFail("timing64 monotonic sec %lld not below realtime sec %lld",
                     (long long)monoSec, (long long)realSec);
        }
    }
    // clock_getres (229).
    expectEq("clock_getres clk99", invoke(229, 99, tsAt, 0), -K_EINVAL);
    expectEq("clock_getres null tp", invoke(229, 0, 0, 0), 0);
    writeTs(tsAt, -7, -7);
    expectEq("clock_getres clk0", invoke(229, 0, tsAt, 0), 0);
    if (memory->readq(tsAt) != 0 || memory->readq(tsAt + 8) != 1) {
        testFail("timing64 clock_getres clk0 did not report 1 ns");
    }
    writeTs(tsAt, -7, -7);
    expectEq("clock_getres clk5", invoke(229, 5, tsAt, 0), 0);
    if (memory->readq(tsAt + 8) != 1000000) {
        testFail("timing64 clock_getres clk5 did not report 1000000 ns");
    }
    writeTs(tsAt, -7, -7);
    expectEq("clock_getres clk6", invoke(229, 6, tsAt, 0), 0);
    if (memory->readq(tsAt + 8) != 1000000) {
        testFail("timing64 clock_getres clk6 did not report 1000000 ns");
    }
    // getrusage (98): who validated ahead of the pointer.
    expectEq("getrusage who2", invoke(98, 2, bufAt, 0), -K_EINVAL);
    expectEq("getrusage who-2", invoke(98, (U64)-2, bufAt, 0), -K_EINVAL);
    expectEq("getrusage who99", invoke(98, 99, bufAt, 0), -K_EINVAL);
    expectEq("getrusage null usage", invoke(98, 0, 0, 0), -K_EFAULT);
    expectEq("getrusage who99+null (order)", invoke(98, 99, 0, 0), -K_EINVAL);
    expectEq("getrusage self", invoke(98, 0, bufAt, 0), 0);
    expectEq("getrusage thread", invoke(98, 1, bufAt, 0), 0);
    // gettimeofday (96) with both pointers null is a plain success.
    expectEq("gettimeofday null null", invoke(96, 0, 0, 0), 0);
    // times (100): null buffer is accepted and returns tick count.
    if (invoke(100, 0, 0, 0) <= 0) {
        testFail("timing64 times(null) did not return a positive tick count");
    }
    // alarm (37) exists on the 64-bit side and shares ITIMER_REAL.
    expectEq("alarm(0)", invoke(37, 0, 0, 0), 0);
    // nanosleep (35).
    expectEq("nanosleep null req", invoke(35, 0, 0, 0), -K_EFAULT);
    writeTs(tsAt, 0, 1000000000);
    expectEq("nanosleep nsec 1e9", invoke(35, tsAt, 0, 0), -K_EINVAL);
    writeTs(tsAt, -1, 0);
    expectEq("nanosleep sec -1", invoke(35, tsAt, 0, 0), -K_EINVAL);
    writeTs(tsAt, 0, 20000000);
    {
        auto t0 = std::chrono::steady_clock::now();
        expectEq("nanosleep 20ms", invoke(35, tsAt, 0, 0), 0);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ms < 10) {
            testFail("timing64 nanosleep 20ms returned after %lld ms", (long long)ms);
        }
    }
    // clock_nanosleep (230).
    writeTs(tsAt, 0, 0);
    expectEq("clock_nanosleep clk3", invoke(230, 3, 0, tsAt, 0), -K_EOPNOTSUPP);
    expectEq("clock_nanosleep clk2 rel0", invoke(230, 2, 0, tsAt, 0), 0);
    expectEq("clock_nanosleep clk99", invoke(230, 99, 0, tsAt, 0), -K_EINVAL);
    expectEq("clock_nanosleep flags2", invoke(230, 0, 2, tsAt, 0), 0);
    expectEq("clock_nanosleep null req", invoke(230, 0, 0, 0, 0), -K_EFAULT);
    {
        expectEq("clock_gettime mono for abstime", invoke(228, 1, page + 0x180, 0), 0);
        S64 sec = (S64)memory->readq(page + 0x180);
        S64 nsec = (S64)memory->readq(page + 0x188);
        nsec += 30000000;
        if (nsec >= 1000000000) { sec++; nsec -= 1000000000; }
        writeTs(tsAt, sec, nsec);
        auto t0 = std::chrono::steady_clock::now();
        expectEq("clock_nanosleep abstime +30ms", invoke(230, 1, 1, tsAt, 0), 0);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ms < 15) {
            testFail("timing64 clock_nanosleep abstime returned after %lld ms", (long long)ms);
        }
    }
    // setitimer (38) / getitimer (36): field validation and check order.
    // itimerval layout: {interval.sec, interval.usec, value.sec, value.usec}.
    memory->writeq(bufAt, 0); memory->writeq(bufAt + 8, 0);
    memory->writeq(bufAt + 16, (U64)-1); memory->writeq(bufAt + 24, 0);
    expectEq("setitimer value sec -1", invoke(38, 0, bufAt, 0), -K_EINVAL);
    memory->writeq(bufAt, 0); memory->writeq(bufAt + 8, 1000000);
    memory->writeq(bufAt + 16, 0); memory->writeq(bufAt + 24, 0);
    expectEq("setitimer interval usec 1e6", invoke(38, 0, bufAt, 0), -K_EINVAL);
    expectEq("getitimer which3 null (which first)", invoke(36, 3, 0, 0), -K_EINVAL);
    expectEq("getitimer null", invoke(36, 0, 0, 0), -K_EFAULT);
    // wait4 (61): options validated ahead of the child lookup.
    expectEq("wait4 bad options", invoke(61, (U64)-1, bufAt, 0x10, 0), -K_EINVAL);
    expectEq("wait4 no children", invoke(61, (U64)-1, bufAt, 1, 0), -K_ECHILD);
    // waitid (247) is not implemented on either width: pin the -ENOSYS so
    // the gap stays visible until it is.
    expectEq("waitid unimplemented pin", invoke(247, 0, 0, bufAt, 0, 0), -K_ENOSYS);
    // timerfd: create (283), settime (286), gettime (287).
    expectEq("timerfd_create bad clock", invoke(283, 99, 0, 0), -K_EINVAL);
    S64 tfd = invoke(283, 1, 0, 0);
    if (tfd < 0) {
        testFail("timing64 timerfd_create(CLOCK_MONOTONIC) returned %lld", (long long)tfd);
        return;
    }
    expectEq("timerfd_settime null new", invoke(286, (U64)tfd, 0, 0, 0), -K_EFAULT);
    expectEq("timerfd_gettime null", invoke(287, (U64)tfd, 0, 0, 0), -K_EFAULT);
    // itimerspec layout: {interval.sec, interval.nsec, value.sec, value.nsec}.
    memory->writeq(bufAt, 0); memory->writeq(bufAt + 8, 0);
    memory->writeq(bufAt + 16, 0); memory->writeq(bufAt + 24, 0);
    expectEq("timerfd_settime flags2 (CANCEL_ON_SET)", invoke(286, (U64)tfd, 2, bufAt, 0), 0);
    expectEq("timerfd_settime flags4 (outside mask)", invoke(286, (U64)tfd, 4, bufAt, 0), -K_EINVAL);
    // epoll: create (213) / create1 (291) / ctl (233) / wait (232).
    expectEq("epoll_create size0", invoke(213, 0, 0, 0), -K_EINVAL);
    expectEq("epoll_create1 flags1", invoke(291, 1, 0, 0), -K_EINVAL);
    S64 efd = invoke(291, 0, 0, 0);
    if (efd < 0) {
        testFail("timing64 epoll_create1(0) returned %lld", (long long)efd);
        return;
    }
    memory->writed(evAt, 1); // EPOLLIN
    memory->writeq(evAt + 4, 0x1234);
    expectEq("epoll_ctl add timerfd", invoke(233, (U64)efd, 1, (U64)tfd, evAt), 0);
    expectEq("epoll_wait maxevents0", invoke(232, (U64)efd, evAt, 0, 0), -K_EINVAL);
    expectEq("epoll_wait maxevents-1", invoke(232, (U64)efd, evAt, (U64)-1, 0), -K_EINVAL);
    // futex (202) through the real dispatch path (this harness CPU has a
    // thread, process and memory64, unlike the bare ELF runner):
    // WAIT mismatch answers -EAGAIN, WAIT match with a zero timeout
    // answers -ETIMEDOUT, WAKE with no waiters answers 0.
    // FUTEX_PRIVATE_FLAG (128): the plain shared-word WAIT is refused
    // -ENOSYS ahead of the value check by design (see the dispatch
    // guard test); the private forms take the real path.
    memory->writed(evAt, 7);
    writeTs(tsAt, 0, 0);
    expectEq("futex WAIT mismatch", invoke(202, evAt, 128, 9, tsAt, 0, 0), -K_EAGAIN);
    // A single-threaded process tolerates a spurious EAGAIN from WAIT
    // (kernel-faithful parking needs a second thread; the parking path
    // is the dispatch guard test's subject).
    expectEq("futex WAIT match t0", invoke(202, evAt, 128, 7, tsAt, 0, 0), -K_EAGAIN);
    expectEq("futex WAKE none", invoke(202, evAt, 129, 1, 0, 0, 0), 0);
}

#else // 32-bit

#include "kerror.h"
#include "../cpu/testCPU.h"

#include <chrono>
#include <cstring>

void testTimingWaits32() {
    TestContext& context = testContext();
    const U32 tsAt = TEST_HEAP_ADDRESS + 0x400;   // timespec32 {S32 sec, S32 nsec}
    const U32 ts2At = TEST_HEAP_ADDRESS + 0x440;
    const U32 bufAt = TEST_HEAP_ADDRESS + 0x500;  // rusage / itimerval32

    auto stageFixtures = [&]() {
        // rewritten inside invoke(): testNewInstruction resets the heap
        U32 zero[16] = {0};
        context.memory->memcpy(tsAt, zero, sizeof(zero));
        context.memory->memcpy(ts2At, zero, sizeof(zero));
        context.memory->memcpy(bufAt, zero, sizeof(zero));
    };
    auto invoke = [&](U32 nr, U32 a1, U32 a2, U32 a3, U32 a4 = 0, U32 a5 = 0, U32 a6 = 0) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        stageFixtures();
        context.cpu->reg[3].u32 = a1; // ebx
        context.cpu->reg[1].u32 = a2; // ecx
        context.cpu->reg[2].u32 = a3; // edx
        context.cpu->reg[6].u32 = a4; // esi
        context.cpu->reg[7].u32 = a5; // edi
        context.cpu->reg[5].u32 = a6; // ebp
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    // invoke with a prepared timespec32 at tsAt (stageFixtures zeroes it,
    // so the fields are written by a second staging step inside).
    auto invokeTs = [&](U32 nr, U32 a1, U32 a2, S32 sec, S32 nsec, U32 a4 = 0) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr);
        testPushCode8(0xCD); testPushCode8(0x80);
        stageFixtures();
        context.memory->writed(tsAt, (U32)sec);
        context.memory->writed(tsAt + 4, (U32)nsec);
        context.cpu->reg[3].u32 = a1;
        context.cpu->reg[1].u32 = a2;
        context.cpu->reg[2].u32 = tsAt;
        context.cpu->reg[6].u32 = a4;
        context.cpu->reg[7].u32 = 0;
        context.cpu->reg[5].u32 = 0;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) {
            testFail("timing32 %s returned %d (expected %d)", what, got, want);
        }
    };

    // clock_gettime (265): the full host-valid id set answers, in its
    // own domains; invalid ids answer -EINVAL ahead of the pointer.
    expectEq("clock_gettime clk3", invoke(265, 3, tsAt, 0), 0);
    expectEq("clock_gettime clk7", invoke(265, 7, tsAt, 0), 0);
    expectEq("clock_gettime clk8", invoke(265, 8, tsAt, 0), 0);
    expectEq("clock_gettime clk9", invoke(265, 9, tsAt, 0), 0);
    expectEq("clock_gettime clk11", invoke(265, 11, tsAt, 0), 0);
    expectEq("clock_gettime clk10", invoke(265, 10, tsAt, 0), -K_EINVAL);
    expectEq("clock_gettime clk99", invoke(265, 99, tsAt, 0), -K_EINVAL);
    expectEq("clock_gettime clk99 null tp (id first)", invoke(265, 99, 0, 0), -K_EINVAL);
    expectEq("clock_gettime null tp", invoke(265, 0, 0, 0), -K_EFAULT);
    {
        S32 r = invoke(265, 0, tsAt, 0);
        S32 realSec = (S32)context.memory->readd(tsAt);
        S32 m = invoke(265, 1, ts2At, 0);
        S32 monoSec = (S32)context.memory->readd(ts2At);
        if (r != 0 || m != 0 || monoSec >= realSec) {
            testFail("timing32 clock domains wrong: real=%d (%d) mono=%d (%d)", r, realSec, m, monoSec);
        }
    }
    // clock_gettime64 (403).
    expectEq("clock_gettime64 clk7", invoke(403, 7, tsAt, 0), 0);
    expectEq("clock_gettime64 clk99", invoke(403, 99, tsAt, 0), -K_EINVAL);
    // clock_getres (266).
    expectEq("clock_getres clk99", invoke(266, 99, tsAt, 0), -K_EINVAL);
    expectEq("clock_getres null tp", invoke(266, 0, 0, 0), 0);
    {
        S32 r = invoke(266, 0, tsAt, 0);
        U32 nsec = context.memory->readd(tsAt + 4);
        if (r != 0 || nsec != 1) {
            testFail("timing32 clock_getres clk0: ret=%d nsec=%u (expected 0/1)", r, nsec);
        }
        r = invoke(266, 5, tsAt, 0);
        nsec = context.memory->readd(tsAt + 4);
        if (r != 0 || nsec != 1000000) {
            testFail("timing32 clock_getres clk5: ret=%d nsec=%u (expected 0/1000000)", r, nsec);
        }
    }
    // nanosleep (162).
    expectEq("nanosleep null req", invoke(162, 0, 0, 0), -K_EFAULT);
    expectEq("nanosleep nsec 1e9", invokeTs(162, tsAt, 0, 0, 1000000000), -K_EINVAL);
    expectEq("nanosleep sec -1", invokeTs(162, tsAt, 0, -1, 0), -K_EINVAL);
    expectEq("nanosleep 1ms", invokeTs(162, tsAt, 0, 0, 1000000), 0);
    // clock_nanosleep (267): (clock, flags, req, rem); TIMER_ABSTIME
    // used to be refused outright on this side.
    expectEq("clock_nanosleep null req", invoke(267, 0, 0, 0), -K_EFAULT);
    expectEq("clock_nanosleep clk3", invokeTs(267, 3, 0, 0, 0), -K_EOPNOTSUPP);
    expectEq("clock_nanosleep clk99", invokeTs(267, 99, 0, 0, 0), -K_EINVAL);
    expectEq("clock_nanosleep flags2 rel0", invokeTs(267, 0, 2, 0, 0), 0);
    {
        S32 r = invoke(265, 1, ts2At, 0); // monotonic now
        S32 sec = (S32)context.memory->readd(ts2At);
        S32 nsec = (S32)context.memory->readd(ts2At + 4);
        nsec += 30000000;
        if (nsec >= 1000000000) { sec++; nsec -= 1000000000; }
        auto t0 = std::chrono::steady_clock::now();
        S32 s = invokeTs(267, 1, 1 /* TIMER_ABSTIME */, sec, nsec);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (r != 0 || s != 0 || ms < 15) {
            testFail("timing32 clock_nanosleep abstime: setup=%d ret=%d elapsed=%lld ms", r, s, (long long)ms);
        }
    }
    // getrusage (77).
    expectEq("getrusage who2", invoke(77, 2, bufAt, 0), -K_EINVAL);
    expectEq("getrusage who99", invoke(77, 99, bufAt, 0), -K_EINVAL);
    expectEq("getrusage null usage", invoke(77, 0, 0, 0), -K_EFAULT);
    expectEq("getrusage who99+null (order)", invoke(77, 99, 0, 0), -K_EINVAL);
    expectEq("getrusage self", invoke(77, 0, bufAt, 0), 0);
    // gettimeofday (78) with both pointers null is a plain success and
    // must not write through the null tv.
    expectEq("gettimeofday null null", invoke(78, 0, 0, 0), 0);
    // setitimer (104) / getitimer (105): itimerval32 layout
    // {interval.sec, interval.usec, value.sec, value.usec} as S32 pairs.
    {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(104);
        testPushCode8(0xCD); testPushCode8(0x80);
        stageFixtures();
        context.memory->writed(bufAt + 8, 0);            // value.sec
        context.memory->writed(bufAt + 12, 1000000);     // value.usec
        context.cpu->reg[3].u32 = 0;
        context.cpu->reg[1].u32 = bufAt;
        context.cpu->reg[2].u32 = 0;
        testRunCPU();
        expectEq("setitimer usec 1e6", (S32)context.cpu->reg[0].u32, -K_EINVAL);
    }
    {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(104);
        testPushCode8(0xCD); testPushCode8(0x80);
        stageFixtures();
        context.memory->writed(bufAt + 8, (U32)-1);      // value.sec = -1
        context.memory->writed(bufAt + 12, 0);
        context.cpu->reg[3].u32 = 0;
        context.cpu->reg[1].u32 = bufAt;
        context.cpu->reg[2].u32 = 0;
        testRunCPU();
        expectEq("setitimer sec -1", (S32)context.cpu->reg[0].u32, -K_EINVAL);
    }
    expectEq("setitimer VIRTUAL pin", invoke(104, 1, bufAt, 0), -K_EINVAL);
    expectEq("getitimer which3 null (which first)", invoke(105, 3, 0, 0), -K_EINVAL);
    // epoll_create (254) / epoll_create1 (329) argument validation.
    expectEq("epoll_create size0", invoke(254, 0, 0, 0), -K_EINVAL);
    expectEq("epoll_create1 flags1", invoke(329, 1, 0, 0), -K_EINVAL);
    if (invoke(329, 0, 0, 0) < 0) {
        testFail("timing32 epoll_create1(0) failed");
    }
    // i386 gap pins: these four are dispatched on the 64-bit side but
    // have no 32-bit dispatch entry at all. ppoll with no descriptors
    // and a zero timeout would answer 0 if it were implemented.
    expectEq("ppoll gap pin", invoke(309, 0, 0, tsAt), -K_ENOSYS);
    expectEq("epoll_pwait gap pin", invoke(319, 0, bufAt, 1, 0, 0, 8), -K_ENOSYS);
    expectEq("rt_sigtimedwait gap pin", invoke(177, bufAt, bufAt, tsAt, 8), -K_ENOSYS);
}

#endif
#endif
