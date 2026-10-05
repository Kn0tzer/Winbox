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
// clears output.
bool jit64EmitWasm(const Jit64Block& block, std::vector<U8>& output);

// Runtime module instantiation: the table-index glue (cf. PR #151's
// boxedwine_wasm_instantiate_runtime_batch, single-module form; batching and
// merging remain phase-4 work). Instantiates an emitted module against the
// host's WebAssembly.Memory and returns a table index (>= 0), or -1 on
// failure. jit64WasmCall invokes the table entry's execute(statePtr), where
// statePtr is a wasm32 host address of a Jit64WasmState in linear memory.
// jit64WasmRelease frees a table slot (slots are reused). Under
// __EMSCRIPTEN__ these are implemented with synchronous WebAssembly.Module /
// WebAssembly.Instance in source/emulation/cpu/jit64wasm_runtime.cpp; on
// native builds they are inline no-ops (instantiate returns -1) so cpu64.cpp
// links without the runtime TU.
#ifdef __EMSCRIPTEN__
int jit64WasmInstantiate(const U8* bytes, U32 byteCount);
void jit64WasmCall(int tableIndex, U32 statePtr);
void jit64WasmRelease(int tableIndex);
inline bool jit64WasmAvailable() { return true; }
#else
inline int jit64WasmInstantiate(const U8*, U32) { return -1; }
inline void jit64WasmCall(int, U32) {}
inline void jit64WasmRelease(int) {}
inline bool jit64WasmAvailable() { return false; }
#endif

#endif
