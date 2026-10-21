/*
 *  Copyright (C) 2012-2026  The Boxedwine Team
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * 32-bit guest WASM JIT statistics ("cpu32" section of the metrics contract).
 *
 * Provenance of every field (no derived or inferred numbers):
 *
 *   engine                     "wasm-jit" when this backend is compiled in.
 *   available                  Compile-time capability of the backend.
 *   enabled                    wasmJit32Enabled() — the latched BW32_JIT value.
 *   safeMemory                 wasmJit32SafeMemoryEnabled() (MT guest TSO fences).
 *   retiredCounting            Emitted code counts retired instructions.
 *   blocksEmitted              Generated modules produced by codegen (a block
 *                              that fails to be emitted is not counted).
 *   blocksEntered              Times C++ entered generated code through the
 *                              table slot. Chained entries count individually.
 *   blocksInstantiated         wasm module compilations/instantiations the JIT
 *                              performed, including lazy cross-worker installs.
 *   instructionsRetired        Guest instructions executed by generated code,
 *                              counted IN the generated code (one increment
 *                              per emitted instruction). Requires
 *                              BW32_JIT_STATS=1; otherwise 0 with
 *                              retiredCounting=false. Never decoded-op or
 *                              cache bookkeeping.
 *   emitNs                     Wall time in JitWasmCodeGen (bytecode emission).
 *   instantiateNs              Wall time in the JS compile + instantiate calls.
 *   fallbacks                  Counts by reason, see WasmJit32Fallback. The
 *                              emulateSingleOp count is taken while a block is
 *                              COMPILED (how many of its guest instructions were
 *                              routed to the interpreter instead of emitted
 *                              code); the other reasons are run-time events.
 *   sampledAtMs                KSystem millisecond clock at snapshot time.
 *
 * All counters live in the pthreads-shared linear-memory data segment, so they
 * cover every worker; updates and reads are atomic. Per-CPU counters are
 * registered by the CPU constructor and summed under a mutex, so a snapshot is
 * stable and tear-free while guest threads keep running.
 *
 * The JSON seam is `bw32_jit_stats_json` (cpu32 section only) and
 * `bw64_jit_stats_json` (the parent contract's single entry point, which wraps
 * the cpu32 section and leaves the cpu64 section to its owner — see the
 * composition note in the .cpp).
 */

#ifndef __WASM_JIT_STATS_H__
#define __WASM_JIT_STATS_H__

#ifdef BOXEDWINE_WASM_JIT

#include <cstdint>

class CPU;

enum class WasmJit32Fallback : uint32_t {
    EmulateSingleOp = 0,   // op handed to the interpreter's single-op path
    SlotMiss = 1,          // table slot not ready in this worker
    InstallFailure = 2,    // module rejected / instantiation failed
    Disabled = 3,          // BW32_JIT=0 (or written-code opt-out) refused it
    RejectDelegated = 4,    // block kept on interpreter due to delegation cost
    Count = 5,
};

struct WasmJit32StatsSnapshot {
    U64 blocksEmitted = 0;
    U64 blocksEntered = 0;
    U64 blocksInstantiated = 0;
    U64 instructionsRetired = 0;
    U64 emitNs = 0;
    U64 instantiateNs = 0;
    U64 fallbacks[(U32)WasmJit32Fallback::Count] = {};
    U64 sampledAtMs = 0;
    bool available = false;
    bool enabled = false;
    bool safeMemory = false;
    bool retiredCounting = false;
};

// --- Recording hooks (called from the backend) -----------------------------
void wasmJit32StatsBlockEmitted();
void wasmJit32StatsBlockInstantiated(U64 ns);
void wasmJit32StatsAddEmitNs(U64 ns);
void wasmJit32StatsFallback(WasmJit32Fallback kind);

// --- CPU registry for the emitted retired-instruction counter --------------
void wasmJit32StatsRegisterCpu(CPU* cpu);
void wasmJit32StatsUnregisterCpu(CPU* cpu);

// --- Reading ---------------------------------------------------------------
void wasmJit32StatsSnapshot(WasmJit32StatsSnapshot* out);

// cpu32 section as a JSON object, NUL terminated. Returns the length written
// (excluding the terminator) or -1 when it does not fit.
//
// RUNTIME API: exported to JavaScript. `Module.ccall('bw32_jit_stats_json',
// 'number', ['number','number'], [ptr, size])` with a caller-allocated buffer
// (Module._malloc), or use bw64_jit_stats_json for both sections.
// EMSCRIPTEN_KEEPALIVE also keeps it through DCE and adds it to
// EXPORTED_FUNCTIONS, so no -sEXPORTED_FUNCTIONS entry is needed.
extern "C" EMSCRIPTEN_KEEPALIVE int bw32_jit_stats_json(char* out, int outSize);

// Parent-contract entry point: {"cpu32":{...},"cpu64":{...}}. Exported for the
// metrics panel (see the composition note in the .cpp).
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_jit_stats_json(char* out, int outSize);

// Convenience for JavaScript callers that cannot allocate in the wasm heap:
// fills a static buffer and returns its address. NUL terminated.
// `Module.UTF8ToString(Module.ccall('bw64_jit_stats_json_text','number',[],[]))`.
// The buffer is shared, so a reader must copy the string before another call.
extern "C" EMSCRIPTEN_KEEPALIVE const char* bw64_jit_stats_json_text();

#endif // BOXEDWINE_WASM_JIT

#endif // __WASM_JIT_STATS_H__