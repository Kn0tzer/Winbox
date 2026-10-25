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
#include "ksocket.h"
#include "kunixsocket.h"
#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#endif

#include <atomic>
#include <chrono>
#include <thread>

namespace {
constexpr U32 kEpollCtlAdd = 1;
constexpr U32 kEpollCtlMod = 3;
constexpr U32 kEpollIn = 0x001;
constexpr U32 kEpollEt = 0x80000000u;
constexpr U32 kWaitTimeoutMs = 500;
constexpr U32 kEventAddress = TEST_HEAP_ADDRESS + 0x100;
constexpr U32 kResultAddress = TEST_HEAP_ADDRESS + 0x120;
constexpr U32 kPayloadAddress = TEST_HEAP_ADDRESS + 0x200;
constexpr U32 kIovAddress = TEST_HEAP_ADDRESS + 0x220;
constexpr U32 kMessageAddress = TEST_HEAP_ADDRESS + 0x240;

class TestUnixSocket : public KUnixSocketObject {
public:
    explicit TestUnixSocket(U32 type) : KUnixSocketObject(K_AF_UNIX, type, 0) {}

    U32 registeredWaiters() const { return lockCond->parentsCount(); }
    void connectTo(const std::shared_ptr<TestUnixSocket>& peer) {
        this->connection = peer;
        this->connected = true;
    }
};

using TestUnixSocketPtr = std::shared_ptr<TestUnixSocket>;

void makePair(U32 type, TestUnixSocketPtr& sender, TestUnixSocketPtr& receiver) {
    sender = std::make_shared<TestUnixSocket>(type);
    receiver = std::make_shared<TestUnixSocket>(type);
    sender->connection = receiver;
    receiver->connection = sender;
    sender->connected = true;
    receiver->connected = true;
}

bool waitForRegistration(const TestUnixSocketPtr& socket) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (socket->registeredWaiters() != 0) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

void ensureFdNode(KProcessPtr process) {
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
}

bool runEpollWake(TestContext& context, U32 socketType, U32 eventMask, bool modifyWhileBlocked,
                  bool closePeer, const char* caseName) {
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    KMemory* memory = context.memory;
    ensureFdNode(process);

    TestUnixSocketPtr sender;
    TestUnixSocketPtr receiver;
    makePair(socketType, sender, receiver);

    KFileDescriptorPtr receiverFd = process->allocFileDescriptor(receiver, K_O_RDWR, 0, -1, 0);
    KFileDescriptorPtr epollFd;
    std::shared_ptr<KEPoll> epoll = std::make_shared<KEPoll>();
    epollFd = process->allocFileDescriptor(epoll, K_O_RDWR, 0, -1, 0);

    auto writeEvent = [&](U32 events) {
        memory->writed(kEventAddress, events);
        memory->writeq(kEventAddress + 4, 0x123456789ABCDEF0ull);
    };
    writeEvent(modifyWhileBlocked ? 0 : eventMask);
    U32 result = epoll->ctl(memory, kEpollCtlAdd, receiverFd->handle, kEventAddress);
    if ((S32)result != 0) {
        testFail("%s epoll_ctl(ADD) returned %d", caseName, (S32)result);
        process->close(epollFd->handle);
        process->close(receiverFd->handle);
        return false;
    }

    std::atomic<S32> waitResult{-999};
    std::thread waiter([&]() {
        KThread::setCurrentThread(thread);
        waitResult.store((S32)epoll->wait(thread, kResultAddress, 4, kWaitTimeoutMs), std::memory_order_release);
    });

    if (!waitForRegistration(receiver)) {
        testFail("%s epoll waiter never registered on the socket condition", caseName);
        waiter.join();
        process->close(epollFd->handle);
        process->close(receiverFd->handle);
        return false;
    }

    if (modifyWhileBlocked) {
        writeEvent(eventMask);
        result = epoll->ctl(memory, kEpollCtlMod, receiverFd->handle, kEventAddress);
        if ((S32)result != 0) {
            testFail("%s epoll_ctl(MOD) returned %d", caseName, (S32)result);
            waiter.join();
            process->close(epollFd->handle);
            process->close(receiverFd->handle);
            return false;
        }
    }

    if (closePeer) {
        KFileDescriptorPtr senderFd = process->allocFileDescriptor(sender, K_O_RDWR, 0, -1, 0);
        process->close(senderFd->handle);
        sender.reset();
        senderFd.reset();
    } else if (socketType == K_SOCK_DGRAM) {
        memory->writeb(kPayloadAddress, 0x5A);
        memory->writed(kIovAddress, kPayloadAddress);
        memory->writed(kIovAddress + 4, 1);
        memory->writed(kMessageAddress, 0); // msg_name
        memory->writed(kMessageAddress + 4, 0); // msg_namelen
        memory->writed(kMessageAddress + 8, kIovAddress);
        memory->writed(kMessageAddress + 12, 1); // msg_iovlen
        memory->writed(kMessageAddress + 16, 0); // msg_control
        memory->writed(kMessageAddress + 20, 0); // msg_controllen
        memory->writed(kMessageAddress + 24, 0); // msg_flags
        result = sender->sendmsg(thread, KFileDescriptorPtr(), kMessageAddress, 0);
        if ((S32)result < 0 || result != 1) {
            testFail("%s sendmsg returned %d", caseName, (S32)result);
        }
    } else {
        U8 payload = 0x5A;
        result = sender->writeNative(&payload, 1);
        if ((S32)result < 0 || result != 1) {
            testFail("%s writeNative returned %d", caseName, (S32)result);
        }
    }

    waiter.join();
    S32 actual = waitResult.load(std::memory_order_acquire);
    if (actual != 1) {
        testFail("%s epoll waiter returned %d, expected one event", caseName, actual);
    } else {
        U32 actualEvents = memory->readd(kResultAddress);
        if ((actualEvents & kEpollIn) == 0) {
            testFail("%s epoll event mask was 0x%X, expected EPOLLIN", caseName, actualEvents);
        }
    }

    process->close(epollFd->handle);
    process->close(receiverFd->handle);
    return actual == 1;
}
}

