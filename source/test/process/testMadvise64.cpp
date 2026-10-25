/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License; either version 2
 *  of the License, or (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"
#include "ksystem.h"
#include "syscall64.h"
#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"

// madvise: MADV_REMOVE must actually discard contents, the fork-inheritance
// advice (DONTFORK/WIPEONFORK) must be recorded and honoured by fork, and
// the locked-page interaction must hold: DONTNEED/REMOVE on an mlocked page
// fail with EINVAL while DONTNEED_LOCKED discards and unlocks.
void testMadvise64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 base = 0x77300000ULL;
    constexpr U64 sharedBase = 0x77900000ULL;
    if (memory->mmapAnonymousFixed(base, K64_PAGE_SIZE * 6, 3) != base ||
        memory->mmapSharedFile(sharedBase, K64_PAGE_SIZE, 3, 0x24681357, "/proc/test/madvise-shared",
                               0, nullptr, 0) != sharedBase) {
        testFail("x64 madvise test could not map fixture pages");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3 = 0) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto madvise = [&](U64 addr, U64 advice) -> S64 { return invoke(28, addr, K64_PAGE_SIZE, advice); };

    // MADV_REMOVE (9) on a private anonymous page discards its contents.
    memory->writed(base, 0x11223344);
    if (madvise(base, 9) != 0 || memory->readd(base) != 0) {
        testFail("x64 madvise REMOVE did not discard private page contents");
    }
    // DONTNEED (4) on an mlocked page is EINVAL; DONTNEED_LOCKED (24)
    // discards the contents and drops the lock.
    const U64 page1 = base + K64_PAGE_SIZE;
    memory->writed(page1, 0x55667788);
    if (invoke(149, page1, K64_PAGE_SIZE) != 0) { // mlock
        testFail("x64 madvise test could not mlock a page");
        return;
    }
    if (madvise(page1, 4) != -K_EINVAL || memory->readd(page1) != 0x55667788) {
        testFail("x64 madvise DONTNEED did not refuse a locked page");
    }
    if (madvise(page1, 24) != 0 || memory->readd(page1) != 0 || madvise(page1, 4) != 0) {
        testFail("x64 madvise DONTNEED_LOCKED did not discard and unlock the page");
    }
    // REMOVE on a locked page is likewise EINVAL until it is unlocked.
    const U64 page2 = base + K64_PAGE_SIZE * 2;
    memory->writed(page2, 0x99AABBCC);
    if (invoke(149, page2, K64_PAGE_SIZE) != 0 || madvise(page2, 9) != -K_EINVAL ||
        invoke(150, page2, K64_PAGE_SIZE) != 0 || madvise(page2, 9) != 0 || memory->readd(page2) != 0) {
        testFail("x64 madvise REMOVE did not honour the locked-page rule");
    }
    // WIPEONFORK is only valid on private anonymous pages; DONTFORK accepts
    // shared pages and DOFORK clears it again.
    if (madvise(sharedBase, 18) != -K_EINVAL || madvise(sharedBase, 10) != 0 || madvise(sharedBase, 11) != 0) {
        testFail("x64 madvise fork advice did not validate shared ranges");
    }

    // Fork inheritance: a DONTFORK page is absent from the child, a
    // WIPEONFORK page is present but zeroed, a normal page is copied, and
    // mlock state is not inherited. Advice/lock bits in K64Page::flags:
    // LOCKED 0x80, DONTFORK 0x100, WIPEONFORK 0x200.
    constexpr U64 forkBase = 0x77A00000ULL;
    constexpr U64 forkCode = 0x77B00000ULL;
    if (memory->mmapAnonymousFixed(forkBase, K64_PAGE_SIZE * 3, 3) != forkBase ||
        memory->mmapAnonymousFixed(forkCode, K64_PAGE_SIZE, 3) != forkCode) {
        testFail("x64 madvise test could not map fork fixtures");
        return;
    }
    memory->writeb(forkCode, 0xEB); // jmp $: keep the fork child alive
    memory->writeb(forkCode + 1, 0xFE);
    const U64 pageA = forkBase, pageB = forkBase + K64_PAGE_SIZE, pageC = forkBase + K64_PAGE_SIZE * 2;
    memory->writed(pageA, 0xAAAA0001);
    memory->writed(pageB, 0xBBBB0002);
    memory->writed(pageC, 0xCCCC0003);
    if (madvise(pageB, 10) != 0 || madvise(pageC, 18) != 0 || invoke(149, pageA, K64_PAGE_SIZE) != 0) {
        testFail("x64 madvise test could not set fork advice");
        return;
    }
    std::shared_ptr<FsNode> oldProcNode = KSystem::procNode;
    if (!KSystem::procNode) {
        KSystem::procNode = std::make_shared<FsFileNode>(0, 0, B("/proc"), B(""), B(""), true, false, nullptr);
    }
    CPU64 forkCpu(memory);
    forkCpu.thread = context.thread;
    CPU64* savedProcessCpu = process->cpu64;
    CPU64* savedThreadCpu = context.thread->cpu64;
    process->cpu64 = &forkCpu;
    context.thread->cpu64 = &forkCpu;
    forkCpu.rip = forkCode;
    forkCpu.reg[X64_RAX].setU64(57); // __NR_fork
    ksyscall64(&forkCpu);
    process->cpu64 = savedProcessCpu;
    context.thread->cpu64 = savedThreadCpu;
    const S64 childPid = (S64)forkCpu.reg[X64_RAX].u64;
    if (childPid <= 0) {
        testFail("x64 madvise fork failed: %lld", (long long)childPid);
        KSystem::procNode = oldProcNode;
        return;
    }
    KProcessPtr child = KSystem::getProcess((U32)childPid);
    if (!child || !child->memory64) {
        testFail("x64 madvise fork child is missing");
    } else {
        KMemory64* childMemory = child->memory64;
        const U32 flagsA = childMemory->getPageFlags(pageA >> K64_PAGE_SHIFT);
        if (!(flagsA & K64_PAGE_MAPPED) || childMemory->readd(pageA) != 0xAAAA0001 || (flagsA & 0x80)) {
            testFail("x64 fork child did not inherit the normal page unlocked (flags=%X)", flagsA);
        }
        if (childMemory->getPageFlags(pageB >> K64_PAGE_SHIFT) != 0) {
            testFail("x64 fork child inherited a MADV_DONTFORK page");
        }
        const U32 flagsC = childMemory->getPageFlags(pageC >> K64_PAGE_SHIFT);
        if (!(flagsC & K64_PAGE_MAPPED) || childMemory->readd(pageC) != 0 || (flagsC & 0x200)) {
            testFail("x64 fork child did not receive a wiped WIPEONFORK page (flags=%X)", flagsC);
        }
    }
    if (memory->readd(pageB) != 0xBBBB0002 || memory->readd(pageC) != 0xCCCC0003) {
        testFail("x64 fork changed the parent madvise fixture pages");
    }
    if (child) {
        child->killAllThreads(nullptr);
        child->exitCode = 37;
        child->terminated = true;
        CPU64 waitCpu(memory);
        waitCpu.thread = context.thread;
        waitCpu.reg[X64_RAX].setU64(61); // wait4
        waitCpu.reg[X64_RDI].setU64((U64)childPid);
        waitCpu.reg[X64_RSI].setU64(base);
        waitCpu.reg[X64_RDX].setU64(0);
        ksyscall64(&waitCpu);
        if (KSystem::getProcess((U32)childPid)) KSystem::eraseProcess((U32)childPid);
    }
    KSystem::procNode = oldProcNode;
}

#endif
#endif
