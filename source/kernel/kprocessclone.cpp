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

#include "bwstepdiag.h"
#include "boxedwine.h"
#include "drvinittrace.h"
#include "kinotify.h"

#include "kscheduler.h"
#include "kpidfd.h"
#include "loader.h"
#include "kstat.h"
#include "bufferaccess.h"
#include "ksignal.h"
#include "wine32stack.h"
#include "kepoll.h"
#include "../io/fsmemnode.h"
#include "../io/fsmemopennode.h"
#include "../io/fsfileopennode.h"
#include "../io/fsfilenode.h"
#include "../x11/x11.h"
#include "kunixsocket.h"
#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#include "kmemory64.h"
#endif

#include <stdlib.h>
#include <unordered_set>
#include <string.h>
#include <stdio.h>
#include <time.h> 

// KProcess process-creation (clone/clone3/fork) implementations.
// Split from kprocess.cpp, which had grown past 5,300 lines covering
// every KProcess operation; the clone family is one self-contained
// topic: it uses no file-local helpers from kprocess.cpp, and its
// flag definitions and clone_args layout are used nowhere else.

#define CSIGNAL         0x000000ff      /* signal mask to be sent at exit */
#define K_CLONE_VM        0x00000100      /* set if VM shared between processes */
#define K_CLONE_FS        0x00000200      /* set if fs info shared between processes */
#define K_CLONE_FILES     0x00000400      /* set if open files shared between processes */
#define K_CLONE_SIGHAND   0x00000800      /* set if signal handlers and blocked signals shared */
#define K_CLONE_PTRACE    0x00002000      /* set if we want to let tracing continue on the child too */
#define K_CLONE_VFORK     0x00004000      /* set if the parent wants the child to wake it up on mm_release */
#define K_CLONE_PARENT    0x00008000      /* set if we want to have the same parent as the cloner */
#define K_CLONE_THREAD    0x00010000      /* Same thread group? */
#define K_CLONE_NEWNS     0x00020000      /* New namespace group? */
#define K_CLONE_SYSVSEM   0x00040000      /* share system V SEM_UNDO semantics */
#define K_CLONE_SETTLS    0x00080000      /* create a new TLS for the child */
#define K_CLONE_PARENT_SETTID     0x00100000      /* set the TID in the parent */
#define K_CLONE_CHILD_CLEARTID    0x00200000      /* clear the TID in the child */
#define K_CLONE_DETACHED          0x00400000      /* Unused, ignored */
#define K_CLONE_UNTRACED          0x00800000      /* set if the tracing process can't force K_CLONE_PTRACE on this clone */
#define K_CLONE_CHILD_SETTID      0x01000000      /* set the TID in the child */
#define K_CLONE_NEWCGROUP         0x02000000      /* New cgroup namespace */
#define K_CLONE_NS_MASK   (K_CLONE_NEWNS|K_CLONE_NEWUTS|K_CLONE_NEWIPC|K_CLONE_NEWUSER|K_CLONE_NEWPID|K_CLONE_NEWNET|K_CLONE_NEWCGROUP)
/* 0x02000000 was previously the unused K_CLONE_STOPPED (Start in stopped state)
and is now available for re-use. */
#define K_CLONE_NEWUTS            0x04000000      /* New utsname group? */
#define K_CLONE_NEWIPC            0x08000000      /* New ipcs */
#define K_CLONE_NEWUSER           0x10000000      /* New user namespace */
#define K_CLONE_NEWPID            0x20000000      /* New pid namespace */
#define K_CLONE_NEWNET            0x40000000      /* New network namespace */
#define K_CLONE_IO                0x80000000      /* Clone io context */

/**
 * struct clone_args - arguments for the clone3 syscall
 * @flags:        Flags for the new process as listed above.
 *                All flags are valid except for CSIGNAL and
 *                CLONE_DETACHED.
 * @pidfd:        If CLONE_PIDFD is set, a pidfd will be
 *                returned in this argument.
 * @child_tid:    If CLONE_CHILD_SETTID is set, the TID of the
 *                child process will be returned in the child's
 *                memory.
 * @parent_tid:   If CLONE_PARENT_SETTID is set, the TID of
 *                the child process will be returned in the
 *                parent's memory.
 * @exit_signal:  The exit_signal the parent process will be
 *                sent when the child exits.
 * @stack:        Specify the location of the stack for the
 *                child process.
 *                Note, @stack is expected to point to the
 *                lowest address. The stack direction will be
 *                determined by the kernel and set up
 *                appropriately based on @stack_size.
 * @stack_size:   The size of the stack for the child process.
 * @tls:          If CLONE_SETTLS is set, the tls descriptor
 *                is set to tls.
 * @set_tid:      Pointer to an array of type *pid_t. The size
 *                of the array is defined using @set_tid_size.
 *                This array is used to select PIDs/TIDs for
 *                newly created processes. The first element in
 *                this defines the PID in the most nested PID
 *                namespace. Each additional element in the array
 *                defines the PID in the parent PID namespace of
 *                the original PID namespace. If the array has
 *                less entries than the number of currently
 *                nested PID namespaces only the PIDs in the
 *                corresponding namespaces are set.
 * @set_tid_size: This defines the size of the array referenced
 *                in @set_tid. This cannot be larger than the
 *                kernel's limit of nested PID namespaces.
 * @cgroup:       If CLONE_INTO_CGROUP is specified set this to
 *                a file descriptor for the cgroup.
 *
 * The structure is versioned by size and thus extensible.
 * New struct members must go at the end of the struct and
 * must be properly 64bit aligned.
 
struct clone_args {
    __aligned_u64 flags;
    __aligned_u64 pidfd;
    __aligned_u64 child_tid;
    __aligned_u64 parent_tid;
    __aligned_u64 exit_signal;
    __aligned_u64 stack;
    __aligned_u64 stack_size;
    __aligned_u64 tls;
    __aligned_u64 set_tid;
    __aligned_u64 set_tid_size;
    __aligned_u64 cgroup;
};
*/
// CLONE_PIDFD as a file-scope constexpr, not a #define: clone364
// declares a same-named function-local constexpr, which a macro would
// break (a local may legally shadow this one).
static constexpr U32 K_CLONE_PIDFD = 0x00001000;

