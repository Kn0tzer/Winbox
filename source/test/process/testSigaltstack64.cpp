#include "boxedwine.h"

#if defined(__TEST) && defined(BOXEDWINE_GUEST_X64)
#include "cpu64.h"
#include "kmemory64.h"
#include "ksystem.h"
#include "ksignal.h"
#include "syscall64.h"
#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"

namespace {
constexpr U32 kSsOnStack = 1;
constexpr U32 kSsDisable = 2;
constexpr U32 kSsAutodisarm = 0x80000000u;
constexpr U64 kAltSize = 16384;

void writeStackT(KMemory64& memory, U64 at, U64 sp, U32 flags, U64 size) {
    memory.writeq(at + 0, sp);
    memory.writed(at + 8, flags);
    memory.writed(at + 12, 0);
    memory.writeq(at + 16, size);
}

S64 invokeSigaltstack(CPU64& cpu, U64 ss, U64 oss) {
    cpu.reg[X64_RAX].setU64(131); // __NR_sigaltstack
    cpu.reg[X64_RDI].setU64(ss);
    cpu.reg[X64_RSI].setU64(oss);
    ksyscall64(&cpu);
    return (S64)cpu.reg[X64_RAX].u64;
}

bool ensureMapped(KMemory64& memory, U64 addr, U64 len, U32 prot) {
    if (memory.getPageFlags(addr >> K64_PAGE_SHIFT)) return true;
    return memory.mmapAnonymousFixed(addr, len, prot) == addr;
}
}

// Linux derives the reported ss_flags from the CURRENT stack pointer
// (sas_ss_flags): SS_ONSTACK while executing on the alternate stack,
// SS_DISABLE when none is registered. Changing the stack while executing
// on it must fail with EPERM and leave the registration untouched.
void testSigaltstack64Semantics() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 alt = 0x7A100000, alt2 = 0x7A180000, structs = 0x7A200000, mainStack = 0x7A300000;
    if (!ensureMapped(*memory, alt, kAltSize, 3) || !ensureMapped(*memory, alt2, kAltSize, 3) ||
        !ensureMapped(*memory, structs, K64_PAGE_SIZE, 3) || !ensureMapped(*memory, mainStack, K64_PAGE_SIZE, 3)) {
        testFail("sigaltstack test could not map its pages");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    cpu.reg[X64_RSP].setU64(mainStack + 0x800); // not on any altstack
    const U64 ssNew = structs, ssOld = structs + 64;

    writeStackT(*memory, ssNew, alt, 0, kAltSize);
    if (invokeSigaltstack(cpu, ssNew, 0) != 0) {
        testFail("sigaltstack install failed");
        return;
    }
    if (invokeSigaltstack(cpu, 0, ssOld) != 0 ||
        memory->readq(ssOld) != alt || memory->readq(ssOld + 16) != kAltSize ||
        memory->readd(ssOld + 8) != 0) {
        testFail("sigaltstack query off the stack misreported the registration");
    }
    // Move onto the stack: the query must now report SS_ONSTACK even
    // though the stored flags are 0.
    cpu.reg[X64_RSP].setU64(alt + 8192);
    if (invokeSigaltstack(cpu, 0, ssOld) != 0 ||
        !(memory->readd(ssOld + 8) & kSsOnStack)) {
        testFail("sigaltstack query on the altstack did not report SS_ONSTACK");
    }
    // Changing or disabling the stack while executing on it: EPERM, and
    // the registration must not move.
    writeStackT(*memory, ssNew, alt2, 0, kAltSize);
    if (invokeSigaltstack(cpu, ssNew, 0) != -K_EPERM) {
        testFail("sigaltstack install while on the stack was not rejected with EPERM");
    }
    writeStackT(*memory, ssNew, 0, kSsDisable, 0);
    if (invokeSigaltstack(cpu, ssNew, 0) != -K_EPERM) {
        testFail("sigaltstack disable while on the stack was not rejected with EPERM");
    }
    if (cpu.sigAltStack.ssSp != alt || cpu.sigAltStack.ssSize != kAltSize) {
        testFail("a rejected on-stack sigaltstack change mutated the registration");
    }
    // Back off the stack: disable succeeds and the query reports it.
    cpu.reg[X64_RSP].setU64(mainStack + 0x800);
    writeStackT(*memory, ssNew, 0, kSsDisable, 0);
    if (invokeSigaltstack(cpu, ssNew, 0) != 0) {
        testFail("sigaltstack disable off the stack failed");
    }
    if (invokeSigaltstack(cpu, 0, ssOld) != 0 ||
        memory->readd(ssOld + 8) != kSsDisable || memory->readq(ssOld) != 0) {
        testFail("sigaltstack query after disable misreported the registration");
    }
}

