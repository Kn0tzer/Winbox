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

#endif
#endif
