#include "boxedwine.h"
#include "ksocket.h"

#ifdef __TEST

#include "../cpu/testCPU.h"

namespace {

void expectSocketRecvFlag(const char* name, bool actual) {
    if (!actual) {
        testFail("socket receive flag was not decoded: %s", name);
    }
}

void expectSocketRecvMask(const char* name, U32 actual, U32 expected) {
    if (actual != expected) {
        testFail("socket receive %s mask expected 0x%X, got 0x%X", name, expected, actual);
    }
}

} // namespace

void testSocketRecvFlags() {
    KSocketRecvFlags flags = ksocketDecodeRecvFlags(K_MSG_PEEK);
    expectSocketRecvFlag("MSG_PEEK", flags.peek);
    expectSocketRecvMask("MSG_PEEK unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(K_MSG_TRUNC);
    expectSocketRecvFlag("MSG_TRUNC", flags.truncate);
    expectSocketRecvMask("MSG_TRUNC unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(K_MSG_DONTWAIT);
    expectSocketRecvFlag("MSG_DONTWAIT", flags.dontWait);
    expectSocketRecvMask("MSG_DONTWAIT unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(K_MSG_WAITALL);
    expectSocketRecvFlag("MSG_WAITALL", flags.waitAll);
    expectSocketRecvMask("MSG_WAITALL unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(K_MSG_CMSG_CLOEXEC);
    expectSocketRecvFlag("MSG_CMSG_CLOEXEC", flags.closeOnExec);
    expectSocketRecvMask("MSG_CMSG_CLOEXEC unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(K_MSG_NOSIGNAL | K_MSG_DONTROUTE | K_MSG_PROXY);
    expectSocketRecvMask("ignored", flags.ignored, K_MSG_NOSIGNAL | K_MSG_DONTROUTE | K_MSG_PROXY);
    expectSocketRecvMask("ignored unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(K_MSG_PEEK | K_MSG_TRUNC);
    expectSocketRecvFlag("hex 0x22 PEEK", flags.peek);
    expectSocketRecvFlag("hex 0x22 TRUNC", flags.truncate);
    expectSocketRecvMask("hex 0x22 unknown", flags.unknown, 0);

    flags = ksocketDecodeRecvFlags(0x80000000);
    expectSocketRecvMask("unknown", flags.unknown, 0x80000000);
}

#endif
