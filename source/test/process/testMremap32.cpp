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
#include "../../io/fsfilenode.h"

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

void testMremap32SharedFile() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    KMemory* memory = context.memory;
    constexpr U32 kMayMove = 1;
    constexpr U32 kFixed = 2;
    constexpr U32 kDontUnmap = 4;
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    U32 fd = process->memfd_create(B("bw-mremap32"), 0);
    if ((S32)fd < 0 || (S32)process->ftruncate64(fd, K_PAGE_SIZE * 2) < 0) {
        testFail("32-bit mremap shared test could not create a memfd");
        if ((S32)fd >= 0) process->close(fd);
        return;
    }
    constexpr U32 source = 0x34000000;
    constexpr U32 second = 0x34100000;
    constexpr U32 moved = 0x34300000;
    if (memory->mmap(thread, source, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED, fd, 0) != source ||
        memory->mmap(thread, second, K_PAGE_SIZE * 2, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED, fd, 0) != second) {
        testFail("32-bit mremap shared test could not map the memfd");
        process->close(fd);
        return;
    }
    memory->writed(source, 0x11223344);
    if (memory->readd(second) != 0x11223344) {
        testFail("32-bit shared memfd mappings do not alias before mremap");
        process->close(fd);
        return;
    }
    // old_size == 0 duplicates a shared mapping; the duplicate must alias
    // the same file pages, not copy them.
    const S32 duplicate = (S32)memory->mremap(thread, source, 0, K_PAGE_SIZE, kMayMove);
    if (duplicate < 0) {
        testFail("32-bit mremap old_size==0 did not duplicate the shared mapping: %d", duplicate);
    } else {
        if (memory->readd((U32)duplicate) != 0x11223344) {
            testFail("32-bit mremap duplicate lost the shared contents");
        }
        memory->writed((U32)duplicate, 0x55667788);
        if (memory->readd(second) != 0x55667788) {
            testFail("32-bit mremap duplicate does not alias the same file pages");
        }
        memory->unmap((U32)duplicate, K_PAGE_SIZE);
    }
    U32 duDestAddr = 0;
    // A move (here with growth) keeps the shared identity: writes through
    // the new address are visible through the independent mapping, and the
    // grown tail demand-loads the following file page.
    if ((S32)memory->mremap(thread, source, K_PAGE_SIZE, K_PAGE_SIZE * 2, kMayMove | kFixed, moved) != (S32)moved) {
        testFail("32-bit mremap shared move failed");
    } else {
        memory->writed(moved, 0x99AABBCC);
        if (memory->readd(second) != 0x99AABBCC) {
            testFail("32-bit mremap shared move lost the file identity");
        }
        memory->writed(moved + K_PAGE_SIZE, 0xDDEEFF00);
        if (memory->readd(second + K_PAGE_SIZE) != 0xDDEEFF00) {
            testFail("32-bit mremap shared grow tail did not reach the following file page");
        }
        // DONTUNMAP on a shared mapping: the destination keeps the contents,
        // the source stays mapped as zero pages, and the shared file pages
        // themselves are not zeroed.
        const S32 duDest = (S32)memory->mremap(thread, moved, K_PAGE_SIZE, K_PAGE_SIZE, kMayMove | kDontUnmap);
        if (duDest >= 0) duDestAddr = (U32)duDest;
        if (duDest < 0) {
            testFail("32-bit mremap DONTUNMAP on a shared mapping failed: %d", duDest);
        } else {
            if (memory->readd((U32)duDest) != 0x99AABBCC) {
                testFail("32-bit mremap DONTUNMAP destination lost the contents");
            }
            if (memory->readd(moved) != 0) {
                testFail("32-bit mremap DONTUNMAP source did not become zero pages");
            }
            if (memory->readd(second) != 0x99AABBCC) {
                testFail("32-bit mremap DONTUNMAP zeroed the shared file pages");
            }
        }
    }
    memory->unmap(second, K_PAGE_SIZE * 2);
    memory->unmap(source, K_PAGE_SIZE);
    memory->unmap(moved, K_PAGE_SIZE * 2);
    if (duDestAddr) memory->unmap(duDestAddr, K_PAGE_SIZE);
    process->close(fd);
}

#endif
#endif
