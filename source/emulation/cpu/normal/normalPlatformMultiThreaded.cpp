/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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
#include "knativesystem.h"
#include "ripSampler.h"
#ifdef BOXEDWINE_JIT
#include "../jit/jitCodeLifecycle.h"
#endif
#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#endif

#if defined(BOXEDWINE_MULTI_THREADED)

std::atomic<int> platformThreadCount = 0;
void platformInitExceptionHandling();

static void platformThread(CPU* cpu) {
#ifdef BOXEDWINE_HOST_EXCEPTIONS
    platformInitExceptionHandling();
#endif
    KThread::setCurrentThread(cpu->thread);
    // FD-128: register this host thread's identity so currentThread()
    // can recover it if the runningThread TLS slot is overwritten (see
    // KThread::currentThread). The guard unregisters on every exit path.
    KThread::PlatformThreadIdentityRegistration identityRegistration(cpu->thread);
    KProcessPtr process = KSystem::getProcess(cpu->thread->process->id);

#ifdef BOXEDWINE_GUEST_X64
    // 64-bit guest: this host thread drives the CPU64 interpreter, not the
    // 32-bit CPU. The 32-bit `cpu` is still allocated (and carries the
    // thread/signal bookkeeping) but its decoder/run-loop must never touch a
    // 64-bit address space — getNextOp() would decode the (truncated) 32-bit
    // eip and page-fault at the real 64-bit entry. Branch before any 32-bit
    // setup runs.
    if (cpu->thread->process->is64Bit &&
        (cpu->thread->cpu64 || cpu->thread->process->cpu64)) {
        // Each guest thread runs on its OWN host thread here, so it must drive
        // its OWN per-thread CPU64 (thread->cpu64) — NOT the process-wide one.
        // The main thread's cpu64 is the same object as process->cpu64; cloned
        // threads (clone64) get a distinct CPU64 sharing memory64. Driving the
        // shared process->cpu64 from every host thread would race two threads
        // through one register file and corrupt guest state.
        //
        // BW64_RIPSAMPLE: claim a sampler registry slot for this host thread so
        // the background sampler can find a guest thread spinning in guest code
        // (a wine busy-loop that makes no syscalls). Returns -1 when disabled, in
        // which case publish/release below are no-ops.
        int ripSlot = ripSamplerAcquireSlot((int)cpu->thread->process->id, (int)cpu->thread->id);
        while (true) {
            // Re-fetch the CPU64 each iteration: execve() tears down the old
            // per-thread CPU64 and loadProgram64 installs a fresh one (new RIP /
            // registers / memory64) on thread->cpu64. Caching it before the loop
            // would keep running the FREED pre-execve CPU64 after a re-exec —
            // which manifested as wine64's post-execve image never executing a
            // single instruction (0 syscalls) and hanging. Always read the
            // live per-thread pointer (main thread: == process->cpu64).
            CPU64* cpu64 = cpu->thread->cpu64 ? cpu->thread->cpu64
                                              : cpu->thread->process->cpu64;
            // Republish the live CPU64 every iteration: execve frees the old one
            // and installs a fresh thread->cpu64, so the slot must track the
            // pointer we're about to run (overwriting the freed pre-execve one
            // before run() touches it). No-op when disabled (ripSlot==-1).
            ripSamplerPublish(ripSlot, cpu64);
            cpu64->yield = false;
            try {
                cpu64->run();
            } catch (...) {
                // CPU64 has no nextOp to re-arm; loop re-enters run().
            }
            if (cpu->thread->process->terminated) {
                BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(cpu->memory->mutex);
                cpu->memory->cleanup();
            }
            if (cpu->thread->terminating) {
                break;
            }
            // CPU64::run() returns on a clean exit syscall (yield=true) with
            // no pending work; if the process isn't terminating, there's
            // nothing left for this thread to do, so leave the loop.
            if (cpu64->yield) {
                break;
            }
        }
        ripSamplerReleaseSlot(ripSlot);
        process->deleteThread(cpu->thread);
        if (--platformThreadCount == 0) {
            KSystem::shutingDown = true;
            KNativeSystem::postQuit();
        }
        return;
    }
#endif

    int ripSlot32 = ripSamplerAcquireSlot((int)cpu->thread->process->id, (int)cpu->thread->id);
    ripSamplerPublish32(ripSlot32, cpu);
    // FD-41: a guest thread whose first fetch faults fatally (entry page
    // unmapped in its address space, no guest handler) unwinds out of the
    // fault machinery with a bare throw. That unwind must never escape
    // this function's frame: this is a std::thread entry point, and an
    // exception leaving it calls std::terminate, taking the whole
    // emulator (or the whole native test suite) down for one guest
    // thread's fault. Contain it here: the fault machinery has already
    // ended the guest process, so retire this thread below instead of
    // entering a run loop that has no first instruction to run.
    try {
        cpu->nextOp = cpu->getNextOp();
        if (!cpu->nextOp) {
            // FD-271: a thread terminated before its first fetch (exit_group
            // or exec racing thread creation) has no first instruction to
            // run -- its null fetch is a retirement, not a fault. Treating
            // it as a fault delivers a signal for a valid address, and the
            // panic that follows kills the whole guest. Retire silently;
            // the block below already retires a thread with no first op.
            if (cpu->thread->terminating) {
                cpu->nextOp = nullptr;
            } else {
                cpu->thread->seg_mapper(cpu->getEipAddress(), true, false, false);
                cpu->nextOp = cpu->getNextOp();
                if (!cpu->nextOp) {
                    kpanic_fmt("Failed to get first op for thread %d of process %d at address %x", cpu->thread->id, process->id, cpu->getEipAddress());
                }
            }
        }
    } catch (...) {
        cpu->thread->terminating = true;
        cpu->nextOp = nullptr;
    }
    if (!cpu->nextOp) {
        // No first instruction (fatal entry fault above): retire the
        // thread exactly the way a finished thread retires, rather than
        // dereferencing a null nextOp in the loop below.
        ripSamplerReleaseSlot(ripSlot32);
#ifdef __TEST
        process->removeThread(cpu->thread);
        return;
#else
        process->deleteThread(cpu->thread);
        if (--platformThreadCount == 0) {
            KSystem::shutingDown = true;
            KNativeSystem::postQuit();
        }
        return;
#endif
    }

    while (true) {
        try {
            cpu->run();
        } catch (...) {
            if (!cpu->thread->terminating) {
                try {
                    cpu->nextOp = cpu->getNextOp();
                } catch (...) {
                    // getNextOp failed (e.g., invalid EIP after signal handling)
                    // Terminate the thread gracefully instead of crashing
                    cpu->thread->terminating = true;
                    cpu->nextOp = nullptr;
                }
            }
        }
#ifdef __TEST
        if (!cpu->nextOp) {
            // FD-41: the run above unwound and no next op could be
            // decoded (the catch handler leaves nextOp null when it
            // terminates the thread). Retire the thread like a TestEnd
            // instead of dereferencing the null op in the check below,
            // which faulted the host thread itself.
            ripSamplerReleaseSlot(ripSlot32);
            process->removeThread(cpu->thread);
            return;
        }
        if (cpu->nextOp->inst == TestEnd) {
            // The guest thread's program ended: unregister it from the
            // process's live-thread table before leaving. Without this,
            // the KThread stays in the table forever and
            // terminateOtherThread (killAllThreads/exitgroup) waits
            // indefinitely for its removal.
            ripSamplerReleaseSlot(ripSlot32);
            process->removeThread(cpu->thread);
            return;
        }
        continue;
#else
        if (cpu->thread->process->terminated) {
            BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(cpu->memory->mutex);
            cpu->memory->cleanup();
        }
        if (cpu->thread->terminating) {
            break;
        }
#endif
    }

    ripSamplerReleaseSlot(ripSlot32);
    process->deleteThread(cpu->thread);
    if (--platformThreadCount == 0) {
        KSystem::shutingDown = true;
        KNativeSystem::postQuit();
    }
}

