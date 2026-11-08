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

#ifndef __KSIGNAL_H__
#define __KSIGNAL_H__

#include "kobject.h"

#define K_SIG_DFL 0
#define K_SIG_IGN 1

#define K_SIGHUP           1
#define K_SIGINT           2
#define K_SIGQUIT          3
#define K_SIGILL           4
#define K_SIGTRAP          5
#define K_SIGABRT          6
#define K_SIGIOT           6
#define K_SIGBUS           7
#define K_SIGFPE           8
#define K_SIGKILL          9
#define K_SIGUSR1         10
#define K_SIGSEGV         11
#define K_SIGUSR2         12
#define K_SIGPIPE         13
#define K_SIGALRM         14
#define K_SIGTERM         15
#define K_SIGSTKFLT       16
#define K_SIGCHLD         17
#define K_SIGCONT         18
#define K_SIGSTOP         19
#define K_SIGTSTP         20
#define K_SIGTTIN         21
#define K_SIGTTOU         22
#define K_SIGURG          23
#define K_SIGXCPU         24
#define K_SIGXFSZ         25
#define K_SIGVTALRM       26
#define K_SIGPROF         27
#define K_SIGWINCH        28
#define K_SIGIO           29
#define K_SIGPOLL         SIGIO
/*
#define SIGLOST         29
*/
#define K_SIGPWR          30
#define K_SIGSYS          31
#define K_SIGUNUSED       31

// On 32-bit Debian 10
// / include / i386 - linux - gnu / bits / siginfo - consts.h:  FPE_INTDIV = 1,		/* Integer divide by zero.  */
// / include / asm - generic / siginfo.h : #define FPE_INTDIV	1	/* integer divide by zero */

#define K_FPE_INTDIV	1	/* integer divide by zero */
#define K_FPE_INTOVF	2	/* integer overflow */
#define K_FPE_FLTDIV	3	/* floating point divide by zero */
#define K_FPE_FLTOVF	4	/* floating point overflow */
#define K_FPE_FLTUND	5	/* floating point underflow */
#define K_FPE_FLTRES	6	/* floating point inexact result */
#define K_FPE_FLTINV	7	/* floating point invalid operation */

// FD-205: identical-fault loop containment. When a guest fault cannot be
// handled, the guest's handler returns to the faulting instruction without
// fixing the underlying fault, and the emulator re-delivers the identical
// fault (same signal, same faulting instruction, same fault address)
// forever -- observed past 28 million deliveries on 64-bit (a fetch fault
// at one address) and past 5.5 million (a data fault), and as a 64-line
// guest-memfault32 loop in a live session. BW64_FAULT_LOOP_LIMIT is how
// many consecutive identical deliveries a thread may receive before the
// fault is contained by ending the faulting thread's process -- the
// Linux/Wine outcome for an unhandled fault -- instead of delivering
// again. A different fault (different faulting instruction or address)
// resets the count. Legitimate repeats at one site are 1-2 per site
// (demand paging, guard-page and stack-growth faults each recur at a new
// address), and a handler that fixes the fault and re-faults repeats a
// handful of times, so 128 sits far above any legitimate recurrence and
// far below the observed runaway counts. The launcher's pre-handshake
// fault-loop trip (32 delivered faults from one thread) is a different,
// stricter context: before the desktop handshake no fault should recur
// at all. Shared by the 64-bit raiseSyncFault path and the 32-bit
// seg_mapper/seg_access/prepareException paths.
#define BW64_FAULT_LOOP_LIMIT 128

#define K_SS_ONSTACK 1
#define K_SS_DISABLE 4

#define K_SIG_BLOCK 0
#define K_SIG_UNBLOCK 1
#define K_SIG_SETMASK 2

#define K_SA_NOCLDSTOP  1          /* Don't send SIGCHLD when children stop.  */
#define K_SA_NOCLDWAIT  2          /* Don't create zombie on child death.  */
#define K_SA_SIGINFO    4          /* Invoke signal-catching function with
                                    three arguments instead of one.  */

#define K_SA_RESTORER     0x04000000u
#define K_SA_ONSTACK      0x08000000u
#define K_SA_RESTART      0x10000000u /* Restart syscall on signal return.  */
#define K_SA_NODEFER      0x40000000u
#define K_SA_RESETHAND    0x80000000u

#define K_SI_USER 0

#define K_POLL_IN         1   /* data input available */
#define K_POLL_OUT        2   /* output buffers available */
#define K_POLL_MSG        3   /* input message available */
#define K_POLL_ERR        4   /* i/o error */
#define K_POLL_PRI        5   /* high priority input available */
#define K_POLL_HUP        6   /* device disconnected */

#define K_CLD_EXITED 1

U32 syscall_signalstack(U32 ss, U32 oss);
U32 syscall_signalfd4(KThread* thread, S32 fildes, U32 mask, U32 maskSize, U32 flags);

class KThread;
class KFileLock;

class KSignal : public KObject {
public:
    KSignal() : KObject(KTYPE_SIGNAL), lockCond(std::make_shared<BoxedWineCondition>(B("KSignal::lockCond"))) {}

    // from KObject
    U32 ioctl(KThread* thread, U32 request) override;
    S64 seek(S64 pos) override;
    S64 length() override;
    S64 getPos() override;
    void setBlocking(bool blocking) override;
    bool isBlocking() override;
    void setAsync(bool isAsync) override;
    bool isAsync() override;
    KFileLock* getLock(KFileLock* lock) override;
    U32 setLock(KFileLock* lock, bool wait) override;
    bool supportsLocks() override;
    bool isOpen() override;
    bool isReadReady() override;
    bool isWriteReady() override;
    void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override;
    U32 writeNative(U8* buffer, U32 len) override;
    U32 readNative(U8* buffer, U32 len) override;
    U32 stat(KProcess* process, U32 address, bool is64) override;
    U32 map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) override;
    bool canMap() override;
    BString selfFd() override;

    bool blocking = false;
    U64 mask = 0;
    U32 signalingPid = 0;
    U32 signalingUid = 0;
    BOXEDWINE_CONDITION lockCond;
    KSigAction sigAction;
};

#endif