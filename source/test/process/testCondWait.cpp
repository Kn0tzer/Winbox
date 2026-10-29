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

#include "kpoll.h"
#include "../cpu/testCPU.h"

#include <atomic>
#include <chrono>
#include <thread>

// Pins for the lifecycle of KThread::condStartWaitTime, the single deadline
// anchor shared by internal_poll_impl (kpoll.cpp) and KThread::sleep
// (kthread.cpp). Audit summary (docs/reports/cond-start-wait-time-audit-20261028.md):
//
//  * The anchor is set on a wait's first pass and read on every later pass
//    to compute the elapsed time against the ORIGINAL timeout. Every
//    terminal exit of both waits (ready, timeout==0, deadline reached,
//    EINTR) resets it to 0.
//  * The -K_CONTINUE exit (signal delivery) deliberately does NOT reset
//    it: the dispatcher leaves the guest EIP on the syscall instruction,
//    the signal frame saves/restores the anchor around the handler
//    (KThread::runSignal / onExitSignal), and the re-executed syscall
//    resumes the SAME deadline. Resetting it there would restart the
//    full timeout after every signal. Scenario 3 pins this.
//  * The epoll-restart exits of internal_poll_impl likewise keep the
//    anchor on purpose (KEPoll::wait rebuilds its snapshot and re-enters
//    with the same deadline; its own comment says so).
//  * A leaked anchor (restart abandoned, e.g. a handler that never
//    returns through sigreturn) can shorten at most ONE later wait:
//    the first wait that observes it takes a terminal exit and resets
//    it. Scenario 4 pins that bound (no persistent busy-spin).
//  * Scenario 1 is the regression guard for the one live defect the
//    audit found: KThread::sleep subtracted the TOTAL elapsed time from
//    an already-reduced timeout on every in-invocation wake
//    (ms -= diff), double-counting elapsed time after any non-terminal
//    wake (a waitingCond kick that is not a signal delivery, a spurious
//    condition wake). internal_poll_impl was already fixed for exactly
//    this pattern (fc6818ec1, "Keep the original deadline"); sleep now
//    recomputes the remaining time from the anchor the same way.

