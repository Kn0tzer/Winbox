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
