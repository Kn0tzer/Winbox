/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

/*
 * Frontier review item D8 (register FD-09): modify_ldt with an
 * out-of-range descriptor index panicked the whole emulator.
 *
 * Host contract, measured with raw modify_ldt syscalls on two real
 * x86-64 Linux kernels (the full matrix is preserved with the lane
 * evidence):
 *   write (func 1 / 0x11), entry 8192 or 0xFFFFFFFF -> -EINVAL
 *   write with a byte count other than 16           -> -EINVAL
 *   write with an unmapped descriptor pointer       -> -EFAULT
 *   unknown func (3, 0x100)                         -> -ENOSYS
 * The guest's read form (func 0) is entry-shaped (the descriptor in
 * the buffer names the entry to describe back), unlike the host's
 * byte-count read; it keeps its shape, with -EINVAL in place of the
 * panic and -EFAULT for an unmapped buffer.
 *
 * The index cases live in their own test so that on a broken build
 * the panic cannot mask the value cases.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "../cpu/testCPU.h"

#ifndef BOXEDWINE_GUEST_X64

namespace {
const U32 kDescAddr = TEST_HEAP_ADDRESS + 0xB00;
const U32 kUnmapped = 0x50000000;
// seg_32bit (bit 0) + usable (bit 6)
const U32 kValidFlags = (1 << 0) | (1 << 6);

void putDesc(KMemory* memory, U32 entry, U32 base, U32 limit, U32 flags) {
    memory->writed(kDescAddr + 0, entry);
    memory->writed(kDescAddr + 4, base);
    memory->writed(kDescAddr + 8, limit);
    memory->writed(kDescAddr + 12, flags);
}
}

void testModifyLdtInvalid32() {
    TestContext& context = testContext();
    KThread* thread = context.thread;
    KMemory* memory = context.memory;

    // Control: a valid write and read-back round-trip.
    putDesc(memory, 100, 0x10000000, 0x2345, kValidFlags);
    if (thread->modify_ldt(1, kDescAddr, 16) != 0) {
        testFail("modify_ldt valid write did not return 0");
    }
    putDesc(memory, 100, 0, 0, 0);
    if (thread->modify_ldt(0, kDescAddr, 16) != 16 ||
        memory->readd(kDescAddr + 4) != 0x10000000 ||
        memory->readd(kDescAddr + 8) != 0x2345) {
        testFail("modify_ldt read-back did not return the installed descriptor");
    }
    // Unknown function numbers stay -ENOSYS, as on the host.
    if (thread->modify_ldt(3, kDescAddr, 16) != -K_ENOSYS) {
        testFail("modify_ldt unknown func did not return -ENOSYS");
    }
    // A write whose byte count is not one descriptor is rejected
    // before the descriptor is read.
    putDesc(memory, 100, 0x10000000, 0x2345, kValidFlags);
    if (thread->modify_ldt(1, kDescAddr, 8) != -K_EINVAL) {
        testFail("modify_ldt write with count 8 did not return -EINVAL");
    }
    // An unmapped descriptor pointer faults.
    if (thread->modify_ldt(1, kUnmapped, 16) != -K_EFAULT) {
        testFail("modify_ldt write with unmapped pointer did not return -EFAULT");
    }
    if (thread->modify_ldt(0, kUnmapped, 16) != -K_EFAULT) {
        testFail("modify_ldt read with unmapped pointer did not return -EFAULT");
    }
}

void testModifyLdtIndex32() {
    TestContext& context = testContext();
    KThread* thread = context.thread;
    KMemory* memory = context.memory;

    // Out-of-range indexes are rejected, never a panic. These are the
    // calls that killed the emulator before the fix, so on a broken
    // build this test dies at the first one.
    putDesc(memory, 8192, 0x10000000, 0x2345, kValidFlags);
    if (thread->modify_ldt(1, kDescAddr, 16) != -K_EINVAL) {
        testFail("modify_ldt write entry 8192 did not return -EINVAL");
    }
    putDesc(memory, 0xFFFFFFFF, 0x10000000, 0x2345, kValidFlags);
    if (thread->modify_ldt(1, kDescAddr, 16) != -K_EINVAL) {
        testFail("modify_ldt write entry 0xFFFFFFFF did not return -EINVAL");
    }
    putDesc(memory, 8192, 0x10000000, 0x2345, kValidFlags);
    if (thread->modify_ldt(0x11, kDescAddr, 16) != -K_EINVAL) {
        testFail("modify_ldt write-old entry 8192 did not return -EINVAL");
    }
    putDesc(memory, 8192, 0, 0, 0);
    if (thread->modify_ldt(0, kDescAddr, 16) != -K_EINVAL) {
        testFail("modify_ldt read entry 8192 did not return -EINVAL");
    }
}
#endif

#endif
