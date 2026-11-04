/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "boxedwine.h"
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

#ifdef __TEST

#include "kerror.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

// FD-137 (host-measured, test-results/fd136-137/hostref-fd137.c): a
// null memfd_create name faults on the host (-EFAULT), while the
// empty string is a legal name that creates the file. Both
// dispatchers erased the null before the process layer could see
// it: the 64-bit case skipped its name copy for a null pointer and
// passed the zeroed buffer (""), and the 32-bit helper's
// KMemory::readString returns "" for a null address by construction.
// The host also validates the flags word before it faults the name
// copy, so a null name with invalid flags answers -EINVAL, and
// these tests pin that order too. FD-138 (same host measurement,
// re-verified in test-results/fd136-138/hostref-fd136b.c): the
// host refuses names longer than 249 chars with -EINVAL while a
// name at the limit still creates; the boundary rows are pinned
// below alongside the null rows.

#ifndef BOXEDWINE_GUEST_X64

void testMemfdNullName32Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    // Same staging as testMemfdCreate: descriptor allocation
    // publishes each fd under /proc/<pid>/fd, and the CPU test
    // harness does not initialize the /proc tree.
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }

    const U32 emptyAt = TEST_HEAP_ADDRESS + 0x400;
    const U32 labelAt = TEST_HEAP_ADDRESS + 0x480;
    const U32 longAt = TEST_HEAP_ADDRESS + 0x500;
    const U32 name249At = TEST_HEAP_ADDRESS + 0x700;
    const U32 name250At = TEST_HEAP_ADDRESS + 0x800;
    // testNewInstruction() resets the whole test heap, so the staged
    // strings are rewritten inside invoke() after the code emission.
    auto stageStrings = [&]() {
        context.memory->memcpy(emptyAt, "", 1);
        context.memory->memcpy(labelAt, "label", 6);
        char longName[320];
        memset(longName, 'm', 300);
        longName[300] = 0;
        context.memory->memcpy(longAt, longName, 301);
        char name249[250];
        memset(name249, 'm', 249);
        name249[249] = 0;
        context.memory->memcpy(name249At, name249, 250);
        char name250[251];
        memset(name250, 'm', 250);
        name250[250] = 0;
        context.memory->memcpy(name250At, name250, 251);
    };
    auto invoke = [&](U32 a1, U32 a2) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(356); // mov eax, __NR_memfd_create
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        stageStrings();
        context.cpu->reg[3].u32 = a1; // ebx: name
        context.cpu->reg[1].u32 = a2; // ecx: flags
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto expectEq = [&](const char* what, S32 got, S32 want) {
        if (got != want) {
            testFail("%s returned %d (expected %d)", what, got, want);
        }
    };
    auto expectFd = [&](const char* what, S32 got) {
        if (got < 0) {
            testFail("%s returned %d (expected a descriptor)", what, got);
        } else {
            process->close((U32)got);
        }
    };

    // Controls: shapes already at the host's answer on the base.
    expectFd("memfd_create empty name creates", invoke(emptyAt, 0));
    expectFd("memfd_create label creates", invoke(labelAt, 0));
    expectFd("memfd_create label CLOEXEC creates", invoke(labelAt, 1));
    expectEq("memfd_create label bad flags", invoke(labelAt, 0x80000000), -K_EINVAL);
    expectEq("memfd_create null name bad flags (flags verdict first)", invoke(0, 0x80000000), -K_EINVAL);

    // The filed shapes: a null name faults; it is not an empty name.
    expectEq("memfd_create null name", invoke(0, 0), -K_EFAULT);
    expectEq("memfd_create null name CLOEXEC", invoke(0, 1), -K_EFAULT);
    expectEq("memfd_create null name ALLOW_SEALING", invoke(0, 2), -K_EFAULT);

    // FD-138: the host's 249-char name limit. A name at the limit
    // still creates; past it the answer is -EINVAL.
    expectFd("memfd_create 249-char name creates", invoke(name249At, 0));
    expectEq("memfd_create 250-char name", invoke(name250At, 0), -K_EINVAL);
    expectEq("memfd_create 300-char name", invoke(longAt, 0), -K_EINVAL);
}

#endif

#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"

S32 ksyscall64(CPU64* cpu);

void testMemfdNullName64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    // Same staging as testMemfdCreate: descriptor allocation
    // publishes each fd under /proc/<pid>/fd, and the CPU test
    // harness does not initialize the /proc tree.
    if (!process->fdNode) {
        process->fdNode = std::make_shared<FsFileNode>(0x3FFFFFFF, 0, B("/proc/test/fd"), B(""), B(""), true, false, nullptr);
    }

    constexpr U64 dataPage = 0x7E000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, K64_PAGE_SIZE, 3) != dataPage) {
        testFail("FD-137 guest test could not map its fixture page");
        return;
    }
    const U64 emptyAt = dataPage;
    const U64 labelAt = dataPage + 0x100;
    const U64 longAt = dataPage + 0x200;
    const U64 name249At = dataPage + 0x400;
    const U64 name250At = dataPage + 0x500;
    memory->memcpyToGuest(emptyAt, "", 1);
    memory->memcpyToGuest(labelAt, "label", 6);
    char longName[320];
    memset(longName, 'm', 300);
    longName[300] = 0;
    memory->memcpyToGuest(longAt, longName, 301);
    char name249[250];
    memset(name249, 'm', 249);
    name249[249] = 0;
    memory->memcpyToGuest(name249At, name249, 250);
    char name250[251];
    memset(name250, 'm', 250);
    name250[250] = 0;
    memory->memcpyToGuest(name250At, name250, 251);

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 a1, U64 a2) -> S64 {
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RAX].setU64(319); // __NR_memfd_create
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto expectEq = [&](const char* what, S64 got, S64 want) {
        if (got != want) {
            testFail("%s returned %lld (expected %lld)", what, (long long)got, (long long)want);
        }
    };
    auto expectFd = [&](const char* what, S64 got) {
        if (got < 0) {
            testFail("%s returned %lld (expected a descriptor)", what, (long long)got);
        } else {
            process->close((U32)got);
        }
    };

    // Controls: shapes already at the host's answer on the base.
    expectFd("memfd_create empty name creates", invoke(emptyAt, 0));
    expectFd("memfd_create label creates", invoke(labelAt, 0));
    expectFd("memfd_create label CLOEXEC creates", invoke(labelAt, 1));
    expectEq("memfd_create label bad flags", invoke(labelAt, 0x80000000), -K_EINVAL);
    expectEq("memfd_create null name bad flags (flags verdict first)", invoke(0, 0x80000000), -K_EINVAL);

    // The filed shapes: a null name faults; it is not an empty name.
    expectEq("memfd_create null name", invoke(0, 0), -K_EFAULT);
    expectEq("memfd_create null name CLOEXEC", invoke(0, 1), -K_EFAULT);
    expectEq("memfd_create null name ALLOW_SEALING", invoke(0, 2), -K_EFAULT);

    // FD-138: the host's 249-char name limit. A name at the limit
    // still creates; past it the answer is -EINVAL.
    expectFd("memfd_create 249-char name creates", invoke(name249At, 0));
    expectEq("memfd_create 250-char name", invoke(name250At, 0), -K_EINVAL);
    expectEq("memfd_create 300-char name", invoke(longAt, 0), -K_EINVAL);
}

#endif

#endif
