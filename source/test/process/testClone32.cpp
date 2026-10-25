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
#ifndef BOXEDWINE_GUEST_X64

#include "ksystem.h"
#include "ksignal.h"
#include "kevent.h"
#include "kerror.h"
#include "../cpu/testCPU.h"
#include "../../io/fsfilenode.h"

// Round 3C, 32-bit side:
//  - CLONE_FS / CLONE_FILES / CLONE_SIGHAND / CLONE_VM sharing for process
//    clones through KProcess::clone (mutations visible across processes).
//  - KProcess::clone must never kpanic on guest-controlled flags: unhandled
//    process-clone flags get correct errno dispositions, cheap ones
//    (CLONE_PARENT, CLONE_PIDFD, CLONE_SETTLS) are implemented, and the
//    thread branch accepts the full flag set Wine/glibc can present.
//  - KProcess::clone3 must honour exit_signal, validate its arguments like
//    the 64-bit path, and stop silently truncating 64-bit flag bits.

namespace {

// Linux clone flags (see the K_CLONE_* defines in kprocess.cpp).
constexpr U32 C_VM           = 0x100;
constexpr U32 C_FS           = 0x200;
constexpr U32 C_FILES        = 0x400;
constexpr U32 C_SIGHAND     = 0x800;
constexpr U32 C_PIDFD       = 0x1000;
constexpr U32 C_PTRACE      = 0x2000;
constexpr U32 C_VFORK       = 0x4000;
constexpr U32 C_PARENT      = 0x8000;
constexpr U32 C_THREAD      = 0x10000;
constexpr U32 C_NEWNS       = 0x20000;
constexpr U32 C_SYSVSEM     = 0x40000;
constexpr U32 C_SETTLS      = 0x80000;
constexpr U32 C_PARENT_SETTID = 0x100000;
constexpr U32 C_CHILD_CLEARTID = 0x200000;
constexpr U32 C_DETACHED    = 0x400000;
constexpr U32 C_CHILD_SETTID = 0x1000000;
constexpr U32 C_INTO_CGROUP = 0x20000000; // bit 29 in clone_args flags

constexpr U32 CODE_PAGE = 0x62000000; // base of the 3-page fixture mapping
constexpr U32 SPIN_PAGE = 0x62001000; // filled with jmp $ pairs
constexpr U32 DATA_PAGE = 0x62002000; // clone_args / buffers

// Cloned children are kept alive for the whole suite: a VM-sharing child's
// destructor must never run while the parent still uses the shared address
// space (that lifetime hazard is part of what round 3C fixes in the kernel;
// the tests stay safe on every tree by leaking the children deliberately).
std::vector<KProcessPtr>& keepAlive() {
    static std::vector<KProcessPtr> children;
    return children;
}

bool fixtureReady = false;
void setupFixture(TestContext& context) {
    if (fixtureReady) return;
    fixtureReady = true;
    context.memory->mmap(context.thread, CODE_PAGE, 0x3000, K_PROT_READ | K_PROT_WRITE | K_PROT_EXEC, K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0);
    // KProcess::clone resumes the child at parent eip + 2. Children are
    // aimed at the spin page: every even offset is a jmp $ pair, so a child
    // that gets scheduled before stashChild unschedules it spins harmlessly
    // (an exit_group landing page faulted inside the cloned address space
    // in this harness, which is unrelated to what these tests assert).
    // Spin page: every even offset is a jmp $ pair.
    for (U32 i = 0; i < 0x1000; i += 2) {
        context.memory->writeb(SPIN_PAGE + i, 0xEB);
        context.memory->writeb(SPIN_PAGE + i + 1, 0xFE);
    }
}

// The harness fixture CPU carries a non-zero CS segment base, which a
// cloned child would inherit: its instruction fetches would land at
// seg[CS].address + eip instead of the guest address the test mapped.
// Real processes run with flat segments, so flatten them for the duration
// of each test (restored on scope exit).
struct FlatSegs {
    CPU* cpu;
    U32 saved[4];
    int which[4];
    explicit FlatSegs(TestContext& context) : cpu(context.cpu) {
        which[0] = CS; which[1] = SS; which[2] = DS; which[3] = ES;
        for (int i = 0; i < 4; i++) {
            saved[i] = cpu->seg[which[i]].address;
            cpu->seg[which[i]].address = 0;
        }
    }
    ~FlatSegs() {
        for (int i = 0; i < 4; i++) cpu->seg[which[i]].address = saved[i];
    }
};

// Process creation registers /proc/<pid> nodes; where the harness has no
// /proc root, install the same stub the x64 process tests use.
struct ProcNodeGuard {
    std::shared_ptr<FsNode> oldProcNode;
    bool installed = false;
    ProcNodeGuard() {
        oldProcNode = KSystem::procNode;
        if (!KSystem::procNode) {
            KSystem::procNode = std::make_shared<FsFileNode>(0, 0, B("/proc"), B(""), B(""), true, false, nullptr);
            installed = true;
        }
    }
    ~ProcNodeGuard() {
        if (installed) KSystem::procNode = oldProcNode;
    }
};

// Point the fixture CPU where clone() expects to resume from.
void aimCpu(TestContext& context, U32 page) {
    context.cpu->eip.u32 = page;
    context.cpu->reg[4].u32 = page + 0x800; // ESP inside the mapped page
}

KProcessPtr stashChild(S32 pid, const char* what) {
    KProcessPtr child = pid > 0 ? KSystem::getProcess((U32)pid) : nullptr;
    if (!child) {
        testFail("%s did not produce a child process (ret=%d)", what, pid);
        return nullptr;
    }
    // The assertions in these tests are all parent-side state checks; the
    // child never needs to execute. Unschedule its threads immediately so
    // the harness platform loop cannot run a half-initialized cloned CPU
    // (in this harness a cloned child's first fetch faults on page-type
    // translation, which is unrelated to what is being tested here).
    if (child->getThread()) unscheduleThread(child->getThread());
    child->terminated = true; // never waited on; keep it inert if reaped later
    KSystem::eraseProcess(child->id);
    keepAlive().push_back(child);
    return child;
}

} // namespace

