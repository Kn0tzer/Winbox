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

#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"
#include "kepoll.h"
#include "kevent.h"
#include "knetlink.h"
#include "knativesocket.h"
#include "kpoll.h"
#include "ksignal.h"
#include "ksocket.h"

// Regression coverage for the kernel cleanup campaign: operations in the
// event/wait/socket subsystems that used to kpanic (killing the whole
// emulator in the default configuration) or silently misreport must now
// answer with the graceful errno, and readiness plumbing between nested
// epoll instances and poll() must work. If any of the old panics come
// back, this test does not fail politely -- the test binary dies, which
// is exactly the guest-visible behavior being guarded against.
void testKernelGrace() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    constexpr U32 eventAddr = TEST_HEAP_ADDRESS + 0x400;
    constexpr U32 statAddr = TEST_HEAP_ADDRESS + 0x440;
    constexpr U32 maskAddr = TEST_HEAP_ADDRESS + 0x480;
    constexpr U32 valueAddr = TEST_HEAP_ADDRESS + 0x4C0;
    constexpr U32 lenAddr = TEST_HEAP_ADDRESS + 0x4C4;
    constexpr U32 bufAddr = TEST_HEAP_ADDRESS + 0x500;
    constexpr U32 recvAddr = TEST_HEAP_ADDRESS + 0x520;
    constexpr U32 srcAddr = TEST_HEAP_ADDRESS + 0x540;
    constexpr U32 srcLenAddr = TEST_HEAP_ADDRESS + 0x560;
    constexpr U32 msgAddr = TEST_HEAP_ADDRESS + 0x580;
    constexpr U32 iovAddr = TEST_HEAP_ADDRESS + 0x5C0;
    constexpr U32 cmsgAddr = TEST_HEAP_ADDRESS + 0x5E0;
    U8 scratch[8] = {};

    // ---- epoll object operations (all previously kpanic) ----
    auto event = std::make_shared<KEvent>();
    event->counter = 0;
    KFileDescriptorPtr eventFd = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    auto epoll = std::make_shared<KEPoll>();
    KFileDescriptorPtr epollFd = process->allocFileDescriptor(epoll, K_O_RDWR, 0, -1, 0);

    epoll->setBlocking(false);
    if (epoll->isBlocking()) { testFail("epoll setBlocking(false) not retained"); return; }
    epoll->setBlocking(true);
    if (!epoll->isBlocking()) { testFail("epoll setBlocking(true) not retained"); return; }
    if (epoll->isWriteReady()) { testFail("epoll isWriteReady must be false"); return; }
    if (epoll->readNative(scratch, 8) != -K_EINVAL) { testFail("epoll readNative must return EINVAL"); return; }
    if (epoll->writeNative(scratch, 8) != -K_EINVAL) { testFail("epoll writeNative must return EINVAL"); return; }
    if ((S32)epoll->stat(context.process.get(), statAddr, false) != -K_ENOTSUP) { testFail("epoll stat must return ENOTSUP"); return; }
    if ((S32)event->stat(context.process.get(), statAddr, false) != -K_ENOTSUP) { testFail("eventfd stat must return ENOTSUP"); return; }

    // ctl error answers
    auto writeEvent = [&](U32 events) {
        context.memory->writed(eventAddr, events);
        context.memory->writeq(eventAddr + 4, 0x1234);
    };
    writeEvent(0x001); // EPOLLIN
    if ((S32)epoll->ctl(context.memory, 1, (FD)eventFd->handle, eventAddr) != 0) { testFail("epoll ctl ADD failed"); return; }
    if ((S32)epoll->ctl(context.memory, 1, (FD)eventFd->handle, eventAddr) != -K_EEXIST) { testFail("epoll ctl duplicate ADD must return EEXIST"); return; }
    writeEvent(0x008); // EPOLLERR cannot be requested by a guest
    if ((S32)epoll->ctl(context.memory, 3, (FD)eventFd->handle, eventAddr) != -K_EINVAL) { testFail("epoll ctl with EPOLLERR must return EINVAL"); return; }
    if ((S32)epoll->ctl(context.memory, 2, (FD)98765, eventAddr) != -K_EBADF) { testFail("epoll ctl on a bad fd must return EBADF"); return; }
    auto event2 = std::make_shared<KEvent>();
    KFileDescriptorPtr eventFd2 = process->allocFileDescriptor(event2, K_O_RDWR, 0, -1, 0);
    if ((S32)epoll->ctl(context.memory, 2, (FD)eventFd2->handle, eventAddr) != -K_ENOENT) { testFail("epoll ctl DEL of unregistered fd must return ENOENT"); return; }

    // Readiness of the registered eventfd, through the object and
    // through poll() on the epoll fd itself.
    if (epoll->isReadReady()) { testFail("epoll must not be readable before the eventfd write"); return; }
    KPollData pollData{};
    pollData.fd = epollFd->handle;
    pollData.events = K_POLLIN;
    if (internal_poll(context.thread, &pollData, 1, 0) != 0) { testFail("poll on an unready epoll fd must return 0"); return; }
    U64 one = 1;
    event->writeNative((U8*)&one, 8);
    if (!epoll->isReadReady()) { testFail("epoll must be readable after the eventfd write"); return; }
    pollData.revents = 0;
    if (internal_poll(context.thread, &pollData, 1, 0) != 1 || !(pollData.revents & K_POLLIN)) {
        testFail("poll on a ready epoll fd must return POLLIN");
        return;
    }
    // Nested epoll: readiness must recurse (bounded) instead of panicking.
    auto epoll2 = std::make_shared<KEPoll>();
    process->allocFileDescriptor(epoll2, K_O_RDWR, 0, -1, 0);
    writeEvent(0x001);
    if ((S32)epoll2->ctl(context.memory, 1, (FD)epollFd->handle, eventAddr) != 0) { testFail("nested epoll ctl ADD failed"); return; }
    if (!epoll2->isReadReady()) { testFail("nested epoll must report the inner readiness"); return; }

    // ---- signalfd validation (previously gated, then kpanic) ----
    context.memory->writeq(maskAddr, 0);
    if ((S32)syscall_signalfd4(context.thread, -1, maskAddr, 99, 0) != -K_EINVAL) { testFail("signalfd with a bad mask size must return EINVAL"); return; }
    if ((S32)syscall_signalfd4(context.thread, -1, maskAddr, 8, 0x40000000u) != -K_EINVAL) { testFail("signalfd with bad flags must return EINVAL"); return; }
    U32 sigFd = syscall_signalfd4(context.thread, -1, maskAddr, 8, 0);
    if ((S32)sigFd < 0) { testFail("valid signalfd creation failed"); return; }
    auto sigObj = process->getFileDescriptor((FD)sigFd)->kobject;
    if (sigObj->isWriteReady()) { testFail("signalfd isWriteReady must be false"); return; }
    if (sigObj->writeNative(scratch, 8) != -K_EINVAL) { testFail("signalfd writeNative must return EINVAL"); return; }
    if ((S32)sigObj->stat(context.process.get(), statAddr, false) != -K_ENOTSUP) { testFail("signalfd stat must return ENOTSUP"); return; }
    sigObj->waitForEvents(context.thread->pollCond, K_POLLOUT); // POLLOUT arm used to kpanic
    process->close((FD)sigFd);

    // ---- netlink option lengths (previously kpanic) ----
    auto netlink = std::make_shared<KNetLinkObject>(K_AF_NETLINK, K_SOCK_RAW, 0);
    KFileDescriptorPtr nlFd = process->allocFileDescriptor(netlink, K_O_RDWR, 0, -1, 0);
    context.memory->writed(valueAddr, 4096);
    if ((S32)netlink->setsockopt(context.thread, nlFd, K_SOL_SOCKET, K_SO_RCVBUF, valueAddr, 3) != -K_EINVAL) {
        testFail("netlink setsockopt with len 3 must return EINVAL");
        return;
    }
    context.memory->writed(lenAddr, 3);
    if ((S32)netlink->getsockopt(context.thread, nlFd, K_SOL_SOCKET, K_SO_RCVBUF, valueAddr, lenAddr) != -K_EINVAL) {
        testFail("netlink getsockopt with len 3 must return EINVAL");
        return;
    }

    // ---- native socket option length (previously kpanic) ----
    auto native = std::make_shared<KNativeSocketObject>(K_AF_INET, K_SOCK_DGRAM, 17);
    if (native->error) { testFail("native datagram socket creation failed"); return; }
    KFileDescriptorPtr nFd = process->allocFileDescriptor(native, K_O_RDWR, 0, -1, 0);
    if ((S32)native->setsockopt(context.thread, nFd, K_IPPROTO_IP, K_IP_TTL, valueAddr, 3) != -K_EINVAL) {
        testFail("native setsockopt IP_TTL with len 3 must return EINVAL");
        return;
    }

    // ---- unix socketpair: sendto/recvfrom/sendmsg honesty ----
    FD fd1 = 0, fd2 = 0;
    if ((S32)ksocketpairFds(context.thread, K_AF_UNIX, K_SOCK_STREAM, 0, 0, fd1, fd2) != 0) { testFail("unix socketpair creation failed"); return; }
    context.memory->writeb(bufAddr, 'x');
    context.memory->writeb(bufAddr + 1, 'y');
    context.memory->writeb(bufAddr + 2, 'z');
    // sendto with no destination used to return 0 without sending.
    if ((S32)ksendto(context.thread, (U32)fd1, bufAddr, 3, 0, 0, 0) != 3) { testFail("unix sendto must deliver and return 3"); return; }
    // recvfrom with a source-address buffer used to return 0 unread.
    context.memory->writed(srcLenAddr, 16);
    if ((S32)krecvfrom(context.thread, (U32)fd2, recvAddr, 8, 0, srcAddr, srcLenAddr) != 3) { testFail("unix recvfrom must read and return 3"); return; }
    if (context.memory->readb(recvAddr) != 'x' || context.memory->readb(recvAddr + 1) != 'y' ||
        context.memory->readw(srcAddr) != K_AF_UNIX || context.memory->readd(srcLenAddr) != 2) {
        testFail("unix recvfrom payload or unnamed peer address wrong");
        return;
    }
    // sendmsg with an unsupported control-message level used to kpanic.
    context.memory->writed(msgAddr, 0);          // msg_name
    context.memory->writed(msgAddr + 4, 0);      // msg_namelen
    context.memory->writed(msgAddr + 8, iovAddr);
    context.memory->writed(msgAddr + 12, 1);     // msg_iovlen
    context.memory->writed(msgAddr + 16, cmsgAddr);
    context.memory->writed(msgAddr + 20, 16);    // msg_controllen
    context.memory->writed(msgAddr + 24, 0);     // msg_flags
    context.memory->writed(iovAddr, bufAddr);
    context.memory->writed(iovAddr + 4, 3);
    context.memory->writed(cmsgAddr, 16);        // cmsg_len
    context.memory->writed(cmsgAddr + 4, 999);   // cmsg_level: not SOL_SOCKET
    context.memory->writed(cmsgAddr + 8, 0);     // cmsg_type
    if ((S32)ksendmsg(context.thread, (U32)fd1, msgAddr, 0) != -K_EINVAL) { testFail("unix sendmsg with a bad cmsg level must return EINVAL"); return; }
    process->close(fd1);
    process->close(fd2);
}

#endif