// SS_AUTODISARM: delivery disarms the registration while the handler
// runs; the frame's uc_stack carries the armed stack and rt_sigreturn
// restores it.
void testSigaltstack64Autodisarm() {
    KMemory64 memory{nullptr};
    CPU64 cpu{&memory};
    constexpr U64 stack = 0x7B100000, alt = 0x7B200000, handler = 0x7B300000;
    constexpr U64 restorer = handler + 0x100;
    memory.mmapAnonymousFixed(stack, 4 * K64_PAGE_SIZE, 3);
    memory.mmapAnonymousFixed(alt, 4 * K64_PAGE_SIZE, 3);
    memory.mmapAnonymousFixed(handler, K64_PAGE_SIZE, 7);
    const U8 ret[] = {0xc3};
    const U8 sigreturnCode[] = {0xb8, 15, 0, 0, 0, 0x0f, 0x05}; // mov eax,15; syscall
    memory.memcpyToGuest(handler, ret, sizeof(ret));
    memory.memcpyToGuest(restorer, sigreturnCode, sizeof(sigreturnCode));
    cpu.reg[X64_RSP].setU64(stack + 4 * K64_PAGE_SIZE - 16);
    cpu.rip = handler + 0x200;

    const U64 ssNew = stack + 0x100;
    writeStackT(memory, ssNew, alt, kSsAutodisarm, 2 * K64_PAGE_SIZE);
    if (invokeSigaltstack(cpu, ssNew, 0) != 0) {
        testFail("SS_AUTODISARM sigaltstack install failed");
        return;
    }
    if (cpu.sigAltStack.ssSp != alt || !(cpu.sigAltStack.ssFlags & kSsAutodisarm)) {
        testFail("SS_AUTODISARM install did not retain the flag");
    }
    auto& sa = cpu.sigActions[K_SIGUSR1];
    sa.installed = true;
    sa.handler = handler;
    sa.restorer = restorer;
    sa.flags = 4 | 0x08000000; // SA_SIGINFO | SA_ONSTACK
    if (!cpu.raiseSyncFault(K_SIGUSR1, 14, 2, 0xdead0000, 5)) {
        testFail("AUTODISARM signal delivery failed");
        return;
    }
    const U64 handlerRsp = cpu.reg[X64_RSP].u64;
    if (handlerRsp < alt || handlerRsp >= alt + 2 * K64_PAGE_SIZE) {
        testFail("SA_ONSTACK delivery did not run on the altstack");
    }
    if (cpu.sigAltStack.ssSp != 0 || cpu.sigAltStack.ssSize != 0 ||
        !(cpu.sigAltStack.ssFlags & kSsDisable)) {
        testFail("SS_AUTODISARM stack was not disarmed while the handler runs");
    }
    const U64 uctx = cpu.reg[X64_RDX].u64;
    if (memory.readq(uctx + 16) != alt || memory.readq(uctx + 32) != 2 * K64_PAGE_SIZE ||
        !(memory.readd(uctx + 24) & kSsAutodisarm)) {
        testFail("signal frame uc_stack lost the AUTODISARM stack");
    }
    cpu.reg[X64_RSP].setU64(uctx);
    cpu.reg[X64_RAX].setU64(15); // rt_sigreturn
    ksyscall64(&cpu);
    if (cpu.sigAltStack.ssSp != alt || cpu.sigAltStack.ssSize != 2 * K64_PAGE_SIZE ||
        !(cpu.sigAltStack.ssFlags & kSsAutodisarm)) {
        testFail("rt_sigreturn did not restore the AUTODISARM stack");
    }
}

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

void reapForkChild(TestContext& context, KProcessPtr parent, KMemory64* memory, U32 pid, U64 statusAddr) {
    KProcessPtr child = KSystem::getProcess(pid);
    if (!child) return;
    child->killAllThreads(nullptr);
    child->exitCode = 0;
    child->terminated = true;
    CPU64 waitCpu(memory);
    waitCpu.thread = context.thread;
    waitCpu.reg[X64_RAX].setU64(61); // wait4
    waitCpu.reg[X64_RDI].setU64(pid);
    waitCpu.reg[X64_RSI].setU64(statusAddr);
    waitCpu.reg[X64_RDX].setU64(0);
    ksyscall64(&waitCpu);
    if (KSystem::getProcess(pid)) KSystem::eraseProcess(pid);
}
}