void testClone32Sharing() {
    TestContext& context = testContext();
    setupFixture(context);
    FlatSegs flatSegs(context);
    ProcNodeGuard procNodeGuard;
    KProcessPtr process = context.process;

    // ---- CLONE_FILES: the descriptor table itself is shared.
    {
        auto ev = std::make_shared<KEvent>();
        KFileDescriptorPtr parentFd = process->allocFileDescriptor(ev, K_O_RDWR, 0, 213, 0);
        if (!parentFd || parentFd->handle != 213) { testFail("clone32 FILES test could not allocate descriptor 213"); return; }
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, C_FILES | 17, 0, 0, 0, 0);
        KProcessPtr child = stashChild(pid, "clone CLONE_FILES");
        if (child) {
            if (!child->getFileDescriptor(213))
                testFail("CLONE_FILES child did not inherit descriptor 213");
            auto ev2 = std::make_shared<KEvent>();
            KFileDescriptorPtr late = process->allocFileDescriptor(ev2, K_O_RDWR, 0, -1, 0);
            if (!late) {
                testFail("clone32 FILES test could not allocate a late descriptor");
            } else if (!child->getFileDescriptor(late->handle)) {
                testFail("CLONE_FILES child cannot see a descriptor the parent opened after cloning");
            }
            child->clearFdHandle(213);
            if (process->getFileDescriptor(213))
                testFail("CLONE_FILES parent still sees descriptor 213 after the child closed it");
            if (late) process->clearFdHandle(late->handle);
        }
        if (process->getFileDescriptor(213)) process->clearFdHandle(213);
    }

    // ---- CLONE_FS: cwd and umask live in one shared fs context. The
    // umask half is the unconditional behavioural proof; the cwd half runs
    // when the environment has a usable Fs root (the native test harness
    // runs rootless, where even chdir("/") is ENOENT — in that case the
    // umask evidence stands for the shared context object, whose cwd
    // field the same clone() adoption shares).
    {
        U32 savedUmask = process->umask(0x022);
        bool dirReady = (process->chdir(B("clone32fs")) == 0);
        if (!dirReady && process->mkdir(B("clone32fs")) == 0)
            dirReady = (process->chdir(B("clone32fs")) == 0);
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, C_FS | 17, 0, 0, 0, 0);
        KProcessPtr child = stashChild(pid, "clone CLONE_FS");
        if (child) {
            child->umask(0x027);
            U32 oldUmask = process->umask(0x022);
            if (oldUmask != 0x027)
                testFail("CLONE_FS parent umask is %03o after the child set 027 (umask not shared)", oldUmask);
            if (dirReady) {
                if (child->chdir(B("/")) != 0) testFail("CLONE_FS child could not chdir to /");
                process->getcwd(DATA_PAGE, 64);
                BString cwd = context.memory->readString(DATA_PAGE);
                if (cwd != B("/"))
                    testFail("CLONE_FS parent cwd is '%s' after the child chdir'd to / (fs context not shared)", cwd.c_str());
            }
        }
        process->umask(savedUmask);
        if (dirReady) process->chdir(B("/"));
    }

    // ---- CLONE_SIGHAND: the handler table is shared.
    {
        KSigAction saved = process->sigActions[K_SIGUSR1];
        process->sigActions[K_SIGUSR1].handlerAndSigAction = 0x12345000;
        aimCpu(context, SPIN_PAGE);
        // Linux requires CLONE_VM with CLONE_SIGHAND.
        S32 pid = process->clone(context.thread, C_SIGHAND | C_VM | 17, 0, 0, 0, 0);
        KProcessPtr child = stashChild(pid, "clone CLONE_SIGHAND");
        if (child) {
            child->sigActions[K_SIGUSR2].handlerAndSigAction = 0x777;
            if (process->sigActions[K_SIGUSR2].handlerAndSigAction != 0x777)
                testFail("CLONE_SIGHAND parent does not see the child's handler change");
            process->sigActions[K_SIGUSR2].reset();
        }
        process->sigActions[K_SIGUSR1] = saved;
    }

    // ---- CLONE_VM: writes through the child's memory are the parent's.
    {
        context.memory->writed(DATA_PAGE, 0x11111111);
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, C_VM | 17, 0, 0, 0, 0);
        KProcessPtr child = stashChild(pid, "clone CLONE_VM");
        if (child) {
            if (child->memory->readd(DATA_PAGE) != 0x11111111)
                testFail("CLONE_VM child does not see the parent's pre-clone contents");
            child->memory->writed(DATA_PAGE, 0x22222222);
            if (context.memory->readd(DATA_PAGE) != 0x22222222)
                testFail("CLONE_VM parent does not see the child's write (address space not shared)");
            // exec semantics: execvReset detaches the child (this is the
            // exact call exitgroup/execve make for a VM-sharing process).
            // The exec'd child starts from an empty address space, so map
            // the page fresh in the child before writing to it.
            child->memory->execvReset(true);
            child->memory->mmap(child->getThread(), DATA_PAGE, 0x1000, K_PROT_READ | K_PROT_WRITE, K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED, -1, 0, false);
            child->memory->writed(DATA_PAGE, 0x33333333);
            if (child->memory->readd(DATA_PAGE) != 0x33333333)
                testFail("CLONE_VM child lost its own write after exec detach");
            if (context.memory->readd(DATA_PAGE) != 0x22222222)
                testFail("CLONE_VM parent memory changed after the child detached for exec");
        }
    }

    // ---- Non-sharing guard: a plain clone snapshots the fd table.
    {
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, 17, 0, 0, 0, 0);
        KProcessPtr child = stashChild(pid, "plain clone");
        if (child) {
            auto ev = std::make_shared<KEvent>();
            KFileDescriptorPtr late = process->allocFileDescriptor(ev, K_O_RDWR, 0, -1, 0);
            if (!late) {
                testFail("plain clone guard could not allocate a descriptor");
            } else {
                if (child->getFileDescriptor(late->handle))
                    testFail("plain clone child sees descriptors opened after the clone (table wrongly shared)");
                process->clearFdHandle(late->handle);
            }
        }
    }
}

