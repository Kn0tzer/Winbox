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

#ifndef __KTHREAD_H__
#define __KTHREAD_H__

#include <atomic>

#define MAX_POLL_DATA 256

#define TLS_ENTRIES 10
#define TLS_ENTRY_START_INDEX 10

class OpenGLVetexPointer {
public:
    OpenGLVetexPointer() = default;
    U32 size = 0;
    U32 type = 0;
    U32 stride = 0;
    U32 count = 0; // used by marshalEdgeFlagPointerEXT
    U32 ptr = 0;
    U8* marshal = nullptr;
    U32 lastMarshalledPtr = 0;
    U32 marshal_size = 0;
    U32 refreshEachCall = 0;
    bool enabled = false;
    bool normalized = false;
    bool isArrayBuffer = false;
    bool isVertexAttrib = false;
};
typedef std::shared_ptr<OpenGLVetexPointer> OpenGLVetexPointerPtr;

class KProcess;
class Memory;
class Wnd;
#ifdef BOXEDWINE_GUEST_X64
class CPU64;
#endif

class KThreadGlContext {
public:
    KThreadGlContext() = default;
    KThreadGlContext(void* context):context(context) {}
    void* context = nullptr;
    bool hasBeenMadeCurrent = false;
    bool sharing = false;
    std::shared_ptr<Wnd> wnd;
};

typedef std::shared_ptr<KThreadGlContext> KThreadGlContextPtr;

// FD-128: registry of host-thread -> guest-thread identity, recorded
// by each platform thread at start (see KThread::registerPlatformThread).
// currentThread() consults it when the TLS slot fails validation.
KThread* bwCurrentThreadRegistryLookup();
class KThread {
public:
    KThread(U32 id, const KProcessPtr& process);
    ~KThread();

    // FD-44: cross-thread lifetime references. The count starts at 1,
    // owned by whoever created the thread (for table threads, the
    // process thread table's teardown path). KProcess::deleteThread
    // releases that reference instead of deleting the thread outright.
    // Any host thread that fetches a KThread from a table and uses it
    // beyond the lookup -- signal delivery (KThread::signal,
    // KThread::runSignals), termination, siginfo queueing -- must hold
    // a reference taken under the table lock (KProcess::getThreadByIdRef
    // and friends), so the object, and the CPU it owns, cannot be freed
    // underneath it. The last release deletes the object. Direct
    // `delete` remains correct only for threads whose reference was
    // never shared (never table-registered and never scheduled).
    void addRef();
    void releaseRef();

    static void runOnMainThread(std::function<void()> callback);

    void addCallbackOnExit(std::function<void(U32 id)> callback) {callbacksOnExit.push_back(callback);}

    void reset();

    struct user_desc* getLDT(U32 index);
    bool isLdtEmpty(struct user_desc* desc);
    U32 signal(U32 signal, bool wait);
    bool readyForSignal(U32 signal);
    void cleanup();

    void seg_mapper(U32 address, bool readFault, bool writeFault, bool throwException=true);
    void seg_access(U32 address, bool readFault, bool writeFault, bool throwException=true);
    // FD-205: identical-fault loop containment (see BW64_FAULT_LOOP_LIMIT
    // in ksignal.h). Counts consecutive deliveries of the identical fault
    // (same signal, same faulting instruction, same fault address) on this
    // thread; returns true once the count passes the limit, in which case
    // the caller must take its no-handler path (end the process) instead
    // of delivering again. A different fault resets the count.
    bool faultLoopTripped(U32 signal, U32 faultRip, U32 faultAddr);
    bool runSignals();
#if defined(BOXEDWINE_GUEST_X64) && defined(BOXEDWINE_MULTI_THREADED)
    // Called by a blocking wait loop (socket read, poll, futex, sleep) after it
    // wakes: if a cross-thread signal is pending on this 64-bit guest thread,
    // deliver it now (CPU64::interruptSyscallWithSignal) and return true - the
    // caller must then abandon the wait and return -K_EINTR.
    bool interruptBlockedSyscall(bool allowRestart = true);
#endif
    void runSignal(U32 signal, U32 trapNo, U32 errorNo);
    void signalIllegalInstruction(int code);   
    void signalTrap(U32 code);
    void clone(KThread* from);
    void setupStack();
    void setTLS(struct user_desc* desc);

