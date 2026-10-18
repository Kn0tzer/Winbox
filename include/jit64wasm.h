// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
#ifndef BOXEDWINE_JIT64_WASM_H
#define BOXEDWINE_JIT64_WASM_H

#include "jit64.h"

// Staging ABI for a wasm32 host with 64-bit guest values. This is separate
// from CPU64's object layout: CPU64::tryWasmExec copies the guest GPRs, RIP and
// RFLAGS in before the call and back out afterwards.
//
// Host pointer width is NOT a guest concept. The emitted modules address this
// struct through a wasm32 linear-memory offset, exactly like the imported
// memory helpers' scratch pointer. A Memory64 build (64-bit host pointers) is
// therefore not supported: jit64WasmAvailable() is false there and CPU64 keeps
// using the interpreter. jit64WasmHostAbiOk() re-checks the assumptions at
// runtime (pointer size and offsets) so a build-configuration mistake degrades
// to the interpreter instead of corrupting state.
//
// Emitted-code-written counters: the module itself increments `blocksEntered`
// at entry and stores `execCount` before it returns. The host only reads them,
// so `wasmBlocks`/`wasmInsns` stats prove *emitted* execution rather than a
// decoded-block cache hit (see docs/reports/winbox-jit64-*.md).
struct Jit64WasmState {
    U64 gpr[16];
    U64 rip;
    U32 rflags;
    U32 blocksEntered; // in/out: emitted code increments this per entry
    U64 memScratch;    // 8-byte scratch for the memRead import's result
    U32 genBase;       // wasm32 address of the page-generation table
    U32 gen0;          // entry generation of the block's first code page
    U32 gen1;          // entry generation of the block's last code page
    U32 execCount;     // out: guest instructions actually executed
    U32 limit;         // in: max guest instructions for this entry
    U32 reserved3;
};
static_assert(offsetof(Jit64WasmState, rip) == 128, "wasm guest RIP offset");
static_assert(offsetof(Jit64WasmState, rflags) == 136, "wasm guest flags offset");
static_assert(offsetof(Jit64WasmState, blocksEntered) == 140, "wasm entered offset");
static_assert(offsetof(Jit64WasmState, memScratch) == 144, "wasm mem scratch offset");
static_assert(offsetof(Jit64WasmState, genBase) == 152, "wasm gen table offset");
static_assert(offsetof(Jit64WasmState, gen0) == 156, "wasm gen0 offset");
static_assert(offsetof(Jit64WasmState, gen1) == 160, "wasm gen1 offset");
static_assert(offsetof(Jit64WasmState, execCount) == 164, "wasm exec count offset");
static_assert(offsetof(Jit64WasmState, limit) == 168, "wasm limit offset");
static_assert(sizeof(Jit64WasmState) == 176, "wasm guest state size");

// UNBOUNDED_LIMIT: the `limit` value for an entry that may run the whole block.
constexpr U32 JIT64_WASM_UNBOUNDED = 0xFFFFFFFFu;

// State bridge (non-optimizing plumbing): copy a plain GPR/RIP/RFLAGS triple
// into the staging struct before execute(), and back out after. Keeps the
// header dependency-free (no cpu64.h/SDL) so the native harness can test it.
void jit64WasmStateInit(Jit64WasmState& state, const U64 gpr[16], U64 rip, U32 rflags);
void jit64WasmStateRead(const Jit64WasmState& state, U64 gpr[16], U64& rip, U32& rflags);
// Entry generations + generation-table address for the inline invalidation
// checks the emitted module performs after every guest store.
void jit64WasmStateSetGens(Jit64WasmState& state, U32 genBase, U32 gen0, U32 gen1);

// Host memory-import descriptor for the emitted module. A shared (pthread)
// host memory must be imported as {min, max, shared:true}; a plain memory is
// imported with min only (see scripts/gen-jit64.mjs docs and the runtime's
// jit64WasmDetectMemory()). Default-constructed = non-shared/min-only, which
// is what native tests and single-threaded builds use.
struct Jit64EmitOptions {
    bool sharedMemory = false; // host linear memory is a SharedArrayBuffer
};

