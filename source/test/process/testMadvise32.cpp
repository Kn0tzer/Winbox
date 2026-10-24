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

    auto invokeMlock = [&](U32 address, U32 length) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(150); // __NR_mlock
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = address;
        context.cpu->reg[1].u32 = length;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    if (invokeMlock(address, K_PAGE_SIZE) != 0 || invokeMlock(address, 0) != -K_EINVAL ||
        invokeMlock(0xFFFFFFF0u, 0x40u) != -K_EINVAL) {
        testFail("32-bit mlock syscall did not validate mapped, empty, and overflowing ranges");
    }
    const U32 unmapped = 0x60000000u;
    if (!context.memory->isPageMapped(unmapped >> K_PAGE_SHIFT) &&
        invokeMlock(unmapped, K_PAGE_SIZE) != -K_ENOMEM) {
        testFail("32-bit mlock syscall did not reject an unmapped range");
    }

    auto invokeMsync = [&](U32 syncAddress, U32 length, U32 flags) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(144); // __NR_msync
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = syncAddress;
        context.cpu->reg[1].u32 = length;
        context.cpu->reg[2].u32 = flags;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    if (invokeMsync(address, K_PAGE_SIZE, 4) != 0 ||
        invokeMsync(address + 1, K_PAGE_SIZE, 4) != -K_EINVAL ||
        invokeMsync(address, K_PAGE_SIZE, 0) != -K_EINVAL ||
        invokeMsync(address, K_PAGE_SIZE, 5) != -K_EINVAL ||
        invokeMsync(unmapped, K_PAGE_SIZE, 4) != -K_ENOMEM) {
        testFail("32-bit msync syscall did not validate mapping and sync flags");
    }
}

#endif
#endif