void testUnixSocketOptions() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    TestUnixSocket socket(K_SOCK_STREAM);
    memory->writed(kPayloadAddress, 5);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PRIORITY,
                               kPayloadAddress, 4) != 0) {
        testFail("Unix SO_PRIORITY(5) set failed");
        return;
    }
    memory->writed(kIovAddress, 4);
    memory->writed(kResultAddress, 0);
    if ((S32)socket.getsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PRIORITY,
                               kResultAddress, kIovAddress) != 0 ||
        memory->readd(kResultAddress) != 5 || memory->readd(kIovAddress) != 4) {
        testFail("Unix SO_PRIORITY get did not round-trip the value and optlen");
    }
    memory->writed(kPayloadAddress, 7);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PRIORITY,
                               kPayloadAddress, 4) != -K_EPERM) {
        testFail("Unix SO_PRIORITY above the unprivileged range was not rejected with EPERM");
    }
    memory->writed(kPayloadAddress, 1);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PASSCRED,
                               kPayloadAddress, 4) != 0) {
        testFail("Unix SO_PASSCRED set failed");
    }
    memory->writed(kIovAddress, 4);
    if ((S32)socket.getsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PASSCRED,
                               kResultAddress, kIovAddress) != 0 ||
        memory->readd(kResultAddress) != 1) {
        testFail("Unix SO_PASSCRED value did not round-trip");
    }

    // SO_PASSCRED attaches the sender's credentials to each received message,
    // even when the sender did not supply an explicit control message.
    auto sender = std::make_shared<TestUnixSocket>(K_SOCK_DGRAM);
    auto receiver = std::make_shared<TestUnixSocket>(K_SOCK_DGRAM);
    sender->connectTo(receiver);
    receiver->connectTo(sender);
    if ((S32)receiver->setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PASSCRED,
                                  kPayloadAddress, 4) != 0) {
        testFail("Unix credential ancillary fixture could not enable SO_PASSCRED");
    }
    constexpr U32 messageAddress = kMessageAddress + 0x100;
    constexpr U32 controlAddress = messageAddress + 0x40;
    memory->writeb(kPayloadAddress, 0x5A);
    memory->writed(kIovAddress, kPayloadAddress);
    memory->writed(kIovAddress + 4, 1);
    memory->writed(messageAddress, 0);
    memory->writed(messageAddress + 4, 0);
    memory->writed(messageAddress + 8, kIovAddress);
    memory->writed(messageAddress + 12, 1);
    memory->writed(messageAddress + 16, 0);
    memory->writed(messageAddress + 20, 0);
    memory->writed(messageAddress + 24, 0);
    if ((S32)sender->sendmsg(context.thread, {}, messageAddress, 0) != 1) {
        testFail("Unix credential ancillary fixture sendmsg failed");
    }
    memory->writed(messageAddress + 16, controlAddress);
    memory->writed(messageAddress + 20, 24);
    memory->writed(messageAddress + 24, 0);
    if ((S32)receiver->recvmsg(context.thread, {}, messageAddress, 0) != 1 ||
        memory->readd(messageAddress + 20) != 24 ||
        memory->readd(messageAddress + 24) != 0 ||
        memory->readd(controlAddress) != 24 ||
        memory->readd(controlAddress + 4) != K_SOL_SOCKET ||
        memory->readd(controlAddress + 8) != K_SCM_CREDENTIALS ||
        memory->readd(controlAddress + 12) != context.process->id ||
        memory->readd(controlAddress + 16) != context.process->userId ||
        memory->readd(controlAddress + 20) != context.process->groupId) {
        testFail("Unix SO_PASSCRED did not deliver the sender's SCM_CREDENTIALS record");
    }
    memory->writed(messageAddress + 16, 0);
    memory->writed(messageAddress + 20, 0);
    if ((S32)sender->sendmsg(context.thread, {}, messageAddress, 0) != 1) {
        testFail("Unix credential truncation fixture sendmsg failed");
    }
    memory->writed(messageAddress + 16, controlAddress);
    memory->writed(messageAddress + 20, 16); // less than CMSG_SPACE(sizeof(ucred))
    memory->writed(messageAddress + 24, 0);
    if ((S32)receiver->recvmsg(context.thread, {}, messageAddress, 0) != 1 ||
        memory->readd(messageAddress + 20) != 0 ||
        memory->readd(messageAddress + 24) != K_MSG_CTRUNC) {
        testFail("Unix SO_PASSCRED did not report a truncated credentials control buffer");
    }
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PRIORITY,
                               kPayloadAddress, 2) != -K_EINVAL) {
        testFail("Unix setsockopt accepted a short integer value");
    }
    memory->writed(kPayloadAddress, 1);
    memory->writed(kPayloadAddress + 4, 37);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_LINGER, kPayloadAddress, 8) != 0) {
        testFail("Unix SO_LINGER set failed");
    }
    memory->writed(kIovAddress, 8);
    if ((S32)socket.getsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_LINGER, kResultAddress, kIovAddress) != 0 ||
        memory->readd(kResultAddress) != 1 || memory->readd(kResultAddress + 4) != 37 ||
        memory->readd(kIovAddress) != 8) {
        testFail("Unix SO_LINGER did not round-trip its pair and optlen");
    }
    memory->writed(kPayloadAddress, 3);
    memory->writed(kPayloadAddress + 4, 1000000);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_RCVTIMEO, kPayloadAddress, 8) != -K_EINVAL) {
        testFail("Unix SO_RCVTIMEO accepted an invalid timeval");
    }
    memory->writed(kPayloadAddress + 4, 7);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_RCVTIMEO, kPayloadAddress, 8) != 0 ||
        (S32)socket.getsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_RCVTIMEO, kResultAddress, kIovAddress) != 0 ||
        memory->readd(kResultAddress) != 3 || memory->readd(kResultAddress + 4) != 7) {
        testFail("Unix SO_RCVTIMEO did not validate/store timeval fields");
    }
    memory->writed(kPayloadAddress, 0);
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_RCVLOWAT, kPayloadAddress, 4) != 0 ||
        (S32)socket.getsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_RCVLOWAT, kResultAddress, kIovAddress) != 0 ||
        memory->readd(kResultAddress) != 1) {
        testFail("Unix SO_RCVLOWAT did not clamp to the Linux minimum");
    }
    if ((S32)socket.setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_SNDLOWAT, kPayloadAddress, 4) != -K_ENOPROTOOPT) {
        testFail("Unix SO_SNDLOWAT should be rejected as unsupported");
    }

