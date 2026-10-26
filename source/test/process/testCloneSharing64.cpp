/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

// Round 3C: CLONE_FS / CLONE_FILES / CLONE_SIGHAND / CLONE_VM sharing for
// 64-bit *process* clones (clone/clone3 without CLONE_THREAD). Linux makes
// the child share the parent's fs context, fd table, signal-handler table
// and (for CLONE_VM) address space: mutations by one are visible to the
// other. These tests drive clone3/clone through ksyscall64 exactly like
// testClone3Abi64/testForkVfork64 and then observe cross-process visibility.

namespace {

// Linux clone flags (see the K_CLONE_* defines in kprocess.cpp).
constexpr U64 F_VM      = 0x100;
constexpr U64 F_FS      = 0x200;
constexpr U64 F_FILES   = 0x400;
constexpr U64 F_SIGHAND = 0x800;

struct SharingFixture {
    KProcessPtr process;
    KThread* thread = nullptr;
    KMemory64* memory = nullptr;
    std::shared_ptr<FsNode> oldProcNode;
    bool installedProcNode = false;
    U64 page = 0x62000000ULL;      // data + clone_args page (RW)
    U64 codePage = 0x62001000ULL;  // child code page (RWX, jmp $)

    explicit SharingFixture(TestContext& context) : process(context.process), thread(context.thread) {
        // Process creation registers /proc/<pid> nodes; the test harness
        // has no real /proc root, so install the same stub testFork64 uses.
        oldProcNode = KSystem::procNode;
        if (!KSystem::procNode) {
            KSystem::procNode = std::make_shared<FsFileNode>(0, 0, B("/proc"), B(""), B(""), true, false, nullptr);
            installedProcNode = true;
        }
        if (!process->memory64) process->memory64 = new KMemory64(process.get());
        memory = process->memory64;
        if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(page, K64_PAGE_SIZE * 2, 3) != page) {
            testFail("clone sharing test could not map fixture pages");
            memory = nullptr;
            return;
        }
        memory->mprotect(codePage, K64_PAGE_SIZE, 7);
        memory->writeb(codePage, 0xEB); // jmp $: children spin harmlessly
        memory->writeb(codePage + 1, 0xFE);
    }
    ~SharingFixture() {
        if (installedProcNode) KSystem::procNode = oldProcNode;
    }

    // A fixture CPU64 (RIP at the spin code) plugged in as the process and
    // thread CPU for the lifetime of the guard.
    struct CpuPlug {
        SharingFixture& fx;
        CPU64 cpu;
        CPU64* savedProcessCpu;
        CPU64* savedThreadCpu;
        explicit CpuPlug(SharingFixture& f) : fx(f), cpu(f.memory), savedProcessCpu(f.process->cpu64), savedThreadCpu(f.thread->cpu64) {
            cpu.thread = fx.thread;
            cpu.rip = fx.codePage;
            fx.process->cpu64 = &cpu;
            fx.thread->cpu64 = &cpu;
        }
        ~CpuPlug() {
            fx.process->cpu64 = savedProcessCpu;
            fx.thread->cpu64 = savedThreadCpu;
        }
    };

    // clone3 with the given flags (exit_signal = SIGCHLD); returns child pid
    // or a negative errno.
    S64 clone3(U64 flags) {
        CpuPlug plug(*this);
        constexpr U64 argsAddr = 0x62000200ULL;
        memory->memsetGuest(argsAddr, 0, 88);
        memory->writeq(argsAddr, flags);
        memory->writed(argsAddr + 32, K_SIGCHLD); // exit_signal
        plug.cpu.reg[X64_RAX].setU64(435); // __NR_clone3
        plug.cpu.reg[X64_RDI].setU64(argsAddr);
        plug.cpu.reg[X64_RSI].setU64(88);
        ksyscall64(&plug.cpu);
        return (S64)plug.cpu.reg[X64_RAX].u64;
    }

    // Legacy clone(2) (nr 56) with an inherited stack; returns child pid or
    // a negative errno.
    S64 legacyClone(U64 flags) {
        CpuPlug plug(*this);
        plug.cpu.reg[X64_RAX].setU64(56); // __NR_clone
        plug.cpu.reg[X64_RDI].setU64(flags);
        plug.cpu.reg[X64_RSI].setU64(0); // stack: inherit
        plug.cpu.reg[X64_RDX].setU64(0); // parent_tid
        plug.cpu.reg[X64_R10].setU64(0); // child_tid
        plug.cpu.reg[X64_R8].setU64(0);  // tls
        ksyscall64(&plug.cpu);
        return (S64)plug.cpu.reg[X64_RAX].u64;
    }

