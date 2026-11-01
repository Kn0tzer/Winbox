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

// Guest-reachable failure paths in KMemory must degrade with a report, not
// kill the emulator. A bad buffer handed to a kernel copy, a cross-page
// getRamPtr request, and an overflowing guest->guest memcpy all used to
// kpanic -- which, with recovery off (the default), exits the whole
// emulator. For the two formerly-fatal paths the process surviving the
// call IS the check; the callback/pointer assertions pin that no partial
// or bogus work happened on the way.

void testMemoryPerformOnMemoryBadBuffer32() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;

    // Sanity: a copy over mapped memory still delivers its chunk.
    const U32 mapped = TEST_HEAP_ADDRESS + 0x1000;
    bool delivered = false;
    memory->performOnMemory(mapped, 4, false, [&delivered](U8* ram, U32 len) {
        delivered = delivered || (ram && len == 4);
        return true;
    });
    if (!delivered) {
        testFail("32-bit performOnMemory no longer delivers mapped memory");
        return;
    }

    // An address backed by an invalid page (never mapped) used to kpanic.
    const U32 unmapped = 0x60000000;
    if (memory->isPageMapped(unmapped >> K_PAGE_SHIFT)) {
        testFail("32-bit performOnMemory fixture page unexpectedly mapped");
        return;
    }
    bool called = false;
    memory->performOnMemory(unmapped, 4, false, [&called](U8*, U32) {
        called = true;
        return true;
    });
    if (called) {
        testFail("32-bit performOnMemory ran its callback for an unmapped buffer");
    }
}

void testMemoryGetRamPtrCrossPage32() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;

    const U32 mapped = TEST_HEAP_ADDRESS + 0x1000;
    if (!memory->getRamPtr(mapped, 4, true)) {
        testFail("32-bit getRamPtr lost its in-page path");
        return;
    }
    // A request straddling the page end used to kpanic; the 64-bit sibling
    // returns nullptr for the identical request (no contiguous host pointer
    // exists for a cross-page span).
    if (memory->getRamPtr(mapped + K_PAGE_SIZE - 4, 8, true)) {
        testFail("32-bit getRamPtr claimed a cross-page span is contiguous");
    }
}

void testMemoryMemcpyOverflow32() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;

    const U32 src = TEST_HEAP_ADDRESS + 0x1000;
    const U32 dst = TEST_HEAP_ADDRESS + 0x2000;
    memory->writed(src, 0x11223344);
    memory->memcpy(dst, src, 4);
    if (memory->readd(dst) != 0x11223344) {
        testFail("32-bit guest->guest memcpy broke for ordinary copies");
        return;
    }
    // dest + len wraps the 32-bit address space: used to kpanic. The copy
    // must simply not happen; surviving the call is the assertion.
    memory->memcpy(0xFFFFFFF0, src, 0x40);
}

void testMemoryMadviseLayer32() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;

    // Behaviour preservation at the KMemory layer: stateful advice is still
    // applied (DONTNEED discards private contents) ...
    const U32 address = TEST_HEAP_ADDRESS + 0x1000;
    memory->writed(address, 0xAABBCCDD);
    if (memory->madvise(address, K_PAGE_SIZE, 4) != 0 || memory->readd(address) != 0) {
        testFail("32-bit madvise layer DONTNEED stopped discarding");
        return;
    }
    // ... and advice the layer does not implement stays a no-op success
    // (the syscall layer is what rejects values outside the known set,
    // before anything is delegated here).
    if (memory->madvise(address, K_PAGE_SIZE, 7) != 0) {
        testFail("32-bit madvise layer unimplemented advice stopped being a no-op");
    }
}

#endif
#endif