namespace {

U64 hostMillies() {
    return (U64)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Wake whatever condition `thread` is currently parked on, the same way
// the cross-thread tgkill kick (syscall64.cpp) and terminateOtherThread
// do: signal thread->waitingCond WITHOUT setting startSignal and without
// queueing a signal. For a wait this is a non-terminal wake -- it must
// resume for the remainder of its original deadline.
void kickWaitingCond(KThread* thread) {
    BOXEDWINE_CONDITION cond = thread->waitingCond;
    if (cond) {
        cond->lock();
        cond->signal();
        cond->unlock();
    }
}

bool waitUntilParked(KThread* thread, U32 timeoutMs) {
    U64 deadline = hostMillies() + timeoutMs;
    while (hostMillies() < deadline) {
        if (thread->waitingCond) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return thread->waitingCond != nullptr;
}

void kickRepeatedly(KThread* thread, int count) {
    for (int i = 0; i < count; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kickWaitingCond(thread);
    }
}

} // namespace

void testCondStartWaitTime() {
    TestContext& context = testContext();
    KThread* thread = context.thread;
    const U32 savedAnchor = thread->condStartWaitTime;
    const bool savedStartSignal = thread->startSignal;

    // ---- Scenario 1 (REGRESSION GUARD): KThread::sleep keeps its
    // original deadline across repeated non-terminal wakes.
    // Pre-fix trace: sleep(2000) woken every ~100 ms returns at ~600 ms
    // because each wake subtracts the total elapsed time again.
    {
        thread->condStartWaitTime = 0;
        thread->startSignal = false;
        std::atomic<S32> rc{-1};
        std::atomic<U64> elapsed{0};
        std::thread sleeper([&]() {
            KThread::setCurrentThread(thread);
            U64 t0 = hostMillies();
            rc.store((S32)thread->sleep(2000));
            elapsed.store(hostMillies() - t0);
        });
        if (!waitUntilParked(thread, 2000)) {
            testFail("sleep deadline: sleeper never parked on its wait condition");
        } else {
            kickRepeatedly(thread, 8);
        }
        sleeper.join();
        if (rc.load() != 0) {
            testFail("sleep deadline: sleep(2000) returned %d, expected 0", rc.load());
        }
        if (elapsed.load() < 1400) {
            testFail("sleep deadline: sleep(2000) returned after %llu ms under repeated "
                     "non-terminal wakes; elapsed time was double-counted and the "
                     "deadline shortened", (unsigned long long)elapsed.load());
        }
        if (elapsed.load() > 6000) {
            testFail("sleep deadline: sleep(2000) took %llu ms, far past its deadline",
                     (unsigned long long)elapsed.load());
        }
        if (thread->condStartWaitTime != 0) {
            testFail("sleep deadline: condStartWaitTime not reset after a completed sleep");
        }
    }

    // ---- Scenario 2 (pin): internal_poll_impl already keeps its
    // original deadline across the same wakes; guard against regressing
    // the pattern sleep was aligned to. A 0-fd poll is a pure timed wait
    // on pollCond.
    {
        thread->condStartWaitTime = 0;
        thread->startSignal = false;
        KPollData dummy = {};
        std::atomic<S32> rc{-1};
        std::atomic<U64> elapsed{0};
        std::thread waiter([&]() {
            KThread::setCurrentThread(thread);
            U64 t0 = hostMillies();
            rc.store(internal_poll(thread, &dummy, 0, 2000));
            elapsed.store(hostMillies() - t0);
        });
        if (!waitUntilParked(thread, 2000)) {
            testFail("poll deadline: waiter never parked on its wait condition");
        } else {
            kickRepeatedly(thread, 8);
        }
        waiter.join();
        if (rc.load() != 0) {
            testFail("poll deadline: poll(2000) returned %d, expected 0", rc.load());
        }
        if (elapsed.load() < 1400) {
            testFail("poll deadline: poll(2000) returned after %llu ms under repeated "
                     "non-terminal wakes", (unsigned long long)elapsed.load());
        }
        if (elapsed.load() > 6000) {
            testFail("poll deadline: poll(2000) took %llu ms, far past its deadline",
                     (unsigned long long)elapsed.load());
        }
        if (thread->condStartWaitTime != 0) {
            testFail("poll deadline: condStartWaitTime not reset after a completed poll");
        }
    }

    // ---- Scenario 3 (pin): a signal-interrupted poll returns
    // -K_CONTINUE with the anchor PRESERVED, and the re-executed wait
    // consumes that same deadline (returns at once once it has fully
    // elapsed, resetting the anchor on its terminal exit).
    {
        thread->condStartWaitTime = 0;
        thread->startSignal = false;
        KPollData dummy = {};
        std::atomic<S32> rc{0};
        std::thread waiter([&]() {
            KThread::setCurrentThread(thread);
            rc.store(internal_poll(thread, &dummy, 0, 30000));
        });
        if (!waitUntilParked(thread, 2000)) {
            testFail("-K_CONTINUE pin: waiter never parked on its wait condition");
            thread->startSignal = true;
            kickWaitingCond(thread);
            waiter.join();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            thread->startSignal = true;
            kickWaitingCond(thread);
            waiter.join();
            if (rc.load() != -K_CONTINUE) {
                testFail("-K_CONTINUE pin: signal-interrupted poll returned %d, "
                         "expected -K_CONTINUE (%d)", rc.load(), -K_CONTINUE);
            }
            U32 anchor = thread->condStartWaitTime;
            if (!anchor) {
                testFail("-K_CONTINUE pin: condStartWaitTime was reset by the "
                         "-K_CONTINUE exit; a restarted wait would lose its "
                         "original deadline and restart the full timeout");
            } else {
                U32 age = KSystem::getMilliesSinceStart() - anchor;
                if (age > 10000) {
                    testFail("-K_CONTINUE pin: preserved anchor is %u ms old, not "
                             "the interrupted wait's start", age);
                }
                // Re-entry with a timeout the preserved deadline has
                // already covered (~150 ms elapsed of a 100 ms request):
                // must return immediately instead of starting a fresh wait.
                U64 t0 = hostMillies();
                S32 rc2 = internal_poll(thread, &dummy, 0, 100);
                U64 el2 = hostMillies() - t0;
                if (rc2 != 0) {
                    testFail("-K_CONTINUE pin: re-entered poll returned %d, expected 0",
                             rc2);
                }
                if (el2 > 1000) {
                    testFail("-K_CONTINUE pin: re-entered poll ignored the preserved "
                             "deadline (took %llu ms)", (unsigned long long)el2);
                }
                if (thread->condStartWaitTime != 0) {
                    testFail("-K_CONTINUE pin: re-entered poll did not reset the "
                             "anchor on its terminal exit");
                }
            }
        }
    }

    // ---- Scenario 4 (characterization pin): a stale anchor leaked by
    // an abandoned restart shortens at most ONE later wait. The first
    // wait that observes it takes a terminal exit and resets it, so no
    // persistent busy-spin is possible through poll/sleep themselves.
    {
        thread->condStartWaitTime = KSystem::getMilliesSinceStart() - 10000;
        U64 t0 = hostMillies();
        U32 rcA = thread->sleep(300);
        U64 elA = hostMillies() - t0;
        if (rcA != 0 || elA > 150) {
            testFail("leaked anchor: sleep(300) with a 10 s stale anchor returned "
                     "%u after %llu ms (expected an immediate 0)", rcA,
                     (unsigned long long)elA);
        }
        if (thread->condStartWaitTime != 0) {
            testFail("leaked anchor: stale anchor was not consumed/reset by the "
                     "first wait that observed it");
        }
        t0 = hostMillies();
        U32 rcB = thread->sleep(300);
        U64 elB = hostMillies() - t0;
        if (rcB != 0 || elB < 220) {
            testFail("leaked anchor: second sleep(300) returned %u after %llu ms; "
                     "a leaked anchor must not shorten more than one wait",
                     rcB, (unsigned long long)elB);
        }
        if (thread->condStartWaitTime != 0) {
            testFail("leaked anchor: condStartWaitTime not reset after a normal sleep");
        }
    }

    thread->condStartWaitTime = savedAnchor;
    thread->startSignal = savedStartSignal;
}

#endif // __TEST
