/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
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

#include "boxedwine.h"

#ifdef __TEST

#include "kerror.h"
#include "knativesocket.h"
#include "ksocket.h"
#include "ksignal.h"
#include "../cpu/testCPU.h"

// fcntl async ownership (F_SETOWN/F_GETOWN/F_SETSIG/F_GETSIG/F_SETOWN_EX)
// must round-trip, O_ASYNC must be settable on any fd without a kpanic,
// and a loopback datagram arriving on an O_ASYNC socket must queue the
// configured signal (default SIGIO) on the owner process.

static U64 sigBit(U32 signo) { return 1ULL << (signo - 1); }

static void writeSockaddrIn(KMemory* memory, U32 address, U16 port) {
    memory->memset(address, 0, 16);
    memory->writew(address, K_AF_INET);
    memory->writeb(address + 2, (U8)(port >> 8));
    memory->writeb(address + 3, (U8)(port & 0xff));
    memory->writed(address + 4, 0x0100007f); // 127.0.0.1 little-endian bytes
}

void testAsyncSigio() {
    TestContext& context = testContext();
    KProcess* process = context.process.get();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;

    constexpr U32 sockaddrAt = TEST_HEAP_ADDRESS + 0x600;
    constexpr U32 payloadAt = TEST_HEAP_ADDRESS + 0x620;
    constexpr U32 ownerExAt = TEST_HEAP_ADDRESS + 0x640;
    constexpr U16 port = 54199;

    auto receiver = std::make_shared<KNativeSocketObject>(K_AF_INET, K_SOCK_DGRAM, 17);
    receiver->loopbackDatagram = true;
    auto receiverFd = process->allocFileDescriptor(receiver, K_O_RDWR, 0, -1, 0);
    if (!receiverFd) {
        testFail("could not allocate a descriptor for the SIGIO receiver");
        return;
    }
    U32 fd = receiverFd->handle;
    writeSockaddrIn(memory, sockaddrAt, port);
    if (receiver->bind(thread, receiverFd, sockaddrAt, 16) != 0) {
        testFail("SIGIO receiver bind failed");
        return;
    }

    // Guest fcntl command numbers (x86): GETFL 3, SETFL 4, SETOWN 8,
    // GETOWN 9, SETSIG 10, GETSIG 11, SETOWN_EX 15, GETOWN_EX 16.
    // No owner yet: Linux returns 0.
    S32 result = process->fcntrl(thread, fd, 9, 0);
    if (result != 0) {
        testFail("F_GETOWN with no owner returned %d, expected 0", result);
    }
    result = process->fcntrl(thread, fd, 8, process->id);
    if (result != 0) {
        testFail("F_SETOWN(self) returned %d", result);
    }
    result = process->fcntrl(thread, fd, 9, 0);
    if (result != (S32)process->id) {
        testFail("F_GETOWN returned %d, expected the owner pid %u", result, process->id);
    }
    // f_owner_ex round-trip.
    memory->writed(ownerExAt, 1); // F_OWNER_PID
    memory->writed(ownerExAt + 4, process->id);
    result = process->fcntrl(thread, fd, 15, ownerExAt);
    if (result != 0) {
        testFail("F_SETOWN_EX returned %d", result);
    }
    memory->memset(ownerExAt, 0, 8);
    result = process->fcntrl(thread, fd, 16, ownerExAt);
    if (result != 0 || memory->readd(ownerExAt) != 1 || memory->readd(ownerExAt + 4) != process->id) {
        testFail("F_GETOWN_EX did not round-trip: result=%d type=%u pid=%u",
                 result, memory->readd(ownerExAt), memory->readd(ownerExAt + 4));
    }
    // Signal selection: default 0 (SIGIO), settable, validated.
    result = process->fcntrl(thread, fd, 11, 0);
    if (result != 0) {
        testFail("F_GETSIG default returned %d, expected 0 (SIGIO)", result);
    }
    result = process->fcntrl(thread, fd, 10, K_SIGUSR1);
    if (result != 0) {
        testFail("F_SETSIG(SIGUSR1) returned %d", result);
    }
    result = process->fcntrl(thread, fd, 11, 0);
    if (result != K_SIGUSR1) {
        testFail("F_GETSIG returned %d, expected SIGUSR1", result);
    }
    result = process->fcntrl(thread, fd, 10, 65);
    if (result != -K_EINVAL) {
        testFail("F_SETSIG(65) returned %d, expected -EINVAL", result);
    }

    // O_ASYNC on a socket must enable, not kpanic.
    U32 flags = process->fcntrl(thread, fd, 3, 0);
    result = process->fcntrl(thread, fd, 4, flags | K_O_ASYNC | K_O_NONBLOCK);
    if (result != 0) {
        testFail("F_SETFL(O_ASYNC) on a socket returned %d", result);
    }
    flags = process->fcntrl(thread, fd, 3, 0);
    if (!(flags & K_O_ASYNC)) {
        testFail("F_GETFL lost O_ASYNC after F_SETFL: flags=0x%x", flags);
    }

    // Delivery: a loopback datagram queues the configured signal.
    auto sender = std::make_shared<KNativeSocketObject>(K_AF_INET, K_SOCK_DGRAM, 17);
    sender->loopbackDatagram = true;
    auto senderFd = process->allocFileDescriptor(sender, K_O_RDWR, 0, -1, 0);
    memory->memcpy(payloadAt, (const U8*)"ping", 4);
    auto sendPing = [&]() {
        writeSockaddrIn(memory, sockaddrAt, port);
        return sender->sendto(thread, senderFd, payloadAt, 4, 0, sockaddrAt, 16);
    };
    process->pendingSignals &= ~sigBit(K_SIGUSR1);
    if (sendPing() != 4) {
        testFail("SIGIO test sendto failed");
    }
    if (!(process->pendingSignals & sigBit(K_SIGUSR1))) {
        testFail("no SIGUSR1 queued for the O_ASYNC owner on packet arrival");
    }
    process->pendingSignals &= ~sigBit(K_SIGUSR1);

    // With F_SETSIG reset to 0 the delivery signal is SIGIO again.
    process->fcntrl(thread, fd, 10, 0);
    process->pendingSignals &= ~sigBit(K_SIGIO);
    if (sendPing() != 4) {
        testFail("SIGIO test second sendto failed");
    }
    if (!(process->pendingSignals & sigBit(K_SIGIO))) {
        testFail("no SIGIO queued for the O_ASYNC owner on packet arrival");
    }
    process->pendingSignals &= ~sigBit(K_SIGIO);

    // Turning O_ASYNC off stops delivery.
    flags = process->fcntrl(thread, fd, 3, 0);
    process->fcntrl(thread, fd, 4, (flags & ~K_O_ASYNC) | K_O_NONBLOCK);
    if (sendPing() != 4) {
        testFail("SIGIO test third sendto failed");
    }
    if (process->pendingSignals & sigBit(K_SIGIO)) {
        testFail("SIGIO queued after O_ASYNC was turned off");
    }

    // O_ASYNC on a non-socket fd (timerfd) also stores instead of kpanic'ing
    // or silently dropping the flag.
    S32 timerFd = process->timerfd_create(1 /* CLOCK_MONOTONIC */, 0);
    if (timerFd >= 0) {
        U32 tflags = process->fcntrl(thread, timerFd, 3, 0);
        process->fcntrl(thread, timerFd, 4, tflags | K_O_ASYNC);
        tflags = process->fcntrl(thread, timerFd, 3, 0);
        if (!(tflags & K_O_ASYNC)) {
            testFail("F_GETFL lost O_ASYNC on a timerfd");
        }
        process->close(timerFd);
    }

    process->close(fd);
    process->close(senderFd->handle);
}

#endif