    KProcessPtr child(S64 pid, const char* what) {
        KProcessPtr result = pid > 0 ? KSystem::getProcess((U32)pid) : nullptr;
        if (!result) testFail("%s did not produce a child process (ret=%lld)", what, (long long)pid);
        return result;
    }

    void reap(KProcessPtr& ch) {
        if (!ch) return;
        ch->killAllThreads(nullptr);
        ch->exitCode = 0;
        ch->terminated = true;
        if (KSystem::getProcess(ch->id)) KSystem::eraseProcess(ch->id);
    }

    // Parent getcwd(2) through the real dispatcher.
    BString parentGetcwd() {
        CpuPlug plug(*this);
        constexpr U64 buf = 0x62000600ULL;
        memory->memsetGuest(buf, 0, 256);
        plug.cpu.reg[X64_RAX].setU64(79); // __NR_getcwd
        plug.cpu.reg[X64_RDI].setU64(buf);
        plug.cpu.reg[X64_RSI].setU64(256);
        ksyscall64(&plug.cpu);
        S64 len = (S64)plug.cpu.reg[X64_RAX].u64;
        if (len <= 0) return BString();
        std::string s;
        for (S64 i = 0; i < len - 1; i++) s.push_back((char)memory->readb(buf + i));
        return BString::copy(s.c_str());
    }
};

} // namespace

