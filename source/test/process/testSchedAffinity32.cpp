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

void testSchedAffinity32Syscalls() {
    TestContext& context = testContext();
    constexpr U32 maskAddress = TEST_HEAP_ADDRESS + 0x1800;
    auto invoke = [&](U32 number, U32 pid, U32 size, U32 mask, U32 inputMask = 1) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(number);
        testPushCode8(0xCD); testPushCode8(0x80);
        if (number == 241) context.memory->writed(mask, inputMask);
        context.cpu->reg[3].u32 = pid;
        context.cpu->reg[1].u32 = size;
        context.cpu->reg[2].u32 = mask;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    S32 setSmall = invoke(241, 0, 4, maskAddress, 3); // CPU 0 plus offline CPU 1.
    S32 setLarge = invoke(241, 0, 16, maskAddress, 1);
    if (setSmall != 0 || setLarge != 0) {
        testFail("32-bit sched_setaffinity did not accept an online CPU bit: %d / %d", setSmall, setLarge);
    }
    if (invoke(241, 0, 4, maskAddress, 2) != -K_EINVAL) {
        testFail("32-bit sched_setaffinity accepted a mask with no online CPUs");
    }
    if (invoke(242, 0, 4, maskAddress) != 4 || context.memory->readd(maskAddress) != 1) {
        testFail("32-bit sched_getaffinity did not return the kernel mask width and CPU 0");
    }
    if (invoke(241, 0, 3, maskAddress) != -K_EINVAL ||
        invoke(242, 0, 4, 0x71000000u) != -K_EFAULT ||
        invoke(242, 0x7fffffffu, 4, maskAddress) != -K_ESRCH) {
        testFail("32-bit affinity syscalls did not validate size, pointer, and PID");
    }
}

#endif
#endif
