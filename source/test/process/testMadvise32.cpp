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
#ifndef BOXEDWINE_GUEST_X64

#include "../cpu/testCPU.h"

void testMadvise32Syscall() {
    TestContext& context = testContext();
    constexpr U32 address = TEST_HEAP_ADDRESS + 0x1000;
    context.memory->writed(address, 0xAABBCCDD);

    testNewInstruction(0);
    testPushCode8(0xB8); testPushCode32(219); // __NR_madvise
    testPushCode8(0xCD); testPushCode8(0x80);
    context.cpu->reg[3].u32 = address;
    context.cpu->reg[1].u32 = K_PAGE_SIZE;
    context.cpu->reg[2].u32 = 4; // MADV_DONTNEED
    testRunCPU();
    if ((S32)context.cpu->reg[0].u32 != 0 || context.memory->readd(address) != 0) {
        testFail("32-bit madvise DONTNEED syscall did not zero private anonymous data");
    }

    testNewInstruction(0);
    testPushCode8(0xB8); testPushCode32(219);
    testPushCode8(0xCD); testPushCode8(0x80);
    context.cpu->reg[3].u32 = address;
    context.cpu->reg[1].u32 = K_PAGE_SIZE;
    context.cpu->reg[2].u32 = 999;
    testRunCPU();
    if ((S32)context.cpu->reg[0].u32 != -K_EINVAL) {
        testFail("32-bit madvise syscall did not reject unknown advice");
    }
}

#endif
#endif