// 32-bit clone3 (kernel round 3C): validated like the 64-bit path
// (clone364) instead of the base implementation, which ignored size,
// dropped exit_signal and CLONE_CLEAR_SIGHAND by truncating the 64-bit
// flags word to U32, and silently accepted set_tid/cgroup requests.
U32 KProcess::clone3(KThread* thread, U32 args, U32 size) {
    constexpr U32 CLONE_ARGS_V0_SIZE = 64;
    constexpr U32 CLONE_ARGS_CURRENT_SIZE = 88;
    if (size < CLONE_ARGS_V0_SIZE) {
        return (U32)-K_EINVAL;
    }
    if (size > K_PAGE_SIZE) {
        return (U32)-K_E2BIG;
    }
    // Versioned by size: a larger structure is only accepted when the
    // extension bytes beyond the known layout are zero.
    for (U32 offset = CLONE_ARGS_CURRENT_SIZE; offset < size; offset++) {
        if (memory->readb(args + offset) != 0) {
            return (U32)-K_E2BIG;
        }
    }
    U64 flags = memory->readq(args);
    U64 pidfdAddr = memory->readq(args + 8);
    U64 child_tid = memory->readq(args + 16);
    U64 parent_tid = memory->readq(args + 24);
    U64 exit_signal = memory->readq(args + 32);
    U64 stack = memory->readq(args + 40);
    U64 stack_size = memory->readq(args + 48);
    U64 tls = memory->readq(args + 56);
    U64 set_tid = memory->readq(args + 64);
    U64 set_tid_size = memory->readq(args + 72);
    U64 cgroup = memory->readq(args + 80);

    if (exit_signal > 64) {
        return (U32)-K_EINVAL;
    }
    if (flags & ~0x3FFFFFFFFULL) { // bits above CLONE_INTO_CGROUP are unknown
        return (U32)-K_EINVAL;
    }
    // CLONE_INTO_CGROUP (bit 33) is unambiguous in clone3's 64-bit flags
    // word: cgroups are not implemented, so refuse EBADF like clone364.
    // A nonzero cgroup field without the flag is EINVAL.
    if (flags & (1ULL << 33)) {
        static bool warnedCgroup = false;
        if (!warnedCgroup) {
            warnedCgroup = true;
            klog("clone3: CLONE_INTO_CGROUP unsupported; returned -EBADF");
        }
        return (U32)-K_EBADF;
    }
    if (cgroup != 0) {
        return (U32)-K_EINVAL;
    }
    if (set_tid_size != 0) {
        if (set_tid_size > 32) {
            return (U32)-K_EINVAL;
        }
        if (!set_tid) {
            return (U32)-K_EFAULT;
        }
        // set_tid (choosing the child's pid) stays REJECTED-LOUDLY,
        // matching clone364: pids are assigned by the emulator.
        static bool warnedSetTid = false;
        if (!warnedSetTid) {
            warnedSetTid = true;
            klog("clone3: set_tid unsupported; returned -EPERM");
        }
        return (U32)-K_EPERM;
    }
    if ((flags & K_CLONE_PIDFD) && !pidfdAddr) {
        return (U32)-K_EFAULT;
    }
    if ((flags & K_CLONE_PIDFD) && (flags & K_CLONE_THREAD)) {
        return (U32)-K_EINVAL; // matches clone364
    }
    // The exit signal travels in clone()'s CSIGNAL byte; bit 32
    // (CLONE_CLEAR_SIGHAND) is handed down explicitly. CLONE_PIDFD is
    // stripped: clone() would alias it onto parent_tid, but clone3 has
    // its own pidfd out-slot, written below. Namespace flags, the
    // thread requirements and the remaining low-word dispositions are
    // all enforced by clone() itself.
    const bool clearSighand = (flags & (1ULL << 32)) != 0;
    U32 result = clone(thread, (U32)((flags & ~0x1000ULL) | (exit_signal & CSIGNAL)), (U32)(stack + stack_size), (U32)parent_tid, (U32)tls, (U32)child_tid, clearSighand);
    if ((S32)result > 0 && (flags & K_CLONE_PIDFD)) {
        // clone3's pidfd has its own out-slot (clone_args.pidfd), unlike
        // legacy clone where it aliases parent_tid.
        U32 pidfd = createPidFdForProcess(thread, result);
        if ((S32)pidfd < 0) {
            return pidfd;
        }
        memory->writed((U32)pidfdAddr, pidfd);
    }
    return result;
}

