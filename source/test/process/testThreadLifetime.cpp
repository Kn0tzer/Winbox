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

#include "../cpu/testCPU.h"
#include "kscheduler.h"
#include "ksignal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

// FD-44: guest platform threads could outlive the thread state they
// execute on. The convicted shape (core dump in the FD-41/FD-42 lane's
// evidence): one host thread delivers a signal to a guest thread --
// KProcess::signalProcess iterating the thread table and running the
// target's signal machinery, or KSystem::tgkill waiting inside
// KThread::signal on the target's conditions -- while the target's own
// platform thread retires it through KProcess::deleteThread, which
// freed the KThread (and its CPU) outright. The delivery thread kept
// executing on the freed object.
//
// This test recreates that race with the product's own entry points:
//   * "victim" host threads play the platform-thread role: they park
//     their guest thread in a blocking sleep, then retire it exactly
//     the way a platform thread does when guest code exits
//     (terminateCurrentThread, then KProcess::deleteThread), each at a
//     staggered time so frees land one by one.
//   * "signaler" host threads hammer the cross-thread delivery entry
//     points the whole time teardown is running: dedicated threads
//     loop KProcess::signalProcess (the table-iterating delivery from
//     the core dump) and others loop KSystem::tgkill with SIGQUIT --
//     the wait variant, wineserver's pattern -- through fresh
//     thread-table lookups.
//   * the main thread allocates and frees bare thread state
//     (KThread + CPU, the cpu.cpp tmpThread pattern) for the whole
//     generation, so a freed victim's memory is recycled by the same
//     allocator arena that created the victims while a delivery
//     against it is still in flight.
//   * generations repeat the whole cycle.
//
// Handlers are installed for both signals so delivery takes the full
// frame-building path (the runSignal -> CPU::fillFlags site from the
// core dump) instead of the default terminate disposition. The guest
// threads are never scheduled; only their kernel objects live and die,
// which is exactly the lifetime under test.
//
// Phase 2 is the orderly counterpart: parked threads are torn down
// through terminateOtherThread in shuffled order (the killAllThreads /
// exitgroup shape) and must all retire.

