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

#ifndef __WASM_JIT_RUNTIME_H__
#define __WASM_JIT_RUNTIME_H__

#ifdef BOXEDWINE_WASM_JIT

// Runtime controls for the 32-bit guest WASM JIT. Each is latched on first
// use and stable for the process lifetime (see wasmJitRuntime.cpp).
extern bool wasmJit32Active;
bool wasmJit32Enabled();
bool wasmJit32SafeMemoryEnabled();
bool wasmJit32CollectRetiredInstructions();
// The runCount value every reader must agree on once the JIT has ruled a block
// out (rejected, failed installation, slot miss, or an op that can never be
// compiled). It must be strictly above the hot threshold, or run()'s probe
// would hand the block back to the generator on every later entry.
U32 wasmJit32DecidedRunCount();
// BW32_JIT_HOT=<1..254> moves the entry count at which a 32-bit guest block is
// handed to the code generator. The compile cost of one block is only amortized
// by a block that is entered far more often than JIT_RUN_COUNT (which is also
// the compiled-in default), so a browser session that runs a lot of cold code
// pays for blocks that never pay it back. Decided against JIT_RUN_COUNT when
// unset, so an unset value keeps the historical policy. Diagnostic +
// measurement knob: it changes only which blocks are compiled, never what a
// compiled block does.
U32 wasmJit32HotThreshold();
// BW32_JIT_NO_GROUPS=1: install each generated block as its own module instead
// of merging blocks into shared runtime groups (see jitWasmCodeGen.cpp).
bool wasmJit32GroupsDisabled();
// BW32_JIT_TRAP_TRACE=1: enter generated blocks through a JavaScript wrapper
// that reports the worker-local slot, guest EIP and block size of a faulting
// module (a raw wasm trap names only an internal function index). Diagnostic
// only: it adds a JS frame per block entry.
bool wasmJit32TrapTraceEnabled();

class CPU;
class DecodedOp;

// Diagnostic entry trace (host env BW32_JIT_TRACE=1, optional
// BW32_JIT_TRACE_LIMIT, default 20000 lines). Logs one line per generated-code
// entry with the worker-local table slot, the guest EIP and the block's op
// count, so a crash inside generated code can be attributed to a block. Kept
// because a faulting generated module reports only an internal function index.
// Returns false once the limit is reached, so callers can skip the formatting.
bool wasmJit32TraceEntry(CPU* cpu, U32 tableIndex, const DecodedOp* op);

#endif

#endif // __WASM_JIT_RUNTIME_H__
