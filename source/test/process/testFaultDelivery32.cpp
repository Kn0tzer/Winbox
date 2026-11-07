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
 *  along with this program, if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "ksignal.h"
#include "../cpu/testCPU.h"

// Guards the 32-bit fault-delivery cleanup (cleanup campaign,
// 2026-10-30): a synchronous guest fault with no guest handler must
// end the guest process -- runSignal's default disposition is a
// SIGKILL for the process -- and return/unwind normally. It must
// never kpanic: in the default configuration kpanic exits the whole
// emulator. On the pre-cleanup code every case below kills this test
// binary outright, so the suite itself is the sabotage proof. Each
// case faults a throwaway process, never the shared fixture process;
// throwaway processes are leaked deliberately so a killed process's
// teardown cannot run underneath the rest of the suite.

namespace {

std::vector<KProcessPtr>& keepAliveFaultProcesses() {
    static std::vector<KProcessPtr> processes;
    return processes;
}

struct FaultFixture {
    KProcessPtr process;
    KMemory* memory = nullptr;
    KThread* thread = nullptr;
    CPU* cpu = nullptr;
};

FaultFixture makeFaultFixture() {
    FaultFixture f;
    f.process = KProcess::create();
    f.memory = KMemory::create(f.process.get());
    f.process->memory = f.memory;
    f.thread = f.process->createThread();
    f.cpu = f.thread->cpu;
    // Mapped stack/code pages so the fatal-fault post-mortem
    // (logPageFault's stack walk) reads mapped memory, and all signal
    // dispositions start at their default.
    f.memory->mmap(f.thread, TEST_STACK_ADDRESS - 0x4000, 0x4000,
                   K_PROT_READ | K_PROT_WRITE, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    f.memory->mmap(f.thread, TEST_CODE_ADDRESS, 0x1000,
                   K_PROT_READ | K_PROT_WRITE | K_PROT_EXEC, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    f.cpu->eip.u32 = TEST_CODE_ADDRESS;
    f.cpu->reg[4].u32 = TEST_STACK_ADDRESS - 16; // ESP
    f.cpu->reg[5].u32 = TEST_STACK_ADDRESS - 16; // EBP
    keepAliveFaultProcesses().push_back(f.process);
    return f;
}

} // namespace

void testFaultDelivery32() {
    TestContext& shared = testContext();

    // Divide by zero with SIGFPE at its default disposition.
    {
        FaultFixture f = makeFaultFixture();
        KThread::setCurrentThread(f.thread);
        f.cpu->prepareException(EXCEPTION_DIVIDE, 0);
        if (!f.thread->terminating) {
            testFail("32-bit divide fault with no handler did not end the guest process");
        }
    }

    // INT3 (signalTrap) with SIGTRAP at its default disposition.
    {
        FaultFixture f = makeFaultFixture();
        KThread::setCurrentThread(f.thread);
        f.thread->signalTrap(1); // TRAP_BRKPT
        if (!f.thread->terminating) {
            testFail("32-bit INT3 with no handler did not end the guest process");
        }
    }

    // Page fault on an unmapped address with SIGSEGV at its default
    // disposition: delivery fails, the post-mortem runs, the process
    // is ended and the faulting thread unwinds with throw 1 (the same
    // unwind the scheduler already catches for recovery-mode panics).
    {
        FaultFixture f = makeFaultFixture();
        KThread::setCurrentThread(f.thread);
        int caught = 0;
        try {
            f.thread->seg_mapper(0x60000000, true, false, true);
        } catch (int e) {
            caught = e;
        }
        if (caught != 1) {
            testFail("32-bit page fault with no handler did not unwind the faulting thread (caught=%d)", caught);
        }
        if (!f.thread->terminating) {
            testFail("32-bit page fault with no handler did not end the guest process");
        }
    }

    // On-demand mapping of the 32-bit Wine syscall_frame window: a
    // read of the still-unmapped window answers the fresh page's
    // zero-filled content (seg_mapper maps it and returns).
    {
        FaultFixture f = makeFaultFixture();
        KThread::setCurrentThread(f.thread);
        if (f.memory->readb(0x76F040) != 0) {
            testFail("read of a fresh syscall_frame window page did not answer 0");
        }
    }

    // A write to the still-unmapped window makes seg_mapper map the
    // page and return; the write must still land. The InvalidPage
    // accessors used to return without re-dispatching, silently
    // dropping it (frontier review D5 audit).
    {
        FaultFixture f = makeFaultFixture();
        KThread::setCurrentThread(f.thread);
        f.memory->writeb(0x76F040, 0xAB);
        if (f.memory->readb(0x76F040) != 0xAB) {
            testFail("first write to the syscall_frame window was dropped (read back 0x%x)", f.memory->readb(0x76F040));
        }
        f.memory->writed(0x76F048, 0x11223344);
        if (f.memory->readd(0x76F048) != 0x11223344) {
            testFail("writed to the mapped syscall_frame window did not land");
        }
        f.memory->writew(0x76F050, 0xBEEF);
        if (f.memory->readw(0x76F050) != 0xBEEF) {
            testFail("writew to the mapped syscall_frame window did not land");
        }
    }

    KThread::setCurrentThread(shared.thread);
}

#endif
#endif