#ifdef BOXEDWINE_GUEST_X64
    // The x64 recvmsg adapter must preserve the 64-bit cmsghdr layout when it
    // translates the 32-bit Unix socket object's SCM_CREDENTIALS record.
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory64 = context.process->memory64;
    constexpr U64 x64Page = 0x77400000ULL;
    if (!memory64->getPageFlags(x64Page >> K64_PAGE_SHIFT) &&
        memory64->mmapAnonymousFixed(x64Page, K64_PAGE_SIZE, 3) != x64Page) {
        testFail("x64 SO_PASSCRED test could not map msghdr buffers");
        return;
    }
    memory64->memsetGuest(x64Page, 0, K64_PAGE_SIZE);
    auto x64Sender = std::make_shared<TestUnixSocket>(K_SOCK_DGRAM);
    auto x64Receiver = std::make_shared<TestUnixSocket>(K_SOCK_DGRAM);
    x64Sender->connectTo(x64Receiver);
    x64Receiver->connectTo(x64Sender);
    memory->writed(kPayloadAddress, 1);
    if ((S32)x64Receiver->setsockopt(context.thread, {}, K_SOL_SOCKET, K_SO_PASSCRED,
                                     kPayloadAddress, 4) != 0) {
        testFail("x64 SO_PASSCRED test could not enable credentials");
        return;
    }
    KFileDescriptorPtr x64SenderFd = context.process->allocFileDescriptor(x64Sender, K_O_RDWR, 0, -1, 0);
    KFileDescriptorPtr x64ReceiverFd = context.process->allocFileDescriptor(x64Receiver, K_O_RDWR, 0, -1, 0);
    constexpr U64 x64Header = x64Page;
    constexpr U64 x64Iov = x64Page + 64;
    constexpr U64 x64Control = x64Page + 128;
    constexpr U64 x64Payload = x64Page + 256;
    memory64->writeb(x64Payload, 0x5A);
    memory64->writeq(x64Iov, x64Payload);
    memory64->writeq(x64Iov + 8, 1);
    memory64->writeq(x64Header + 16, x64Iov);
    memory64->writeq(x64Header + 24, 1);
    CPU64 cpu64(memory64);
    cpu64.thread = context.thread;
    auto invokeSocketCall = [&](U64 syscallNumber, U32 fd) -> S64 {
        cpu64.reg[X64_RAX].setU64(syscallNumber);
        cpu64.reg[X64_RDI].setU64(fd);
        cpu64.reg[X64_RSI].setU64(x64Header);
        cpu64.reg[X64_RDX].setU64(0);
        ksyscall64(&cpu64);
        return (S64)cpu64.reg[X64_RAX].u64;
    };
    if (invokeSocketCall(46, x64SenderFd->handle) != 1) {
        testFail("x64 sendmsg for credential fixture failed");
    }
    memory64->writeq(x64Header + 32, x64Control);
    memory64->writeq(x64Header + 40, 32);
    memory64->writeq(x64Header + 48, 0);
    if (invokeSocketCall(47, x64ReceiverFd->handle) != 1 ||
        memory64->readq(x64Header + 40) != 32 || memory64->readd(x64Header + 48) != 0 ||
        memory64->readq(x64Control) != 28 ||
        memory64->readd(x64Control + 8) != K_SOL_SOCKET ||
        memory64->readd(x64Control + 12) != K_SCM_CREDENTIALS ||
        memory64->readd(x64Control + 16) != context.process->id ||
        memory64->readd(x64Control + 20) != context.process->userId ||
        memory64->readd(x64Control + 24) != context.process->groupId) {
        testFail("x64 recvmsg did not translate SCM_CREDENTIALS into the 64-bit cmsg ABI");
    }
    context.process->close(x64SenderFd->handle);
    context.process->close(x64ReceiverFd->handle);