U32 KProcess::clone(KThread* thread, U32 flags, U32 child_stack, U32 ptid, U32 tls, U32 ctid, bool clearSighand) {    
    // The exit signal rides in the low byte (CSIGNAL). It is part of the
    // child contract, not a creation flag: capture it before masking.
    // The base code masked first and never stored it, so every child of a
    // legacy clone reported the SIGCHLD default to its parent.
    const U32 exitSignal = flags & CSIGNAL;
    flags &= ~CSIGNAL;

    if (!(flags & K_CLONE_THREAD)) { // new thread group (process)
        bool vFork = false;
        if (flags & K_CLONE_VFORK) {
            flags &=~K_CLONE_VFORK;
            vFork = true;
        }
        bool vm = false;
        if (flags & K_CLONE_VM) {
            flags &=~K_CLONE_VM;
            vm = true;
        }
        // Flag dispositions (kernel round 3C). Every bit of the flags word
        // is accounted for: implemented, accepted because it has no
        // observable effect in this emulator, or refused loudly with the
        // errno Linux uses. The base code kpanic'd on anything outside a
        // small allowlist, letting a guest take the whole emulator down
        // with one clone call.
        if (flags & K_CLONE_NS_MASK) {
            // Namespace creation is not implemented (EINVAL, once-logged
            // per fresh flag bit, mirroring clone64).
            static U32 warnedNsFlags = 0;
            const U32 fresh = (flags & K_CLONE_NS_MASK) & ~warnedNsFlags;
            if (fresh) {
                warnedNsFlags |= flags & K_CLONE_NS_MASK;
                klog_fmt("clone: namespace flags 0x%X unsupported; returned -EINVAL", fresh);
            }
            return (U32)-K_EINVAL;
        }
        if ((flags & K_CLONE_SIGHAND) && !vm) {
            // Linux requires CLONE_VM with CLONE_SIGHAND.
            return (U32)-K_EINVAL;
        }
        if ((flags & K_CLONE_PIDFD) && !ptid) {
            // Legacy clone returns the pidfd through the parent_tid slot.
            return (U32)-K_EFAULT;
        }
        // CLONE_PTRACE / CLONE_UNTRACED / CLONE_DETACHED are accepted:
        // ptrace is not implemented and there is no detached state to
        // model, so they have no observable effect. CLONE_SYSVSEM and
        // CLONE_IO name subsystems whose syscalls are ENOSYS here, so
        // there is no shared undo/io state to model either.
        constexpr U32 PROCESS_KNOWN_FLAGS = K_CLONE_CHILD_SETTID|K_CLONE_CHILD_CLEARTID|K_CLONE_PARENT_SETTID|
            K_CLONE_FS|K_CLONE_FILES|K_CLONE_SIGHAND|K_CLONE_PARENT|K_CLONE_PIDFD|K_CLONE_SETTLS|
            K_CLONE_PTRACE|K_CLONE_UNTRACED|K_CLONE_DETACHED|K_CLONE_SYSVSEM|K_CLONE_IO;
        if (flags & ~PROCESS_KNOWN_FLAGS) {
            kwarn_fmt("clone: unhandled flag bits 0x%X; returned -EINVAL", (U32)(flags & ~PROCESS_KNOWN_FLAGS));
            return (U32)-K_EINVAL;
        }
        KProcessPtr newProcess = KProcess::create();
        newProcess->memory = KMemory::create(newProcess.get());
        newProcess->memory->clone(this->memory, vm);

        KThread* newThread = newProcess->createThread();

        newProcess->parentId = this->id;        
        newProcess->cloneVM = vm;
        newProcess->clone(shared_from_this(), flags & (K_CLONE_FS|K_CLONE_FILES|K_CLONE_SIGHAND));
        newProcess->childExitSignal64 = exitSignal;
        if (flags & K_CLONE_PARENT) {
            // CLONE_PARENT: the child is our sibling, not our child.
            newProcess->parentId = this->parentId;
        }
        if (clearSighand) {
            // CLONE_CLEAR_SIGHAND (clone3 only): all dispositions reset.
            // Runs after the CLONE_SIGHAND adoption inside clone(from),
            // so a combined SIGHAND|CLEAR_SIGHAND flushes the shared
            // table, matching the kernel's copy-then-clear order.
            for (auto& action : newProcess->sigActions) action.reset();
        }
        newThread->clone(thread);
        if ((flags & K_CLONE_SETTLS) && tls) {
            // Mirror the thread path's TLS install for process clones.
            struct user_desc desc;
            memory->memcpy(&desc, tls, sizeof(struct user_desc));
            if (desc.base_addr != 0 && desc.entry_number != 0) {
                struct user_desc* ldt = newThread->getLDT(desc.entry_number);
                *ldt = desc;
                newThread->cpu->setSegment(GS, desc.entry_number << 3);
            }
        }

        // will only create them if they are missing
        //newProcess->initStdio();
        
        if ((flags & K_CLONE_CHILD_SETTID)!=0) {
            if (ctid!=0) {
                ChangeThread c(newThread); // so that writed will go to the new memory space
                newThread->memory->writed(ctid, newThread->id);
            }
        }
        if ((flags & K_CLONE_CHILD_CLEARTID)!=0) {
            newThread->clear_child_tid = ctid;
        }
        if ((flags & K_CLONE_PARENT_SETTID)!=0) {
            // CLONE_PARENT_SETTID(since Linux 2.5.49)
            // Store the child thread ID at the location pointed to by
            // parent_tid(clone()) or cl_args.parent_tid(clone3()) in
            // the parent's memory.  (In Linux 2.5.32-2.5.48 there was a
            // flag CLONE_SETTID that did this.)  The store operation
            // completes before the clone call returns control to user
            // space.
            if (ptid) {                
                memory->writed(ptid, newThread->id);
            }
        }
        if (flags & K_CLONE_PIDFD) {
            U32 pidfd = createPidFdForProcess(thread, newProcess->id);
            if ((S32)pidfd < 0) {
                // The child already exists; the only failures are ESRCH
                // (impossible here) or EMFILE in the caller's table.
                kwarn_fmt("clone: CLONE_PIDFD allocation failed (%d) for child %u", (S32)pidfd, newProcess->id);
            } else {
                memory->writed(ptid, pidfd);
            }
        }
        if (child_stack!=0) {
            newThread->cpu->reg[4].u32 = child_stack;
        }
        if (vm) {
            // vfork: child shares the parent's VM until execve. The CPU was
            // cloned from the parent above (EIP at the int 0x80); step over it.
            // (Was: eip = peek32(0), always 0 -> EIP=0 fault loop.)
            newThread->cpu->eip.u32 += 2; // step over clone call in the new thread
        } else {
            newThread->cpu->eip.u32 += 2; // step over clone call in the new thread
        }
        newThread->cpu->reg[0].u32 = 0;
        //runThreadSlice(newThread); // if the new thread runs before the current thread, it will likely call exec which will prevent unnessary copy on write actions when running the current thread first        
        if (vFork) {
#ifndef BOXEDWINE_MULTI_THREADED
            // don't re-enter when we wake up
            thread->cpu->eip.u32+=2; // don't re-enter
            thread->cpu->reg[0].u32 = newProcess->id;
#endif
            {
                BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(newProcess->exitOrExecCond);
                scheduleThread(newThread);
                BOXEDWINE_CONDITION_WAIT(newProcess->exitOrExecCond);
            }
#ifdef BOXEDWINE_MULTI_THREADED
			if (thread->terminating) {
				return -K_EINTR;
			}
#endif
        } else {
            //klog("starting %d/%d", newThread->process->id, newThread->id);            
            scheduleThread(newThread);
        }
        return newProcess->id;
    } else { // CLONE_THREAD: a new thread in this process
        // Dispositions (kernel round 3C): the base code required one exact
        // flag mask and kpanic'd on any deviation — including
        // CLONE_CHILD_SETTID, which glibc can legitimately present.
        // Linux requires CLONE_VM|CLONE_SIGHAND with CLONE_THREAD and
        // forbids CLONE_PARENT / CLONE_PIDFD / CLONE_VFORK there.
        if ((flags & (K_CLONE_VM | K_CLONE_SIGHAND)) != (K_CLONE_VM | K_CLONE_SIGHAND) ||
            (flags & (K_CLONE_PARENT | K_CLONE_PIDFD | K_CLONE_VFORK))) {
            return (U32)-K_EINVAL;
        }
        if (flags & K_CLONE_NS_MASK) {
            static U32 warnedNsFlags = 0;
            const U32 fresh = (flags & K_CLONE_NS_MASK) & ~warnedNsFlags;
            if (fresh) {
                warnedNsFlags |= flags & K_CLONE_NS_MASK;
                klog_fmt("clone: namespace flags 0x%X unsupported; returned -EINVAL", fresh);
            }
            return (U32)-K_EINVAL;
        }
        constexpr U32 THREAD_KNOWN_FLAGS = K_CLONE_THREAD|K_CLONE_VM|K_CLONE_FS|K_CLONE_FILES|K_CLONE_SIGHAND|
            K_CLONE_SETTLS|K_CLONE_PARENT_SETTID|K_CLONE_CHILD_SETTID|K_CLONE_CHILD_CLEARTID|K_CLONE_SYSVSEM|
            K_CLONE_PTRACE|K_CLONE_UNTRACED|K_CLONE_DETACHED|K_CLONE_IO;
        if (flags & ~THREAD_KNOWN_FLAGS) {
            kwarn_fmt("clone: unhandled thread flag bits 0x%X; returned -EINVAL", (U32)(flags & ~THREAD_KNOWN_FLAGS));
            return (U32)-K_EINVAL;
        }
        KThread* newThread = this->createThread();
        newThread->clone(thread); // inherit parent CPU (EIP at int 0x80, regs, segs, FPU) - MUST come before TLS setup
        if ((flags & K_CLONE_SETTLS) && tls) {
            struct user_desc desc;
            memory->memcpy(&desc, tls, sizeof(struct user_desc));
#ifdef BW64_INST_TRACE
            klog_fmt("INSTTRACE CLONE_TLS tid=%u tls_ptr=%08X base=%08X entry=%u", newThread->id, tls, desc.base_addr, desc.entry_number);
#endif
            if (desc.base_addr!=0 && desc.entry_number!=0) {
                struct user_desc* ldt = newThread->getLDT(desc.entry_number);
                *ldt = desc;
                newThread->cpu->setSegment(GS, desc.entry_number << 3);
            }
        }
        if (flags & K_CLONE_CHILD_CLEARTID) {
            newThread->clear_child_tid = ctid;
        }
        if ((flags & K_CLONE_PARENT_SETTID) && ptid) {
            memory->writed(ptid, newThread->id);
        }
        if (child_stack != 0) {
            newThread->cpu->reg[4].u32 = child_stack; // ESP = new thread stack
        }
        newThread->cpu->eip.u32 += 2; // step over clone call in the new thread
        newThread->cpu->reg[0].u32 = 0; // child returns 0 from clone
        if ((flags & K_CLONE_CHILD_SETTID) && ctid) {
            // The address space is shared (CLONE_VM), so this store lands
            // in the child's memory too, before the thread can run.
            memory->writed(ctid, newThread->id);
        }
        scheduleThread(newThread);
        return newThread->id;
    }
}