// Emit execute(state_ptr: i32) importing env.memory and the memory helpers.
// Supported: register/memory 8/16/32/64-bit MOV, ALU (ADD/ADC/SUB/SBB/AND/OR/
// XOR/CMP/TEST), shifts/rotates by immediate/1/CL, IMUL (1/2/3-operand),
// MOVZX/MOVSX/MOVSXD, LEA, PUSH/POP, group 3 (TEST/NOT/NEG/MUL), CBW/CWDE/
// CDQE, CWD/CDQ/CQO, CMOVcc, SETcc, BT/BTS/BTR/BTC, string ops, and the
// control flow the block terminator needs: NOP, Jcc, JMP rel, CALL rel, RET,
// LEAVE.
//
// Every instruction advances the guest RIP individually (L_ENTRY + the
// instruction's end offset) so an early exit (page-generation change after a
// guest store, or the `limit` bound) reports the exact next RIP and executed
// count. After every guest store the module re-reads the block's code-page
// generations and leaves at the next RIP when either moved (same contract as
// CPU64::execBlock). The module never runs past its last decoded instruction
// and never past `limit` instructions. Rejects the whole block for any
// unsupported op/operand; on rejection, clears output.
bool jit64EmitWasm(const Jit64Block& block, const Jit64EmitOptions& options,
                   std::vector<U8>& output);
inline bool jit64EmitWasm(const Jit64Block& block, std::vector<U8>& output) {
    return jit64EmitWasm(block, Jit64EmitOptions(), output);
}

#ifdef JIT64_GEN_VERIFY
// Byte-identical migration test hooks (defined in jit64wasm.cpp).
// jit64EmitOld emits one op with the hand-written emitter, jit64EmitNew
// with the table-generated one. Return 0xFE when the kind has no emitter
// in that set (the test skips such ops).
U32 jit64EmitOld(std::vector<U8>& body, const Jit64Op& op);
U32 jit64EmitNew(std::vector<U8>& body, const Jit64Op& op);
#endif

// Runtime module instantiation and table ownership.
//
// The emitted function is installed into a WebAssembly.Table (the JIT build
// passes -sALLOW_TABLE_GROWTH) and the returned index is called from C++ as a
// function pointer, which compiles to call_indirect on that table: no
// JavaScript crossing per block. When table growth is unavailable the runtime
// falls back to a JS-side array (jit64WasmEngineMode() reports "js-array"), so
// correctness never depends on the build flag; only the call overhead does.
//
// Table indices are worker/table-local. A Jit64State records the owner id
// returned by jit64WasmOwnerId(); a mismatch (guest thread migrated to a
// different worker) drops the cache without touching the other worker's
// table, so a numeric slot is never reused across workers.
class KMemory64; // forward: the header stays dependency-free

// The runtime lives in source/emulation/cpu/jit64wasm_runtime.cpp, which is
// compiled-but-empty unless the target is both emscripten-hosted AND a
// guest-X64 build. Everywhere else these are inline no-ops, so a 32-bit build
// neither references nor links the CPU64 runtime.
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
// Instantiate the emitted module and publish its execute() in the host's wasm
// table. Returns a table index >= 0, or -1 when the module cannot be
// instantiated (the emitter is deterministic, so callers cache the failure).
int jit64WasmInstantiate(const U8* bytes, U32 byteCount, const Jit64EmitOptions& options);
// Call the emitted execute() for `tableIndex`. Synchronous; the module cannot
// re-enter the CPU.
void jit64WasmCall(int tableIndex, Jit64WasmState* state);
// Null the table slot (or drop the JS-array entry) and recycle it.
void jit64WasmRelease(int tableIndex);
U32 jit64WasmOwnerId();
// Routes the emitted module's memRead/memWrite imports at `mem` for the
// duration of the next synchronous jit64WasmCall. Thread-local: wasm
// execution never yields, so the import always observes the setter's thread.
void jit64WasmSetActiveMem(KMemory64* mem);
bool jit64WasmAvailable();
bool jit64WasmHostAbiOk();
// "table" (ALLOW_TABLE_GROWTH call_indirect), "js-array" (fallback), or
// "unavailable". For the metrics panel; never changes behavior.
const char* jit64WasmEngineMode();
// Detected host-memory descriptor (cached after the first call).
Jit64EmitOptions jit64WasmMemoryOptions();
#else
inline int jit64WasmInstantiate(const U8*, U32, const Jit64EmitOptions&) { return -1; }
inline void jit64WasmCall(int, Jit64WasmState*) {}
inline void jit64WasmRelease(int) {}
inline U32 jit64WasmOwnerId() { return 0; }
inline void jit64WasmSetActiveMem(KMemory64*) {}
inline bool jit64WasmAvailable() { return false; }
inline bool jit64WasmHostAbiOk() { return false; }
inline const char* jit64WasmEngineMode() { return "unavailable"; }
inline Jit64EmitOptions jit64WasmMemoryOptions() { return Jit64EmitOptions(); }
#endif

#endif