// Linux sas_ss_reset (kernel/fork.c): a CLONE_VM child that is not a
// vfork starts with NO alternate signal stack, while a plain fork (and
// vfork) inherits it. cloneRegistersFrom copies it unconditionally
// today, so thread clones and CLONE_VM process clones wrongly inherit.
void testClone64AltstackReset() {
    TestContext& context = testContext();
    std::shared_ptr<FsNode> oldProcNode = KSystem::procNode;
    if (!KSystem::procNode) {
        KSystem::procNode = std::make_shared<FsFileNode>(0, 0, B("/proc"), B(""), B(""), true, false, nullptr);
    }
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 codePage = 0x7C100000, stackPage = 0x7C200000;
    constexpr U64 alt = 0x7C300000;
    if (!ensureMapped(*memory, codePage, K64_PAGE_SIZE, 7) ||
        !ensureMapped(*memory, stackPage, 2 * K64_PAGE_SIZE, 3)) {
        testFail("clone altstack test could not map its pages");
        KSystem::procNode = oldProcNode;
        return;
    }
    // Flag-release loop: spin until the flag dword is set, then exit(60)
    // (thread-scoped). The flag lives at codePage + 0x800.
    const U8 flagLoop[] = {
        0x83, 0x3D, 0xF9, 0x07, 0x00, 0x00, 0x00, // cmp dword [rip+0x7F9], 0
        0x74, 0xF7,                               // je loop
        0xB8, 0x3C, 0x00, 0x00, 0x00,             // mov eax, 60
        0x31, 0xFF,                               // xor edi, edi
        0x0F, 0x05                                // syscall
    };
    memory->memcpyToGuest(codePage, flagLoop, sizeof(flagLoop));
    memory->writed(codePage + 0x800, 0);
    const U64 selfLoop = codePage + 0x100;
    memory->writeb(selfLoop, 0xEB); // jmp $
    memory->writeb(selfLoop + 1, 0xFE);
    const U64 statusAddr = codePage + 0x900;

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    RestoreProcessCpu64 restoreCpu{*process, process->cpu64};
    RestoreThreadCpu64 restoreThreadCpu{*context.thread, context.thread->cpu64};
    process->cpu64 = &cpu;
    context.thread->cpu64 = &cpu;
    cpu.sigAltStack.ssSp = alt;
    cpu.sigAltStack.ssFlags = 0;
    cpu.sigAltStack.ssSize = kAltSize;

    auto invokeClone = [&](U64 flags, U64 childStack) -> S64 {
        cpu.reg[X64_RAX].setU64(56); // __NR_clone
        cpu.reg[X64_RDI].setU64(flags);
        cpu.reg[X64_RSI].setU64(childStack);
        cpu.reg[X64_RDX].setU64(0);
        cpu.reg[X64_R10].setU64(0);
        cpu.reg[X64_R8].setU64(0);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    // Thread clone (CLONE_VM|FS|FILES|SIGHAND|THREAD): altstack reset.
    cpu.rip = codePage;
    const S64 tid = invokeClone(0x100 | 0x200 | 0x400 | 0x800 | 0x10000,
                                stackPage + 2 * K64_PAGE_SIZE);
    if (tid <= 0) {
        testFail("thread clone failed (ret=%lld)", (long long)tid);
    } else {
        KThread* childThread = process->getThreadById((U32)tid);
        if (!childThread || !childThread->cpu64) {
            testFail("thread clone produced no child CPU64");
        } else if (childThread->cpu64->sigAltStack.ssSp != 0 ||
                   childThread->cpu64->sigAltStack.ssSize != 0 ||
                   !(childThread->cpu64->sigAltStack.ssFlags & kSsDisable)) {
            testFail("CLONE_VM thread inherited the creator's sigaltstack (Linux resets it)");
        }
        memory->writed(codePage + 0x800, 1); // release the child to exit
    }

    // CLONE_VM process clone (not vfork): altstack reset.
    cpu.rip = selfLoop;
    const S64 vmPid = invokeClone(0x100 | K_SIGCHLD, 0);
    if (vmPid <= 0) {
        testFail("CLONE_VM clone failed (ret=%lld)", (long long)vmPid);
    } else {
        KProcessPtr child = KSystem::getProcess((U32)vmPid);
        if (!child || !child->cpu64) {
            testFail("CLONE_VM clone produced no child process CPU64");
        } else if (child->cpu64->sigAltStack.ssSp != 0 ||
                   child->cpu64->sigAltStack.ssSize != 0 ||
                   !(child->cpu64->sigAltStack.ssFlags & kSsDisable)) {
            testFail("CLONE_VM process clone inherited the parent's sigaltstack (sas_ss_reset)");
        }
        reapForkChild(context, process, memory, (U32)vmPid, statusAddr);
    }

    // Plain fork (control): the altstack IS inherited.
    const S64 forkPid = invokeClone(K_SIGCHLD, 0);
    if (forkPid <= 0) {
        testFail("fork clone failed (ret=%lld)", (long long)forkPid);
    } else {
        KProcessPtr child = KSystem::getProcess((U32)forkPid);
        if (!child || !child->cpu64) {
            testFail("fork produced no child process CPU64");
        } else if (child->cpu64->sigAltStack.ssSp != alt ||
                   child->cpu64->sigAltStack.ssSize != kAltSize ||
                   child->cpu64->sigAltStack.ssFlags != 0) {
            testFail("plain fork did not inherit the parent's sigaltstack");
        }
        reapForkChild(context, process, memory, (U32)forkPid, statusAddr);
    }
    KSystem::procNode = oldProcNode;
}
#endif