void testCloneSharing64() {
    SharingFixture fx(testContext());
    if (!fx.memory) return;
    KProcessPtr process = fx.process;

    // ---- CLONE_FILES: the child shares the descriptor table itself.
    {
        auto ev = std::make_shared<KEvent>();
        KFileDescriptorPtr parentFd = process->allocFileDescriptor(ev, K_O_RDWR, 0, 231, 0);
        if (!parentFd || parentFd->handle != 231) { testFail("clone FILES test could not allocate parent descriptor 231"); return; }
        S64 pid = fx.clone3(F_FILES);
        KProcessPtr ch = fx.child(pid, "clone3 CLONE_FILES");
        if (ch) {
            KFileDescriptorPtr childFd = ch->getFileDescriptor(231);
            if (!childFd) {
                testFail("CLONE_FILES child did not inherit the parent descriptor");
            } else if (childFd != parentFd) {
                testFail("CLONE_FILES child holds a copy of the descriptor, not the shared one");
            }
            // An fd opened by the parent AFTER the clone is visible to the
            // child only if the table itself is shared.
            auto ev2 = std::make_shared<KEvent>();
            KFileDescriptorPtr late = process->allocFileDescriptor(ev2, K_O_RDWR, 0, -1, 0);
            if (!late) {
                testFail("clone FILES test could not allocate a late descriptor");
            } else if (!ch->getFileDescriptor(late->handle)) {
                testFail("CLONE_FILES child cannot see a descriptor the parent opened after cloning");
            }
            // A close by the child closes it for the parent too.
            if (childFd) {
                ch->clearFdHandle(231);
                if (process->getFileDescriptor(231))
                    testFail("CLONE_FILES parent still sees a descriptor the child closed");
            }
            if (late) process->clearFdHandle(late->handle);
            fx.reap(ch);
        }
        if (process->getFileDescriptor(231)) process->clearFdHandle(231);
    }

    // ---- Non-sharing guard: a plain clone3 child keeps a snapshot table.
    {
        S64 pid = fx.clone3(0);
        KProcessPtr ch = fx.child(pid, "plain clone3");
        if (ch) {
            auto ev = std::make_shared<KEvent>();
            KFileDescriptorPtr late = process->allocFileDescriptor(ev, K_O_RDWR, 0, -1, 0);
            if (!late) {
                testFail("plain clone3 guard could not allocate a descriptor");
            } else {
                if (ch->getFileDescriptor(late->handle))
                    testFail("plain clone3 child sees descriptors opened after the clone (table wrongly shared)");
                process->clearFdHandle(late->handle);
            }
            fx.reap(ch);
        }
    }

    // ---- CLONE_FS: cwd and umask live in one shared fs context. The
    // umask half is the unconditional behavioural proof; the cwd half runs
    // when the environment has a usable Fs root (this harness runs
    // rootless, where even chdir("/") is ENOENT — the umask evidence then
    // stands for the shared context object, whose cwd field the same
    // clone adoption shares).
    {
        U32 savedUmask = process->umask(0x022);
        bool dirReady = (process->chdir(B("clone3c_fs")) == 0);
        if (!dirReady && process->mkdir(B("clone3c_fs")) == 0)
            dirReady = (process->chdir(B("clone3c_fs")) == 0);
        S64 pid = fx.clone3(F_FS);
        KProcessPtr ch = fx.child(pid, "clone3 CLONE_FS");
        if (ch) {
            ch->umask(0x027);
            U32 oldUmask = process->umask(0x022); // returns previous, restores 022
            if (oldUmask != 0x027)
                testFail("CLONE_FS parent umask is %03o after the child set 027 (umask not shared)", oldUmask);
            if (dirReady) {
                if (ch->chdir(B("/")) != 0) testFail("CLONE_FS child could not chdir to /");
                BString cwd = fx.parentGetcwd();
                if (cwd != B("/"))
                    testFail("CLONE_FS parent cwd is '%s' after the child chdir'd to / (fs context not shared)", cwd.c_str());
            }
            fx.reap(ch);
        }
        process->umask(savedUmask);
        if (dirReady) process->chdir(B("/"));
    }

    // ---- CLONE_SIGHAND: one handler table for both processes, in both the
    // KProcess table and the authoritative CPU64 table.
    {
        KSigAction& parentAction = process->sigActions[K_SIGUSR1];
        const KSigAction savedAction = parentAction;
        parentAction.handlerAndSigAction = 0x12345000;
        parentAction.flags = K_SA_SIGINFO;
        {
            SharingFixture::CpuPlug plug(fx);
            plug.cpu.sigActions[K_SIGUSR1].handler = 0x12345000;
            constexpr U64 argsAddr = 0x62000200ULL;
            fx.memory->memsetGuest(argsAddr, 0, 88);
            fx.memory->writeq(argsAddr, F_VM | F_SIGHAND); // SIGHAND requires VM
            fx.memory->writed(argsAddr + 32, K_SIGCHLD);
            plug.cpu.reg[X64_RAX].setU64(435);
            plug.cpu.reg[X64_RDI].setU64(argsAddr);
            plug.cpu.reg[X64_RSI].setU64(88);
            ksyscall64(&plug.cpu);
            S64 pid = (S64)plug.cpu.reg[X64_RAX].u64;
            KProcessPtr ch = fx.child(pid, "clone3 CLONE_SIGHAND");
            if (ch) {
                if (!ch->cpu64) {
                    testFail("CLONE_SIGHAND child has no CPU64");
                } else {
                    ch->cpu64->sigActions[K_SIGUSR2].handler = 0x888;
                    if (plug.cpu.sigActions[K_SIGUSR2].handler != 0x888)
                        testFail("CLONE_SIGHAND parent CPU64 table does not see the child's handler change");
                    ch->sigActions[K_SIGUSR2].handlerAndSigAction = 0x777;
                    if (process->sigActions[K_SIGUSR2].handlerAndSigAction != 0x777)
                        testFail("CLONE_SIGHAND parent process table does not see the child's handler change");
                }
                fx.reap(ch);
            }
        }
        process->sigActions[K_SIGUSR2].reset();
        parentAction = savedAction;
    }

    // ---- CLONE_VM (process, no vfork): the child runs in the parent's
    // address space.
    {
        fx.memory->writed(fx.page, 0x11223344);
        S64 pid = fx.clone3(F_VM);
        KProcessPtr ch = fx.child(pid, "clone3 CLONE_VM");
        if (ch) {
            if (ch->memory64 != fx.memory)
                testFail("CLONE_VM child got a copied address space instead of sharing the parent's");
            if (!ch->memory64Shared)
                testFail("CLONE_VM child is not marked as sharing its address space");
            fx.memory->writed(fx.page, 0xFEEDFACE);
            if (!ch->memory64 || ch->memory64->readd(fx.page) != 0xFEEDFACE)
                testFail("CLONE_VM child does not see a parent write made after cloning");
            fx.reap(ch);
        }
    }

    // ---- Legacy clone(2) honours the same sharing flags.
    {
        S64 pid = fx.legacyClone(F_FILES | F_FS | 17 /*SIGCHLD*/);
        KProcessPtr ch = fx.child(pid, "legacy clone CLONE_FILES|CLONE_FS");
        if (ch) {
            auto ev = std::make_shared<KEvent>();
            KFileDescriptorPtr late = process->allocFileDescriptor(ev, K_O_RDWR, 0, -1, 0);
            if (!late) {
                testFail("legacy clone test could not allocate a descriptor");
            } else {
                if (!ch->getFileDescriptor(late->handle))
                    testFail("legacy clone CLONE_FILES child cannot see a descriptor opened after cloning");
                process->clearFdHandle(late->handle);
            }
            fx.reap(ch);
        }
    }
}

#endif
#endif