    // syscalls
    U32 futex(U32 addr, U32 op, U32 value, U32 pTime, U32 val2, U32 val3, bool time64) ;
    // futex_waitv (449): one parsed waiter. key is the futex-table key the
    // sibling futex paths use (guest address for a private futex, host RAM
    // pointer for a shared one; for 64-bit guests always the host RAM
    // pointer); word is the host pointer the expected value is compared
    // against. expireTimeInMillies is an absolute deadline on the
    // KSystem::getMilliesSinceStart() clock, 0xFFFFFFFF for no timeout.
    struct FutexWaitvEntry {
        U64 key;
        U32* word;
        U32 val;
    };
    U32 futexWaitv(const FutexWaitvEntry* entries, U32 nr, U32 expireTimeInMillies);
#ifdef BOXEDWINE_GUEST_X64
    // 64-bit guest futex. Blocks/wakes via the same global system_futex[]
    // table as the 32-bit futex(), keyed on the stable host pointer that
    // KMemory64::getRamPtr returns for the guest futex word. Only WAIT/WAKE
    // (+ _BITSET, + _PRIVATE) are real; other ops return -ENOSYS so glibc
    // takes its user-space fallback. arg4 (when non-zero) points at a
    // guest `struct timespec` for the relative WAIT timeout.
    S64 futex64(U64 addr, U32 op, U32 value, U64 arg4, U64 addr2, U32 val3);
    S64 futexWaitv64(const FutexWaitvEntry* entries, U32 nr, U32 expireTimeInMillies);
#endif
    U32 modify_ldt(U32 func, U32 ptr, U32 count);
    U32 signalstack(U32 ss, U32 oss);
    U32 sigprocmask(U32 how, U32 set, U32 oset, U32 sigsetSize);
    U32 sigreturn();
    U32 set_robust_list(U32 head, U32 len);
    U32 get_robust_list(U32 pid, U32 head_ptr, U32 len_ptr);
    U32 rseq(U32 rseq, U32 rseq_len, U32 flags, U32 sig);
    U32 sigsuspend(U32 mask, U32 sigsetSize);
    U32 sigtimedwait(U32 set, U32 info, U32 timeout, U32 sizeofSet, bool time64);
    U32 sleep(U32 ms);
    U32 nanoSleep(U64 nano);
    U32 clockNanoSleep(U32 clock, U32 flags, U64 nano, U32 addressRemain);

    U32 id = 0;
    U64 sigMask = 0; // :TODO: what happens when this is changed while in a signal
    U64 inSigMask = 0;
    U32 alternateStack = 0;
    U32 alternateStackSize = 0;
    CPU* cpu = nullptr;
#ifdef BOXEDWINE_GUEST_X64
    // 64-bit per-thread CPU. Only set for is64Bit processes; shares the one
    // process->memory64 with every sibling thread. The main thread's cpu64
    // also lives on KProcess::cpu64 (set by loadProgram64); clone64 allocates
    // a fresh CPU64 here for each new thread. nullptr for 32-bit guests, where
    // the `cpu` member above is the working path.
    CPU64* cpu64 = nullptr;
#endif
    KProcessPtr process;
    KMemory* const memory;
    bool interrupted = false;
    U32 inSignal = 0;    
#ifdef BOXEDWINE_MULTI_THREADED
    bool exited = false;	
    bool startSignal = false;    
    U64 threadStartTime = 0;
#else
    U64 userTime = 0;    
#endif
    bool terminating = false;
    // FD-205: identical-fault loop containment state for this thread.
    U32 faultLoopSig = 0;
    U32 faultLoopRip = 0;
    U32 faultLoopAddr = 0;
    U32 faultLoopCount = 0;
    // FD-44: see addRef/releaseRef above.
    std::atomic<U32> refCount{1};
    U32 clear_child_tid = 0;
#ifdef BOXEDWINE_GUEST_X64
    // 64-bit CLONE_CHILD_CLEARTID address (set_tid_address / clone tls arg).
    // Separate from the 32-bit U32 field above because guest addresses are
    // 64-bit. On thread exit the kernel zeroes this word and wakes one futex
    // waiter on it — that is how glibc's pthread_join observes child death.
    U64 clear_child_tid64 = 0;
#endif

