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

#include "kerror.h"
#include "ksignal.h"
#include "ktimer.h"
#include "../cpu/testCPU.h"

#include <chrono>
#include <thread>

void runTimers();

// Expiration probes driven through the real KProcess marshalling (not the
// KTimer helpers the older tests use): a relative CLOCK_MONOTONIC timerfd
// armed through timerfd_settime must actually expire, a CLOCK_REALTIME
// timerfd must still expire, and setitimer must queue SIGALRM, round-trip
// through getitimer, disarm cleanly, and reject ITIMER_VIRTUAL/PROF with
// -EINVAL instead of kpanic'ing the emulator.

static void writeTimespec32(KMemory* memory, U32 address, U64 sec, U32 nsec) {
    memory->writed(address, (U32)sec);
    memory->writed(address + 4, nsec);
}

static void writeItimerspec32(KMemory* memory, U32 address, U64 valueSec, U32 valueNsec,
                              U64 intervalSec, U32 intervalNsec) {
    writeTimespec32(memory, address, intervalSec, intervalNsec);
    writeTimespec32(memory, address + 8, valueSec, valueNsec);
}

static void writeItimerval32(KMemory* memory, U32 address, U32 valueSec, U32 valueUsec,
                             U32 intervalSec, U32 intervalUsec) {
    memory->writed(address, intervalSec);
    memory->writed(address + 4, intervalUsec);
    memory->writed(address + 8, valueSec);
    memory->writed(address + 12, valueUsec);
}

static void pumpForMs(int ms) {
    // The guest main loop pumps timers continuously; model that with
    // short slices instead of one pump at the end, so periodic timers
    // accumulate one expiration per period.
    int remaining = ms;
    while (remaining > 0) {
        int slice = remaining > 5 ? 5 : remaining;
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        runTimers();
        remaining -= slice;
    }
}

