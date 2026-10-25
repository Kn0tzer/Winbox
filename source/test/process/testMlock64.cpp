/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License; either version 2
 *  of the License, or (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"
#include "syscall64.h"
#include "../cpu/testCPU.h"

// mlock family: locked pages are accounted per process against
// RLIMIT_MEMLOCK. The default limit is RLIM_INFINITY (what getrlimit has
// always reported here); a guest that lowers it gets Linux ENOMEM/EPERM
// behaviour, mlockall(MCL_FUTURE) makes new mappings count, and mlock2 is
// dispatched instead of falling into the ENOSYS path.
void testMlock64Syscalls() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 base = 0x77600000ULL;
    constexpr U64 scratch = 0x77700000ULL;
    if (memory->mmapAnonymousFixed(base, K64_PAGE_SIZE * 4, 3) != base ||
        memory->mmapAnonymousFixed(scratch, K64_PAGE_SIZE, 3) != scratch) {
        testFail("x64 mlock test could not map fixture pages");
        return;
    }
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3 = 0, U64 a4 = 0) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto invokeMmap = [&](U64 addr, U64 len) -> S64 {
        cpu.reg[X64_RAX].setU64(9); // __NR_mmap
        cpu.reg[X64_RDI].setU64(addr);
        cpu.reg[X64_RSI].setU64(len);
        cpu.reg[X64_RDX].setU64(3); // PROT_READ|PROT_WRITE
        cpu.reg[X64_R10].setU64(0x22); // MAP_PRIVATE|MAP_ANONYMOUS
        cpu.reg[X64_R8].setU64(~0ULL); // fd = -1
        cpu.reg[X64_R9].setU64(0);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    constexpr U64 RLIMIT_MEMLOCK = 8;
    constexpr U64 INFINITY64 = ~0ULL;
    auto getLimit = [&](U64& cur, U64& max) {
        memory->writeq(scratch, 0xDEAD);
        memory->writeq(scratch + 8, 0xBEEF);
        invoke(97, RLIMIT_MEMLOCK, scratch); // getrlimit
        cur = memory->readq(scratch);
        max = memory->readq(scratch + 8);
    };
    auto setLimit = [&](U64 cur, U64 max) -> S64 {
        memory->writeq(scratch, cur);
        memory->writeq(scratch + 8, max);
        return invoke(160, RLIMIT_MEMLOCK, scratch); // setrlimit
    };
    // Baseline: the limit guests have always observed here is infinity, and
    // plain lock/unlock of mapped ranges works.
    U64 cur = 0, max = 0;
    getLimit(cur, max);
    if (cur != INFINITY64 || max != INFINITY64) {
        testFail("x64 RLIMIT_MEMLOCK default changed: cur=%llu max=%llu",
                 (unsigned long long)cur, (unsigned long long)max);
        return;
    }
    if (invoke(149, base, K64_PAGE_SIZE * 2) != 0 || invoke(149, base, K64_PAGE_SIZE * 2) != 0 ||
        invoke(150, base, K64_PAGE_SIZE * 2) != 0) {
        testFail("x64 mlock/munlock rejected or failed to repeat a mapped range");
        return;
    }
    if (invoke(149, 0x710000000ULL, K64_PAGE_SIZE) != -K_ENOMEM) {
        testFail("x64 mlock did not reject an unmapped range");
        return;
    }
    // Lower only the soft limit to two pages: two pages lock, a third fails,
    // unlocking makes room again.
    if (setLimit(K64_PAGE_SIZE * 2, INFINITY64) != 0) {
        testFail("x64 setrlimit(RLIMIT_MEMLOCK) rejected a valid soft limit");
        return;
    }
    if (invoke(149, base, K64_PAGE_SIZE * 2) != 0 ||
        invoke(149, base + K64_PAGE_SIZE * 2, K64_PAGE_SIZE) != -K_ENOMEM) {
        testFail("x64 mlock did not enforce RLIMIT_MEMLOCK accounting");
        return;
    }
    if (invoke(150, base, K64_PAGE_SIZE) != 0 ||
        invoke(149, base + K64_PAGE_SIZE * 2, K64_PAGE_SIZE) != 0) {
        testFail("x64 munlock did not release RLIMIT_MEMLOCK accounting room");
        return;
    }
    if (invoke(150, base, K64_PAGE_SIZE * 3) != 0) {
        testFail("x64 munlock of the locked fixture failed");
        return;
    }
    // MCL_FUTURE: new mappings are locked on creation and charged.
    if (invoke(151, 2, 0) != 0) { // mlockall(MCL_FUTURE)
        testFail("x64 mlockall(MCL_FUTURE) failed");
        return;
    }
    constexpr U64 futureBase = 0x77800000ULL;
    if (invokeMmap(futureBase, K64_PAGE_SIZE * 2) != (S64)futureBase ||
        invokeMmap(futureBase + K64_PAGE_SIZE * 4, K64_PAGE_SIZE) != -K_ENOMEM) {
        testFail("x64 mmap under MCL_FUTURE did not lock and charge new mappings");
        return;
    }
    if (invoke(152, 0, 0) != 0) { // munlockall
        testFail("x64 munlockall failed");
        return;
    }
    if (invokeMmap(futureBase + K64_PAGE_SIZE * 4, K64_PAGE_SIZE) != (S64)(futureBase + K64_PAGE_SIZE * 4)) {
        testFail("x64 mmap still charged after munlockall cleared MCL_FUTURE");
        return;
    }
    memory->munmap(futureBase, K64_PAGE_SIZE * 8);
    // mlockall(MCL_CURRENT) cannot fit this process in two pages, and the
    // failure must not leave a partial lock behind.
    if (invoke(151, 1, 0) != -K_ENOMEM || invoke(149, base, K64_PAGE_SIZE * 2) != 0) {
        testFail("x64 mlockall(MCL_CURRENT) over the limit did not fail atomically");
        return;
    }
    if (invoke(150, base, K64_PAGE_SIZE * 2) != 0) {
        testFail("x64 munlock after mlockall check failed");
        return;
    }
    // mlock2: MLOCK_ONFAULT locks (pages are always resident here), other
    // flag bits are invalid.
    if (invoke(325, base, K64_PAGE_SIZE, 1) != 0 || invoke(325, base, K64_PAGE_SIZE, 2) != -K_EINVAL ||
        invoke(150, base, K64_PAGE_SIZE) != 0) {
        testFail("x64 mlock2 did not implement ONFAULT/flag validation");
        return;
    }
    // prlimit64 observes and changes the same stored limit.
    memory->writeq(scratch, K64_PAGE_SIZE);
    memory->writeq(scratch + 8, INFINITY64);
    if (invoke(302, 0, RLIMIT_MEMLOCK, scratch, 0) != 0) { // prlimit64(pid, res, new, old=NULL in r10)
        testFail("x64 prlimit64 could not set RLIMIT_MEMLOCK");
        return;
    }
    getLimit(cur, max);
    if (cur != K64_PAGE_SIZE || max != INFINITY64) {
        testFail("x64 prlimit64/getrlimit disagree on RLIMIT_MEMLOCK: cur=%llu max=%llu",
                 (unsigned long long)cur, (unsigned long long)max);
        return;
    }
    // Restore an infinite soft limit (allowed: it does not exceed the hard
    // limit), then verify the unprivileged hard-limit rules last, since the
    // hard limit cannot be raised again afterwards.
    if (setLimit(INFINITY64, INFINITY64) != 0) {
        testFail("x64 setrlimit could not restore RLIMIT_MEMLOCK");
        return;
    }
    if (setLimit(K64_PAGE_SIZE, K64_PAGE_SIZE * 2) != 0 ||
        setLimit(K64_PAGE_SIZE, K64_PAGE_SIZE * 4) != -K_EPERM ||
        setLimit(K64_PAGE_SIZE * 4, K64_PAGE_SIZE * 2) != -K_EINVAL) {
        testFail("x64 setrlimit(RLIMIT_MEMLOCK) did not enforce hard-limit rules");
        return;
    }
    getLimit(cur, max);
    if (cur != K64_PAGE_SIZE || max != K64_PAGE_SIZE * 2) {
        testFail("x64 RLIMIT_MEMLOCK was mutated by rejected setrlimit calls: cur=%llu max=%llu",
                 (unsigned long long)cur, (unsigned long long)max);
    }
}

#endif
#endif
