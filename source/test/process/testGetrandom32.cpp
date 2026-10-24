/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include <cstring>

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "../cpu/testCPU.h"

void testGetrandom32Syscall() {
    TestContext& context = testContext();
    constexpr U32 address = TEST_HEAP_ADDRESS + 0x1800;

    auto invoke = [&](U32 buffer, U32 length, U32 flags) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(355); // __NR_getrandom
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = buffer;
        context.cpu->reg[1].u32 = length;
        context.cpu->reg[2].u32 = flags;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };

    U8 before[16];
    std::memset(before, 0xA5, sizeof(before));
    context.memory->memcpy(address, before, sizeof(before));
    if (invoke(address, sizeof(before), 4) != (S32)sizeof(before)) {
        testFail("32-bit getrandom did not fill a valid buffer");
    }
    U8 first[16];
    context.memory->memcpy(first, address, sizeof(first));
    if (std::memcmp(first, before, sizeof(first)) == 0) {
        testFail("32-bit getrandom left the output buffer unchanged");
    }
    if (invoke(address, 16, 8) != -K_EINVAL) {
        testFail("32-bit getrandom did not reject unsupported flags outside recovery mode");
    }
    if (invoke(0x70000000u, 8, 0) != -K_EFAULT) {
        testFail("32-bit getrandom did not reject an unmapped output buffer");
    }
    if (invoke(0, 0, 0) != 0) {
        testFail("32-bit getrandom did not accept a zero-length null buffer");
    }
    if (invoke(address, 32u * 1024u * 1024u + 1u, 0) != -K_EINVAL) {
        testFail("32-bit getrandom did not enforce the Linux request-size limit");
    }
}

#endif
#endif
