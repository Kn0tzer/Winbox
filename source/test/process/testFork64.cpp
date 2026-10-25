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
#include "ksystem.h"
#include "ksignal.h"
#include "syscall64.h"
#include "kevent.h"
#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"

namespace {
struct RestoreProcessCpu64 {
    KProcess& process;
    CPU64* saved;
    ~RestoreProcessCpu64() { process.cpu64 = saved; }
};
struct RestoreThreadCpu64 {
    KThread& thread;
    CPU64* saved;
    ~RestoreThreadCpu64() { thread.cpu64 = saved; }
};
struct RestoreSignalAction {
    KSigAction& target;
    KSigAction saved;
    ~RestoreSignalAction() { target = saved; }
};
}

// fork(2)=57 and vfork(2)=58 must create a real process with its own address
// space even though glibc normally reaches clone/clone3. The test drives the
// dispatcher directly and verifies parent/child return values, wait4 behavior,
// and memory isolation. The child runs a short self-loop until cleanup stops it.
static void runForkCase(TestContext& context, U64 nr, const char* name) {
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x61000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE * 2, 3) != page) {
        testFail("%s test could not map x64 page", name);
        return;
    }
    if (!memory->getPageFlags((page + K64_PAGE_SIZE) >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page + K64_PAGE_SIZE, K64_PAGE_SIZE, 3) != page + K64_PAGE_SIZE) {
        testFail("%s test could not map child code page", name);
        return;
    }
    memory->writed(page, 0x11223344);
    memory->writed(page + 4, 0xAABBCCDD);
    const U64 childCode = page + K64_PAGE_SIZE + 0x10;
    if (nr == 58 /*vfork*/) {
        // Linux vfork shares the address space until exec/exit: the child
        // writes a magic value into the data page, and the parent must
        // observe that write in its own page after vfork returns.
        const U8 writeMagic[] = {
            0xC7, 0x05, 0xE6, 0xEF, 0xFF, 0xFF, // mov dword ptr [rip-0x101A], imm32
            0xBE, 0xBA, 0xFE, 0xCA              // 0xCAFEBABE -> page
        };
        const U8 exitAfterDelay[] = {
            0xB9, 0xA0, 0x86, 0x01, 0x00, // mov ecx, 100000
            0xFF, 0xC9,                 // dec ecx
            0x75, 0xFC,                 // jnz dec
            0xB8, 0x3C, 0x00, 0x00, 0x00, // mov eax, SYS_exit
            0x31, 0xFF,                 // xor edi, edi
            0x0F, 0x05                  // syscall
        };
        memory->memcpyToGuest(childCode, writeMagic, sizeof(writeMagic));
        memory->memcpyToGuest(childCode + sizeof(writeMagic), exitAfterDelay, sizeof(exitAfterDelay));
    } else {
        memory->writeb(childCode, 0xEB);     // jmp $: keep fork child alive
        memory->writeb(childCode + 1, 0xFE); // until test cleanup stops it.
    }

    constexpr U32 testFdHandle = 197;
    if (process->getFileDescriptor(testFdHandle)) {
        testFail("%s test FD handle is unexpectedly occupied", name);
        return;
    }
    auto sharedObject = std::make_shared<KEvent>();
    KFileDescriptorPtr parentFd = process->allocFileDescriptor(sharedObject, K_O_RDWR, 0, testFdHandle, 0);
    if (!parentFd) {
        testFail("%s test could not allocate descriptor", name);
        return;
    }
    KSigAction& parentAction = process->sigActions[K_SIGUSR1];
    RestoreSignalAction restoreAction{parentAction, parentAction};
    parentAction.handlerAndSigAction = 0x12345000;
    parentAction.flags = K_SA_SIGINFO;

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    RestoreProcessCpu64 restoreCpu{*process, process->cpu64};
    RestoreThreadCpu64 restoreThreadCpu{*context.thread, context.thread->cpu64};
    process->cpu64 = &cpu;
    context.thread->cpu64 = &cpu;
    cpu.rip = childCode;
    U64 clone3PidfdAddress = 0;
    if (nr == 435 /*clone3*/) {
        constexpr U64 cloneArgs = page + 0x200;
        clone3PidfdAddress = page + 0x300;
        memory->memsetGuest(cloneArgs, 0, 88);
        memory->writeq(cloneArgs, 0x1000); // CLONE_PIDFD
        memory->writeq(cloneArgs + 8, clone3PidfdAddress);
        memory->writed(cloneArgs + 32, K_SIGUSR1); // clone_args.exit_signal
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(cloneArgs);
        cpu.reg[X64_RSI].setU64(88);
    } else {
        cpu.reg[X64_RAX].setU64(nr);
    }
    ksyscall64(&cpu);
    const S64 forkResult = (S64)cpu.reg[X64_RAX].u64;
    if (forkResult <= 0) {
        testFail("%s did not return a child pid (ret=%lld)", name, (long long)forkResult);
        return;
    }
    KProcessPtr child = KSystem::getProcess((U32)forkResult);
    if (!child) {
        testFail("%s returned pid %lld but no child process exists", name, (long long)forkResult);
        return;
    }
    if (child->parentId != process->id) testFail("%s child has the wrong parent", name);
    if (!child->is64Bit) testFail("%s child is not a 64-bit process", name);
    if (child->childExitSignal64 != (nr == 435 ? K_SIGUSR1 : K_SIGCHLD))
        testFail("%s child did not retain the requested exit signal", name);
    FD clone3Pidfd = -1;
    if (nr == 435) {
        clone3Pidfd = (FD)memory->readd(clone3PidfdAddress);
        KFileDescriptorPtr descriptor = process->getFileDescriptor(clone3Pidfd);
        if (!descriptor || !descriptor->kobject || descriptor->kobject->type != KTYPE_EVENT ||
            descriptor->kobject->isReadReady())
            testFail("clone3 CLONE_PIDFD did not return a live process-pollable descriptor (fd=%d)", clone3Pidfd);
    }
    if (nr != 58 && (!child->memory64 || child->memory64 == memory)) testFail("%s child did not get its own address space", name);
    if (nr == 58 && memory->readd(page) != 0xCAFEBABE)
        testFail("vfork child writes were not visible to the parent (address space was not shared)");
    if (!child->cpu64) {
        testFail("%s child has no CPU64", name);
    } else if (nr != 58 /*vfork child exits before parent resumes*/) {
        if (child->cpu64->reg[X64_RAX].u64 != 0) testFail("%s child does not return 0", name);
        if (child->cpu64->rip != childCode) testFail("%s child did not inherit RIP", name);
    }
    if (nr != 58 /*vfork child has already exited and closed its FDs*/) {
        KFileDescriptorPtr childFd = child->getFileDescriptor(testFdHandle);
        if (!childFd || childFd->kobject != sharedObject) {
            testFail("%s child did not inherit its descriptor/open-file description", name);
        }
        if (child->sigActions[K_SIGUSR1].handlerAndSigAction != parentAction.handlerAndSigAction ||
            child->sigActions[K_SIGUSR1].flags != parentAction.flags) {
            testFail("%s child did not inherit its signal disposition", name);
        }
    }
    if (nr != 58 && child->memory64) {
        if (child->memory64->readd(page) != 0x11223344 || child->memory64->readd(page + 4) != 0xAABBCCDD) {
            testFail("%s child did not inherit the parent memory contents", name);
        }
        memory->writed(page, 0x55667788);
        if (child->memory64->readd(page) != 0x11223344) {
            testFail("%s child memory is not isolated from later parent writes", name);
        }
    }
    CPU64 waitCpu(memory);
    waitCpu.thread = context.thread;
    waitCpu.reg[X64_RAX].setU64(61); // wait4
    waitCpu.reg[X64_RDI].setU64((U32)forkResult);
    waitCpu.reg[X64_RSI].setU64(page + 8);
    memory->writed(page + 8, 0xDEADBEEF);
    if (nr != 58 /*fork and clone3 children stay alive*/) {
        waitCpu.reg[X64_RDX].setU64(1); // WNOHANG
        ksyscall64(&waitCpu);
        if ((S64)waitCpu.reg[X64_RAX].u64 != 0) {
            testFail("%s wait4(WNOHANG) should report a running child", name);
        }
        if (memory->readd(page + 8) != 0xDEADBEEF) {
            testFail("%s wait4(WNOHANG) overwrote status for a running child", name);
        }
    } else if (!child->vforkReleased || !child->terminated) {
        testFail("vfork returned before the child exited or execed");
    }
    // Fork queues a real guest thread; stop and join it before dropping the
    // process table entry or the test fixture can race its CPU teardown.
    child->killAllThreads(nullptr);
    if (nr != 58 /*fork and clone3 children stay alive*/) {
        child->exitCode = 37;
        child->terminated = true;
    }
    if (clone3Pidfd >= 0) {
        KFileDescriptorPtr descriptor = process->getFileDescriptor(clone3Pidfd);
        if (!descriptor || !descriptor->kobject || !descriptor->kobject->isReadReady())
            testFail("clone3 pidfd did not become readable after child exit");
    }
    waitCpu.reg[X64_RAX].setU64(61); // wait4 the now-exited child
    waitCpu.reg[X64_RDX].setU64(0);
    ksyscall64(&waitCpu);
    if ((S64)waitCpu.reg[X64_RAX].u64 != forkResult) {
        testFail("%s wait4 did not reap its terminated child", name);
    }
    const U32 expectedStatus = nr != 58 ? (37u << 8) : (child->exitCode << 8);
    if (memory->readd(page + 8) != expectedStatus) {
        testFail("%s wait4 returned the wrong exit status", name);
    }
    if (clone3Pidfd >= 0) process->close(clone3Pidfd);
    if (KSystem::getProcess((U32)forkResult)) {
        KSystem::eraseProcess((U32)forkResult);
    }
    process->clearFdHandle(testFdHandle);
}

void testForkVfork64() {
    TestContext& context = testContext();
    std::shared_ptr<FsNode> oldProcNode = KSystem::procNode;
    if (!KSystem::procNode) {
        KSystem::procNode = std::make_shared<FsFileNode>(0, 0, B("/proc"), B(""), B(""), true, false, nullptr);
    }
    runForkCase(context, 57 /*fork*/, "fork");
    runForkCase(context, 58 /*vfork*/, "vfork");
    runForkCase(context, 435 /*clone3*/, "clone3");
    KSystem::procNode = oldProcNode;
}

#endif
#endif
