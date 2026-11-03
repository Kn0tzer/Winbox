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
#include "kepoll.h"
#include "kevent.h"
#include "kunixsocket.h"
#include "ksocketobject.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

#include <atomic>
#include <chrono>
#include <thread>

// FD-23 census follow-through. Three modern x86-64 syscalls that real
// libraries issue — epoll_pwait (281, the call behind glibc's
// epoll_pwait()), accept4 (288) and dup3 (292) — had no dispatch case at
// all and fell into the -ENOSYS fallback. These tests drive the real
// dispatcher (ksyscall64), so on the pre-implementation tree every call
// below answers -ENOSYS and the tests fail; with the dispatch cases in
// place they pass. The accept4 cases also pin the SOCK_CLOEXEC flag
// translation: the guest ABI carries CLOEXEC as O_CLOEXEC (0x80000)
// while the socket implementations consume the descriptor-flag bit, and
// the untranslated hand-off silently dropped the flag on the 32-bit
// accept4 path before the translation was centralised in kaccept.

void testEpollPwaitSyscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x76000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) && memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("epoll_pwait test could not map x64 buffers");
        return;
    }
    auto event = std::make_shared<KEvent>();
    event->counter = 1;
    KFileDescriptorPtr eventFd = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    auto epoll = std::make_shared<KEPoll>();
    KFileDescriptorPtr epollFd = process->allocFileDescriptor(epoll, K_O_RDWR, 0, -1, 0);
    auto emptyEpoll = std::make_shared<KEPoll>();
    KFileDescriptorPtr emptyEpollFd = process->allocFileDescriptor(emptyEpoll, K_O_RDWR, 0, -1, 0);
    constexpr U32 registration = TEST_HEAP_ADDRESS + 0x100;
    context.memory->writed(registration, 1);
    context.memory->writeq(registration + 4, 0x1122334455667788ULL);
    if ((S32)epoll->ctl(context.memory, 1, (FD)eventFd->handle, registration) != 0) {
        testFail("epoll_pwait test registration failed");
        process->close(epollFd->handle); process->close(emptyEpollFd->handle); process->close(eventFd->handle);
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto syscall = [&](S32 epfd, U64 maxevents, S64 timeoutMs, U64 mask, U64 maskSize) {
        cpu.reg[X64_RAX].setU64(281);
        cpu.reg[X64_RDI].setU64((U64)(S64)epfd);
        cpu.reg[X64_RSI].setU64(page + 0x100);
        cpu.reg[X64_RDX].setU64(maxevents);
        cpu.reg[X64_R10].setU64((U64)(S64)timeoutMs);
        cpu.reg[X64_R8].setU64(mask);
        cpu.reg[X64_R9].setU64(maskSize);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    // Ready event with a plain millisecond timeout (the difference from
    // epoll_pwait2, whose timeout is a timespec pointer).
    if (syscall(epollFd->handle, 1, 0, 0, 0) != 1 || memory->readd(page + 0x100) != 1 ||
        memory->readq(page + 0x104) != 0x1122334455667788ULL) {
        testFail("epoll_pwait did not return its ready event and 64-bit payload");
    }
    // Nothing ready + zero timeout -> 0, and a bad sigset size or a zero
    // maxevents is EINVAL, exactly as epoll_pwait2 answers.
    memory->writeq(page + 0x40, 0);
    if (syscall(emptyEpollFd->handle, 1, 0, 0, 0) != 0 ||
        syscall(epollFd->handle, 1, 0, page + 0x40, 4) != -K_EINVAL ||
        syscall(epollFd->handle, 0, 0, 0, 0) != -K_EINVAL) {
        testFail("epoll_pwait timeout or argument validation was incorrect");
    }
    // The signal mask applies only for the duration of the wait: the
    // thread's mask must be restored on return.
    context.thread->sigMask = 0x5A5A;
    memory->writeq(page + 0x40, 0x1234);
    if (syscall(epollFd->handle, 1, 0, page + 0x40, 8) != 1 || context.thread->sigMask != 0x5A5A) {
        testFail("epoll_pwait did not restore the thread signal mask after the wait");
    }
    context.thread->sigMask = 0;
    process->close(epollFd->handle);
    process->close(emptyEpollFd->handle);
    process->close(eventFd->handle);
}

void testDup3Syscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto event = std::make_shared<KEvent>();
    KFileDescriptorPtr eventFd = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    auto syscall = [&](S32 oldfd, S32 newfd, U64 flags) {
        cpu.reg[X64_RAX].setU64(292);
        cpu.reg[X64_RDI].setU64((U64)(S64)oldfd);
        cpu.reg[X64_RSI].setU64((U64)(S64)newfd);
        cpu.reg[X64_RDX].setU64(flags);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    // Plain dup3: the new descriptor must NOT carry FD_CLOEXEC (dup2
    // semantics, which KProcess::dup2 already implements).
    if (syscall(eventFd->handle, 40, 0) != 40) {
        testFail("dup3 did not duplicate to the requested descriptor");
    } else {
        KFileDescriptorPtr dup = process->getFileDescriptor(40);
        if (!dup || (dup->descriptorFlags & FD_CLOEXEC)) {
            testFail("dup3 without O_CLOEXEC produced a close-on-exec descriptor");
        }
    }
    // dup3 with O_CLOEXEC: the new descriptor must carry FD_CLOEXEC.
    if (syscall(eventFd->handle, 41, K_O_CLOEXEC) != 41) {
        testFail("dup3 with O_CLOEXEC did not duplicate to the requested descriptor");
    } else {
        KFileDescriptorPtr dup = process->getFileDescriptor(41);
        if (!dup || !(dup->descriptorFlags & FD_CLOEXEC)) {
            testFail("dup3 with O_CLOEXEC did not set FD_CLOEXEC on the new descriptor");
        }
    }
    // Linux dup3 refusals: oldfd == newfd is EINVAL (unlike dup2), any
    // flag other than O_CLOEXEC is EINVAL, an invalid oldfd is EBADF.
    if (syscall(eventFd->handle, eventFd->handle, 0) != -K_EINVAL ||
        syscall(eventFd->handle, 42, K_O_NONBLOCK) != -K_EINVAL ||
        syscall(9999, 43, 0) != -K_EBADF) {
        testFail("dup3 did not reject equal descriptors, unknown flags, or a bad oldfd");
    }
    process->close(40);
    process->close(41);
    process->close(eventFd->handle);
}

namespace {
// pendingConnections is protected on KUnixSocketObject; a test subclass
// (the same pattern as the unix-socket wakeup test) stages a pending
// connection the way a connecting client would, without needing a
// filesystem-bound socket path.
class TestListenerSocket : public KUnixSocketObject {
public:
    TestListenerSocket() : KUnixSocketObject(1, 1, 0) {} // AF_UNIX, SOCK_STREAM
    void queuePending(const std::shared_ptr<KUnixSocketObject>& client) { pendingConnections.push_back(client); }
    size_t pendingCount() { return pendingConnections.size(); }
};
}

void testAccept4Syscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    CPU64 cpu(memory);
    cpu.thread = context.thread;

    auto listener = std::make_shared<TestListenerSocket>();
    KFileDescriptorPtr listenerFd = process->allocFileDescriptor(listener, K_O_RDWR, 0, -1, 0);
    // listen() requires a bound node (what bind() would have installed).
    listener->node = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/tmp/bw64-accept4-test.sock"), B(""), B(""), false, false, nullptr);
    if ((S32)listener->listen(context.thread, listenerFd, 4) != 0) {
        testFail("accept4 test listener setup failed");
        process->close(listenerFd->handle);
        return;
    }
    std::shared_ptr<KUnixSocketObject> pendingClient;
    auto queueClient = [&]() {
        pendingClient = std::make_shared<KUnixSocketObject>(1, 1, 0);
        listener->queuePending(pendingClient);
    };
    auto syscall = [&](S32 fd, U64 flags) {
        cpu.reg[X64_RAX].setU64(288);
        cpu.reg[X64_RDI].setU64((U64)(S64)fd);
        cpu.reg[X64_RSI].setU64(0);
        cpu.reg[X64_RDX].setU64(0);
        cpu.reg[X64_R10].setU64(flags);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    // Unknown flag bits are EINVAL and must not consume the connection.
    queueClient();
    if (syscall(listenerFd->handle, 0x10) != -K_EINVAL || listener->pendingCount() != 1) {
        testFail("accept4 did not reject unknown flags before accepting");
    }
    // SOCK_CLOEXEC (guest encoding O_CLOEXEC) must land as FD_CLOEXEC on
    // the accepted descriptor — the assertion the 32-bit path failed
    // before the kaccept translation.
    S64 accepted = syscall(listenerFd->handle, K_O_CLOEXEC);
    if (accepted < 0) {
        testFail("accept4 with SOCK_CLOEXEC did not accept the pending connection");
    } else {
        KFileDescriptorPtr acceptedFd = process->getFileDescriptor((FD)accepted);
        if (!acceptedFd || !(acceptedFd->descriptorFlags & FD_CLOEXEC)) {
            testFail("accept4 with SOCK_CLOEXEC did not set FD_CLOEXEC on the accepted descriptor");
        }
        process->close((FD)accepted);
    }
    // SOCK_NONBLOCK must make the accepted socket non-blocking, and a
    // flags==0 accept must produce a plain blocking, non-CLOEXEC socket.
    queueClient();
    S64 acceptedNb = syscall(listenerFd->handle, K_O_NONBLOCK);
    if (acceptedNb < 0) {
        testFail("accept4 with SOCK_NONBLOCK did not accept the pending connection");
    } else {
        KFileDescriptorPtr acceptedFd = process->getFileDescriptor((FD)acceptedNb);
        auto acceptedSocket = acceptedFd ? std::dynamic_pointer_cast<KSocketObject>(acceptedFd->kobject) : nullptr;
        if (!acceptedSocket || acceptedSocket->blocking || (acceptedFd->descriptorFlags & FD_CLOEXEC)) {
            testFail("accept4 with SOCK_NONBLOCK did not produce a non-blocking plain descriptor");
        }
        process->close((FD)acceptedNb);
    }
    // Error surface: bad descriptor is EBADF, a non-socket is ENOTSOCK.
    auto event = std::make_shared<KEvent>();
    KFileDescriptorPtr eventFd = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    if (syscall(9999, 0) != -K_EBADF || syscall(eventFd->handle, 0) != -K_ENOTSOCK) {
        testFail("accept4 did not answer EBADF / ENOTSOCK for bad descriptors");
    }
    process->close(eventFd->handle);
    process->close(listenerFd->handle);
}

// FD-23 residue: select (23). On the base tree select is a deliberate stub
// answering through unimplementedSyscall64, so every case below fails there.
// The contract pinned here is host-measured: a ready descriptor returns with
// only its bit surviving; the timeval comes back holding the time REMAINING
// (a full timeout expiry returns it zeroed, not untouched); a descriptor
// that is not open answers -EBADF in any set, for select and pselect6
// alike; negative nfds and negative timeval fields answer -EINVAL (an
// oversized usec is normalized by the kernel, not refused); unmapped set or
// timeout pointers answer -EFAULT; bytes past nfds in a set are not part of
// the result write-back; pselect6 never writes its timespec back.
void testSelectSyscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) {
        process->memory64 = new KMemory64(process.get());
    }
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x7A000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) && memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("select test could not map its x64 buffers");
        return;
    }
    auto readyEvent = std::make_shared<KEvent>();
    readyEvent->counter = 1;
    KFileDescriptorPtr readyFd = process->allocFileDescriptor(readyEvent, K_O_RDWR, 0, -1, 0);
    auto idleEvent = std::make_shared<KEvent>();
    KFileDescriptorPtr idleFd = process->allocFileDescriptor(idleEvent, K_O_RDWR, 0, -1, 0);
    auto goneEvent = std::make_shared<KEvent>();
    KFileDescriptorPtr goneFd = process->allocFileDescriptor(goneEvent, K_O_RDWR, 0, -1, 0);
    const U32 closedHandle = goneFd->handle;
    process->close(closedHandle);

    const U64 rset = page + 0x100, wset = page + 0x200, eset = page + 0x300;
    const U64 tvAddr = page + 0x400, tsAddr = page + 0x480;
    auto setBit = [&](U64 base, U32 fd) {
        memory->writeb(base + fd / 8, (U8)(memory->readb(base + fd / 8) | (U8)(1u << (fd % 8))));
    };
    auto bitIsSet = [&](U64 base, U32 fd) {
        return (memory->readb(base + fd / 8) & (1u << (fd % 8))) != 0;
    };
    auto clearSets = [&]() {
        for (U32 i = 0; i < 128; i++) {
            memory->writeb(rset + i, 0);
            memory->writeb(wset + i, 0);
            memory->writeb(eset + i, 0);
        }
    };
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto select = [&](S64 nfds, U64 r, U64 w, U64 e, U64 tv) {
        cpu.reg[X64_RAX].setU64(23);
        cpu.reg[X64_RDI].setU64((U64)nfds);
        cpu.reg[X64_RSI].setU64(r);
        cpu.reg[X64_RDX].setU64(w);
        cpu.reg[X64_R10].setU64(e);
        cpu.reg[X64_R8].setU64(tv);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto pselect = [&](S64 nfds, U64 r, U64 ts) {
        cpu.reg[X64_RAX].setU64(270);
        cpu.reg[X64_RDI].setU64((U64)nfds);
        cpu.reg[X64_RSI].setU64(r);
        cpu.reg[X64_RDX].setU64(0);
        cpu.reg[X64_R10].setU64(0);
        cpu.reg[X64_R8].setU64(ts);
        cpu.reg[X64_R9].setU64(0);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto setTv = [&](S64 sec, S64 usec) {
        memory->writeq(tvAddr, (U64)sec);
        memory->writeq(tvAddr + 8, (U64)usec);
    };

    // Ready descriptor, generous timeout: count 1, only the ready bit
    // survives, and the timeval holds the remaining time (just under 5s).
    clearSets();
    setBit(rset, readyFd->handle);
    setBit(rset, idleFd->handle);
    setTv(5, 0);
    S64 rc = select(1024, rset, 0, 0, tvAddr);
    U64 secAfter = memory->readq(tvAddr);
    if (rc != 1 || !bitIsSet(rset, readyFd->handle) || bitIsSet(rset, idleFd->handle) || secAfter != 4) {
        testFail("select did not return the ready set or write back the remaining time (rc=%lld sec=%llu)", (long long)rc, (unsigned long long)secAfter);
    }
    // Nothing ready, 250ms timeout: count 0 after really waiting, and the
    // timeval comes back as the unslept remainder (~0), not the original.
    clearSets();
    setBit(rset, idleFd->handle);
    setTv(0, 250000);
    U64 startMs = KSystem::getMilliesSinceStart();
    rc = select(idleFd->handle + 1, rset, 0, 0, tvAddr);
    U64 elapsedMs = KSystem::getMilliesSinceStart() - startMs;
    if (rc != 0 || elapsedMs < 200 || memory->readq(tvAddr) != 0 || memory->readq(tvAddr + 8) > 60000) {
        testFail("select timeout did not wait or did not zero the remaining time (rc=%lld elapsed=%llu left=%llu)", (long long)rc, (unsigned long long)elapsedMs, (unsigned long long)memory->readq(tvAddr + 8));
    }
    // The sleep idiom: nfds 0, no sets, just a timeout.
    setTv(0, 120000);
    if (select(0, 0, 0, 0, tvAddr) != 0 || memory->readq(tvAddr) != 0 || memory->readq(tvAddr + 8) > 60000) {
        testFail("select with no sets did not sleep and write back the remainder");
    }
    // A closed descriptor in a set is -EBADF, for select and pselect6 alike.
    clearSets();
    setBit(rset, closedHandle);
    setTv(0, 0);
    if (select(closedHandle + 1, rset, 0, 0, tvAddr) != -K_EBADF) {
        testFail("select did not answer -EBADF for a closed descriptor");
    }
    clearSets();
    setBit(rset, closedHandle);
    memory->writeq(tsAddr, 0);
    memory->writeq(tsAddr + 8, 0);
    if (pselect(closedHandle + 1, rset, tsAddr) != -K_EBADF) {
        testFail("pselect6 did not answer -EBADF for a closed descriptor");
    }
    // Validation: negative nfds and negative timeval fields are -EINVAL.
    if (select(-1, 0, 0, 0, 0) != -K_EINVAL) {
        testFail("select did not answer -EINVAL for negative nfds");
    }
    setTv(0, -1);
    if (select(1, 0, 0, 0, tvAddr) != -K_EINVAL) {
        testFail("select did not answer -EINVAL for a negative tv_usec");
    }
    setTv(-1, 0);
    if (select(1, 0, 0, 0, tvAddr) != -K_EINVAL) {
        testFail("select did not answer -EINVAL for a negative tv_sec");
    }
    // Unmapped set and timeout pointers are -EFAULT.
    if (select(1, 0x50000000ULL, 0, 0, 0) != -K_EFAULT) {
        testFail("select did not answer -EFAULT for an unmapped read set");
    }
    clearSets();
    setBit(rset, idleFd->handle);
    if (select(idleFd->handle + 1, rset, 0, 0, 0x50000000ULL) != -K_EFAULT) {
        testFail("select did not answer -EFAULT for an unmapped timeout");
    }
    // Write readiness: an eventfd with room is writable.
    clearSets();
    setBit(wset, idleFd->handle);
    setTv(0, 0);
    if (select(idleFd->handle + 1, 0, wset, 0, tvAddr) != 1 || !bitIsSet(wset, idleFd->handle)) {
        testFail("select did not report the writable descriptor");
    }
    // Bytes past nfds in a set buffer are not part of the result.
    clearSets();
    setBit(rset, idleFd->handle);
    memory->writeb(rset + 100, 0xAA);
    setTv(0, 0);
    select(16, rset, 0, 0, tvAddr);
    if (memory->readb(rset + 100) != 0xAA) {
        testFail("select wrote back bytes past nfds");
    }
    // pselect6 never writes its timespec back, even over a full timeout.
    clearSets();
    setBit(rset, idleFd->handle);
    memory->writeq(tsAddr, 0);
    memory->writeq(tsAddr + 8, 150000000);
    rc = pselect(idleFd->handle + 1, rset, tsAddr);
    if (rc != 0 || memory->readq(tsAddr) != 0 || memory->readq(tsAddr + 8) != 150000000) {
        testFail("pselect6 wrote back its timespec (rc=%lld nsec=%llu)", (long long)rc, (unsigned long long)memory->readq(tsAddr + 8));
    }
    process->close(readyFd->handle);
    process->close(idleFd->handle);
}

// FD-23 residue: pidfd_getfd (438). On the base tree the call has no
// dispatch case and answers -ENOSYS. Host-measured contract: nonzero flags
// answer -EINVAL before the pidfd is even looked at; a non-pidfd descriptor
// or a target descriptor that does not exist answers -EBADF; a terminated
// target answers -ESRCH; a target owned by a different user answers -EPERM;
// success installs the SAME open file description in the caller with
// FD_CLOEXEC set.
void testPidfdGetfdSyscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) {
        process->memory64 = new KMemory64(process.get());
    }
    CPU64 cpu(process->memory64);
    cpu.thread = context.thread;
    auto syscall = [&](U64 number, U64 a1, U64 a2, U64 a3) {
        cpu.reg[X64_RAX].setU64(number);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    KProcessPtr target = KProcess::create();
    target->parentId = process->id;
    target->userId = process->userId;
    auto targetEvent = std::make_shared<KEvent>();
    KFileDescriptorPtr targetFd = target->allocFileDescriptor(targetEvent, K_O_RDWR, 0, -1, 0);

    S64 pidfd = syscall(434, target->id, 0, 0);
    if (pidfd < 0) {
        testFail("pidfd_open of the getfd test target failed");
        KSystem::eraseProcess(target->id);
        return;
    }
    // Flags are validated before the pidfd itself (host order).
    if (syscall(438, (U64)pidfd, targetFd->handle, 1) != -K_EINVAL ||
        syscall(438, 9999, targetFd->handle, 1) != -K_EINVAL) {
        testFail("pidfd_getfd did not validate flags before the pidfd");
    }
    // A descriptor that is not a pidfd, and target descriptors that do not
    // exist, answer -EBADF.
    auto plainEvent = std::make_shared<KEvent>();
    KFileDescriptorPtr plainFd = process->allocFileDescriptor(plainEvent, K_O_RDWR, 0, -1, 0);
    if (syscall(438, plainFd->handle, targetFd->handle, 0) != -K_EBADF ||
        syscall(438, (U64)pidfd, 9999, 0) != -K_EBADF ||
        syscall(438, (U64)pidfd, (U64)(S64)-1, 0) != -K_EBADF) {
        testFail("pidfd_getfd did not answer -EBADF for a non-pidfd or a missing target descriptor");
    }
    // Success: same open file description, FD_CLOEXEC on the new descriptor.
    S64 dup = syscall(438, (U64)pidfd, targetFd->handle, 0);
    if (dup < 0) {
        testFail("pidfd_getfd failed on a live same-user target (%lld)", (long long)dup);
    } else {
        KFileDescriptorPtr dupFd = process->getFileDescriptor((FD)dup);
        if (!dupFd || dupFd->kobject != targetFd->kobject || !(dupFd->descriptorFlags & FD_CLOEXEC)) {
            testFail("pidfd_getfd did not duplicate the description with FD_CLOEXEC set");
        } else {
            U64 value = 7;
            U8 buf[8];
            memcpy(buf, &value, 8);
            dupFd->kobject->writeNative(buf, 8);
            U8 out[8] = {0};
            U32 n = targetFd->kobject->readNative(out, 8);
            U64 got = 0;
            memcpy(&got, out, 8);
            if (n != 8 || got != 7) {
                testFail("pidfd_getfd descriptors do not share one open file description");
            }
        }
        process->close((FD)dup);
    }
    // A different-user target refuses with -EPERM; opening its pidfd is fine.
    KProcessPtr foreign = KProcess::create();
    foreign->parentId = process->id;
    foreign->userId = process->userId + 999;
    auto foreignEvent = std::make_shared<KEvent>();
    KFileDescriptorPtr foreignFd = foreign->allocFileDescriptor(foreignEvent, K_O_RDWR, 0, -1, 0);
    S64 foreignPidfd = syscall(434, foreign->id, 0, 0);
    if (foreignPidfd < 0 || syscall(438, (U64)foreignPidfd, foreignFd->handle, 0) != -K_EPERM) {
        testFail("pidfd_getfd did not refuse a different-user target with -EPERM");
    }
    // A terminated target answers -ESRCH.
    KProcessPtr dead = KProcess::create();
    dead->parentId = process->id;
    dead->userId = process->userId;
    auto deadEvent = std::make_shared<KEvent>();
    KFileDescriptorPtr deadFd = dead->allocFileDescriptor(deadEvent, K_O_RDWR, 0, -1, 0);
    S64 deadPidfd = syscall(434, dead->id, 0, 0);
    dead->terminated = true;
    if (deadPidfd < 0 || syscall(438, (U64)deadPidfd, deadFd->handle, 0) != -K_ESRCH) {
        testFail("pidfd_getfd did not answer -ESRCH for a terminated target");
    }
    process->close((FD)pidfd);
    if (foreignPidfd >= 0) {
        process->close((FD)foreignPidfd);
    }
    if (deadPidfd >= 0) {
        process->close((FD)deadPidfd);
    }
    process->close(plainFd->handle);
    KSystem::eraseProcess(target->id);
    KSystem::eraseProcess(foreign->id);
    KSystem::eraseProcess(dead->id);
}

