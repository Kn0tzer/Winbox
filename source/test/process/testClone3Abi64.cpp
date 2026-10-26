/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "ksystem.h"
#include "ksignal.h"
#include "../cpu/testCPU.h"

void testClone3Abi64() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x77100000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 clone3 test could not map clone_args");
        return;
    }
    memory->memsetGuest(page, 0, K64_PAGE_SIZE);
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 args, U64 size) -> S64 {
        cpu.reg[X64_RAX].setU64(435); // __NR_clone3
        cpu.reg[X64_RDI].setU64(args);
        cpu.reg[X64_RSI].setU64(size);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    const S64 shortResult = invoke(page, 63);
    const S64 largeResult = invoke(page, 4097);
    const S64 pointerResult = invoke(0x7fff000000000000ULL, 88);
    if (shortResult != -K_EINVAL || largeResult != -K_E2BIG || pointerResult != -K_EFAULT) {
        testFail("x64 clone3 size/pointer validation returned %lld, %lld, %lld",
                 (long long)shortResult, (long long)largeResult, (long long)pointerResult);
    }
    memory->writed(page + 33, 65); // exit_signal outside the Linux signal range for the unaligned struct
    const S64 signalResult = invoke(page + 1, 64); // clone_args need not be naturally aligned
    if (signalResult != -K_EINVAL) testFail("x64 clone3 exit_signal validation returned %lld", (long long)signalResult);
    memory->writed(page + 33, 0);
    memory->writeb(page + 88, 1); // nonzero bytes in an unknown future extension
    const S64 extensionResult = invoke(page, 96);
    if (extensionResult != -K_E2BIG) testFail("x64 clone3 extension validation returned %lld", (long long)extensionResult);
    memory->writeb(page + 88, 0);
    memory->writeq(page, 17); // CSIGNAL is forbidden in clone3.flags
    const S64 flagsResult = invoke(page, 88);
    if (flagsResult != -K_EINVAL) testFail("x64 clone3 flags validation returned %lld", (long long)flagsResult);
    memory->writeq(page, 0x1000); // CLONE_PIDFD requires a writable pidfd output pointer
    const S64 pidfdResult = invoke(page, 88);
    if (pidfdResult != -K_EFAULT) testFail("x64 clone3 accepted a null CLONE_PIDFD output pointer");
    // ---- Round 3A: feature validation and the remaining clone_args fields.
    // A child wrongly created by a case that must be rejected is spinning on
    // a jmp $ fixture page; reap any such child so the suite stays clean.
    constexpr U64 codePage = page + K64_PAGE_SIZE;
    if (!memory->getPageFlags(codePage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(codePage, K64_PAGE_SIZE, 3) != codePage) {
        testFail("x64 clone3 test could not map a child code page");
        return;
    }
    memory->writeb(codePage, 0xEB); // jmp $
    memory->writeb(codePage + 1, 0xFE);
    auto invokeClone3 = [&](bool parentHandler) -> S64 {
        CPU64 cloneCpu(memory);
        cloneCpu.thread = context.thread;
        if (parentHandler) cloneCpu.sigActions[K_SIGUSR1].handler = 0x12345000;
        CPU64* savedProcessCpu = context.process->cpu64;
        CPU64* savedThreadCpu = context.thread->cpu64;
        context.process->cpu64 = &cloneCpu;
        context.thread->cpu64 = &cloneCpu;
        cloneCpu.rip = codePage;
        cloneCpu.reg[X64_RAX].setU64(435);
        cloneCpu.reg[X64_RDI].setU64(page);
        cloneCpu.reg[X64_RSI].setU64(88);
        ksyscall64(&cloneCpu);
        context.process->cpu64 = savedProcessCpu;
        context.thread->cpu64 = savedThreadCpu;
        return (S64)cloneCpu.reg[X64_RAX].u64;
    };
    auto reapChild = [&](S64 pid) {
        KProcessPtr child = KSystem::getProcess((U32)pid);
        if (!child) {
            testFail("x64 clone3 child %lld vanished before reap", (long long)pid);
            return;
        }
        child->killAllThreads(nullptr);
        child->exitCode = 37;
        child->terminated = true;
        CPU64 waitCpu(memory);
        waitCpu.thread = context.thread;
        waitCpu.reg[X64_RAX].setU64(61); // wait4
        waitCpu.reg[X64_RDI].setU64((U64)pid);
        waitCpu.reg[X64_RSI].setU64(page + 0x100);
        waitCpu.reg[X64_RDX].setU64(0);
        ksyscall64(&waitCpu);
        if (KSystem::getProcess((U32)pid)) KSystem::eraseProcess((U32)pid);
    };
    auto runCase = [&](auto&& setup) -> S64 {
        memory->memsetGuest(page, 0, 88);
        setup();
        const S64 result = invokeClone3(false);
        if (result > 0) reapChild(result); // wrongly created: clean up, caller reports
        return result;
    };
    // Namespace creation and unknown flag bits are refused, not dropped.
    if (runCase([&] { memory->writeq(page, 0x20000); }) != -K_EINVAL) // CLONE_NEWNS
        testFail("x64 clone3 did not reject CLONE_NEWNS");
    if (runCase([&] { memory->writeq(page, 0x10000000000ULL); }) != -K_EINVAL) // unknown bit 40
        testFail("x64 clone3 did not reject an unknown flag bit");
    // Flag combination rules: CLONE_THREAD needs CLONE_VM|CLONE_SIGHAND, and
    // CLONE_SIGHAND needs CLONE_VM.
    if (runCase([&] {
            memory->writeq(page, 0x10000 | 0x100); // THREAD|VM, no SIGHAND
            memory->writeq(page + 40, page + 0x8000);
            memory->writeq(page + 48, 0x1000);
        }) != -K_EINVAL)
        testFail("x64 clone3 accepted CLONE_THREAD without CLONE_SIGHAND");
    if (runCase([&] { memory->writeq(page, 0x800); }) != -K_EINVAL) // SIGHAND alone
        testFail("x64 clone3 accepted CLONE_SIGHAND without CLONE_VM");
    // set_tid: oversized arrays are EINVAL, a null array pointer is EFAULT,
    // and a well-formed request is EPERM (PID assignment needs CAP_SYS_ADMIN
    // in the namespace; the guest is unprivileged).
    if (runCase([&] { memory->writeq(page + 64, page + 0x380); memory->writeq(page + 72, 33); }) != -K_EINVAL)
        testFail("x64 clone3 did not reject an oversized set_tid array");
    if (runCase([&] { memory->writeq(page + 72, 1); }) != -K_EFAULT)
        testFail("x64 clone3 did not fault a null set_tid pointer");
    memory->writed(page + 0x380, 12345);
    if (runCase([&] { memory->writeq(page + 64, page + 0x380); memory->writeq(page + 72, 1); }) != -K_EPERM)
        testFail("x64 clone3 did not refuse set_tid with EPERM");
    // cgroup: the field without CLONE_INTO_CGROUP is EINVAL; with the flag
    // no descriptor here refers to a cgroup, so EBADF.
    if (runCase([&] { memory->writeq(page + 80, 5); }) != -K_EINVAL)
        testFail("x64 clone3 accepted a cgroup field without CLONE_INTO_CGROUP");
    if (runCase([&] { memory->writeq(page, 0x200000000ULL); memory->writeq(page + 80, 9999); }) != -K_EBADF)
        testFail("x64 clone3 did not reject CLONE_INTO_CGROUP with a non-cgroup fd");
    // CLONE_CLEAR_SIGHAND: the child starts from default dispositions in
    // both signal tables while a plain fork inherits them.
    KSigAction& parentAction = context.process->sigActions[K_SIGUSR1];
    const KSigAction savedAction = parentAction;
    parentAction.handlerAndSigAction = 0x12345000;
    parentAction.flags = K_SA_SIGINFO;
    auto forkViaClone3 = [&](U64 flags) -> S64 {
        memory->memsetGuest(page, 0, 88);
        memory->writeq(page, flags);
        memory->writed(page + 32, K_SIGCHLD); // exit_signal
        return invokeClone3(true);
    };
    const S64 cleared = forkViaClone3(0x100000000ULL); // CLONE_CLEAR_SIGHAND
    if (cleared <= 0) {
        testFail("x64 clone3 CLONE_CLEAR_SIGHAND fork failed: %lld", (long long)cleared);
    } else {
        KProcessPtr child = KSystem::getProcess((U32)cleared);
        if (!child || child->sigActions[K_SIGUSR1].handlerAndSigAction != 0 ||
            !child->cpu64 || child->cpu64->sigActions[K_SIGUSR1].handler != 0) {
            testFail("x64 clone3 CLONE_CLEAR_SIGHAND child inherited a signal disposition");
        }
        reapChild(cleared);
    }
    const S64 inherited = forkViaClone3(0);
    if (inherited <= 0) {
        testFail("x64 clone3 plain fork failed: %lld", (long long)inherited);
    } else {
        KProcessPtr child = KSystem::getProcess((U32)inherited);
        if (!child || child->sigActions[K_SIGUSR1].handlerAndSigAction != 0x12345000 ||
            !child->cpu64 || child->cpu64->sigActions[K_SIGUSR1].handler != 0x12345000) {
            testFail("x64 clone3 plain fork did not inherit signal dispositions");
        }
        reapChild(inherited);
    }
    parentAction = savedAction;
    // CLONE_PARENT: the new process is parented to the caller parent.
    const S64 byParent = forkViaClone3(0x8000); // CLONE_PARENT
    if (byParent <= 0) {
        testFail("x64 clone3 CLONE_PARENT fork failed: %lld", (long long)byParent);
    } else {
        KProcessPtr child = KSystem::getProcess((U32)byParent);
        if (!child || child->parentId != context.process->parentId) {
            testFail("x64 clone3 CLONE_PARENT child has parent %u, expected %u",
                     child ? child->parentId : 0, context.process->parentId);
        }
        reapChild(byParent);
    }
    // An explicit stack in clone_args is honoured for process creation: the
    // child starts with RSP at stack + stack_size.
    memory->memsetGuest(page, 0, 88);
    memory->writeq(page + 40, 0x7FF00000ULL); // stack base
    memory->writeq(page + 48, 0x2000);        // stack size
    const S64 stacked = invokeClone3(false);
    if (stacked <= 0) {
        testFail("x64 clone3 stack fork failed: %lld", (long long)stacked);
    } else {
        KProcessPtr child = KSystem::getProcess((U32)stacked);
        if (!child || !child->cpu64 || child->cpu64->reg[X64_RSP].u64 != 0x7FF00000ULL + 0x2000) {
            testFail("x64 clone3 child did not start on the requested stack (rsp=%llx)",
                     child && child->cpu64 ? (unsigned long long)child->cpu64->reg[X64_RSP].u64 : 0ULL);
        }
        reapChild(stacked);
    }
}

#endif
#endif
