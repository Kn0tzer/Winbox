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

#include "kerror.h"
#include "../cpu/testCPU.h"

#include <atomic>
#include <chrono>
#include <thread>

// Lost-wakeup scenarios for the 32-bit futex (KThread::futex), the wait
// machinery every 32-bit guest thread parks on (Wine's alert futex,
// WaitOnAddress and glibc locks inside 32-bit processes such as the
// Chromium demo). The 64-bit futex path already carries the three
// protections these scenarios pin (commits 791c6e59, 5b25df22, cbf36b367):
//
//  * a WAKE must flag a registered slot even if the waiter has not set
//    its `waiting` flag yet (the alloc -> park window);
//  * a WAKE that arrives BEFORE the wait (the no-word-change "pure
//    signal" futex: the word is 0 on both sides, so the waiter's word
//    re-check can never fire) must be remembered and consumed by the
//    next WAIT, which returns a spurious EWOULDBLOCK instead of parking
//    on a signal that already happened;
//  * the pending record is consumed exactly once, so a later WAIT on
//    the same address parks normally again.
//
// Scenario 1 is deterministic: pre-fix the WAIT parks for its full
// timeout and returns ETIMEDOUT; post-fix it returns EWOULDBLOCK
// immediately. Scenario 3 hammers the alloc -> park window from two
// host threads and counts lost wakes (a lost wake parks until the
// round timeout, so it is visible as both a wrong result and a stall).

namespace {

constexpr U32 FUTEX_WAIT_OP = 0;
constexpr U32 FUTEX_WAKE_OP = 1;
constexpr U32 FUTEX_PRIVATE = 128;

U64 hostMillies() {
    return (U64)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void writeTimeout(KMemory* memory, U32 addr, U32 ms) {
    memory->writed(addr, ms / 1000);
    memory->writed(addr + 4, (ms % 1000) * 1000000);
}

} // namespace

void testFutex32WakeRace() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KMemory* memory = context.memory;
    KThread* main = context.thread;

    const U32 tsAddr = TEST_HEAP_ADDRESS + 0x700;
    const U32 alertWord = TEST_HEAP_ADDRESS + 0x800;
    const U32 parkWord = TEST_HEAP_ADDRESS + 0x804;
    const U32 raceBase = TEST_HEAP_ADDRESS + 0x900;

    // ---- Scenario 1 (deterministic): WAKE before WAIT on a
    // no-word-change alert futex. The wake finds nobody parked (returns
    // 0), the word stays 0, and the WAIT that follows must NOT park.
    memory->writed(alertWord, 0);
    U32 woke = main->futex(alertWord, FUTEX_WAKE_OP | FUTEX_PRIVATE, 1, 0, 0, 0, false);
    if (woke != 0) {
        testFail("futex32 wake-before-wait: WAKE with no waiter returned %u, expected 0", woke);
    }
    writeTimeout(memory, tsAddr, 400);
    U64 start = hostMillies();
    S32 result = (S32)main->futex(alertWord, FUTEX_WAIT_OP | FUTEX_PRIVATE, 0, tsAddr, 0, 0, false);
    U64 elapsed = hostMillies() - start;
    if (result != -K_EWOULDBLOCK) {
        testFail("futex32 wake-before-wait: WAIT after a lost WAKE returned %d after %llums, "
                 "expected -EWOULDBLOCK immediately (the wake was lost and the waiter parked)",
                 result, (unsigned long long)elapsed);
    } else if (elapsed > 200) {
        testFail("futex32 wake-before-wait: WAIT returned EWOULDBLOCK only after %llums "
                 "(parked first; pending wake was not consumed up front)",
                 (unsigned long long)elapsed);
    }
    // The pending wake is consumed exactly once: a second WAIT on the
    // same word parks normally and times out.
    writeTimeout(memory, tsAddr, 300);
    start = hostMillies();
    result = (S32)main->futex(alertWord, FUTEX_WAIT_OP | FUTEX_PRIVATE, 0, tsAddr, 0, 0, false);
    elapsed = hostMillies() - start;
    if (result != -K_ETIMEDOUT || elapsed < 250) {
        testFail("futex32 wake-before-wait: second WAIT returned %d after %llums, "
                 "expected -ETIMEDOUT after ~300ms (pending wake must be consumed once)",
                 result, (unsigned long long)elapsed);
    }
    // Word-mismatch WAIT still fails fast (the pending machinery must
    // not turn real mismatches into parks or vice versa).
    memory->writed(alertWord, 5);
    result = (S32)main->futex(alertWord, FUTEX_WAIT_OP | FUTEX_PRIVATE, 0, tsAddr, 0, 0, false);
    if (result != -K_EWOULDBLOCK) {
        testFail("futex32: word-mismatch WAIT returned %d, expected -EWOULDBLOCK", result);
    }
    memory->writed(alertWord, 0);

