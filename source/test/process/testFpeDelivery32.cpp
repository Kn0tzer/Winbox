/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

/*
 * Frontier review item D7: floating-point exception delivery.
 *
 * Host contract, measured with raw signals on a real x86-64 Linux
 * kernel (the full matrix is preserved with the lane evidence):
 *   integer divide by zero        -> SIGFPE, FPE_INTDIV, trap 0
 *   integer divide overflow       -> SIGFPE, FPE_INTDIV, trap 0
 *                                    (not FPE_INTOVF, not trap 4)
 *   INTO with overflow flag set   -> SIGSEGV, SI_KERNEL, trap 4
 *   x87 exceptions                -> SIGFPE, FPE_FLT*, trap 16
 *   SSE exceptions                -> SIGFPE, FPE_FLT*, trap 19
 * Wine 8.0's i386 signal handler classifies traps 16 and 19 only in
 * its SIGFPE handler, so FPU exceptions must arrive through the
 * SIGFPE slot for the guest to see the right Windows exception.
 *
 * Two deviations are pinned here: the divide-overflow path delivered
 * FPE_INTOVF with trap 4, and CPU::prepareFpuException (the x87/SSE
 * delivery helper) wrote into the SIGSEGV slot while claiming
 * SIGFPE. INTO through the SIGSEGV slot is host-correct and stays.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "ksignal.h"
#include "../cpu/testCPU.h"

#ifndef BOXEDWINE_GUEST_X64

namespace {
// Follows the port-IO fault suite's runGpCase pattern: the handler
// stub (int 0x97, the harness's test-end marker) is written after
// testNewInstruction because that call resets the code page. Both
// slots get the stub so a wrong-slot delivery still ends the run and
// fails the assertions instead of ending the fixture process.
void runFpeCase(TestContext& context, const U8* code, size_t size) {
    testNewInstruction(0);
    KSigAction& fpe = context.process->sigActions[K_SIGFPE];
    fpe.reset();
    memset(fpe.sigInfo, 0, sizeof(fpe.sigInfo));
    fpe.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
    fpe.flags = K_SA_SIGINFO;
    KSigAction& segv = context.process->sigActions[K_SIGSEGV];
    segv.reset();
    memset(segv.sigInfo, 0, sizeof(segv.sigInfo));
    segv.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
    segv.flags = K_SA_SIGINFO;
    context.memory->writeb(TEST_CODE_ADDRESS + 0x100, 0xCD);
    context.memory->writeb(TEST_CODE_ADDRESS + 0x101, 0x97);
    for (size_t i = 0; i < size; ++i) {
        testPushCode8(code[i]);
    }
    testRunCPU();
}
}

void testFpeDelivery32() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;

    // Case 1: idiv quotient overflow -> SIGFPE / FPE_INTDIV / trap 0.
    // (Delivered FPE_INTOVF / trap 4 before the fix.)
    static const U8 idivOverflow[] = {
        0xB8, 0x00, 0x00, 0x00, 0x80, // mov eax, INT_MIN
        0xBA, 0xFF, 0xFF, 0xFF, 0xFF, // mov edx, -1
        0xB9, 0xFF, 0xFF, 0xFF, 0xFF, // mov ecx, -1
        0xF7, 0xF9                    // idiv ecx
    };
    runFpeCase(context, idivOverflow, sizeof(idivOverflow));
    {
        U32* fpe = process->sigActions[K_SIGFPE].sigInfo;
        U32* segv = process->sigActions[K_SIGSEGV].sigInfo;
        if (fpe[0] != (U32)K_SIGFPE || fpe[2] != (U32)K_FPE_INTDIV || fpe[4] != 0 || segv[0] != 0) {
            testFail("idiv overflow delivered sig=%d code=%d trap=%d (segv slot sig=%d), expected SIGFPE/FPE_INTDIV/trap 0",
                fpe[0], fpe[2], fpe[4], segv[0]);
        }
    }

    // Case 2: divide by zero -> SIGFPE / FPE_INTDIV / trap 0 (control,
    // already correct before the fix).
    static const U8 divZero[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1
        0x31, 0xD2,                   // xor edx, edx
        0x31, 0xC9,                   // xor ecx, ecx
        0xF7, 0xF1                    // div ecx
    };
    runFpeCase(context, divZero, sizeof(divZero));
    {
        U32* fpe = process->sigActions[K_SIGFPE].sigInfo;
        U32* segv = process->sigActions[K_SIGSEGV].sigInfo;
        if (fpe[0] != (U32)K_SIGFPE || fpe[2] != (U32)K_FPE_INTDIV || fpe[4] != 0 || segv[0] != 0) {
            testFail("div by zero delivered sig=%d code=%d trap=%d (segv slot sig=%d), expected SIGFPE/FPE_INTDIV/trap 0",
                fpe[0], fpe[2], fpe[4], segv[0]);
        }
    }

    // Case 3: INTO with the overflow flag set -> SIGSEGV / trap 4.
    // Host-correct as-is; this pins it so a future FPE change cannot
    // drag INTO along.
    static const U8 intoOverflow[] = {
        0xB8, 0xFF, 0xFF, 0xFF, 0x7F, // mov eax, INT_MAX
        0x83, 0xC0, 0x01,             // add eax, 1
        0xCE                          // into
    };
    runFpeCase(context, intoOverflow, sizeof(intoOverflow));
    {
        U32* fpe = process->sigActions[K_SIGFPE].sigInfo;
        U32* segv = process->sigActions[K_SIGSEGV].sigInfo;
        if (segv[0] != (U32)K_SIGSEGV || segv[4] != 4 || fpe[0] != 0) {
            testFail("INTO delivered sig=%d trap=%d (fpe slot sig=%d), expected SIGSEGV/trap 4",
                segv[0], segv[4], fpe[0]);
        }
    }

    // Case 4: the FPU delivery helper itself goes through the SIGFPE
    // slot with trap 16. (Wrote into the SIGSEGV slot before the fix.)
    {
        KSigAction& fpe = process->sigActions[K_SIGFPE];
        fpe.reset();
        memset(fpe.sigInfo, 0, sizeof(fpe.sigInfo));
        fpe.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
        fpe.flags = K_SA_SIGINFO;
        KSigAction& segv = process->sigActions[K_SIGSEGV];
        segv.reset();
        memset(segv.sigInfo, 0, sizeof(segv.sigInfo));
        segv.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
        segv.flags = K_SA_SIGINFO;
        context.cpu->prepareFpuException(K_FPE_FLTDIV, 0);
        U32* f = process->sigActions[K_SIGFPE].sigInfo;
        U32* s = process->sigActions[K_SIGSEGV].sigInfo;
        if (f[0] != (U32)K_SIGFPE || f[2] != (U32)K_FPE_FLTDIV || f[4] != 16 || s[0] != 0) {
            testFail("prepareFpuException delivered fpe slot sig=%d code=%d trap=%d, segv slot sig=%d; expected SIGFPE/FPE_FLTDIV/trap 16 in the SIGFPE slot only",
                f[0], f[2], f[4], s[0]);
        }
    }
}
#endif

#endif
