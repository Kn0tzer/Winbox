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

void testMremap32Syscall() {
    TestContext& context = testContext();
    constexpr U32 source = 0x32000000;
    constexpr U32 fixedDestination = 0x32100000;
    constexpr U32 dontUnmapSource = 0x32200000;
    auto map = [&](U32 address, U32 length) {
        return context.memory->mmap(context.thread, address, length, K_PROT_READ | K_PROT_WRITE,
                                   K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED_NOREPLACE, -1, 0);
    };
    if (map(source, K_PAGE_SIZE * 2) != source || map(fixedDestination, K_PAGE_SIZE * 2) != fixedDestination ||
        map(dontUnmapSource, K_PAGE_SIZE) != dontUnmapSource) {
        testFail("32-bit mremap test could not map fixture ranges");
        return;
    }
    auto invoke = [&](U32 oldAddr, U32 oldLen, U32 newLen, U32 flags, U32 newAddr = 0) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(163); // __NR_mremap
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = oldAddr; // EBX
        context.cpu->reg[1].u32 = oldLen; // ECX
        context.cpu->reg[2].u32 = newLen; // EDX
        context.cpu->reg[6].u32 = flags; // ESI
        context.cpu->reg[7].u32 = newAddr; // EDI
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    context.memory->writed(source, 0x11223344);
    if ((U32)invoke(source, K_PAGE_SIZE, K_PAGE_SIZE * 2, 3, fixedDestination) != fixedDestination ||
        context.memory->readd(fixedDestination) != 0x11223344 ||
        context.memory->isPageMapped(source >> K_PAGE_SHIFT) || context.memory->readd(fixedDestination + K_PAGE_SIZE) != 0) {
        testFail("32-bit MREMAP_FIXED did not move contents and zero the grown tail");
    }
    context.memory->writed(dontUnmapSource, 0xAABBCCDD);
    S32 moved = invoke(dontUnmapSource, K_PAGE_SIZE, K_PAGE_SIZE, 5);
    if (moved < 0 || (U32)moved == dontUnmapSource || context.memory->readd((U32)moved) != 0xAABBCCDD ||
        !context.memory->isPageMapped(dontUnmapSource >> K_PAGE_SHIFT) || context.memory->readd(dontUnmapSource) != 0) {
        testFail("32-bit MREMAP_DONTUNMAP did not retain and zero the source mapping");
    }
    if (invoke(dontUnmapSource, K_PAGE_SIZE, K_PAGE_SIZE, 4) != -K_EINVAL ||
        invoke(dontUnmapSource, K_PAGE_SIZE, K_PAGE_SIZE, 2, fixedDestination) != -K_EINVAL) {
        testFail("32-bit mremap accepted invalid flags or missing MAYMOVE");
    }
}

#endif
#endif
