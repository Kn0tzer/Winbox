/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

/*
 * Frontier review item D2: futex operations on an unmapped address.
 *
 * Host contract, measured with raw futex syscalls (never the libc
 * wrapper, whose answer would not be the kernel's) on two real Linux
 * kernels (x86-64 and aarch64; the full matrix is preserved with the
 * lane evidence):
 *   WAIT / WAIT_BITSET on an unmapped word       -> -EFAULT (both flavours)
 *   WAKE / WAKE_BITSET shared on unmapped word   -> -EFAULT
 *   WAKE / WAKE_BITSET private on unmapped word  -> 0
 *   WAIT_BITSET / WAKE_BITSET with val3 == 0     -> -EINVAL (checked first)
 *   WAIT with an unmapped timeout pointer        -> -EFAULT
 *
 * Before the fix the 32-bit shared path panicked the whole emulator
 * ("Could not find futex address": the shared cases live in their own
 * test so the panic cannot mask the value cases), the private WAIT
 * read the null page and parked on garbage, the bitset val3 rule was
 * missing on the 32-bit side, and on the 64-bit side the address
 * resolution materialized a fresh zero page for an unmapped address,
 * so operations silently acted on a phantom word (a shared WAKE
 * answered 0 where the host faults, a WAIT waited on nothing).
 */

#include "boxedwine.h"

#ifdef __TEST

#include "../cpu/testCPU.h"

namespace {
const U32 kOpWait = 0;
const U32 kOpWake = 1;
const U32 kOpWaitBitset = 9;
const U32 kOpWakeBitset = 10;
const U32 kOpPrivate = 128;
const U32 kUnmapped = 0x50000000;
}

#ifndef BOXEDWINE_GUEST_X64

void testFutexUnmapped32() {
    TestContext& context = testContext();
    KMemory* memory = context.memory;
    KThread* thread = context.thread;
    const U32 heapWord = TEST_HEAP_ADDRESS + 0xA00;
    const U32 shortTimeout = TEST_HEAP_ADDRESS + 0xA40;
    memory->writed(heapWord, 7);
    // A mapped 1 ms timeout: on a broken build the WAIT cases below
    // park briefly and report a wrong answer instead of hanging.
    memory->writed(shortTimeout, 0);
    memory->writed(shortTimeout + 4, 1000000);

    // Private WAKE on an unmapped word is a no-op success on the host.
    if (thread->futex(kUnmapped, kOpWake | kOpPrivate, 1, 0, 0, 0, false) != 0) {
        testFail("private FUTEX_WAKE on an unmapped address did not return 0");
    }
    // val3 == 0 is rejected before the address is ever touched.
    if (thread->futex(kUnmapped, kOpWaitBitset | kOpPrivate, 0, shortTimeout, 0, 0, false) != -K_EINVAL) {
        testFail("FUTEX_WAIT_BITSET with val3==0 did not return -EINVAL");
    }
    if (thread->futex(heapWord, kOpWakeBitset | kOpPrivate, 1, 0, 0, 0, false) != -K_EINVAL) {
        testFail("FUTEX_WAKE_BITSET with val3==0 did not return -EINVAL");
    }
    // Private WAIT on an unmapped word faults.
    if (thread->futex(kUnmapped, kOpWait | kOpPrivate, 0, shortTimeout, 0, 0, false) != -K_EFAULT) {
        testFail("private FUTEX_WAIT on an unmapped address did not return -EFAULT");
    }
    // WAIT with a matching value but an unmapped timeout pointer faults.
    if (thread->futex(heapWord, kOpWait | kOpPrivate, 7, kUnmapped, 0, 0, false) != -K_EFAULT) {
        testFail("FUTEX_WAIT with an unmapped timeout pointer did not return -EFAULT");
    }
    // Controls on a mapped word.
    if (thread->futex(heapWord, kOpWait | kOpPrivate, 9, 0, 0, 0, false) != -K_EAGAIN) {
        testFail("private FUTEX_WAIT value mismatch did not return -EAGAIN");
    }
    if (thread->futex(heapWord, kOpWake | kOpPrivate, 1, 0, 0, 0, false) != 0) {
        testFail("private FUTEX_WAKE on a mapped word did not return 0");
    }
}

void testFutexSharedUnmapped32() {
    TestContext& context = testContext();
    KThread* thread = context.thread;
    const U32 shortTimeout = TEST_HEAP_ADDRESS + 0xA40;
    context.memory->writed(shortTimeout, 0);
    context.memory->writed(shortTimeout + 4, 1000000);

    // Shared operations on an unmapped word fault on the host. These
    // are the calls that panicked the emulator before the fix, so on a
    // broken build this test dies at the first one.
    if (thread->futex(kUnmapped, kOpWake, 1, 0, 0, 0, false) != -K_EFAULT) {
        testFail("shared FUTEX_WAKE on an unmapped address did not return -EFAULT");
    }
    if (thread->futex(kUnmapped, kOpWakeBitset, 1, 0, 0, 0xFFFFFFFF, false) != -K_EFAULT) {
        testFail("shared FUTEX_WAKE_BITSET on an unmapped address did not return -EFAULT");
    }
    if (thread->futex(kUnmapped, kOpWait, 0, shortTimeout, 0, 0, false) != -K_EFAULT) {
        testFail("shared FUTEX_WAIT on an unmapped address did not return -EFAULT");
    }
}
#endif

#ifdef BOXEDWINE_GUEST_X64
#include "kmemory64.h"

void testFutexUnmapped64() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x76000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT)) memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3);
    const U64 word = page + 0x100;
    memory->writed(word, 0);
    KThread* thread = context.thread;
    const U64 unmapped = 0x5000000000ULL;

    // Private wake forms on an unmapped word are no-op successes on the
    // host; the guest faulted them before the fix.
    if (thread->futex64(unmapped, kOpWake | kOpPrivate, 1, 0, 0, 0) != 0) {
        testFail("64-bit private FUTEX_WAKE on an unmapped address did not return 0");
    }
    if (thread->futex64(unmapped, kOpWakeBitset | kOpPrivate, 1, 0, 0, 1) != 0) {
        testFail("64-bit private FUTEX_WAKE_BITSET on an unmapped address did not return 0");
    }
    // Wait forms and shared forms on an unmapped word fault.
    if (thread->futex64(unmapped, kOpWait | kOpPrivate, 0, 0, 0, 0) != -K_EFAULT) {
        testFail("64-bit private FUTEX_WAIT on an unmapped address did not return -EFAULT");
    }
    if (thread->futex64(unmapped, kOpWake, 1, 0, 0, 0) != -K_EFAULT) {
        testFail("64-bit shared FUTEX_WAKE on an unmapped address did not return -EFAULT");
    }
    // Control on a mapped word.
    if (thread->futex64(word, kOpWake | kOpPrivate, 1, 0, 0, 0) != 0) {
        testFail("64-bit private FUTEX_WAKE on a mapped word did not return 0");
    }
}
#endif

#endif
