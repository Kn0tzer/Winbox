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

// Exercises the prctl(2) surface that the x64 guest can observe. The virtual
// CPU has no host page pool, so the memory-policy options are modelled as
// round-tripping state (which is what an application can actually detect),
// while options that need unavailable hardware return the documented
// EINVAL/EPERM and log once. MDWE is enforced for real on mmap/mprotect.
void testPrctl64Options() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x74000000ULL;
    constexpr U64 scratch = page + 0x200;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("prctl test could not map x64 scratch page");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    struct {
        CPU64* cpu;
        S64 operator()(U64 option, U64 arg2 = 0, U64 arg3 = 0) const {
            cpu->reg[X64_RAX].setU64(157);
            cpu->reg[X64_RDI].setU64(option);
            cpu->reg[X64_RSI].setU64(arg2);
            cpu->reg[X64_RDX].setU64(arg3);
            cpu->reg[X64_R10].setU64(0);
            cpu->reg[X64_R8].setU64(0);
            cpu->reg[X64_R9].setU64(0);
            ksyscall64(cpu);
            return (S64)cpu->reg[X64_RAX].u64;
        }
    } prctl{&cpu};

    if (prctl(3 /*GET_DUMPABLE*/) != 1) testFail("prctl GET_DUMPABLE did not default to 1");
    if (prctl(4 /*SET_DUMPABLE*/, 0) != 0 || prctl(3, 0) != 0) testFail("prctl SET_DUMPABLE did not round-trip");
    if (prctl(4, 2) != -K_EINVAL) testFail("prctl SET_DUMPABLE accepted an invalid value");

    memory->memcpyToGuest(scratch, "unit-test", 10);
    if (prctl(15 /*SET_NAME*/, scratch) != 0) testFail("prctl SET_NAME failed");
    memory->memsetGuest(scratch, 0, 16);
    if (prctl(16 /*GET_NAME*/, scratch) != 0) testFail("prctl GET_NAME failed");
    char readName[16] = {};
    memory->memcpyFromGuest(readName, scratch, 16);
    if (strcmp(readName, "unit-test") != 0) {
        testFail("prctl GET_NAME did not echo SET_NAME");
    }

    if (prctl(29 /*SET_TIMERSLACK*/, 123456) != 0 || prctl(30 /*GET_TIMERSLACK*/) != 123456) {
        testFail("prctl timer slack did not round-trip");
    }
    if (prctl(29, 0) != 0 || prctl(30, 0) != 50000) testFail("prctl timer slack zero did not restore the 50us default");

    if (prctl(38 /*SET_NO_NEW_PRIVS*/, 1) != 0 || prctl(39, 0) != 1) testFail("prctl NO_NEW_PRIVS did not round-trip");
    if (prctl(38, 2) != -K_EINVAL) testFail("prctl NO_NEW_PRIVS accepted an invalid value");

    if (prctl(1 /*SET_PDEATHSIG*/, 9) != -K_EINVAL) testFail("prctl SET_PDEATHSIG accepted SIGKILL");
    if (prctl(1, 11) != 0 || prctl(2 /*GET_PDEATHSIG*/, scratch) != 0 ||
        memory->readd(scratch) != 11) {
        testFail("prctl parent death signal did not round-trip");
    }
    if (prctl(19 /*GET_ENDIAN*/, scratch) != 0 || memory->readd(scratch) != 1) {
        testFail("prctl GET_ENDIAN did not report little-endian");
    }
    if (prctl(22 /*SET_SECCOMP*/, 1, 0) != -K_EINVAL) testFail("prctl SET_SECCOMP was not loudly rejected");
    if (prctl(0x12345678, 0) != -K_EINVAL) testFail("prctl accepted an unknown option");

    if (prctl(45 /*SET_FP_MODE*/, 0) != 0 || prctl(46 /*GET_FP_MODE*/) != 0) {
        testFail("prctl FP mode did not round-trip PR_FP_MODE_NONE");
    }
    if (prctl(45, 1) != -K_EINVAL) testFail("prctl accepted an unsupported FP mode");

    if (prctl(53 /*SET_SPECULATION_CTRL*/, 1, 0x4 | 0x8) != 0 ||
        prctl(52 /*GET_SPECULATION_CTRL*/, 1) != (S64)(0x4 | 0x8)) {
        testFail("prctl speculation control did not round-trip");
    }
    if (prctl(53, 3, 0) != -K_EINVAL) testFail("prctl speculation control accepted an invalid class");

    memory->memsetGuest(scratch, 0xCC, 8 * 16);
    S64 auxv = prctl(0x41555856 /*PR_GET_AUXV*/, scratch, 8 * 16);
    if (auxv <= 0) {
        testFail("prctl PR_GET_AUXV failed");
    } else {
        bool sawPageSize = false;
        for (S64 i = 0; i + 16 <= auxv; i += 16) {
            if (memory->readq(scratch + i) == 6 /*AT_PAGESZ*/ && memory->readq(scratch + i + 8) == 4096) {
                sawPageSize = true;
            }
        }
        if (!sawPageSize) testFail("prctl PR_GET_AUXV did not report AT_PAGESZ");
    }

    // PR_GET_TID_ADDRESS must expose the thread's clear_child_tid pointer.
    cpu.thread->clear_child_tid64 = 0x12345000ULL;
    if (prctl(40 /*PR_GET_TID_ADDRESS*/, scratch) != 0 || memory->readq(scratch) != 0x12345000ULL) {
        testFail("prctl PR_GET_TID_ADDRESS did not report the thread tid address");
    }

    // MDWE is enforced, so these cases run last: once REFUSE_EXEC_GAIN is set it
    // cannot be cleared.
    if (prctl(65 /*SET_MDWE*/, 3) != 0 || prctl(66 /*GET_MDWE*/) != 3) {
        testFail("prctl MDWE did not round-trip");
    }
    if (prctl(65, 1) != -K_EPERM) testFail("prctl MDWE allowed clearing REFUSE_EXEC_GAIN");
    if (prctl(65, 4) != -K_EINVAL) testFail("prctl MDWE accepted unknown flags");
    cpu.reg[X64_RAX].setU64(9 /*mmap*/);
    cpu.reg[X64_RDI].setU64(0x76000000ULL);
    cpu.reg[X64_RSI].setU64(K64_PAGE_SIZE);
    cpu.reg[X64_RDX].setU64(7);
    cpu.reg[X64_R10].setU64(0x22 /*MAP_PRIVATE|MAP_ANONYMOUS*/);
    cpu.reg[X64_R8].setU64((U64)-1);
    cpu.reg[X64_R9].setU64(0);
    ksyscall64(&cpu);
    if ((S64)cpu.reg[X64_RAX].u64 != -K_EACCES) testFail("prctl MDWE did not reject mmap PROT_WRITE|PROT_EXEC");
    if (prctl(67 /*SET_MEMORY_MERGE*/, 1) != -K_EINVAL) testFail("prctl SET_MEMORY_MERGE was not loudly rejected");
    if (prctl(68 /*GET_MEMORY_MERGE*/) != 0) testFail("prctl GET_MEMORY_MERGE did not report disabled");
}

#endif
#endif
