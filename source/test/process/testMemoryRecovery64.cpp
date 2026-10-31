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

#include "kmemory64.h"
#include "../cpu/testCPU.h"

// Behaviour preservation at the KMemory64 madvise layer: the stateful
// advice is applied, and advice the layer does not implement stays a
// no-op success -- the syscall layer rejects values outside the known
// set before delegating, so the layer's default case is a reported
// no-op, never a false error.
void testMemoryMadviseLayer64() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 base = 0x77500000ULL;
    if (memory->mmapAnonymousFixed(base, K64_PAGE_SIZE, 3) != base) {
        testFail("x64 madvise layer test could not map its fixture page");
        return;
    }
    memory->writed(base, 0x11223344);
    if (memory->madvise(base, K64_PAGE_SIZE, 9) != 0 || memory->readd(base) != 0) {
        testFail("x64 madvise layer REMOVE stopped discarding contents");
        return;
    }
    if (memory->madvise(base, K64_PAGE_SIZE, 7) != 0) {
        testFail("x64 madvise layer unimplemented advice stopped being a no-op");
    }
}

#endif
#endif