#ifdef BOXEDWINE_GUEST_X64
U32 createPidFdForClone64(KThread* thread, U32 processId);

U32 KProcess::clone64(KThread* thread, U64 flags, U64 child_stack, U64 ptid, U64 tls, U64 ctid, U64 exitSignal) {
    // pthread_create issues clone with the thread-group flag set. We only
    // support the thread case (shared VM/FS/files/sighand); a real fork() in
    // 64-bit is a separate, larger effort. Anything else: log + ENOSYS so
    // glibc surfaces a clean failure instead of us kpanic'ing.
    // Namespace creation is rejected loudly (kernel round 3A): the emulator
    // has exactly one namespace of each kind, so CLONE_NEW* can never be
    // honoured. Warn once per flag bit, then EINVAL like an unprivileged
    // caller without CAP_SYS_ADMIN in the namespace.
    constexpr U64 K_CLONE_NEWCGROUP_BIT = 0x2000000ULL;
    constexpr U64 K_CLONE_PIDFD_BIT = 0x1000ULL;
    constexpr U64 NS_FLAGS = K_CLONE_NEWNS | K_CLONE_NEWUTS | K_CLONE_NEWIPC |
        K_CLONE_NEWUSER | K_CLONE_NEWPID | K_CLONE_NEWNET | K_CLONE_NEWCGROUP_BIT;
    if (flags & NS_FLAGS) {
        static U64 warnedNsFlags = 0;
        const U64 fresh = (flags & NS_FLAGS) & ~warnedNsFlags;
        if (fresh) {
            warnedNsFlags |= flags & NS_FLAGS;
            klog_fmt("clone64: namespace flags 0x%llx unsupported; returned -EINVAL",
                     (unsigned long long)fresh);
        }
        return (U32)-K_EINVAL;
    }
    if ((flags & K_CLONE_PIDFD_BIT) && (flags & K_CLONE_THREAD)) {
        return (U32)-K_EINVAL;
    }
    const U64 THREAD_FLAGS = K_CLONE_VM | K_CLONE_THREAD | K_CLONE_SIGHAND;
    if ((flags & THREAD_FLAGS) != THREAD_FLAGS) {
        // Non-thread clone == real fork(2). wine forks to spawn wineserver (and
        // its services), then the child execve's the new image. Implement a real
        // fork: a new KProcess with its own memory64 (a deep copy of ours) and a
        // child CPU64 that returns 0. child_stack==0 here (fork keeps the same
        // stack pointer), unlike thread-clone, so don't require it.
        return this->forkProcess64(thread, flags, ctid, ptid, exitSignal, child_stack);
    }
    if (child_stack == 0) {
        return (U32)-K_EINVAL;
    }

    KThread* newThread = this->createThread();
    // New per-thread CPU64 sharing this process's one memory64.
    CPU64* parentCpu = thread->cpu64 ? thread->cpu64 : this->cpu64;
    CPU64* childCpu = new CPU64(this->memory64);
    childCpu->cloneRegistersFrom(parentCpu);
    // Linux sas_ss_reset (kernel/fork.c): a CLONE_VM child that is not a
    // vfork starts with NO alternate signal stack; a thread never
    // inherits its creator's sigaltstack.
    childCpu->sigAltStack = {};
    childCpu->thread = newThread;
    newThread->cpu64 = childCpu;

    // clone ABI for the child:
    //   - returns 0 in the child (parent gets the tid);
    //   - resumes at the instruction after the parent's SYSCALL — the parent's
    //     RIP already points there (SYSCALL advanced it before ksyscall64);
    //   - RSP = child_stack (glibc passes the top of the new thread stack);
    //   - fsbase = tls (CLONE_SETTLS — glibc's TLS/TCB pointer for the child).
    childCpu->reg[X64_RAX].setU64(0);
    childCpu->reg[X64_RSP].setU64(child_stack);
    if (flags & K_CLONE_SETTLS) {
        childCpu->fsbase = tls;
    }
    // childCpu->rip is already the parent's post-SYSCALL RIP via the register
    // copy, which is exactly where the child should begin.

    if (flags & K_CLONE_CHILD_CLEARTID) {
        newThread->clear_child_tid64 = ctid;
    }
    if (flags & K_CLONE_CHILD_SETTID) {
        this->memory64->writed(ctid, newThread->id);
    }
    if (flags & K_CLONE_PARENT_SETTID) {
        this->memory64->writed(ptid, newThread->id);
    }

    // BW64_THREADLOG: witness how many host threads each guest process runs.
    // Decisive for bug #2: if wineserver only ever has ONE guest thread, the
    // "concurrent async-queue list surgery" race is impossible and the
    // free_async_queue double-release must be a request-ORDERING issue instead.
    if (getenv("BW64_THREADLOG")) {
        klog_fmt("THREADLOG: clone64 pid=%u (%s) -> new tid=%u (parent tid=%u)",
                 (unsigned)this->id,
                 this->exe.length() ? this->exe.c_str() : this->name.c_str(),
                 (unsigned)newThread->id, (unsigned)thread->id);
    }
    scheduleThread(newThread);
    return newThread->id;
}

