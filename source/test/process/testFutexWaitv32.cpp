/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include <cstring>

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "../cpu/testCPU.h"

// FD-23 residue: futex_waitv (449) on i386. The number sits beyond the
// dispatch table's old end (439), so on the base tree every call below
// answers -ENOSYS from the bounds check. The contract pinned here is the
// host-measured one: per-call flags must be 0; nr in [1,128]; only U32
// entries (size field FUTEX_32, val fitting 32 bits, reserved 0, aligned
// uaddr); clockid CLOCK_REALTIME/CLOCK_MONOTONIC with an absolute
// timeout; unmapped array or word answers -EFAULT; a value mismatch
// answers -EAGAIN; an expired deadline answers -ETIMEDOUT; a wake that
// raced ahead of the wait is consumed and answers with the woken index.
void testFutexWaitv32Syscall() {
    TestContext& context = testContext();
    constexpr U32 words = TEST_HEAP_ADDRESS + 0x1000;
    constexpr U32 entries = TEST_HEAP_ADDRESS + 0x1100;
    constexpr U32 tsAddr = TEST_HEAP_ADDRESS + 0x1200;
    constexpr U32 F32P = 0x02 | 0x80; // FUTEX_32 | FUTEX_PRIVATE_FLAG

    U32 wordValues[3] = {10, 20, 30};
    U8 entryBytes[3 * 24];
    auto stageEntries = [&]() {
        context.memory->memcpy(words, wordValues, sizeof(wordValues));
        context.memory->memcpy(entries, entryBytes, sizeof(entryBytes));
    };
    auto setEntry = [&](U32 i, U64 val, U64 addr, U32 flags, U32 reserved = 0) {
        std::memcpy(entryBytes + i * 24, &val, 8);
        std::memcpy(entryBytes + i * 24 + 8, &addr, 8);
        std::memcpy(entryBytes + i * 24 + 16, &flags, 4);
        std::memcpy(entryBytes + i * 24 + 20, &reserved, 4);
    };
    auto goodEntries = [&]() {
        setEntry(0, 10, words, F32P);
        setEntry(1, 20, words + 4, F32P);
        setEntry(2, 30, words + 8, F32P);
    };
    U8 tsBytes[8] = {0};
    bool stageTs = false;
    auto invoke = [&](U32 nr_sys, U32 a1, U32 a2, U32 a3, U32 a4, U32 a5) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(nr_sys); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80);    // int 0x80
        stageEntries();
        if (stageTs) {
            context.memory->memcpy(tsAddr, tsBytes, sizeof(tsBytes));
        }
        context.cpu->reg[3].u32 = a1; // EBX
        context.cpu->reg[1].u32 = a2; // ECX
        context.cpu->reg[2].u32 = a3; // EDX
        context.cpu->reg[6].u32 = a4; // ESI
        context.cpu->reg[7].u32 = a5; // EDI
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto waitv = [&](U32 arr, U32 nr, U32 flags, U32 ts, U32 clockid) -> S32 {
        return invoke(449, arr, nr, flags, ts, clockid);
    };
    auto setTs = [&](U32 sec, U32 nsec) {
        std::memcpy(tsBytes, &sec, 4);
        std::memcpy(tsBytes + 4, &nsec, 4);
        stageTs = true;
    };

    goodEntries();
    if (waitv(entries, 0, 0, 0, 1) != -K_EINVAL ||
        waitv(entries, 129, 0, 0, 1) != -K_EINVAL ||
        waitv(entries, 3, 1, 0, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not validate nr / flags");
    }
    if (waitv(0x70000000u, 3, 0, 0, 1) != -K_EFAULT) {
        testFail("32-bit futex_waitv did not answer -EFAULT for an unmapped waiters array");
    }
    setEntry(1, 20, words + 4, F32P, 1);
    if (waitv(entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse a nonzero reserved field");
    }
    goodEntries();
    setEntry(1, 20, words + 4, 0x03 | 0x80); // size U64
    if (waitv(entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse a non-U32 entry");
    }
    setEntry(1, 20, words + 4, F32P | 0x40); // undefined flag bit
    if (waitv(entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse an undefined entry flag bit");
    }
    goodEntries();
    setEntry(0, 0x100000000ULL, words, F32P); // val does not fit in 32 bits
    if (waitv(entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse a val wider than the entry size");
    }
    goodEntries();
    setEntry(0, 10, words + 2, F32P); // misaligned uaddr
    if (waitv(entries, 3, 0, 0, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse a misaligned uaddr");
    }
    goodEntries();
    setEntry(0, 10, 0x70000000u, F32P); // unmapped futex word
    if (waitv(entries, 3, 0, 0, 1) != -K_EFAULT) {
        testFail("32-bit futex_waitv did not answer -EFAULT for an unmapped futex word");
    }
    goodEntries();
    if (waitv(entries, 3, 0, 0, 2) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse clockid 2");
    }
    setTs(0, 1000000000); // nsec out of range
    if (waitv(entries, 3, 0, tsAddr, 1) != -K_EINVAL) {
        testFail("32-bit futex_waitv did not refuse an out-of-range timespec");
    }
    // A mismatched value answers -EAGAIN (and proves the dispatch table
    // now reaches the handler at all: the base tree answers -ENOSYS).
    setEntry(1, 999, words + 4, F32P);
    if (waitv(entries, 3, 0, 0, 1) != -K_EAGAIN) {
        testFail("32-bit futex_waitv did not answer -EAGAIN for a mismatched value");
    }
    goodEntries();

    // Waits need a second thread in the process: a lone thread answers
    // -EWOULDBLOCK by design (no sibling could ever wake it).
    KThread* worker = context.process->createThread();
    // All values match and the deadline has already passed: -ETIMEDOUT.
    setTs(1, 0);
    if (waitv(entries, 3, 0, tsAddr, 1) != -K_ETIMEDOUT) {
        testFail("32-bit futex_waitv did not answer -ETIMEDOUT for a passed deadline");
    }
    // A real deadline: waits, then -ETIMEDOUT.
    U64 deadline = KSystem::getMilliesSinceStart() + 150;
    setTs((U32)(deadline / 1000), (U32)(deadline % 1000) * 1000000);
    U64 start = KSystem::getMilliesSinceStart();
    S32 rc = waitv(entries, 3, 0, tsAddr, 1);
    if (rc != -K_ETIMEDOUT || KSystem::getMilliesSinceStart() - start < 100) {
        testFail("32-bit futex_waitv did not wait out its deadline (rc=%d)", rc);
    }
    // A FUTEX_WAKE that finds no waiter is remembered; a waitv on that
    // word consumes it and answers with the woken entry's index (0).
    if (invoke(240, words, 1 | 128, 1, 0, 0) != 0) { // __NR_futex: FUTEX_WAKE | FUTEX_PRIVATE_FLAG, nr=1
        testFail("32-bit futex FUTEX_WAKE found an unexpected waiter");
    }
    if (waitv(entries, 1, 0, 0, 1) != 0) {
        testFail("32-bit futex_waitv did not consume the pending wake as index 0");
    }
    context.process->removeThread(worker);
    delete worker;
}

#endif
#endif
