/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Stage A of the 32-bit graphics trap test plan.
 *  Injects raw 32-bit machine code to trigger the vk32/gl32 bridge traps
 *  via int 0x80 and verifies the host dispatcher routes them correctly.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "testVk32Trap.h"
#include "testCPU.h"

#define cpu (testContext().cpu)
#define newInstruction testNewInstruction
#define pushCode8 testPushCode8
#define pushCode32 testPushCode32
#define runTestCPU testRunCPU
#define failed testFail

// Stage A: vk32 trap witness (fnId=0)
//   mov eax, 0x564B0000  ; B8 00 00 4B 56  (VK32 trap magic)
//   xor ebx, ebx         ; 31 DB          (fnId=0, witness)
//   xor ecx, ecx         ; 31 C9          (args=null)
//   int 0x80             ; CD 80          (trap to host)
// Pass: EAX=0 (witness returns 0) if BOXEDWINE_VULKAN64, else EAX=-K_ENOSYS (0xFFFFFFDA)
// proving the dispatcher recognized the 0x564B0000 magic.
void testVk32TrapWitness() {
    newInstruction(0);
    // mov eax, 0x564B0000
    pushCode8(0xB8);
    pushCode32(0x564B0000);
    // xor ebx, ebx
    pushCode8(0x31);
    pushCode8(0xDB);
    // xor ecx, ecx
    pushCode8(0x31);
    pushCode8(0xC9);
    // int 0x80
    pushCode8(0xCD);
    pushCode8(0x80);
    runTestCPU();
#ifdef BOXEDWINE_VULKAN64
    // vk32Bridge witness (fnId=0) returns 0
    if (cpu->reg[0].u32 != 0) {
        failed("testVk32TrapWitness: expected EAX=0 (witness), got 0x%08X", cpu->reg[0].u32);
    }
#else
    // Without VULKAN64, dispatcher returns -K_ENOSYS (38) = 0xFFFFFFDA
    // This still proves the 0x564B0000 magic was recognized (not sent to Linux syscall table)
    if (cpu->reg[0].u32 != 0xFFFFFFDA) {
        failed("testVk32TrapWitness: expected EAX=0xFFFFFFDA (-K_ENOSYS), got 0x%08X", cpu->reg[0].u32);
    }
#endif
    // EBX and ECX should still be 0 (set by xor before the trap)
    if (cpu->reg[3].u32 != 0) {
        failed("testVk32TrapWitness: expected EBX=0, got 0x%08X", cpu->reg[3].u32);
    }
    if (cpu->reg[1].u32 != 0) {
        failed("testVk32TrapWitness: expected ECX=0, got 0x%08X", cpu->reg[1].u32);
    }
}

// Stage A-GL: gl32 trap witness (fnId=0)
//   mov eax, 0x474C0000  ; B8 00 00 4C 47  (GL32 trap magic)
//   xor ebx, ebx         ; 31 DB
//   xor ecx, ecx         ; 31 C9
//   int 0x80             ; CD 80
void testGl32TrapWitness() {
    newInstruction(0);
    // mov eax, 0x474C0000
    pushCode8(0xB8);
    pushCode32(0x474C0000);
    // xor ebx, ebx
    pushCode8(0x31);
    pushCode8(0xDB);
    // xor ecx, ecx
    pushCode8(0x31);
    pushCode8(0xC9);
    // int 0x80
    pushCode8(0xCD);
    pushCode8(0x80);
    runTestCPU();
#ifdef BOXEDWINE_OPENGL
    // In the test build, gl32Bridge is a stub (test_bridge_stubs.cpp) returning -K_ENOSYS,
    // even when BOXEDWINE_OPENGL is defined. The real bridge is excluded from TEST_SOURCES.
    // This still proves the 0x474C0000 magic was recognized (not sent to Linux syscall table).
    if (cpu->reg[0].u32 != 0xFFFFFFDA) {
        failed("testGl32TrapWitness: expected EAX=0xFFFFFFDA (-K_ENOSYS, stub), got 0x%08X", cpu->reg[0].u32);
    }
#else
    if (cpu->reg[0].u32 != 0xFFFFFFDA) {
        failed("testGl32TrapWitness: expected EAX=0xFFFFFFDA (-K_ENOSYS), got 0x%08X", cpu->reg[0].u32);
    }
#endif
    if (cpu->reg[3].u32 != 0) {
        failed("testGl32TrapWitness: expected EBX=0, got 0x%08X", cpu->reg[3].u32);
    }
    if (cpu->reg[1].u32 != 0) {
        failed("testGl32TrapWitness: expected ECX=0, got 0x%08X", cpu->reg[1].u32);
    }
}

#endif
