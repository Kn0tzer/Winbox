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
 * Guest memory ordering policy for the multi-threaded WASM JIT.
 *
 * Guest x86 code is TSO; ordinary Wasm loads/stores are not, so a plain
 * load/store can observe a publication flag before its data on a weakly
 * ordered host (ARM64). The MT WASM JIT therefore emits an `atomic.fence`
 * at every guest memory-instruction boundary, and the interpreter emits the
 * equivalent fence during warmup and single-op fallback.
 *
 * Those fences cost real time on memory-heavy MT workloads, so the policy is
 * latched once at startup: `true` keeps the fences, `false` leaves only a
 * compiler barrier. Builds default to off, matching the previously deployed
 * interpreter; `safemem=1` restores fences to diagnose ordering bugs on weakly
 * ordered hosts. Whatever is chosen is latched before any CPU exists and mixed
 * into the emitted-block cache identity, so a fenced and an unfenced module
 * can never be confused.
 *
 * Single-threaded and native builds are unaffected by this policy.
 */

#ifndef __WASM_MEMORY_ORDERING_H__
#define __WASM_MEMORY_ORDERING_H__

#if defined(BOXEDWINE_WASM_JIT) && defined(BOXEDWINE_MULTI_THREADED)

constexpr bool WASM_JIT_DEFAULT_SAFE_MEMORY = false;

// Configure once at startup, before any CPU can execute guest instructions.
// Shared by all pthreads and guest processes; never changed afterward.
extern bool wasmJitNeedsMemoryFence;
void wasmJitInitMemoryOrdering(bool safeMemory = WASM_JIT_DEFAULT_SAFE_MEMORY);

#endif

#endif // __WASM_MEMORY_ORDERING_H__
