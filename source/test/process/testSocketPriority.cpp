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

#include "knetlink.h"
#include "knativesocket.h"
#include "ksocket.h"
#include "kunixsocket.h"
#include "../cpu/testCPU.h"

static void checkSocketPriority(KSocketObject& socket, const char* socketName, U32 priority) {
    TestContext& context = testContext();
    constexpr U32 valueAddress = TEST_HEAP_ADDRESS + 0x100;
    constexpr U32 resultAddress = valueAddress + 4;
    constexpr U32 lengthAddress = resultAddress + 4;

    context.memory->writed(valueAddress, priority);
    U32 result = socket.setsockopt(context.thread, KFileDescriptorPtr(), K_SOL_SOCKET,
                                   K_SO_PRIORITY, valueAddress, 4);
    if ((S32)result != 0) {
        testFail("%s setsockopt(SO_PRIORITY) returned %d", socketName, (S32)result);
        return;
    }

    context.memory->writed(resultAddress, 0);
    context.memory->writed(lengthAddress, 4);
    result = socket.getsockopt(context.thread, KFileDescriptorPtr(), K_SOL_SOCKET,
                               K_SO_PRIORITY, resultAddress, lengthAddress);
    if ((S32)result != 0) {
        testFail("%s getsockopt(SO_PRIORITY) returned %d", socketName, (S32)result);
        return;
    }
    U32 received = context.memory->readd(resultAddress);
    U32 receivedLength = context.memory->readd(lengthAddress);
    if (received != priority || receivedLength != 4) {
        testFail("%s SO_PRIORITY expected %u/len 4, got %u/len %u",
                 socketName, priority, received, receivedLength);
    }
}

void testInetDatagramLoopback() {
    TestContext& context = testContext();
    auto socket = std::make_shared<KNativeSocketObject>(K_AF_INET, K_SOCK_DGRAM, 17);
    if (socket->error) {
        testFail("AF_INET datagram socket creation failed: %d", socket->error);
        return;
    }
    // Exercise the in-guest loopback endpoint used by browser AF_INET UDP.
    socket->loopbackDatagram = true;
    KFileDescriptorPtr fd = std::make_shared<KFileDescriptor>(
        context.process, socket, K_O_RDWR | K_O_NONBLOCK, FD_CLOEXEC, 3);
    socket->setBlocking(false);
    if (socket->isBlocking() || !(fd->descriptorFlags & FD_CLOEXEC) ||
        !(fd->accessFlags & K_O_NONBLOCK)) {
        testFail("datagram socket flags were not retained");
        return;
    }

    constexpr U32 address = TEST_HEAP_ADDRESS + 0x200;
    constexpr U32 name = TEST_HEAP_ADDRESS + 0x240;
    constexpr U32 nameLength = TEST_HEAP_ADDRESS + 0x280;
    constexpr U32 message = TEST_HEAP_ADDRESS + 0x300;
    constexpr U32 received = TEST_HEAP_ADDRESS + 0x320;
    constexpr U32 source = TEST_HEAP_ADDRESS + 0x340;
    constexpr U32 sourceLength = TEST_HEAP_ADDRESS + 0x380;

    context.memory->writew(address, K_AF_INET);
    context.memory->writew(address + 2, 0);
    context.memory->writeb(address + 4, 127);
    context.memory->writeb(address + 5, 0);
    context.memory->writeb(address + 6, 0);
    context.memory->writeb(address + 7, 1);
    U32 result = socket->bind(context.thread, fd, address, 16);
    if ((S32)result != 0) {
        testFail("AF_INET datagram bind(127.0.0.1:0) returned %d", (S32)result);
        return;
    }

    context.memory->writed(nameLength, 16);
    result = socket->getsockname(context.thread, fd, name, nameLength);
    const U16 port = (U16)((context.memory->readb(name + 2) << 8) | context.memory->readb(name + 3));
    if ((S32)result != 0 || !port || context.memory->readd(nameLength) != 16) {
        testFail("AF_INET datagram getsockname did not return its assigned loopback port");
        return;
    }

    context.memory->writew(address + 2, (U16)((port << 8) | (port >> 8)));
    context.memory->writeb(message, 'h');
    context.memory->writeb(message + 1, 'e');
    context.memory->writeb(message + 2, 'l');
    context.memory->writeb(message + 3, 'l');
    context.memory->writeb(message + 4, 'o');
    result = socket->sendto(context.thread, fd, message, 5, 0, name, 16);
    if ((S32)result != 5) {
        testFail("AF_INET datagram sendto loopback returned %d", (S32)result);
        return;
    }

    context.memory->writed(sourceLength, 16);
    result = socket->recvfrom(context.thread, fd, received, 8, 0, source, sourceLength);
    if ((S32)result != 5 || context.memory->readb(received) != 'h' ||
        context.memory->readb(received + 1) != 'e' || context.memory->readb(received + 2) != 'l' ||
        context.memory->readb(received + 3) != 'l' || context.memory->readb(received + 4) != 'o' ||
        context.memory->readd(sourceLength) != 16) {
        testFail("AF_INET datagram recvfrom did not return the loopback payload and source address");
    }
}