#endif
}

void testEpollOneShotAndFlags() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    auto event = std::make_shared<KEvent>();
    event->counter = 1;
    KFileDescriptorPtr eventFd = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    auto epoll = std::make_shared<KEPoll>();
    KFileDescriptorPtr epollFd = process->allocFileDescriptor(epoll, K_O_RDWR, 0, -1, 0);
    auto writeRegistration = [&](U32 flags) {
        context.memory->writed(kEventAddress, flags);
        context.memory->writeq(kEventAddress + 4, 0xAABBCCDDEEFF0011ULL);
        return epoll->ctl(context.memory, kEpollCtlAdd, (FD)eventFd->handle, kEventAddress);
    };
    U32 result = writeRegistration(kEpollIn | 0x40000000u); // EPOLLONESHOT
    if ((S32)result != 0) {
        testFail("EPOLL_CTL_ADD EPOLLONESHOT returned %d", (S32)result);
        process->close(epollFd->handle); process->close(eventFd->handle);
        return;
    }
    if (epoll->wait(context.thread, kResultAddress, 1, 0) != 1 ||
        epoll->wait(context.thread, kResultAddress, 1, 0) != 0) {
        testFail("EPOLLONESHOT did not disable the registration after one event");
    }
    context.memory->writed(kEventAddress, kEpollIn | 0x40000000u);
    context.memory->writeq(kEventAddress + 4, 0xAABBCCDDEEFF0011ULL);
    if ((S32)epoll->ctl(context.memory, kEpollCtlMod, (FD)eventFd->handle, kEventAddress) != 0 ||
        epoll->wait(context.thread, kResultAddress, 1, 0) != 1) {
        testFail("EPOLL_CTL_MOD failed to rearm EPOLLONESHOT");
    }
    process->close(epollFd->handle);

    auto exclusive = std::make_shared<KEPoll>();
    KFileDescriptorPtr exclusiveFd = process->allocFileDescriptor(exclusive, K_O_RDWR, 0, -1, 0);
    context.memory->writed(kEventAddress, kEpollIn | 0x10000000u); // EPOLLEXCLUSIVE
    context.memory->writeq(kEventAddress + 4, 1);
    if ((S32)exclusive->ctl(context.memory, kEpollCtlAdd, (FD)eventFd->handle, kEventAddress) != 0) {
        testFail("EPOLL_CTL_ADD rejected valid EPOLLEXCLUSIVE");
    }
    if ((S32)exclusive->ctl(context.memory, kEpollCtlMod, (FD)eventFd->handle, kEventAddress) != -K_EINVAL) {
        testFail("EPOLL_CTL_MOD accepted EPOLLEXCLUSIVE");
    }
    process->close(exclusiveFd->handle);

    U64 eventValue = 0;
    if (event->readNative((U8*)&eventValue, sizeof(eventValue)) != 8 || eventValue != 1) {
        testFail("EPOLLET eventfd fixture could not clear its initial readable state");
    }
    auto edge = std::make_shared<KEPoll>();
    KFileDescriptorPtr edgeFd = process->allocFileDescriptor(edge, K_O_RDWR, 0, -1, 0);
    context.memory->writed(kEventAddress, kEpollIn | kEpollEt);
    context.memory->writeq(kEventAddress + 4, 0x1122334455667788ULL);
    if ((S32)edge->ctl(context.memory, kEpollCtlAdd, (FD)eventFd->handle, kEventAddress) != 0 ||
        edge->wait(context.thread, kResultAddress, 1, 0) != 0) {
        testFail("EPOLLET reported an event before the descriptor became ready");
    }
    eventValue = 1;
    event->writeNative((U8*)&eventValue, sizeof(eventValue));
    if (edge->wait(context.thread, kResultAddress, 1, 0) != 1 ||
        edge->wait(context.thread, kResultAddress, 1, 0) != 0) {
        testFail("EPOLLET did not suppress repeated delivery while readiness remained asserted");
    }
    event->readNative((U8*)&eventValue, sizeof(eventValue));
    if (edge->wait(context.thread, kResultAddress, 1, 0) != 0) {
        testFail("EPOLLET reported an event after readiness was cleared");
    }
    eventValue = 1;
    event->writeNative((U8*)&eventValue, sizeof(eventValue));
    if (edge->wait(context.thread, kResultAddress, 1, 0) != 1) {
        testFail("EPOLLET failed to report a new ready transition");
    }
    process->close(edgeFd->handle);

    auto event2 = std::make_shared<KEvent>();
    event2->counter = 1;
    KFileDescriptorPtr eventFd2 = process->allocFileDescriptor(event2, K_O_RDWR, 0, -1, 0);
    auto limited = std::make_shared<KEPoll>();
    KFileDescriptorPtr limitedFd = process->allocFileDescriptor(limited, K_O_RDWR, 0, -1, 0);
    context.memory->writed(kEventAddress, kEpollIn | kEpollEt);
    context.memory->writeq(kEventAddress + 4, 0x1111);
    context.memory->writed(kEventAddress + 0x10, kEpollIn | kEpollEt);
    context.memory->writeq(kEventAddress + 0x14, 0x2222);
    if ((S32)limited->ctl(context.memory, kEpollCtlAdd, (FD)eventFd->handle, kEventAddress) != 0 ||
        (S32)limited->ctl(context.memory, kEpollCtlAdd, (FD)eventFd2->handle, kEventAddress + 0x10) != 0) {
        testFail("EPOLLET maxevents fixture registration failed");
    } else {
        const U32 firstCount = limited->wait(context.thread, kResultAddress, 1, 0);
        const U64 firstPayload = context.memory->readq(kResultAddress + 4);
        const U32 secondCount = limited->wait(context.thread, kResultAddress, 1, 0);
        const U64 secondPayload = context.memory->readq(kResultAddress + 4);
        if (firstCount != 1 || secondCount != 1 || firstPayload == secondPayload ||
            (firstPayload != 0x1111 && firstPayload != 0x2222) ||
            (secondPayload != 0x1111 && secondPayload != 0x2222) ||
            limited->wait(context.thread, kResultAddress, 1, 0) != 0) {
            testFail("EPOLLET consumed an edge for a ready descriptor omitted by maxevents");
        }
    }
    process->close(limitedFd->handle);
    process->close(eventFd2->handle);
    process->close(eventFd->handle);
}

void testUnixSocketWakeup() {
    TestContext& context = testContext();
    runEpollWake(context, K_SOCK_STREAM, kEpollIn, false, false, "SOCK_STREAM level-triggered");
    runEpollWake(context, K_SOCK_STREAM, kEpollIn | kEpollEt, false, false, "SOCK_STREAM EPOLLET");
    runEpollWake(context, K_SOCK_SEQPACKET, kEpollIn, false, false, "SOCK_SEQPACKET level-triggered");
    runEpollWake(context, K_SOCK_DGRAM, kEpollIn, false, false, "SOCK_DGRAM level-triggered");
    runEpollWake(context, K_SOCK_STREAM, kEpollIn, true, false, "EPOLL_CTL_MOD while waiter blocked");
    runEpollWake(context, K_SOCK_STREAM, kEpollIn, false, true, "close peer wakes epoll waiter");
}

#endif