U64 KProcess::clone364(KThread* thread, U64 argsAddr, U64 size) {
    // Read the clone_args struct from guest memory64 (see the struct comment
    // above). Modern glibc (2.34+) issues clone3 first and only falls back to
    // clone on ENOSYS — that fallback path is fragile in our interpreter, so
    // implement clone3 directly and route to clone64.
    constexpr U64 CLONE_ARGS_V0_SIZE = 64;
    constexpr U64 CLONE_ARGS_CURRENT_SIZE = 88;
    if (!this->memory64 || size < CLONE_ARGS_V0_SIZE) {
        return (U64)-K_EINVAL;
    }
    if (size > K_PAGE_SIZE) return (U64)-K_E2BIG;
    if (argsAddr > ~0ULL - size) return (U64)-K_EFAULT;
    const U64 firstPage = argsAddr >> K64_PAGE_SHIFT;
    const U64 lastPage = (argsAddr + size - 1) >> K64_PAGE_SHIFT;
    for (U64 page = firstPage;; ++page) {
        const U32 pageFlags = this->memory64->getPageFlags(page);
        if (!(pageFlags & K64_PAGE_MAPPED) || !(pageFlags & K64_PAGE_READ)) return (U64)-K_EFAULT;
        if (page == lastPage) break;
    }
    // clone_args is versioned by size. Newer kernels accept a larger structure
    // only when unknown extension bytes are zero, so callers can probe safely.
    for (U64 offset = CLONE_ARGS_CURRENT_SIZE; offset < size; ++offset) {
        if (this->memory64->readb(argsAddr + offset) != 0) return (U64)-K_E2BIG;
    }
    U64 args[CLONE_ARGS_CURRENT_SIZE / sizeof(U64)] = {};
    const U64 copiedSize = size < sizeof(args) ? size : sizeof(args);
    this->memory64->memcpyFromGuest(args, argsAddr, copiedSize);
    U64 flags       = args[0];
    U64 pidfdAddr   = args[1];
    U64 child_tid   = args[2];
    U64 parent_tid  = args[3];
    U64 exit_signal = args[4];
    U64 stack       = args[5];
    U64 stack_size  = args[6];
    U64 tls         = args[7];
    constexpr U64 K_CLONE_PIDFD = 0x00001000;
    if ((flags & K_CLONE_PIDFD) && (flags & K_CLONE_THREAD)) return (U64)-K_EINVAL;
    if (flags & K_CLONE_PIDFD) {
        if (!pidfdAddr || pidfdAddr > ~0ULL - 3) return (U64)-K_EFAULT;
        const U64 first = pidfdAddr >> K64_PAGE_SHIFT;
        const U64 last = (pidfdAddr + 3) >> K64_PAGE_SHIFT;
        for (U64 page = first;; ++page) {
            const U32 pageFlags = this->memory64->getPageFlags(page);
            if (!(pageFlags & K64_PAGE_MAPPED) || !(pageFlags & K64_PAGE_WRITE)) return (U64)-K_EFAULT;
            if (page == last) break;
        }
    }
    // clone3 carries the exit signal in its own field; the low CSIGNAL bits
    // are invalid in flags (unlike legacy clone(2)).
    if ((flags & 0xff) || exit_signal > 64 ||
        ((stack == 0) != (stack_size == 0)) || stack > ~0ULL - stack_size) {
        return (U64)-K_EINVAL;
    }
    // clone3 passes the stack base + size; the child's RSP is the top.
    // Round 3A validation, per clone3(2): unknown flag bits, the flag
    // combinations clone3 forbids, set_tid and CLONE_INTO_CGROUP. Known
    // flag bits are CSIGNAL (rejected above), bits 0x100..0x80000000,
    // CLONE_CLEAR_SIGHAND (bit 32) and CLONE_INTO_CGROUP (bit 33).
    if (flags & ~0x3FFFFFFF00ULL) return (U64)-K_EINVAL;
    constexpr U64 NS_FLAGS3 = 0x20000ULL | 0x4000000ULL | 0x8000000ULL | 0x10000000ULL |
        0x20000000ULL | 0x40000000ULL | 0x2000000ULL;
    if (flags & NS_FLAGS3) {
        static U64 warnedNs3 = 0;
        const U64 fresh = (flags & NS_FLAGS3) & ~warnedNs3;
        if (fresh) {
            warnedNs3 |= flags & NS_FLAGS3;
            klog_fmt("clone3: namespace flags 0x%llx unsupported; returned -EINVAL",
                     (unsigned long long)fresh);
        }
        return (U64)-K_EINVAL;
    }
    constexpr U64 F_THREAD = 0x10000ULL, F_VM = 0x100ULL, F_SIGHAND = 0x800ULL;
    if ((flags & F_THREAD) && ((flags & (F_VM | F_SIGHAND)) != (F_VM | F_SIGHAND))) return (U64)-K_EINVAL;
    if ((flags & F_SIGHAND) && !(flags & F_VM)) return (U64)-K_EINVAL;
    // set_tid: choosing the child tid requires CAP_SYS_ADMIN in the pid
    // namespace; guests run as unprivileged uid 1000, so a well-formed
    // request is EPERM (logged once), malformed ones EINVAL/EFAULT.
    const U64 setTidAddr = args[8];
    const U64 setTidSize = args[9];
    if (setTidSize > 32) return (U64)-K_EINVAL;
    if (setTidSize > 0) {
        if (!setTidAddr) return (U64)-K_EFAULT;
        const U64 first = setTidAddr >> K64_PAGE_SHIFT;
        const U64 last = (setTidAddr + setTidSize * 4 - 1) >> K64_PAGE_SHIFT;
        for (U64 page = first;; ++page) {
            const U32 pageFlags = this->memory64->getPageFlags(page);
            if (!(pageFlags & K64_PAGE_MAPPED) || !(pageFlags & K64_PAGE_READ)) return (U64)-K_EFAULT;
            if (page == last) break;
        }
        static bool warnedSetTid = false;
        if (!warnedSetTid) {
            warnedSetTid = true;
            klog("clone3: set_tid unsupported for unprivileged guests; returned -EPERM");
        }
        return (U64)-K_EPERM;
    }
    // cgroup: a nonzero fd is only meaningful with CLONE_INTO_CGROUP, and
    // there is no cgroupfs here to validate it against.
    if (args[10] != 0) {
        if (!(flags & 0x200000000ULL)) return (U64)-K_EINVAL;
        static bool warnedCgroup = false;
        if (!warnedCgroup) {
            warnedCgroup = true;
            klog("clone3: CLONE_INTO_CGROUP unsupported (no cgroupfs); returned -EBADF");
        }
        return (U64)-K_EBADF;
    }
    U64 result = clone64(thread, flags, stack + stack_size, parent_tid, tls, child_tid, exit_signal);
    if ((S32)result < 0) return (U64)(S64)(S32)result;
    if (!(flags & K_CLONE_PIDFD)) return result;
    U32 pidfd = createPidFdForClone64(thread, (U32)result);
    if ((S32)pidfd < 0) return (U64)(S64)(S32)pidfd;
    this->memory64->writed(pidfdAddr, pidfd);
    return result;
}