    U64 getThreadUserTime();

    U64 kernelTime = 0;
    U32 inSysCall = 0;
    BOXEDWINE_CONDITION waitingForSignalToEndCond;
    BOXEDWINE_CONDITION sigWaitCond;
    U64 sigWaitMask = 0;
    U64 foundWaitSignal = 0;

    U64 waitingForSignalToEndMaskToRestore = 0;
    U64 pendingSignals = 0;
    BOXEDWINE_MUTEX pendingSignalsMutex;
    KThreadGlContextPtr getGlContextById(U32 id);
    void removeGlContextById(U32 id);
    KThreadGlContextPtr addGlContext(U32 id, void* context);
    void removeAllGlContexts();
    bool hasContextBeenMadeCurrentSinceCreation = false;

    BHashTable<U32, std::shared_ptr<KThreadGlContext>> glContext;
    BString name;

    std::vector<KPollData> pollData;
public:
    U32 currentContext = 0;
    U32 glLastError = 0;
    bool log = false; // syscalls
    OpenGLVetexPointer glVertextPointer; // 0 index
    BHashTable<U32, OpenGLVetexPointerPtr> glVertextPointersByIndex; // indexes greater than 0
    BHashTable<U32, OpenGLVetexPointerPtr> glVertexAttribPointerNVByIndex;
    OpenGLVetexPointer glNormalPointer;
    OpenGLVetexPointer glFogPointer;
    OpenGLVetexPointer glFogPointerEXT;
    OpenGLVetexPointer glTangentPointerEXT;
    OpenGLVetexPointer glColorPointer;
    OpenGLVetexPointer glSecondaryColorPointer;
    OpenGLVetexPointer glSecondaryColorPointerEXT;
    OpenGLVetexPointer glIndexPointer;
    OpenGLVetexPointer glTexCoordPointer;
    BHashTable<U32, OpenGLVetexPointerPtr> glMultiTexCoordPointerEXTByTexunit;
    BHashTable<U32, OpenGLVetexPointerPtr> glMultiTexCoordPointerSGISByTarget;
    OpenGLVetexPointer glEdgeFlagPointer;
    OpenGLVetexPointer glEdgeFlagPointerEXT;
    OpenGLVetexPointer glElementPointerAPPLE;
    OpenGLVetexPointer glElementPointerATI;
    BHashTable<U32, OpenGLVetexPointerPtr> glVariantPointerEXTById;
    OpenGLVetexPointer glMatrixIndexPointerARB;
    OpenGLVetexPointer glVertexWeightPointerEXT;
    OpenGLVetexPointer glWeightPointerARB;
    OpenGLVetexPointer glInterleavedArray;
    U32 marshalIndex = 0;

    inline static KThread* currentThread() {
        KThread* thread = runningThread;
        // FD-128: in the browser build the TLS slot holding
        // runningThread can be overwritten with a small integer
        // during heavy Vulkan bridge activity (observed values
        // 0x25/0x9 in place of the worker's KThread pointer; no
        // setCurrentThread call is involved). Every consumer of the
        // current-thread identity then operates on low static
        // memory: condition waits publish waitingCond there, and
        // alignment-checked atomics on the "thread's" fields --
        // the shared_ptr release in BoxedWineCondition::wait and
        // the fault-stage markers in KFile::preadNative -- trap
        // with "operation does not support unaligned accesses",
        // killing the worker in the first frames' image work.
        // A genuine KThread is heap allocated: 8-aligned and above
        // the static region. When the slot fails that test, fall
        // back to the identity this host thread registered at
        // platform-thread start and heal the slot with it.
        if (thread && ((size_t)thread < 0x100000 || ((size_t)thread & 7) != 0)) {
            KThread* registered = bwCurrentThreadRegistryLookup();
            if (registered) {
                runningThread = registered;
                return registered;
            }
        }
        return thread;
    }
    static void registerPlatformThread(KThread* thread);
    static void unregisterPlatformThread();
	inline static void setCurrentThread(KThread* thread) { runningThread = thread; }
	// FD-128: see currentThread() above.
	struct PlatformThreadIdentityRegistration {
		explicit PlatformThreadIdentityRegistration(KThread* thread) { registerPlatformThread(thread); }
		~PlatformThreadIdentityRegistration() { unregisterPlatformThread(); }
	};

