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
#include "../cpu/testCPU.h"

// A guest resolver sends DNS queries as UDP datagrams to 127.0.0.1:53
// (the rootfs resolv.conf) where nothing used to be bound: the datagram
// was silently dropped and the lookup hung. The socket layer must now
// answer: a well-formed A query for a resolvable name gets a response
// from the nameserver address, an unresolvable name gets an error
// response (not silence), and garbage gets no response at all.

static void writeSockaddrIn(KMemory* memory, U32 address, U32 ip, U16 port) {
    memory->memset(address, 0, 16);
    memory->writew(address, K_AF_INET);
    memory->writeb(address + 2, (U8)(port >> 8));
    memory->writeb(address + 3, (U8)(port & 0xff));
    memory->writed(address + 4, ip);
}

// Builds a one-question query at `at`, returns its length.
static U32 buildQuery(KMemory* memory, U32 at, U16 id, const char* name, U16 qtype) {
    U8 packet[512];
    memset(packet, 0, sizeof(packet));
    packet[0] = (U8)(id >> 8);
    packet[1] = (U8)(id & 0xff);
    packet[2] = 0x01; // RD
    packet[5] = 0x01; // QDCOUNT = 1
    U32 pos = 12;
    const char* p = name;
    while (*p) {
        const char* dot = strchr(p, '.');
        U32 len = dot ? (U32)(dot - p) : (U32)strlen(p);
        packet[pos++] = (U8)len;
        memcpy(packet + pos, p, len);
        pos += len;
        p += len + (dot ? 1 : 0);
    }
    packet[pos++] = 0;
    packet[pos++] = (U8)(qtype >> 8);
    packet[pos++] = (U8)(qtype & 0xff);
    packet[pos++] = 0;
    packet[pos++] = 1; // IN
    memory->memcpy(at, packet, pos);
    return pos;
}

void testDnsLoopbackService() {
    TestContext& context = testContext();
    KProcess* process = context.process.get();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;

    constexpr U32 packetAt = TEST_HEAP_ADDRESS + 0x800;
    constexpr U32 sockaddrAt = TEST_HEAP_ADDRESS + 0xa20;
    constexpr U32 fromAt = TEST_HEAP_ADDRESS + 0xa40;
    constexpr U32 lenAt = TEST_HEAP_ADDRESS + 0xa60;

    auto socket = std::make_shared<KNativeSocketObject>(K_AF_INET, K_SOCK_DGRAM, 17);
    socket->loopbackDatagram = true;
    auto descriptor = process->allocFileDescriptor(socket, K_O_RDWR, 0, -1, 0);
    if (!descriptor) {
        testFail("could not allocate a descriptor for the DNS test socket");
        return;
    }
    writeSockaddrIn(memory, sockaddrAt, 0x0100007f, 0);
    if (socket->bind(thread, descriptor, sockaddrAt, 16) != 0) {
        testFail("DNS test socket bind failed");
        return;
    }
    socket->setBlocking(false);

    auto query = [&](const char* name, U16 id, U16 qtype) -> S64 {
        U32 length = buildQuery(memory, packetAt, id, name, qtype);
        writeSockaddrIn(memory, sockaddrAt, 0x0100007f, 53);
        return (S32)socket->sendto(thread, descriptor, packetAt, length, 0, sockaddrAt, 16);
    };
    // Returns the response length, or a negative errno; header fields are
    // read back through the out parameters.
    auto receive = [&](U16* idOut, U16* flagsOut, U16* answersOut, U32* answerIpOut) -> S64 {
        memory->writed(lenAt, 16);
        // Socket methods return U32; negative errnos need the S32 view.
        S64 result = (S32)socket->recvfrom(thread, descriptor, packetAt, 512, 0, fromAt, lenAt);
        if (result < 12) {
            *idOut = 0; *flagsOut = 0; *answersOut = 0; *answerIpOut = 0;
            return result;
        }
        U8 header[12];
        memory->memcpy(header, packetAt, sizeof(header));
        *idOut = ((U16)header[0] << 8) | header[1];
        *flagsOut = ((U16)header[2] << 8) | header[3];
        *answersOut = ((U16)header[6] << 8) | header[7];
        *answerIpOut = 0;
        if (*answersOut > 0) {
            // Skip the copied question, then read the first answer's
            // rdata (name pointer, type, class, ttl, rdlength, rdata).
            U32 pos = 12;
            while (memory->readb(packetAt + pos) != 0) pos += memory->readb(packetAt + pos) + 1;
            pos += 5; // root byte + qtype + qclass
            U16 rdlength = (memory->readb(packetAt + pos + 10) << 8) | memory->readb(packetAt + pos + 11);
            if (rdlength == 4) {
                *answerIpOut = memory->readd(packetAt + pos + 12);
            }
        }
        return result;
    };

    // A resolvable name: answered by 127.0.0.1:53 with 127.0.0.1.
    U16 id, flags, answers;
    U32 answerIp;
    if (query("localhost", 0x1234, 1) < 0) {
        testFail("DNS query sendto failed");
        return;
    }
    S64 received = receive(&id, &flags, &answers, &answerIp);
    if (received < 12) {
        testFail("no DNS response for localhost (dropped datagram?): %lld", (long long)received);
    } else {
        if (id != 0x1234) {
            testFail("DNS response id 0x%x did not echo the query id", id);
        }
        if (!(flags & 0x8000) || (flags & 0xf) != 0) {
            testFail("DNS response flags wrong for localhost: 0x%x", flags);
        }
        if (answers < 1 || answerIp != 0x0100007f) {
            testFail("DNS response for localhost had %u answers, first A = 0x%x", answers, answerIp);
        }
        // The response's source must be the nameserver address.
        if (memory->readd(fromAt + 4) != 0x0100007f ||
            memory->readb(fromAt + 2) != 0 || memory->readb(fromAt + 3) != 53) {
            testFail("DNS response did not come from 127.0.0.1:53");
        }
    }

    // An unresolvable name: an error response, not silence.
    if (query("k3b-nonexistent.invalid", 0x2345, 1) < 0) {
        testFail("DNS negative query sendto failed");
        return;
    }
    received = receive(&id, &flags, &answers, &answerIp);
    if (received < 12) {
        testFail("no DNS response for an unresolvable name (must be NXDOMAIN/SERVFAIL, not silence)");
    } else if (id != 0x2345 || !(flags & 0x8000) || ((flags & 0xf) != 3 && (flags & 0xf) != 2)) {
        testFail("DNS negative response wrong: id=0x%x flags=0x%x", id, flags);
    }

    // Garbage to port 53: dropped, exactly like a closed UDP port.
    memory->memcpy(packetAt, (const U8*)"\x01\x02\x03", 3);
    writeSockaddrIn(memory, sockaddrAt, 0x0100007f, 53);
    socket->sendto(thread, descriptor, packetAt, 3, 0, sockaddrAt, 16);
    received = receive(&id, &flags, &answers, &answerIp);
    if (received != -K_EWOULDBLOCK) {
        testFail("garbage DNS packet got a response: %lld", (long long)received);
    }

    process->close(descriptor->handle);
}

#endif