void testTimerfdProcessAbiProbes() {
    TestContext& context = testContext();
    KProcess* process = context.process.get();
    KMemory* memory = context.memory;
    constexpr U32 spec = TEST_HEAP_ADDRESS + 0x500;
    constexpr U32 readBack = TEST_HEAP_ADDRESS + 0x540;

    // --- CLOCK_MONOTONIC relative one-shot through timerfd_settime ---
    S32 fd = process->timerfd_create(1 /* CLOCK_MONOTONIC */, K_O_NONBLOCK | K_O_CLOEXEC);
    if (fd < 0) {
        testFail("timerfd_create(CLOCK_MONOTONIC) failed: %d", fd);
        return;
    }
    std::shared_ptr<KFileDescriptor> descriptor = process->getFileDescriptor(fd);
    writeItimerspec32(memory, spec, 0, 30 * 1000 * 1000, 0, 0); // 30 ms one-shot
    U32 result = process->timerfd_settime(fd, 0, spec, 0);
    if (result != 0) {
        testFail("timerfd_settime(MONOTONIC relative) failed: %d", (S32)result);
    }
    // Before expiry a nonblocking read reports EAGAIN.
    U64 expirations = 0xdead;
    result = descriptor->kobject->readNative((U8*)&expirations, 8);
    if ((S32)result != -K_EWOULDBLOCK) {
        testFail("timerfd read before expiry returned %d, expected -EWOULDBLOCK", (S32)result);
    }
    pumpForMs(60);
    result = descriptor->kobject->readNative((U8*)&expirations, 8);
    if (result != 8 || expirations != 1) {
        testFail("MONOTONIC timerfd did not expire once through the process ABI: read=%d count=%llu",
                 (S32)result, (unsigned long long)expirations);
    }
    // One-shot: a second read is EAGAIN again, and gettime reports disarmed.
    result = descriptor->kobject->readNative((U8*)&expirations, 8);
    if ((S32)result != -K_EWOULDBLOCK) {
        testFail("one-shot timerfd read after expiry returned %d, expected -EWOULDBLOCK", (S32)result);
    }
    result = process->timerfd_gettime(fd, readBack);
    if (result != 0 || memory->readd(readBack + 8) != 0 || memory->readd(readBack + 12) != 0) {
        testFail("expired one-shot timerfd gettime did not report a disarmed value");
    }
    process->close(fd);

    // --- CLOCK_REALTIME relative still expires (regression pin) ---
    fd = process->timerfd_create(0 /* CLOCK_REALTIME */, K_O_NONBLOCK);
    if (fd < 0) {
        testFail("timerfd_create(CLOCK_REALTIME) failed: %d", fd);
        return;
    }
    descriptor = process->getFileDescriptor(fd);
    writeItimerspec32(memory, spec, 0, 30 * 1000 * 1000, 0, 0);
    result = process->timerfd_settime(fd, 0, spec, 0);
    if (result != 0) {
        testFail("timerfd_settime(REALTIME relative) failed: %d", (S32)result);
    }
    pumpForMs(60);
    expirations = 0;
    result = descriptor->kobject->readNative((U8*)&expirations, 8);
    if (result != 8 || expirations != 1) {
        testFail("REALTIME timerfd did not expire once: read=%d count=%llu",
                 (S32)result, (unsigned long long)expirations);
    }
    process->close(fd);

    // --- Periodic timerfd accumulates expirations ---
    fd = process->timerfd_create(1 /* CLOCK_MONOTONIC */, K_O_NONBLOCK);
    if (fd < 0) {
        testFail("timerfd_create(MONOTONIC periodic) failed: %d", fd);
        return;
    }
    descriptor = process->getFileDescriptor(fd);
    writeItimerspec32(memory, spec, 0, 25 * 1000 * 1000, 0, 25 * 1000 * 1000); // 25 ms period
    result = process->timerfd_settime(fd, 0, spec, 0);
    if (result != 0) {
        testFail("timerfd_settime(periodic) failed: %d", (S32)result);
    }
    pumpForMs(90);
    expirations = 0;
    result = descriptor->kobject->readNative((U8*)&expirations, 8);
    if (result != 8 || expirations < 2) {
        testFail("periodic timerfd accumulated %llu expirations, expected at least 2",
                 (unsigned long long)expirations);
    }
    // Disarm: value 0 stops further expirations.
    writeItimerspec32(memory, spec, 0, 0, 0, 0);
    result = process->timerfd_settime(fd, 0, spec, 0);
    if (result != 0) {
        testFail("timerfd_settime(disarm) failed: %d", (S32)result);
    }
    descriptor->kobject->readNative((U8*)&expirations, 8); // drain
    pumpForMs(60);
    expirations = 0xdead;
    result = descriptor->kobject->readNative((U8*)&expirations, 8);
    if ((S32)result != -K_EWOULDBLOCK) {
        testFail("disarmed timerfd still expired: read=%d count=%llu",
                 (S32)result, (unsigned long long)expirations);
    }
    process->close(fd);
}