    BOXEDWINE_CONDITION waitingCond = nullptr;    
    // FD-77 fault-stage marker: while this thread is inside the
    // demand/fault path, faultStage names the stage it is in (see
    // include/faultstage.h for the stage list) and faultStageSinceMs
    // records when it entered that stage. The guest thread dump reads
    // these locklessly (plain atomic loads) to separate the waits the
    // demand path can hide: a thread frozen at FS_MEM_LOCK is piled
    // behind KMemory::mutex (whose owner the dump prints), at
    // FS_RAM_ALLOC behind the RAM-page allocator, at FS_FILEPOS_LOCK
    // behind one mapped file's position lock, and at FS_BACKING_READ
    // inside the backing read itself (faultFile/faultFileOffset/
    // faultNodeType name the file). Written only by the thread itself
    // through FaultStageScope / MemoryDemandLock; restored on scope
    // exit, so a thread outside the demand path reads FS_NONE.
    std::atomic<U32> faultStage{0};
    std::atomic<U64> faultStageSinceMs{0};
    std::atomic<U32> faultPage{0};
    std::atomic<U64> faultFileOffset{0};
    std::atomic<U32> faultNodeType{0};
    char faultFile[128] = {};
    void setFaultStage(U32 stage, U32 page);

    BOXEDWINE_CONDITION pollCond;
#ifdef BOXEDWINE_MULTI_THREADED
    BOXEDWINE_MUTEX waitingCondSync;
#else
    KListNode<KThread*> scheduledThreadNode;
    KListNode<KThread*> waitThreadNode;
        
    BoxedWineConditionTimer condTimer;
#endif

    U32 condStartWaitTime = 0;
private:
    void internalCleanup();
    void exitRobustList();
    U32 handleFutexDeath(U32 uaddr, bool pi, bool pending_op);

    U32 robustList = 0;

    std::shared_ptr<FsNode> threadNode; // in /proc/<pid>/task/<tid>
    std::shared_ptr<FsNode> commNode; // in /proc/<pid>/task/<tid>/comm

    std::vector< std::function<void(U32) > > callbacksOnExit;

    void clearFutexes();

    thread_local static KThread* runningThread;

    BOXEDWINE_CONDITION sleepCond;      

    struct user_desc tls[TLS_ENTRIES] = {};
    BOXEDWINE_MUTEX tlsMutex;

    static BOXEDWINE_MUTEX_NR futexesMutex;
};

class ChangeThread {
public:
    ChangeThread(KThread* thread);
    ~ChangeThread();
    KThread* savedThread;
};

#define SIGSUSPEND_RETURN 0xF000000000000000l
#define RESTORE_SIGNAL_MASK 0x0FFFFFFFFFFFFFFFl

void OPCALL onExitSignal(CPU* cpu, DecodedOp* op);

void common_signalIllegalInstruction(CPU* cpu, int code);

// One greppable BW64-FAIL line per 32-bit guest fault delivery (defined
// in kthread.cpp; see KThread::seg_mapper). kind names the fault class
// (memfault32 / exception32 / trap32 / ill32); delivered is false when no
// guest handler received the fault and the default disposition applied.
void bw64LogGuestFault32(const char* kind, KThread* thread, U32 address, U32 eip, U32 error, bool delivered);

#if defined(BOXEDWINE_GUEST_X64) && defined(BOXEDWINE_MULTI_THREADED)
// Wait on `cond`, but let a 64-bit guest thread wake every 200 ms so that a
// signal armed between its pendingSignals check and the wait (tgkill can only
// kick a condition that is already registered in thread->waitingCond) is never
// lost. Other threads wait exactly as before.
#define BOXEDWINE_CONDITION_WAIT_SIGNALABLE(cond) \
    do { \
        KThread* bwSigThread = KThread::currentThread(); \
        if (bwSigThread && bwSigThread->cpu64) { BOXEDWINE_CONDITION_WAIT_TIMEOUT(cond, 200); } \
        else { BOXEDWINE_CONDITION_WAIT(cond); } \
    } while (0)
#else
#define BOXEDWINE_CONDITION_WAIT_SIGNALABLE(cond) BOXEDWINE_CONDITION_WAIT(cond)
#endif

#endif
