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
#include "../cpu/testCPU.h"

void testGetrandom64Syscalls() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x75000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 getrandom test could not map output page");
        return;
    }

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 buffer, U64 length, U64 flags) -> S64 {
        cpu.reg[X64_RAX].setU64(318); // __NR_getrandom
        cpu.reg[X64_RDI].setU64(buffer);
        cpu.reg[X64_RSI].setU64(length);
        cpu.reg[X64_RDX].setU64(flags);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    U8 sentinel[32];
    std::memset(sentinel, 0xA5, sizeof(sentinel));
    memory->memcpyToGuest(page, sentinel, sizeof(sentinel));
    if (invoke(page, sizeof(sentinel), 4) != (S64)sizeof(sentinel)) {
        testFail("x64 getrandom did not fill a valid buffer");
        return;
    }
    U8 first[32];
    memory->memcpyFromGuest(first, page, sizeof(first));
    if (std::memcmp(first, sentinel, sizeof(first)) == 0) {
        testFail("x64 getrandom left the output buffer unchanged");
    }
    memory->memsetGuest(page, 0x5A, sizeof(sentinel));
    if (invoke(page, sizeof(sentinel), 0) != (S64)sizeof(sentinel)) {
        testFail("x64 getrandom did not return the requested byte count");
    }
    U8 second[32];
    memory->memcpyFromGuest(second, page, sizeof(second));
    if (std::memcmp(first, second, sizeof(first)) == 0) {
        testFail("x64 getrandom repeated the same output across calls");
    }
    if (invoke(page, 8, 8) != -K_EINVAL) {
        testFail("x64 getrandom did not reject unknown flag bits");
    }
    if (invoke(0, 0, 0) != 0) {
        testFail("x64 getrandom did not accept a zero-length null buffer");
    }
    constexpr U64 readOnlyPage = page + K64_PAGE_SIZE;
    if (memory->mmapAnonymousFixed(readOnlyPage, K64_PAGE_SIZE, 1) != readOnlyPage) {
        testFail("x64 getrandom test could not map read-only page");
    } else if (invoke(readOnlyPage, 8, 0) != -K_EFAULT) {
        testFail("x64 getrandom did not reject a read-only output range");
    }
    if (invoke(0x7fff000000000000ULL, 8, 0) != -K_EFAULT) {
        testFail("x64 getrandom did not reject an unmapped output range");
    }
}

#endif
#endif