void testSetitimerProbes() {
    TestContext& context = testContext();
    KProcess* process = context.process.get();
    KMemory* memory = context.memory;
    // Block SIGALRM on the test thread: expiry then queues the signal in
    // the process pending mask (the Linux behaviour for a blocked signal),
    // which is what these probes assert. With the signal unblocked and no
    // handler installed, the emulator takes the default-terminate path,
    // which cannot complete inside the single-threaded test harness.
    context.thread->sigMask |= (1ULL << (K_SIGALRM - 1));
    constexpr U32 value = TEST_HEAP_ADDRESS + 0x580;
    constexpr U32 oldValue = TEST_HEAP_ADDRESS + 0x5c0;

    process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));

    // ITIMER_REAL one-shot queues SIGALRM on expiry.
    writeItimerval32(memory, value, 0, 30 * 1000, 0, 0); // 30 ms
    U32 result = process->setitimer(0 /* ITIMER_REAL */, value, oldValue);
    if (result != 0) {
        testFail("setitimer(ITIMER_REAL) failed: %d", (S32)result);
        return;
    }
    if (process->pendingSignals & (1ULL << (K_SIGALRM - 1))) {
        testFail("SIGALRM was pending before the itimer expired");
    }
    // getitimer reports a live countdown close to the armed value.
    result = process->getitimer(0 /* ITIMER_REAL */, oldValue);
    U32 remainUsec = memory->readd(oldValue + 8) * 1000000 + memory->readd(oldValue + 12);
    if (result != 0 || remainUsec == 0 || remainUsec > 30000) {
        testFail("getitimer(ITIMER_REAL) wrong remaining time: result=%d usec=%u", (S32)result, remainUsec);
    }
    pumpForMs(60);
    if (!(process->pendingSignals & (1ULL << (K_SIGALRM - 1)))) {
        testFail("setitimer(ITIMER_REAL) never queued SIGALRM");
    }
    process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));

    // Periodic itimer: the interval round-trips through getitimer and the
    // signal refires after the next period.
    writeItimerval32(memory, value, 0, 25 * 1000, 0, 25 * 1000);
    result = process->setitimer(0 /* ITIMER_REAL */, value, 0);
    if (result != 0) {
        testFail("setitimer(periodic) failed: %d", (S32)result);
    }
    result = process->getitimer(0 /* ITIMER_REAL */, oldValue);
    if (result != 0 || memory->readd(oldValue) != 0 || memory->readd(oldValue + 4) != 25000) {
        testFail("getitimer did not round-trip the 25 ms interval: sec=%u usec=%u",
                 memory->readd(oldValue), memory->readd(oldValue + 4));
    }
    pumpForMs(40);
    if (!(process->pendingSignals & (1ULL << (K_SIGALRM - 1)))) {
        testFail("periodic setitimer did not queue SIGALRM on its first expiry");
    }
    process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));
    pumpForMs(40);
    if (!(process->pendingSignals & (1ULL << (K_SIGALRM - 1)))) {
        testFail("periodic setitimer did not refire after its interval");
    }
    process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));

    // Disarm stops delivery.
    writeItimerval32(memory, value, 0, 0, 0, 0);
    result = process->setitimer(0 /* ITIMER_REAL */, value, 0);
    if (result != 0) {
        testFail("setitimer(disarm) failed: %d", (S32)result);
    }
    pumpForMs(50);
    if (process->pendingSignals & (1ULL << (K_SIGALRM - 1))) {
        testFail("disarmed setitimer still queued SIGALRM");
    }

    // CPU-time timers are honestly rejected, not a kpanic.
    writeItimerval32(memory, value, 0, 1000, 0, 0);
    result = process->setitimer(1 /* ITIMER_VIRTUAL */, value, 0);
    if ((S32)result != -K_EINVAL) {
        testFail("setitimer(ITIMER_VIRTUAL) returned %d, expected -EINVAL", (S32)result);
    }
    result = process->setitimer(2 /* ITIMER_PROF */, value, 0);
    if ((S32)result != -K_EINVAL) {
        testFail("setitimer(ITIMER_PROF) returned %d, expected -EINVAL", (S32)result);
    }
    result = process->getitimer(1 /* ITIMER_VIRTUAL */, oldValue);
    if ((S32)result != -K_EINVAL) {
        testFail("getitimer(ITIMER_VIRTUAL) returned %d, expected -EINVAL", (S32)result);
    }
    result = process->setitimer(99, value, 0);
    if ((S32)result != -K_EINVAL) {
        testFail("setitimer(bad which) returned %d, expected -EINVAL", (S32)result);
    }
    context.thread->sigMask &= ~(1ULL << (K_SIGALRM - 1));
    process->pendingSignals &= ~(1ULL << (K_SIGALRM - 1));
}

#endif
