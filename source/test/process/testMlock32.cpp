/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "../cpu/testCPU.h"

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

void testMlock32Syscalls() {
    TestContext& context = testContext();
    constexpr U32 address = TEST_HEAP_ADDRESS + 0x2800;
    auto invoke = [&](U32 number, U32 arg1, U32 arg2) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(number);
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = arg1;
        context.cpu->reg[1].u32 = arg2;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    if (invoke(150, address, 16) != 0 || invoke(151, address, 16) != 0) {
        testFail("32-bit mlock/munlock rejected a mapped range");
    }
    if (invoke(150, 0x71000000u, 16) != -K_ENOMEM || invoke(151, 0x71000000u, 16) != -K_ENOMEM) {
        testFail("32-bit mlock/munlock did not reject an unmapped range");
    }
    if (invoke(152, 3, 0) != 0 || invoke(152, 7, 0) != 0 || invoke(153, 0, 0) != 0) {
        testFail("32-bit mlockall/munlockall rejected valid MCL flags");
    }
    if (invoke(152, 8, 0) != -K_EINVAL || invoke(152, 4, 0) != -K_EINVAL) {
        testFail("32-bit mlockall did not reject invalid MCL flags");
    }
}

#endif
#endif