void testInetStreamLoopback() {
    TestContext& context = testContext();
    constexpr U32 address = TEST_HEAP_ADDRESS + 0x500;
    constexpr U32 name = TEST_HEAP_ADDRESS + 0x540;
    constexpr U32 nameLength = TEST_HEAP_ADDRESS + 0x580;
    constexpr U32 optionValue = TEST_HEAP_ADDRESS + 0x5c0;
    constexpr U32 optionLength = TEST_HEAP_ADDRESS + 0x5c4;
    const U32 family = K_AF_INET;

    auto server = std::make_shared<KNativeSocketObject>(family, K_SOCK_STREAM, 6);
    auto client = std::make_shared<KNativeSocketObject>(family, K_SOCK_STREAM, 6);
    if (server->error || client->error) {
        testFail("AF_INET stream socket creation failed");
        return;
    }
    // Exercise the browser-local transport on native builds without making a
    // host TCP connection.
    server->loopbackStream = true;
    client->loopbackStream = true;
    KFileDescriptorPtr serverFd = std::make_shared<KFileDescriptor>(context.process, server, K_O_RDWR, 0, 30);
    KFileDescriptorPtr clientFd = std::make_shared<KFileDescriptor>(
        context.process, client, K_O_RDWR | K_O_NONBLOCK, FD_CLOEXEC, 31);
    client->setBlocking(false);
    if (client->isBlocking() || !(clientFd->accessFlags & K_O_NONBLOCK) ||
        !(clientFd->descriptorFlags & FD_CLOEXEC)) {
        testFail("AF_INET stream nonblocking/cloexec flags were not retained");
        return;
    }

    context.memory->writed(optionValue, 1);
    U32 result = server->setsockopt(context.thread, serverFd, K_SOL_SOCKET,
                                    K_SO_REUSEADDR, optionValue, 4);
    context.memory->writed(optionLength, 4);
    if ((S32)result || (S32)server->getsockopt(context.thread, serverFd, K_SOL_SOCKET,
                                                K_SO_REUSEADDR, optionValue, optionLength) ||
        context.memory->readd(optionValue) != 1) {
        testFail("AF_INET stream SO_REUSEADDR did not round-trip");
        return;
    }

    context.memory->writew(address, K_AF_INET);
    context.memory->writew(address + 2, 0);
    context.memory->writeb(address + 4, 127);
    context.memory->writeb(address + 5, 0);
    context.memory->writeb(address + 6, 0);
    context.memory->writeb(address + 7, 1);
    result = server->bind(context.thread, serverFd, address, 16);
    if ((S32)result || (S32)server->listen(context.thread, serverFd, 4)) {
        testFail("AF_INET stream bind/listen on 127.0.0.1:0 failed");
        return;
    }

    context.memory->writed(nameLength, 16);
    result = server->getsockname(context.thread, serverFd, name, nameLength);
    if ((S32)result || context.memory->readd(nameLength) != 16 ||
        (context.memory->readb(name + 2) == 0 && context.memory->readb(name + 3) == 0)) {
        testFail("AF_INET stream getsockname did not return an assigned port");
        return;
    }
    auto refused = std::make_shared<KNativeSocketObject>(family, K_SOCK_STREAM, 6);
    refused->loopbackStream = true;
    KFileDescriptorPtr refusedFd = std::make_shared<KFileDescriptor>(context.process, refused, K_O_RDWR, 0, 33);
    const U16 listeningPort = (U16)((context.memory->readb(name + 2) << 8) | context.memory->readb(name + 3));
    const U16 refusedPort = listeningPort == 65535 ? 1 : listeningPort + 1;
    context.memory->writeb(name + 2, (refusedPort >> 8) & 0xff);
    context.memory->writeb(name + 3, refusedPort & 0xff);
    if ((S32)refused->connect(context.thread, refusedFd, name, 16) != -K_ECONNREFUSED) {
        testFail("AF_INET stream connect to a non-listening port did not return ECONNREFUSED");
        return;
    }
    // Restore the listening endpoint's address for the successful client.
    context.memory->writeb(name + 2, (listeningPort >> 8) & 0xff);
    context.memory->writeb(name + 3, listeningPort & 0xff);
    result = client->connect(context.thread, clientFd, name, 16);
    if ((S32)result || !server->isReadReady() || !client->isWriteReady()) {
        testFail("AF_INET stream loopback connect/readiness failed: %d", (S32)result);
        return;
    }

    std::shared_ptr<KNativeSocketObject> accepted;
    result = server->takeLoopbackStreamConnection(accepted);
    if ((S32)result < 0 || !accepted) {
        testFail("AF_INET stream loopback accept failed: %d", (S32)result);
        return;
    }
    KFileDescriptorPtr acceptedFd = std::make_shared<KFileDescriptor>(context.process, accepted, K_O_RDWR, 0, 32);
    const U8 request[] = {'p', 'i', 'n', 'g'};
    U8 buffer[8] = {};
    const U32 sent = client->writeNative((U8*)request, sizeof(request));
    const bool serverReady = accepted->isReadReady();
    const U32 receivedRequest = accepted->readNative(buffer, sizeof(buffer));
    if ((S32)sent != (S32)sizeof(request) || !serverReady ||
        (S32)receivedRequest != (S32)sizeof(request) || memcmp(buffer, request, sizeof(request)) != 0) {
        testFail("AF_INET stream client-to-server data failed: sent=%d ready=%d received=%d first=%d",
                 (S32)sent, serverReady ? 1 : 0, (S32)receivedRequest, buffer[0]);
        return;
    }

    const U8 response[] = {'p', 'o', 'n', 'g'};
    result = accepted->writeNative((U8*)response, sizeof(response));
    memset(buffer, 0, sizeof(buffer));
    if ((S32)result != (S32)sizeof(response) || !client->isReadReady() ||
        (S32)client->readNative(buffer, sizeof(buffer)) != (S32)sizeof(response) ||
        memcmp(buffer, response, sizeof(response)) != 0) {
        testFail("AF_INET stream server-to-client loopback data failed");
        return;
    }

    accepted.reset();
    acceptedFd.reset();
    if (!client->isReadReady() || client->readNative(buffer, sizeof(buffer)) != 0) {
        testFail("AF_INET stream close did not produce readable EOF");
        return;
    }
}

void testSocketPriority() {
    KUnixSocketObject unixSocket(K_AF_UNIX, K_SOCK_STREAM, 0);
    KNetLinkObject netlinkSocket(K_AF_NETLINK, K_SOCK_DGRAM, 0);
    KNativeSocketObject nativeSocket(K_AF_INET, K_SOCK_DGRAM, 17);

    checkSocketPriority(unixSocket, "AF_UNIX", 3);
    checkSocketPriority(netlinkSocket, "AF_NETLINK", 5);
    if (nativeSocket.nativeSocket < 0) {
        testFail("AF_INET datagram socket creation failed: %d", nativeSocket.error);
    } else {
        checkSocketPriority(nativeSocket, "AF_INET", 6);
    }
}

#endif
