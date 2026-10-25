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

void testClone3Abi64() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x77100000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("x64 clone3 test could not map clone_args");
        return;
    }
    memory->memsetGuest(page, 0, K64_PAGE_SIZE);
    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 args, U64 size) -> S64 {
        cpu.reg[X64_RAX].setU64(435); // __NR_clone3
        cpu.reg[X64_RDI].setU64(args);
        cpu.reg[X64_RSI].setU64(size);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    const S64 shortResult = invoke(page, 63);
    const S64 largeResult = invoke(page, 4097);
    const S64 pointerResult = invoke(0x7fff000000000000ULL, 88);
    if (shortResult != -K_EINVAL || largeResult != -K_E2BIG || pointerResult != -K_EFAULT) {
        testFail("x64 clone3 size/pointer validation returned %lld, %lld, %lld",
                 (long long)shortResult, (long long)largeResult, (long long)pointerResult);
    }
    memory->writed(page + 33, 65); // exit_signal outside the Linux signal range for the unaligned struct
    const S64 signalResult = invoke(page + 1, 64); // clone_args need not be naturally aligned
    if (signalResult != -K_EINVAL) testFail("x64 clone3 exit_signal validation returned %lld", (long long)signalResult);
    memory->writed(page + 33, 0);
    memory->writeb(page + 88, 1); // nonzero bytes in an unknown future extension
    const S64 extensionResult = invoke(page, 96);
    if (extensionResult != -K_E2BIG) testFail("x64 clone3 extension validation returned %lld", (long long)extensionResult);
    memory->writeb(page + 88, 0);
    memory->writeq(page, 17); // CSIGNAL is forbidden in clone3.flags
    const S64 flagsResult = invoke(page, 88);
    if (flagsResult != -K_EINVAL) testFail("x64 clone3 flags validation returned %lld", (long long)flagsResult);
}

#endif
#endif
