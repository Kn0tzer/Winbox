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
#include "ksignal.h"
#include "../cpu/testCPU.h"

// Guards the "never skip an undecoded opcode" rule: an unhandled instruction
// must leave RIP at the faulting instruction when no handler exists, and must
// deliver SIGILL (not advance past the opcode) when the guest installed one.
// This runs the real CPU64 interpreter, so it covers both classic undefined
// opcodes and a VEX-prefixed instruction (which must fault because CPUID does
// not advertise AVX).
void testOpcodeFault64Delivery() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 codePage = 0x60000000ULL;
    constexpr U64 stackPage = 0x60010000ULL;
    constexpr U64 handlerAddress = 0x60030000ULL;
    if (!memory->getPageFlags(codePage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(codePage, K64_PAGE_SIZE, 7) != codePage) {
        testFail("opcode-fault test could not map code page");
        return;
    }
    if (!memory->getPageFlags(stackPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(stackPage, K64_PAGE_SIZE * 4, 3) != stackPage) {
        testFail("opcode-fault test could not map stack page");
        return;
    }
    if (bw64RecoverEnabled()) {
        testFail("instruction recovery must be disabled by default on every build");
        return;
    }

    CPU64 cpu(memory);
    cpu.thread = nullptr; // no process signal path: test the CPU fault directly
    cpu.rip = codePage;
    cpu.reg[X64_RSP].setU64(stackPage + K64_PAGE_SIZE * 4 - 16);
    const U8 undefinedOpcode[] = { 0x0F, 0xB9, 0xC0 };
    memory->memcpyToGuest(codePage, undefinedOpcode, sizeof(undefinedOpcode));

    cpu.runBounded(1);
    if (cpu.rip != codePage) {
        testFail("unhandled opcode advanced RIP by %llu bytes instead of faulting",
                 (unsigned long long)(cpu.rip - codePage));
    }
    if (!cpu.yield) {
        testFail("unhandled opcode did not stop the interpreter");
    }

    // With a guest SIGILL handler installed, delivery must jump to the handler
    // and leave the faulting RIP in the saved ucontext — never skip forward.
    auto& sa = cpu.sigActions[K_SIGILL];
    sa.installed = true;
    sa.handler = handlerAddress;
    sa.restorer = handlerAddress;

    cpu.yield = false;
    cpu.rip = codePage;
    cpu.runBounded(1);
    if (cpu.rip != handlerAddress) {
        testFail("unhandled opcode did not deliver SIGILL to the guest handler (rip=0x%llx)",
                 (unsigned long long)cpu.rip);
    }

    const U8 vexOpcode[] = { 0xC5, 0xF8, 0x77 }; // VEX2 vzeroupper
    memory->memcpyToGuest(codePage, vexOpcode, sizeof(vexOpcode));
    cpu.yield = false;
    cpu.rip = codePage;
    cpu.runBounded(1);
    if (cpu.rip != handlerAddress) {
        testFail("VEX-prefixed opcode did not deliver SIGILL (rip=0x%llx)",
                 (unsigned long long)cpu.rip);
    }
}

void testCpu64AvxCpuidMask() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 codePage = 0x60040000ULL;
    if (!memory->getPageFlags(codePage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(codePage, K64_PAGE_SIZE, 7) != codePage) {
        testFail("AVX CPUID test could not map executable page");
        return;
    }
    const U8 cpuid[] = { 0x0F, 0xA2 };
    memory->memcpyToGuest(codePage, cpuid, sizeof(cpuid));

    CPU64 cpu(memory);
    cpu.thread = nullptr;
    cpu.memory = memory;
    cpu.rip = codePage;
    cpu.yield = false;
    cpu.reg[X64_RAX].setU64(1);
    cpu.reg[X64_RCX].setU64(0);
    cpu.runBounded(1);
    const U32 leaf1Ecx = cpu.reg[X64_RCX].u32;
    if (leaf1Ecx & (1u << 28)) testFail("CPUID advertises AVX without AVX execution support");
    if (leaf1Ecx & (1u << 27)) testFail("CPUID advertises OSXSAVE without AVX execution support");

    cpu.rip = codePage;
    cpu.yield = false;
    cpu.reg[X64_RAX].setU64(7);
    cpu.reg[X64_RCX].setU64(0);
    cpu.runBounded(1);
    if (cpu.reg[X64_RBX].u32 & (1u << 5)) {
        testFail("CPUID advertises AVX2 while AVX and OSXSAVE are hidden");
    }
}

#endif
#endif
