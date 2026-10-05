// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
#ifndef BOXEDWINE_JIT64_WASM_H
#define BOXEDWINE_JIT64_WASM_H

#include "jit64.h"

// Staging ABI for a wasm32 host with 64-bit guest values. This is separate
// from CPU64's object layout; the bridge below copies state in/out.
struct Jit64WasmState {
    U64 gpr[16];
    U64 rip;
    U32 rflags;
    U32 reserved;
};
static_assert(offsetof(Jit64WasmState, rip) == 128, "wasm guest RIP offset");
static_assert(offsetof(Jit64WasmState, rflags) == 136, "wasm guest flags offset");
static_assert(sizeof(Jit64WasmState) == 144, "wasm guest state size");

// State bridge (non-optimizing plumbing): copy a plain GPR/RIP/RFLAGS triple
// into the staging struct before execute(), and back out after. Keeps the
// header dependency-free (no cpu64.h/SDL) so the native harness can test it;
// cpu64.cpp will pass cpu->reg[i].u64 / cpu->rip / cpu->rflags once the
// runtime instantiation glue exists.
void jit64WasmStateInit(Jit64WasmState& state, const U64 gpr[16], U64 rip, U32 rflags);
void jit64WasmStateRead(const Jit64WasmState& state, U64 gpr[16], U64& rip, U32& rflags);

// Emit execute(state_ptr: i32) importing env.memory. Supports register-direct
// 32/64-bit MOV and immediate MOV, plus register-direct ALU (ADD/ADC/SUB/
// SBB/AND/OR/XOR/CMP/TEST, /0../7, r/m-r, r-r/m, r/m-imm and acc-imm forms)
// with eager, interpreter-exact flag computation (CF/PF/AF/ZF/SF/OF; other
// RFLAGS bits preserved). Advances RIP, zero-extends 32-bit destinations.
// Rejects the entire block for any unsupported op/operand. On rejection,
// clears output. No CPU64 dispatch or wasm-table ownership is installed yet.
bool jit64EmitWasm(const Jit64Block& block, std::vector<U8>& output);

#endif
