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


// FD-205: an identical fault redelivered forever must be contained.
// A guest handler that returns to the faulting instruction without
// fixing the fault makes the emulator re-deliver the identical fault
// (observed past 5.5M deliveries on 32-bit, and as a 64-line
// guest-memfault32 loop in a live session); after
// BW64_FAULT_LOOP_LIMIT identical deliveries the fault takes the
// no-handler path -- the process is ended and the faulting thread
// unwinds -- instead of being delivered again. seg_mapper throws 2
// on a delivery (unwind to the signal handler) and 1 on the
// no-handler path; the test simulates the guest handler's sigreturn
// after each delivery (EIP back to the faulting instruction, ESP back
// to its pre-frame value so the signal frames do not overflow the
// fixture stack). Each case faults a throwaway process, never the
// shared fixture process.

// A fixture with a real, mapped stack. makeFaultFixture's stack sits at
// TEST_STACK_ADDRESS (0xE0000000), above the process mmap limit
// (0xC0000000), so its mmap silently fails and the stack is unmapped --
// a signal frame cannot be built there (the first frame write faults
// and, with a handler installed, is delivered instead of mapping the
// page). Map a stack below the limit and point ESP/EBP at it; the frame
// is then built on a mapped page (demand allocation populates it).
FaultFixture makeContainmentFixture() {
    FaultFixture f = makeFaultFixture();
    const U32 stackBase = 0x70000000;
    U32 r = f.memory->mmap(f.thread, stackBase, 0x4000,
                           K_PROT_READ | K_PROT_WRITE,
                           K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    if (r != stackBase)
        testFail("could not map a fixture stack (mmap=0x%x)", r);
    f.cpu->reg[4].u32 = stackBase + 0x4000 - 16; // ESP
    f.cpu->reg[5].u32 = stackBase + 0x4000 - 16; // EBP
    // Warm the page the signal frame will be built on.
    f.memory->writed(stackBase + 0x4000 - 16, 0x11223344);
    if (f.memory->readd(stackBase + 0x4000 - 16) != 0x11223344)
        testFail("fixture stack page did not warm up");
    return f;
}

void testFaultLoopContainment32() {
    TestContext& shared = testContext();
    const U32 faultAddr = 0x60000000;
    const U32 otherAddr = 0x60000004;
    auto installHandler = [](FaultFixture& f) {
        KSigAction& segv = f.process->sigActions[K_SIGSEGV];
        segv.reset();
        memset(segv.sigInfo, 0, sizeof(segv.sigInfo));
        segv.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
        segv.flags = K_SA_SIGINFO;
    };
    // Main case: 128 identical deliveries, then the 129th is contained.
    {
        FaultFixture f = makeContainmentFixture();
        KThread::setCurrentThread(f.thread);
        installHandler(f);
        const U32 origEsp = f.cpu->reg[4].u32;
        const U32 origEip = f.cpu->eip.u32;
        for (U32 i = 0; i < BW64_FAULT_LOOP_LIMIT; i++) {
            f.cpu->eip.u32 = origEip;
            f.cpu->reg[4].u32 = origEsp;
            int caught = 0;
            try {
                f.thread->seg_mapper(faultAddr, true, false, true);
            } catch (int e) {
                caught = e;
            }
            if (caught != 2)
                testFail("identical fault did not deliver before the loop limit (delivery %u, caught=%d)", i + 1, caught);
            if (f.thread->terminating)
                testFail("identical fault ended the process before the loop limit (delivery %u)", i + 1);
            if (f.cpu->eip.u32 != TEST_CODE_ADDRESS + 0x100)
                testFail("fault delivery did not transfer to the handler (delivery %u, eip=0x%x)", i + 1, f.cpu->eip.u32);
        }
        // One more identical delivery must take the no-handler path.
        f.cpu->eip.u32 = origEip;
        f.cpu->reg[4].u32 = origEsp;
        int caught = 0;
        try {
            f.thread->seg_mapper(faultAddr, true, false, true);
        } catch (int e) {
            caught = e;
        }
        if (caught != 1)
            testFail("identical fault past the loop limit did not unwind the faulting thread (caught=%d)", caught);
        if (!f.thread->terminating)
            testFail("identical fault past the loop limit did not end the guest process");
        if (f.thread->faultLoopCount != BW64_FAULT_LOOP_LIMIT + 1)
            testFail("fault loop counter is %u, expected %u", f.thread->faultLoopCount, BW64_FAULT_LOOP_LIMIT + 1);
    }
    // Reset case: a different fault resets the count, so the original
    // fault delivers again instead of tripping at the limit.
    {
        FaultFixture f = makeContainmentFixture();
        KThread::setCurrentThread(f.thread);
        installHandler(f);
        const U32 origEsp = f.cpu->reg[4].u32;
        const U32 origEip = f.cpu->eip.u32;
        auto deliver = [&](U32 addr) {
            f.cpu->eip.u32 = origEip;
            f.cpu->reg[4].u32 = origEsp;
            int caught = 0;
            try {
                f.thread->seg_mapper(addr, true, false, true);
            } catch (int e) {
                caught = e;
            }
            return caught;
        };
        for (U32 i = 0; i < BW64_FAULT_LOOP_LIMIT - 1; i++) {
            if (deliver(faultAddr) != 2)
                testFail("reset-case fault did not deliver (delivery %u)", i + 1);
        }
        // A different fault resets the count to 1 for its own key.
        if (deliver(otherAddr) != 2)
            testFail("a different fault did not deliver");
        // The original fault now delivers again (its count restarted),
        // instead of tripping as delivery 129 would have.
        if (deliver(faultAddr) != 2)
            testFail("original fault did not deliver after the count reset");
        if (f.thread->terminating)
            testFail("original fault ended the process after the count reset");
    }
    KThread::setCurrentThread(shared.thread);
}

#endif
#endif