void testClone32Flags() {
    TestContext& context = testContext();
    setupFixture(context);
    FlatSegs flatSegs(context);
    ProcNodeGuard procNodeGuard;
    KProcessPtr process = context.process;

    // CLONE_PIDFD: the parent_tid slot receives a pidfd descriptor.
    {
        context.memory->writed(DATA_PAGE, 0);
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, C_PIDFD | 17, 0, DATA_PAGE, 0, 0);
        KProcessPtr child = stashChild(pid, "clone CLONE_PIDFD");
        if (child) {
            U32 fd = context.memory->readd(DATA_PAGE);
            KFileDescriptorPtr pidFd = process->getFileDescriptor((S32)fd);
            if (!pidFd) {
                testFail("CLONE_PIDFD did not store a valid pidfd in the parent_tid slot (got %u)", fd);
            } else {
                if (!pidFd->kobject || pidFd->kobject->type != KTYPE_EVENT)
                    testFail("CLONE_PIDFD descriptor is not a pidfd event object");
                process->clearFdHandle((S32)fd);
            }
        }
    }

    // CLONE_PARENT: the child's parent is the caller's parent.
    {
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, C_PARENT | 17, 0, 0, 0, 0);
        KProcessPtr child = stashChild(pid, "clone CLONE_PARENT");
        if (child && child->parentId != process->parentId)
            testFail("CLONE_PARENT child has parent %u, expected the caller's parent %u", child->parentId, process->parentId);
    }

    // CLONE_SETTLS on a process clone must not abort (descriptor with a
    // zero base/entry is a no-op install, mirroring the thread path).
    {
        context.memory->memset(DATA_PAGE + 0x100, 0, 16);
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone(context.thread, C_SETTLS | 17, 0, 0, DATA_PAGE + 0x100, 0);
        stashChild(pid, "clone CLONE_SETTLS");
    }

    // CLONE_INTO_CGROUP via legacy clone carries the cgroup fd in the
    // child_tid slot; cgroups are not implemented, so it refuses EBADF.
    {
        aimCpu(context, SPIN_PAGE);
        S32 result = process->clone(context.thread, C_INTO_CGROUP | 17, 0, 0, 0, 9999);
        if (result != -K_EBADF)
            testFail("clone CLONE_INTO_CGROUP returned %d, expected -EBADF", result);
        if (result > 0) stashChild(result, "clone CLONE_INTO_CGROUP (unexpected child)");
    }

    // Thread branch: the full Wine/glibc flag set, including CHILD_SETTID,
    // must be accepted (the base tree's exact-mask check kpanics on it).
    {
        context.memory->writed(DATA_PAGE + 0x40, 0xAAAAAAAA); // child_tid slot
        context.memory->writed(DATA_PAGE + 0x44, 0xAAAAAAAA); // parent_tid slot
        aimCpu(context, SPIN_PAGE);
        U32 flags = C_THREAD | C_VM | C_FS | C_FILES | C_SIGHAND | C_SYSVSEM |
                    C_SETTLS | C_PARENT_SETTID | C_CHILD_CLEARTID | C_CHILD_SETTID | 17;
        S32 tid = process->clone(context.thread, flags, 0, DATA_PAGE + 0x44, 0, DATA_PAGE + 0x40);
        if (tid <= 0) {
            testFail("thread clone with CHILD_SETTID returned %d, expected a tid", tid);
        } else {
            if (context.memory->readd(DATA_PAGE + 0x40) != (U32)tid)
                testFail("CLONE_CHILD_SETTID did not write the tid to child memory");
            if (context.memory->readd(DATA_PAGE + 0x44) != (U32)tid)
                testFail("CLONE_PARENT_SETTID did not write the tid to parent memory");
            KThread* childThread = process->getThreadById((U32)tid);
            if (childThread) {
                unscheduleThread(childThread);
                process->deleteThread(childThread);
            }
        }
        aimCpu(context, SPIN_PAGE);
    }

    // Namespace creation is not implemented: it must refuse loudly with
    // EINVAL, not abort the emulator (the base tree kpanics here; kept
    // last so the value-returning cases above report first).
    {
        aimCpu(context, SPIN_PAGE);
        S32 result = process->clone(context.thread, C_NEWNS | 17, 0, 0, 0, 0);
        if (result != -K_EINVAL)
            testFail("clone CLONE_NEWNS returned %d, expected -EINVAL", result);
        if (result > 0) stashChild(result, "clone CLONE_NEWNS (unexpected child)");
    }
}