// FD-23 residue: futex_waitv (449). On the base tree the call has no
// dispatch case and answers -ENOSYS. Host-measured contract: the per-call
// flags must be 0; nr must be in [1,128]; only U32 entries are accepted
// (the 2-bit size field must be FUTEX_32, val must fit 32 bits, reserved
// must be 0, uaddr must be futex-aligned); clockid is CLOCK_REALTIME or
// CLOCK_MONOTONIC and the timeout is absolute; an unmapped waiters array
// or futex word answers -EFAULT; a value mismatch at entry answers
// -EAGAIN immediately (and beats an already-expired timeout); an expired
// timeout answers -ETIMEDOUT; a wake answers with the 0-based index of
// the woken entry.
void testFutexWaitvSyscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) {
        process->memory64 = new KMemory64(process.get());
    }
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x7C000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT)) {
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    }
    const U64 words = page + 0x100;
    const U64 entries = page + 0x200;
    const U64 tsAddr = page + 0x300;
    memory->writed(words, 10);
    memory->writed(words + 4, 20);
    memory->writed(words + 8, 30);
    auto setEntry = [&](U32 i, U64 val, U64 addr, U32 flags, U32 reserved = 0) {
        memory->writeq(entries + i * 24, val);
        memory->writeq(entries + i * 24 + 8, addr);
        memory->writed(entries + i * 24 + 16, flags);
        memory->writed(entries + i * 24 + 20, reserved);
    };
    constexpr U32 F32P = 0x02 | 0x80; // FUTEX_32 | FUTEX_PRIVATE_FLAG
    auto goodEntries = [&]() {
        setEntry(0, 10, words, F32P);
        setEntry(1, 20, words + 4, F32P);
        setEntry(2, 30, words + 8, F32P);
    };
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto waitv = [&](CPU64& c, U64 arr, U64 nr, U64 flags, U64 ts, U64 clockid) {
        c.reg[X64_RAX].setU64(449);
        c.reg[X64_RDI].setU64(arr);
        c.reg[X64_RSI].setU64(nr);
        c.reg[X64_RDX].setU64(flags);
        c.reg[X64_R10].setU64(ts);
        c.reg[X64_R8].setU64(clockid);
        ksyscall64(&c);
        return (S64)c.reg[X64_RAX].u64;
    };
    // A second thread in the process from the start: futex waits by a lone
    // thread answer -EWOULDBLOCK by design in this emulator (no sibling
    // could ever wake it), which is not what these cases exercise.
    KThread* worker = process->createThread();
    CPU64 workerCpu(memory);
    workerCpu.thread = worker;
    worker->cpu64 = &workerCpu;

    goodEntries();
    if (waitv(cpu, entries, 0, 0, 0, 1) != -K_EINVAL ||
        waitv(cpu, entries, 129, 0, 0, 1) != -K_EINVAL ||
        waitv(cpu, entries, 3, 1, 0, 1) != -K_EINVAL) {
        testFail("futex_waitv did not validate nr / flags");
    }
    if (waitv(cpu, 0x50000000ULL, 3, 0, 0, 1) != -K_EFAULT) {
        testFail("futex_waitv did not answer -EFAULT for an unmapped waiters array");
    }
    setEntry(1, 20, words + 4, F32P, 1);
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("futex_waitv did not refuse a nonzero reserved field");
    }
    goodEntries();
    setEntry(1, 20, words + 4, 0x03 | 0x80); // size U64
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("futex_waitv did not refuse a non-U32 entry");
    }
    setEntry(1, 20, words + 4, F32P | 0x40); // undefined flag bit
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("futex_waitv did not refuse an undefined entry flag bit");
    }
    goodEntries();
    setEntry(0, 0x100000000ULL, words, F32P); // val does not fit in 32 bits
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("futex_waitv did not refuse a val wider than the entry size");
    }
    goodEntries();
    setEntry(0, 10, words + 2, F32P); // misaligned uaddr
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("futex_waitv did not refuse a misaligned uaddr");
    }
    goodEntries();
    setEntry(0, 10, 0x50000000ULL, F32P); // unmapped futex word
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EFAULT) {
        testFail("futex_waitv did not answer -EFAULT for an unmapped futex word");
    }
    goodEntries();
    if (waitv(cpu, entries, 3, 0, 0, 2) != -K_EINVAL) {
        testFail("futex_waitv did not refuse clockid 2");
    }
    memory->writeq(tsAddr, 0);
    memory->writeq(tsAddr + 8, 1000000000ULL); // nsec out of range
    if (waitv(cpu, entries, 3, 0, tsAddr, 1) != -K_EINVAL) {
        testFail("futex_waitv did not refuse an out-of-range timespec");
    }
    // A mismatched value anywhere answers -EAGAIN immediately, and beats an
    // already-expired timeout.
    setEntry(1, 999, words + 4, F32P);
    if (waitv(cpu, entries, 3, 0, 0, 1) != -K_EAGAIN) {
        testFail("futex_waitv did not answer -EAGAIN for a mismatched value");
    }
    memory->writeq(tsAddr, 1);
    memory->writeq(tsAddr + 8, 0);
    if (waitv(cpu, entries, 3, 0, tsAddr, 1) != -K_EAGAIN) {
        testFail("futex_waitv mismatch did not beat an expired timeout");
    }
    goodEntries();
    // All values match and the timeout has already passed: -ETIMEDOUT.
    if (waitv(cpu, entries, 3, 0, tsAddr, 1) != -K_ETIMEDOUT) {
        testFail("futex_waitv did not answer -ETIMEDOUT for a passed deadline");
    }
    // All values match with a real deadline: waits, then -ETIMEDOUT.
    U64 deadline = KSystem::getMilliesSinceStart() + 150;
    memory->writeq(tsAddr, deadline / 1000);
    memory->writeq(tsAddr + 8, (deadline % 1000) * 1000000);
    U64 start = KSystem::getMilliesSinceStart();
    S64 rc = waitv(cpu, entries, 3, 0, tsAddr, 1);
    if (rc != -K_ETIMEDOUT || KSystem::getMilliesSinceStart() - start < 100) {
        testFail("futex_waitv did not wait out its deadline (rc=%lld)", (long long)rc);
    }
    // A wake answers with the 0-based index of the woken entry.
    auto wakeCase = [&](U32 wakeIndex, S64 expected) {
        std::atomic<bool> started{false};
        S64 result = -999;
        std::thread waiter([&]() {
            KThread::setCurrentThread(worker);
            started.store(true, std::memory_order_release);
            result = waitv(workerCpu, entries, 3, 0, 0, 1);
        });
        while (!started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cpu.reg[X64_RAX].setU64(202);
        cpu.reg[X64_RDI].setU64(words + wakeIndex * 4);
        cpu.reg[X64_RSI].setU64(1 | 128); // FUTEX_WAKE | FUTEX_PRIVATE_FLAG
        cpu.reg[X64_RDX].setU64(1);
        cpu.reg[X64_R10].setU64(0);
        cpu.reg[X64_R8].setU64(0);
        cpu.reg[X64_R9].setU64(0);
        ksyscall64(&cpu);
        S64 woke = (S64)cpu.reg[X64_RAX].u64;
        waiter.join();
        if (woke != 1 || result != expected) {
            testFail("futex_waitv wake of entry %u returned %lld (woke %lld)", wakeIndex, (long long)result, (long long)woke);
        }
    };
    goodEntries();
    wakeCase(2, 2);
    goodEntries();
    wakeCase(0, 0);
    worker->cpu64 = nullptr;
    process->removeThread(worker);
    delete worker;
}

#endif
#endif