#ifdef __TEST
void initThreadForTesting() {
}

void joinThread(KThread* thread) {
    std::thread* cppThread = (std::thread*)thread->cpu->nativeHandle;
    cppThread->join();
    delete cppThread;
}
#endif

void platformSetThreadDescription(KThread* thread);

void scheduleThread(KThread* thread) {
    platformThreadCount++;
    CPU* cpu = thread->cpu;
#ifdef BOXEDWINE_JIT
    // A JIT backend may have process-lifetime state to prepare before the host
    // thread can execute compiled guest blocks (WASM JIT: per-worker table).
    jitThreadStartPreparing(cpu);
#endif
#ifdef __TEST
    cpu->nativeHandle = (U64)new std::thread(platformThread, cpu);
#else
    std::thread cppThread = std::thread(platformThread, cpu);
    cpu->nativeHandle = (U64)cppThread.native_handle();
#if defined(_DEBUG) && defined(BOXEDWINE_MSVC)
    platformSetThreadDescription(thread);
#endif
    cppThread.detach();
#endif
    if (!thread->process->isSystemProcess() && KSystem::cpuAffinityCountForApp) {
        Platform::setCpuAffinityForThread(thread, KSystem::cpuAffinityCountForApp);
    }
}

void terminateOtherThread(const KProcessPtr& process, U32 threadId) {
    // FD-44: hold a reference while marking and waking the target --
    // it can retire (and release the table's reference) concurrently.
    KThread* thread = process->getThreadByIdRef(threadId);
    if (thread) {
        thread->terminating = true;

        const std::shared_ptr<BoxedWineCondition> cond = thread->waitingCond;

        // wake up the thread if it is waiting
        if (cond) {
            cond->lock();
            cond->signal();
            cond->unlock();
        }
        thread->releaseRef();
    }

    while (true) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(process->threadRemovedCondition);
        if (!process->getThreadById(threadId)) {
            break;
        }
        BOXEDWINE_CONDITION_WAIT_TIMEOUT(process->threadRemovedCondition, 1000);
    }
}

void terminateCurrentThread(KThread* thread) {
    thread->terminating = true;
}

void unscheduleThread(KThread* thread) {
}

#endif