    // ---- Scenario 2 (sanity): a parked waiter is woken by a later WAKE.
    KThread* worker = process->createThread();
    std::atomic<bool> waiterDone{false};
    S32 waitResult = -999;
    U64 waitElapsed = 0;
    memory->writed(parkWord, 0);
    std::thread waiter([&]() {
        KThread::setCurrentThread(worker);
        writeTimeout(memory, tsAddr + 0x40, 5000);
        U64 t0 = hostMillies();
        waitResult = (S32)worker->futex(parkWord, FUTEX_WAIT_OP | FUTEX_PRIVATE, 0, tsAddr + 0x40, 0, 0, false);
        waitElapsed = hostMillies() - t0;
        waiterDone.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    U32 woke2 = main->futex(parkWord, FUTEX_WAKE_OP | FUTEX_PRIVATE, 1, 0, 0, 0, false);
    waiter.join();
    if (!waiterDone.load() || woke2 != 1 || waitResult != 0 || waitElapsed > 2000) {
        testFail("futex32: parked waiter was not woken (woke=%u result=%d elapsed=%llums)",
                 woke2, waitResult, (unsigned long long)waitElapsed);
    }

    // ---- Scenario 3 (stress): race each round's single WAKE against
    // the waiter's registration. Any interleaving is legal EXCEPT the
    // waiter parking until its timeout: the wake must either flag the
    // registered slot or be remembered for the waiter to consume.
    const int rounds = 120;
    int lost = 0;
    for (int round = 0; round < rounds; round++) {
        const U32 word = raceBase + (U32)(round % 8) * 4;
        memory->writed(word, 0);
        std::atomic<bool> ready{false};
        std::atomic<bool> done{false};
        S32 roundResult = -999;
        U64 roundElapsed = 0;
        std::thread roundWaiter([&]() {
            KThread::setCurrentThread(worker);
            writeTimeout(memory, tsAddr + 0x80, 600);
            ready.store(true, std::memory_order_release);
            U64 t0 = hostMillies();
            roundResult = (S32)worker->futex(word, FUTEX_WAIT_OP | FUTEX_PRIVATE, 0, tsAddr + 0x80, 0, 0, false);
            roundElapsed = hostMillies() - t0;
            done.store(true, std::memory_order_release);
        });
        while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
        // Vary the wake's landing point inside the waiter's entry path.
        if (round % 4 == 1) std::this_thread::sleep_for(std::chrono::microseconds(50));
        if (round % 4 == 2) std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (round % 4 == 3) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        main->futex(word, FUTEX_WAKE_OP | FUTEX_PRIVATE, 1, 0, 0, 0, false);
        roundWaiter.join();
        if (!done.load() || roundResult == -K_ETIMEDOUT || roundElapsed > 500) {
            lost++;
        }
    }
    if (lost) {
        testFail("futex32 wake race: %d of %d rounds lost the wake (waiter parked to timeout)", lost, rounds);
    }

    process->removeThread(worker);
    delete worker;
}

#endif