U32 KProcess::forkProcess64(KThread* thread, U64 flags, U64 ctid, U64 ptid, U64 exitSignal, U64 childStack) {
    if (!this->memory64) {
        return (U32)-K_ENOSYS;
    }
    CPU64* parentCpu = thread->cpu64 ? thread->cpu64 : this->cpu64;

    // New process sharing nothing but a snapshot. KProcess::clone() copies the
    // FD table, cwd, exe, sigActions, etc. (arch-neutral); we add the 64-bit
    // bits (memory64 deep-copy, is64Bit, brk/phdr, CPU64) by hand.
    KProcessPtr newProcess = KProcess::create();
    newProcess->memory = KMemory::create(newProcess.get());  // unused 32-bit side
    newProcess->clone(shared_from_this(), (U32)(flags & (K_CLONE_FS | K_CLONE_FILES | K_CLONE_SIGHAND))); // FDs, cwd, exe, sig (shared when the clone flags ask for it)
    if (flags & K_CLONE_PARENT) {
        // CLONE_PARENT: the child is a sibling, reparented to our parent.
        newProcess->parentId = this->parentId;
    }

    newProcess->is64Bit = true;
    newProcess->childExitSignal64 = (U32)exitSignal;
    newProcess->brkEnd64 = this->brkEnd64;
    newProcess->entry64 = this->entry64;
    newProcess->phdr64 = this->phdr64;
    newProcess->phentsize64 = this->phentsize64;
    newProcess->phnum64 = this->phnum64;
    newProcess->startupArgs64 = this->startupArgs64;
    newProcess->startupEnv64 = this->startupEnv64;

    // Address space: shared for vfork, a deep copy otherwise.
    if (flags & K_CLONE_VFORK) {
        // vfork: the child runs in the SAME address space until it execs or
        // exits (the parent is suspended below until then). Exec releases
        // the child's reference (the object is deleted only when the last
        // sharer releases it, see the is64Bit branch of execve) and exit
        // just releases the reference: nothing deletes memory64 at exit.
        this->memory64->retainVmRef();
        newProcess->memory64 = this->memory64;
        newProcess->memory64Shared = true;
    } else if (flags & K_CLONE_VM) {
        // CLONE_VM without vfork (kernel round 3C): the child shares the
        // address space for real, like vfork, but the parent is NOT
        // suspended and keeps running concurrently with the child. The
        // exec/exit reference rules are the same as vfork's.
        this->memory64->retainVmRef();
        newProcess->memory64 = this->memory64;
        newProcess->memory64Shared = true;
    } else {
        newProcess->memory64 = new KMemory64(newProcess.get());
        newProcess->memory64->cloneFrom(this->memory64);
    }

    // The child's main thread + its CPU64 (own register file, shared with no
    // one), seeded from the parent's registers. fork returns 0 in the child;
    // RIP/RSP are inherited so it resumes right after the SYSCALL on the same
    // (copied) stack.
    KThread* newThread = newProcess->createThread();
    CPU64* childCpu = new CPU64(newProcess->memory64);
    childCpu->cloneRegistersFrom(parentCpu);
    if (flags & K_CLONE_SIGHAND) {
        // Signal delivery consults the CPU64 table, so a CLONE_SIGHAND
        // child shares it too (the KProcess table was shared inside
        // KProcess::clone above). This runs before the CLEAR_SIGHAND
        // reset below, so a combined CLONE_SIGHAND|CLONE_CLEAR_SIGHAND
        // flushes the shared table, matching the kernel's order.
        childCpu->sigActions.shareFrom(parentCpu->sigActions);
    }
    if (flags & 0x100000000ULL) { // CLONE_CLEAR_SIGHAND
        // All dispositions reset to SIG_DFL: both the process table and the
        // CPU64 table (the one signal delivery consults).
        for (auto& action : newProcess->sigActions) action.reset();
        for (auto& action : childCpu->sigActions) action = CPU64::SigAction();
    }
    if (childStack) {
        // clone/clone3 with an explicit stack: the child starts there
        // (clone3 already converted base+size to the top).
        childCpu->reg[X64_RSP].setU64(childStack);
    }
    if ((flags & K_CLONE_VM) && !(flags & K_CLONE_VFORK)) {
        // sas_ss_reset (kernel/fork.c): CLONE_VM children that are not
        // vfork start with no alternate signal stack. Plain fork and
        // vfork (no CLONE_VM, or CLONE_VM|CLONE_VFORK) inherit it.
        childCpu->sigAltStack = {};
    }
    childCpu->thread = newThread;
    childCpu->memory = newProcess->memory64;
    childCpu->reg[X64_RAX].setU64(0);   // child sees fork() == 0
    newProcess->cpu64 = childCpu;
    newThread->cpu64 = childCpu;

    if (flags & K_CLONE_CHILD_SETTID) {
        // Write into the CHILD's address space (its own memory64 copy).
        if (ctid) newProcess->memory64->writed(ctid, newThread->id);
    }
    if (flags & K_CLONE_CHILD_CLEARTID) {
        newThread->clear_child_tid64 = ctid;
    }
    if (flags & K_CLONE_PARENT_SETTID) {
        if (ptid) this->memory64->writed(ptid, newProcess->id);
    }

    scheduleThread(newThread);
    if (flags & K_CLONE_VFORK) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(newProcess->exitOrExecCond);
        while (!newProcess->vforkReleased) {
            BOXEDWINE_CONDITION_WAIT(newProcess->exitOrExecCond);
        }
#ifdef BOXEDWINE_MULTI_THREADED
        if (thread->terminating) return (U32)-K_EINTR;
#endif
    }
    return newProcess->id;   // parent sees the child pid
}
#endif
