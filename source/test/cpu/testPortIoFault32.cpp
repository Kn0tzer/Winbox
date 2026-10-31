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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program, if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef __TEST
#ifndef BOXEDWINE_GUEST_X64

#include "ksignal.h"
#include "../cpu/testCPU.h"

// Guards FD-06 (defect register): a 32-bit guest that touches an
// unimplemented hardware port, or executes int 0x99 with an
// unregistered callback index, must receive #GP -- and must never
// panic the emulator. The permission calls settle the port model:
// syscall_ioperm and syscall_iopl return success while granting
// nothing, so no port access is ever permitted and #GP(0) is the
// honest answer for every port instruction (the disposition the
// frontier review chose, REVIEW.md D9). int 0x99 delivers #GP with
// error code (0x99<<3)|2, the software-interrupt shape guest
// exception handlers recognise, mirroring the x64 INT fix.
//
// Sabotage shape: on the pre-fix code the port cases land on the
// invalid-opcode path (SIGILL -- caught by a stub handler so the
// case fails its SIGSEGV assertions cleanly instead of ending the
// fixture process); and the int 0x99 case kpanics inside
// callOpenGL, killing this test binary outright.

namespace {

void runGpCase(const char* name, const U8* code, size_t size, U32 expectedError) {
    TestContext& context = testContext();
    testNewInstruction(0);
    KSigAction& action = context.process->sigActions[K_SIGSEGV];
    action.reset();
    action.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
    action.flags = K_SA_SIGINFO;
    context.memory->writeb(TEST_CODE_ADDRESS + 0x100, 0xcd);
    context.memory->writeb(TEST_CODE_ADDRESS + 0x101, 0x97);
    // A SIGILL handler on the same stub keeps a wrong-signal
    // delivery (the pre-fix behaviour) from ending the fixture
    // process and hanging the run: the case then fails its SIGSEGV
    // assertions cleanly instead.
    KSigAction& illAction = context.process->sigActions[K_SIGILL];
    illAction.reset();
    illAction.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
    context.cpu->reg[2].u32 = 0x3f8; // DX = port
    for (size_t i = 0; i < size; ++i) {
        testPushCode8(code[i]);
    }
    testRunCPU();
    if (action.sigInfo[0] != K_SIGSEGV || action.sigInfo[4] != 13 || action.sigInfo[1] != expectedError) {
        testFail("%s expected SIGSEGV trap 13 error %u, got signal=%u trap=%u error=%u",
                 name, expectedError, action.sigInfo[0], action.sigInfo[4], action.sigInfo[1]);
    }
}

} // namespace

void testPortIoFault32() {
    // In the 32-bit fixture, 0x6d/0x6f alone are the 32-bit forms
    // and the 0x66 prefix selects the 16-bit forms.
    static const U8 insb[] = {0x6c};
    runGpCase("INSB", insb, sizeof(insb), 0);
    static const U8 insw[] = {0x66, 0x6d};
    runGpCase("INSW", insw, sizeof(insw), 0);
    static const U8 insd[] = {0x6d};
    runGpCase("INSD", insd, sizeof(insd), 0);
    static const U8 repInsb[] = {0xf3, 0x6c};
    runGpCase("REP INSB", repInsb, sizeof(repInsb), 0);
    static const U8 outsb[] = {0x6e};
    runGpCase("OUTSB", outsb, sizeof(outsb), 0);
    static const U8 outsw[] = {0x66, 0x6f};
    runGpCase("OUTSW", outsw, sizeof(outsw), 0);
    static const U8 outsd[] = {0x6f};
    runGpCase("OUTSD", outsd, sizeof(outsd), 0);

    // int 0x99 with an unregistered callback index on the stack.
    // Runs last: on the pre-fix code it panics and kills the binary.
    static const U8 int99[] = {0xcd, 0x99};
    TestContext& context = testContext();
    testNewInstruction(0);
    KSigAction& action = context.process->sigActions[K_SIGSEGV];
    action.reset();
    action.handlerAndSigAction = TEST_CODE_ADDRESS + 0x100;
    action.flags = K_SA_SIGINFO;
    context.memory->writeb(TEST_CODE_ADDRESS + 0x100, 0xcd);
    context.memory->writeb(TEST_CODE_ADDRESS + 0x101, 0x97);
    context.cpu->push32(0x00ffffff); // unregistered callback index
    for (size_t i = 0; i < sizeof(int99); ++i) {
        testPushCode8(int99[i]);
    }
    testRunCPU();
    if (action.sigInfo[0] != K_SIGSEGV || action.sigInfo[4] != 13 || action.sigInfo[1] != ((0x99 << 3) | 2)) {
        testFail("int 0x99 expected SIGSEGV trap 13 error %u, got signal=%u trap=%u error=%u",
                 (0x99 << 3) | 2, action.sigInfo[0], action.sigInfo[4], action.sigInfo[1]);
    }
}

#endif
#endif
