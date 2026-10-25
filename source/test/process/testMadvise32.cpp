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
    auto invoke3 = [&](U32 nr, U32 arg1, U32 arg2, U32 arg3) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr);
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = arg1;
        context.cpu->reg[1].u32 = arg2;
        context.cpu->reg[2].u32 = arg3;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    // MADV_REMOVE discards contents (the mlock case above left this page
    // locked; unlock it first, which succeeds either way).
    invoke3(151, address, K_PAGE_SIZE, 0); // munlock
    context.memory->writed(address, 0x5A5A5A5A);
    if (invoke3(219, address, K_PAGE_SIZE, 9) != 0 || context.memory->readd(address) != 0) {
        testFail("32-bit madvise REMOVE did not discard page contents");
    }
    // A locked page refuses DONTNEED; DONTNEED_LOCKED discards and unlocks.
    context.memory->writed(address, 0xA5A5A5A5);
    if (invokeMlock(address, K_PAGE_SIZE) != 0) {
        testFail("32-bit madvise test could not lock the heap page");
    } else {
        if (invoke3(219, address, K_PAGE_SIZE, 4) != -K_EINVAL) {
            testFail("32-bit madvise DONTNEED did not refuse a locked page");
        }
        if (invoke3(219, address, K_PAGE_SIZE, 24) != 0 || context.memory->readd(address) != 0) {
            testFail("32-bit madvise DONTNEED_LOCKED did not discard the locked page");
        }
    }
    // WIPEONFORK is invalid on a shared file mapping.
    if (!context.process->fdNode) {
        context.process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }
    U32 memfd = context.process->memfd_create(B("bw-madvise32"), 0);
    if ((S32)memfd < 0 || (S32)context.process->ftruncate64(memfd, K_PAGE_SIZE) < 0) {
        testFail("32-bit madvise test could not create a memfd");
    } else {
        constexpr U32 sharedAddress = 0x34800000;
        if (context.memory->mmap(context.thread, sharedAddress, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE,
                                 K_MAP_SHARED, memfd, 0) != sharedAddress) {
            testFail("32-bit madvise test could not map the memfd");
        } else if (invoke3(219, sharedAddress, K_PAGE_SIZE, 18) != -K_EINVAL) {
            testFail("32-bit madvise WIPEONFORK did not reject a shared mapping");
        }
        context.memory->unmap(sharedAddress, K_PAGE_SIZE);
        context.process->close(memfd);
    }
}

void testMadvise32ForkInheritance() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    constexpr U32 base = 0x33800000;
    if (memory->mmap(context.thread, base, K_PAGE_SIZE * 3, K_PROT_READ | K_PROT_WRITE,
                     K_MAP_PRIVATE | K_MAP_ANONYMOUS, -1, 0) != base) {
        testFail("32-bit madvise fork test could not map fixture pages");
        return;
    }
    memory->writed(base, 0xAAAA0001);
    memory->writed(base + K_PAGE_SIZE, 0xBBBB0002);
    memory->writed(base + K_PAGE_SIZE * 2, 0xCCCC0003);
    auto invokeMadvise = [&](U32 address, U32 advice) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(219); // __NR_madvise
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = address;
        context.cpu->reg[1].u32 = K_PAGE_SIZE;
        context.cpu->reg[2].u32 = advice;
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    if (invokeMadvise(base + K_PAGE_SIZE, 10) != 0 || // MADV_DONTFORK
        invokeMadvise(base + K_PAGE_SIZE * 2, 18) != 0) { // MADV_WIPEONFORK
        testFail("32-bit madvise fork test could not set fork advice");
        return;
    }
    KProcessPtr child = KProcess::create();
    if (!child->memory) child->memory = KMemory::create(child.get());
    child->memory->clone(memory, false);
    if (child->memory->readd(base) != 0xAAAA0001) {
        testFail("32-bit fork did not copy a normal page (madvise fixture)");
    }
    if (child->memory->isPageMapped((base + K_PAGE_SIZE) >> K_PAGE_SHIFT)) {
        testFail("32-bit fork inherited a MADV_DONTFORK page");
    }
    if (!child->memory->isPageMapped((base + K_PAGE_SIZE * 2) >> K_PAGE_SHIFT) ||
        child->memory->readd(base + K_PAGE_SIZE * 2) != 0) {
        testFail("32-bit fork did not wipe a MADV_WIPEONFORK page");
    }
    if (memory->readd(base + K_PAGE_SIZE) != 0xBBBB0002 ||
        memory->readd(base + K_PAGE_SIZE * 2) != 0xCCCC0003) {
        testFail("32-bit fork changed the parent madvise fixture pages");
    }
    memory->unmap(base, K_PAGE_SIZE * 3);
}

#endif
#endif