void testClone3Abi32() {
    TestContext& context = testContext();
    setupFixture(context);
    FlatSegs flatSegs(context);
    ProcNodeGuard procNodeGuard;
    KProcessPtr process = context.process;
    constexpr U32 ARGS = DATA_PAGE + 0x200;

    auto writeArgs = [&](U64 flags, U32 exitSignal, U64 setTid = 0, U64 setTidSize = 0, U64 cgroup = 0) {
        context.memory->memset(ARGS, 0, 88);
        context.memory->writeq(ARGS, flags);          // +0 flags
        context.memory->writed(ARGS + 32, exitSignal); // +32 exit_signal
        context.memory->writeq(ARGS + 64, setTid);     // +64 set_tid
        context.memory->writeq(ARGS + 72, setTidSize); // +72 set_tid_size
        context.memory->writeq(ARGS + 80, cgroup);     // +80 cgroup
    };

    // exit_signal must reach the child (the base tree reads and drops it).
    {
        writeArgs(0, K_SIGUSR1);
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone3(context.thread, ARGS, 88);
        KProcessPtr child = stashChild(pid, "clone3 exit_signal");
        if (child && child->childExitSignal64 != (U32)K_SIGUSR1)
            testFail("clone3 child exit signal is %u, expected SIGUSR1 (%d)", child->childExitSignal64, K_SIGUSR1);
    }

    // CLONE_CLEAR_SIGHAND is flag bit 32: a 32-bit truncation must not drop
    // it (the base tree truncates clone3 flags to U32 and loses the bit).
    {
        KSigAction saved = process->sigActions[K_SIGUSR1];
        process->sigActions[K_SIGUSR1].handlerAndSigAction = 0x12345000;
        writeArgs(1ULL << 32, K_SIGCHLD);
        aimCpu(context, SPIN_PAGE);
        S32 pid = process->clone3(context.thread, ARGS, 88);
        KProcessPtr child = stashChild(pid, "clone3 CLONE_CLEAR_SIGHAND");
        if (child && child->sigActions[K_SIGUSR1].handlerAndSigAction != 0)
            testFail("clone3 CLONE_CLEAR_SIGHAND child inherited a handler (bit 32 was dropped)");
        process->sigActions[K_SIGUSR1] = saved;
    }

    // set_tid stays REJECTED-LOUDLY (EPERM), matching the 64-bit path.
    {
        writeArgs(0, K_SIGCHLD, DATA_PAGE + 0x100, 1);
        context.memory->writed(DATA_PAGE + 0x100, 12345);
        aimCpu(context, SPIN_PAGE);
        S32 result = process->clone3(context.thread, ARGS, 88);
        if (result != -K_EPERM)
            testFail("clone3 with set_tid returned %d, expected -EPERM", result);
        if (result > 0) stashChild(result, "clone3 set_tid (unexpected child)");
    }

    // CLONE_INTO_CGROUP stays REJECTED-LOUDLY (EBADF).
    {
        writeArgs(1ULL << 33, K_SIGCHLD, 0, 0, 9999);
        aimCpu(context, SPIN_PAGE);
        S32 result = process->clone3(context.thread, ARGS, 88);
        if (result != -K_EBADF)
            testFail("clone3 CLONE_INTO_CGROUP returned %d, expected -EBADF", result);
        if (result > 0) stashChild(result, "clone3 cgroup (unexpected child)");
    }

    // A short clone_args is EINVAL (the base tree ignores size entirely).
    {
        writeArgs(0, K_SIGCHLD);
        aimCpu(context, SPIN_PAGE);
        S32 result = process->clone3(context.thread, ARGS, 40);
        if (result != -K_EINVAL)
            testFail("clone3 with size 40 returned %d, expected -EINVAL", result);
        if (result > 0) stashChild(result, "clone3 short args (unexpected child)");
    }

    // Namespace flags are EINVAL, never a kpanic (last: the base tree
    // aborts the whole test process on this call).
    {
        writeArgs(C_NEWNS, K_SIGCHLD);
        aimCpu(context, SPIN_PAGE);
        S32 result = process->clone3(context.thread, ARGS, 88);
        if (result != -K_EINVAL)
            testFail("clone3 CLONE_NEWNS returned %d, expected -EINVAL", result);
        if (result > 0) stashChild(result, "clone3 CLONE_NEWNS (unexpected child)");
    }
}

#endif
#endif