namespace {

U64 hostMillies() {
    return (U64)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

const U32 VICTIM_STACK_BASE = 0x71000000;
const U32 VICTIM_STACK_SIZE = 0x10000; // 64 KB per victim
const U32 HANDLER_PAGE = 0x72000000;

struct LifetimeFixture {
    KProcessPtr process;
    KMemory* memory = nullptr;
    std::vector<KThread*> victims;

    bool setup(U32 victimCount) {
        process = KProcess::create();
        memory = KMemory::create(process.get());
        process->memory = memory;
        KThread* first = process->createThread();
        // One shared stack region (a slice per victim) and a handler
        // page; runSignal builds frames on the target's stack and
        // needs it mapped.
        memory->mmap(first, VICTIM_STACK_BASE, victimCount * VICTIM_STACK_SIZE,
                     K_PROT_READ | K_PROT_WRITE, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
        memory->mmap(first, HANDLER_PAGE, K_PAGE_SIZE,
                     K_PROT_READ | K_PROT_WRITE | K_PROT_EXEC, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
        process->sigActions[K_SIGQUIT].handlerAndSigAction = HANDLER_PAGE;
        process->sigActions[K_SIGUSR1].handlerAndSigAction = HANDLER_PAGE;
        victims.push_back(first);
        for (U32 i = 1; i < victimCount; i++) {
            victims.push_back(process->createThread());
        }
        for (U32 i = 0; i < victimCount; i++) {
            victims[i]->cpu->reg[4].u32 = VICTIM_STACK_BASE + (i + 1) * VICTIM_STACK_SIZE - 0x40;
        }
        return true;
    }

    void teardownRegistration() {
        if (process) {
            KSystem::eraseProcess(process->id);
        }
    }
};

// The platform-thread role: park until the generation ends, wait out a
// per-victim stagger so the frees land one at a time, then retire this
// thread through the product teardown entry points. Between parks the
// victim completes any delivered signal exactly the way a running
// guest thread does when its handler returns through sigreturn
// (onExitSignal: inSignal drops, the stack pointer is restored, and
// the handler-end condition is signaled for any thread waiting in
// KThread::signal). An unscheduled thread cannot run the handler
// itself; this is the bookkeeping the delivery side observes, and it
// is what lets deliveries keep taking the frame-building path instead
// of piling up behind the in-handler mask.
void victimMain(KProcessPtr process, KThread* thread, U32 stackTop, U32 staggerMs, std::atomic<bool>* stop) {
    KThread::setCurrentThread(thread);
    while (!stop->load(std::memory_order_acquire) && !thread->terminating) {
        if (thread->inSignal > 0) {
            thread->inSignal = 0;
            thread->cpu->reg[4].u32 = stackTop;
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(thread->waitingForSignalToEndCond);
            BOXEDWINE_CONDITION_SIGNAL_ALL(thread->waitingForSignalToEndCond);
        }
        thread->sleep(5);
    }
    if (staggerMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(staggerMs));
    }
    terminateCurrentThread(thread);
    process->deleteThread(thread);
    KThread::setCurrentThread(nullptr);
}

// The table-iterating delivery role (the core dump's path).
void signalProcessMain(KProcessPtr process, std::atomic<bool>* stop) {
    while (!stop->load(std::memory_order_acquire)) {
        process->signalProcess(K_SIGUSR1);
    }
}

// The thread-directed delivery role: fresh table lookup per call.
void tgkillMain(KProcessPtr process, std::vector<U32> tids, std::atomic<bool>* stop) {
    while (!stop->load(std::memory_order_acquire)) {
        for (U32 tid : tids) {
            if (stop->load(std::memory_order_acquire)) {
                break;
            }
            KSystem::tgkill(process->id, tid, K_SIGQUIT);
        }
    }
}

} // namespace

// Phase 2's platform-thread role: park on a real condition wait (the
// condition's wait primitive is what publishes waitingCond, the
// condition terminateOtherThread kicks), re-parking after any wake
// that is not the end, then retire through the product teardown entry
// points when terminated.
void phase2VictimMain(KProcessPtr process, KThread* thread, std::atomic<bool>* stop) {
    KThread::setCurrentThread(thread);
    std::shared_ptr<BoxedWineCondition> cond =
        std::make_shared<BoxedWineCondition>(B("fd44 phase2 park"));
    while (!stop->load(std::memory_order_acquire) && !thread->terminating) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(cond);
        BOXEDWINE_CONDITION_WAIT(cond);
    }
    if (thread->terminating) {
        terminateCurrentThread(thread);
        process->deleteThread(thread);
    }
}

void testThreadLifetimeTeardown() {
    std::mt19937 rng(0xFD44);

    // A private, unregistered process purely as the churn allocator's
    // owner (no /proc nodes, no thread table entries).
    KProcessPtr churnProcess = std::make_shared<KProcess>(0xF444);
    churnProcess->memory = KMemory::create(churnProcess.get());

    // ---- Phase 1: signal delivery racing thread teardown ----
    const U32 GENERATIONS = 48;
    const U32 VICTIMS = 8;
    for (U32 gen = 0; gen < GENERATIONS; gen++) {
        LifetimeFixture fx;
        fx.setup(VICTIMS);
        std::vector<U32> tids;
        for (KThread* t : fx.victims) {
            tids.push_back(t->id);
        }
        std::atomic<bool> stopVictims{false};
        std::atomic<bool> stopSignalers{false};
        std::vector<std::thread> victimThreads;
        for (U32 i = 0; i < fx.victims.size(); i++) {
            victimThreads.emplace_back(victimMain, fx.process, fx.victims[i],
                                       VICTIM_STACK_BASE + (i + 1) * VICTIM_STACK_SIZE - 0x40,
                                       i * 15, &stopVictims);
        }
        std::vector<std::thread> signalers;
        signalers.emplace_back(signalProcessMain, fx.process, &stopSignalers);
        signalers.emplace_back(signalProcessMain, fx.process, &stopSignalers);
        signalers.emplace_back(tgkillMain, fx.process, tids, &stopSignalers);
        signalers.emplace_back(tgkillMain, fx.process, tids, &stopSignalers);
        // Let deliveries get dense, then end the victims: every victim
        // retires itself, staggered, while deliveries are still landing.
        // The main thread churns thread-state allocations the whole
        // window (same arena that allocated the victims), so freed
        // victims are recycled under in-flight deliveries.
        {
            U64 churnUntil = hostMillies() + 90 + (rng() % 120);
            while (hostMillies() < churnUntil) {
                KThread* t = new KThread(0, churnProcess);
                delete t;
            }
        }
        stopVictims.store(true, std::memory_order_release);
        for (auto& th : victimThreads) {
            th.join();
        }
        stopSignalers.store(true, std::memory_order_release);
        for (auto& th : signalers) {
            th.join();
        }
        if (fx.process->getThreadCount() != 0) {
            testFail("FD-44 phase 1 gen %u: %u threads left in the table after teardown",
                     gen, fx.process->getThreadCount());
        }
        fx.teardownRegistration();
        // fx.process drops here; the KProcess destructor runs its own
        // teardown on an empty table.
    }

    // ---- Phase 2: any-order teardown through terminateOtherThread ----
    for (U32 gen = 0; gen < 4; gen++) {
        LifetimeFixture fx;
        fx.setup(VICTIMS);
        std::atomic<bool> neverStop{false};
        std::vector<std::thread> victimThreads;
        for (KThread* t : fx.victims) {
            victimThreads.emplace_back(phase2VictimMain, fx.process, t, &neverStop);
        }
        // Give every victim time to park on its condition wait
        // (waitingCond set). A parked victim stays parked until
        // kicked, so each one just needs to be observed once.
        U64 deadline = hostMillies() + 20000;
        std::vector<char> seenParked(fx.victims.size(), 0);
        bool allParked = false;
        while (hostMillies() < deadline && !allParked) {
            allParked = true;
            for (size_t i = 0; i < fx.victims.size(); i++) {
                if (fx.victims[i]->waitingCond) {
                    seenParked[i] = 1;
                }
                if (!seenParked[i]) {
                    allParked = false;
                }
            }
            if (!allParked) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (!allParked) {
            testFail("FD-44 phase 2 gen %u: victims never parked", gen);
        }
        std::vector<U32> order;
        for (KThread* t : fx.victims) {
            order.push_back(t->id);
        }
        std::shuffle(order.begin(), order.end(), rng);
        for (U32 tid : order) {
            terminateOtherThread(fx.process, tid);
        }
        for (auto& th : victimThreads) {
            th.join();
        }
        if (fx.process->getThreadCount() != 0) {
            testFail("FD-44 phase 2 gen %u: %u threads left in the table after teardown",
                     gen, fx.process->getThreadCount());
        }
        fx.teardownRegistration();
    }

}

#endif
