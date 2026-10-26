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

void testMremap64Syscall() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 source = 0x76000000ULL;
    constexpr U64 fixedDestination = 0x76100000ULL;
    constexpr U64 dontUnmapSource = 0x76200000ULL;
    constexpr U64 sharedSource = 0x76300000ULL;
    if (memory->mmapAnonymousFixed(source, K64_PAGE_SIZE * 2, 3) != source ||
        memory->mmapAnonymousFixed(fixedDestination, K64_PAGE_SIZE * 2, 3) != fixedDestination ||
        memory->mmapAnonymousFixed(dontUnmapSource, K64_PAGE_SIZE, 3) != dontUnmapSource ||
        memory->mmapSharedFile(sharedSource, K64_PAGE_SIZE, 3, 0x12345678, "/proc/test/mremap-shared",
                               0, nullptr, 0) != sharedSource) {
        testFail("mremap test could not map fixture ranges");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 oldAddr, U64 oldLen, U64 newLen, U64 flags, U64 newAddr = 0) -> S64 {
        cpu.reg[X64_RAX].setU64(25);
        cpu.reg[X64_RDI].setU64(oldAddr);
        cpu.reg[X64_RSI].setU64(oldLen);
        cpu.reg[X64_RDX].setU64(newLen);
        cpu.reg[X64_R10].setU64(flags);
        cpu.reg[X64_R8].setU64(newAddr);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    memory->writeq(source, 0x1122334455667788ULL);
    if (invoke(source, K64_PAGE_SIZE, K64_PAGE_SIZE * 2, 1 | 2, fixedDestination) != (S64)fixedDestination ||
        memory->readq(fixedDestination) != 0x1122334455667788ULL ||
        memory->getPageFlags(source >> K64_PAGE_SHIFT) & K64_PAGE_MAPPED ||
        memory->readq(fixedDestination + K64_PAGE_SIZE) != 0) {
        testFail("MREMAP_FIXED did not move contents, unmap source, or zero the grown tail");
    }

    memory->writeq(dontUnmapSource, 0xAABBCCDDEEFF0011ULL);
    S64 moved = invoke(dontUnmapSource, K64_PAGE_SIZE, K64_PAGE_SIZE, 1 | 4);
    if (moved < 0 || (U64)moved == dontUnmapSource ||
        memory->readq((U64)moved) != 0xAABBCCDDEEFF0011ULL ||
        !(memory->getPageFlags(dontUnmapSource >> K64_PAGE_SHIFT) & K64_PAGE_MAPPED) ||
        memory->readq(dontUnmapSource) != 0) {
        testFail("MREMAP_DONTUNMAP did not retain and zero the original mapping");
    }
    if (invoke(dontUnmapSource, K64_PAGE_SIZE, K64_PAGE_SIZE, 4) != -K_EINVAL ||
        invoke(dontUnmapSource, K64_PAGE_SIZE, K64_PAGE_SIZE, 1 | 2, 0) != -K_EINVAL) {
        testFail("mremap accepted invalid DONTUNMAP or FIXED arguments");
    }
    memory->writeq(sharedSource, 0x13579BDF2468ACE0ULL);
    S64 sharedDuplicate = invoke(sharedSource, 0, K64_PAGE_SIZE, 1);
    if (sharedDuplicate < 0 || memory->readq((U64)sharedDuplicate) != 0x13579BDF2468ACE0ULL) {
        testFail("mremap old_size zero did not duplicate a shared mapping");
    } else {
        memory->writeq((U64)sharedDuplicate, 0x0FEDCBA987654321ULL);
        if (memory->readq(sharedSource) != 0x0FEDCBA987654321ULL)
            testFail("mremap old_size-zero mapping did not preserve shared backing");
    }
}

#endif
#endif
