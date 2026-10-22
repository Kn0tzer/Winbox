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
 * Runtime control for the 32-bit guest WASM JIT (the "cpu32" engine).
 *
 * The backend is compiled in by -DBOXEDWINE_WASM_JIT. Whether it actually
 * compiles and enters generated modules is decided at runtime and latched on
 * first use, because:
 *
 *   - the launcher's public `jit=0|1` URL parameter has to reach host code
 *     before any guest instruction runs (guest environment variables cannot
 *     switch host codegen);
 *   - a latched decision keeps the emitted-module cache identity stable for
 *     the whole session (see wasmMemoryOrdering.h and the block hash in
 *     JitWasmCodeGen::preCompile).
 *
 * Host environment variables (read once, then latched):
 *
 *   BW32_JIT=0|1                1 requests generated code, 0 keeps the
 *                               interpreter (the safe fallback used by the
 *                               public jit=0 parameter). Unset = compiled-in
 *                               default, which is disabled until representative acceptance passes.
 *   BW32_JIT_SAFE_MEMORY=0|1    guest memory-ordering policy for MT builds;
 *                               see wasmMemoryOrdering.h. Unset = the
 *                               backend default (disabled).
 *   BW32_JIT_STATS=0|1          emit a per-instruction retired counter inside
 *                               generated code. Off by default: it costs a
 *                               load/add/store per guest instruction and a
 *                               different module cache identity.
 *
 * Without BOXEDWINE_WASM_JIT this translation unit is empty and the
 * interpreter is the only engine.
 */

#include "boxedwine.h"

#ifdef BOXEDWINE_WASM_JIT

#include "wasmJitRuntime.h"
#include "wasmMemoryOrdering.h"

#include <mutex>

namespace {
    // Guest TSO fences only exist in the multi-threaded backend.
#ifdef BOXEDWINE_MULTI_THREADED
    constexpr bool BW32_JIT_DEFAULT_SAFE_MEMORY = WASM_JIT_DEFAULT_SAFE_MEMORY;
#else
    constexpr bool BW32_JIT_DEFAULT_SAFE_MEMORY = false;
#endif

    std::once_flag g_bw32JitEnabledInit;
    std::atomic<int> g_bw32JitSafeMemory{-1};
    std::atomic<int> g_bw32JitCollectRetired{0};

    int readBoolEnv(const char* name, int fallback) {
        const char* value = std::getenv(name);
        if (!value || !value[0]) {
            return fallback;
        }
        if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0) {
            return 0;
        }
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0) {
            return 1;
        }
        return fallback;
    }
}

bool wasmJit32Active = false;

bool wasmJit32Enabled() {
    std::call_once(g_bw32JitEnabledInit, [] {
        // Explicit opt-in while browser acceptance and cold-start costs are
        // evaluated. The public jit=1 parameter sets this before startup.
        wasmJit32Active = readBoolEnv("BW32_JIT", 0) != 0;
        klog(wasmJit32Active ? "BW32 JIT: enabled (generated modules)"
                             : "BW32 JIT: disabled by BW32_JIT=0 (interpreter only)");
    });
    return wasmJit32Active;
}

bool wasmJit32SafeMemoryEnabled() {
    int cached = g_bw32JitSafeMemory.load(std::memory_order_acquire);
    if (cached < 0) {
        cached = readBoolEnv("BW32_JIT_SAFE_MEMORY", BW32_JIT_DEFAULT_SAFE_MEMORY ? 1 : 0);
        g_bw32JitSafeMemory.store(cached, std::memory_order_release);
    }
    return cached == 1;
}

bool wasmJit32CollectRetiredInstructions() {
    int cached = g_bw32JitCollectRetired.load(std::memory_order_acquire);
    if (cached == 0) {
        cached = readBoolEnv("BW32_JIT_STATS", 0) ? 1 : 2;
        g_bw32JitCollectRetired.store(cached, std::memory_order_release);
        if (cached == 1) {
            klog("BW32 JIT: emitted retired-instruction counters enabled (BW32_JIT_STATS=1)");
        }
    }
    return cached == 1;
}

bool wasmJit32GroupsDisabled() {
    static const bool disabled = readBoolEnv("BW32_JIT_NO_GROUPS", 0) != 0;
    return disabled;
}

U32 wasmJit32HotThreshold() {
    // Latched once: the value decides when a block is handed to the generator,
    // and every reader (run()'s probe gate, the generator trigger, the
    // direct-dispatch chain rule) must see the same number for the whole
    // session. DecodedOp::runCount is a U8 with JIT_RUN_COUNT+1 as the
    // "decided" sentinel and 0xff as "never compile again", so the useful
    // range stops one below the sentinel.
    static const U32 threshold = [] {
        const char* raw = std::getenv("BW32_JIT_HOT");
        if (!raw || !raw[0]) {
            return (U32)JIT_RUN_COUNT;
        }
        char* end = nullptr;
        unsigned long parsed = std::strtoul(raw, &end, 10);
        if (end == raw || parsed == 0 || parsed > 254) {
            klog_fmt("BW32 JIT: ignoring BW32_JIT_HOT=%s (expected 1..254)", raw);
            return (U32)JIT_RUN_COUNT;
        }
        klog_fmt("BW32 JIT: hot threshold %lu (BW32_JIT_HOT)", parsed);
        return (U32)parsed;
    }();
    return threshold;
}

U32 wasmJit32DecidedRunCount() {
    U32 threshold = wasmJit32HotThreshold();
    // runCount is a U8; 0xff is the "never compile this op again" value that
    // firstDynamicOp and the reject paths already use.
    return threshold >= 254 ? 0xff : threshold + 1;
}

bool wasmJit32TrapTraceEnabled() {
    static const bool enabled = readBoolEnv("BW32_JIT_TRAP_TRACE", 0) != 0;
    return enabled;
}

bool wasmJit32TraceEntry(CPU* cpu, U32 tableIndex, const DecodedOp* op) {
    static std::atomic<int> enabled{0};
    static std::atomic<U64> emitted{0};
    static std::atomic<U64> limit{0};
    int cache = enabled.load(std::memory_order_acquire);
    if (cache == 0) {
        cache = readBoolEnv("BW32_JIT_TRACE", 0) ? 1 : 2;
        enabled.store(cache, std::memory_order_release);
        if (cache == 1) {
            const char* raw = std::getenv("BW32_JIT_TRACE_LIMIT");
            U64 parsed = raw && raw[0] ? (U64)std::strtoull(raw, nullptr, 10) : 20000;
            limit.store(parsed ? parsed : 1, std::memory_order_release);
            klog("BW32 JIT: entry trace enabled (BW32_JIT_TRACE=1)");
        } else {
            limit.store(0, std::memory_order_release);
        }
    }
    if (cache != 1 || !cpu) {
        return false;
    }
    if (emitted.fetch_add(1, std::memory_order_relaxed) >= limit.load(std::memory_order_relaxed)) {
        return false;
    }
    klog_fmt("[BW32 JIT entry] slot=%u guestEip=%.8X ops=%u flags2=%x cpu=%p",
        tableIndex, cpu->eip.u32, op ? (U32)op->blockOpCount : 0u,
        op ? (U32)op->flags2 : 0u, (void*)cpu);
    return true;
}

#ifdef BOXEDWINE_MULTI_THREADED
// Guest memory-ordering policy for the multi-threaded JIT (see
// wasmMemoryOrdering.h). Lives here so the interpreter and the codegen share
// one latch; only this translation unit defines the storage.
bool wasmJitNeedsMemoryFence = WASM_JIT_DEFAULT_SAFE_MEMORY;

void wasmJitInitMemoryOrdering(bool safeMemory) {
    static std::once_flag initFlag;
    std::call_once(initFlag, [safeMemory]() {
        // Startup (NormalCPU's constructor) configures this before any guest
        // instruction can run. The static initializer above is only a fallback
        // for embedders that never construct a CPU.
        wasmJitNeedsMemoryFence = safeMemory;
        klog(safeMemory ? "WASM JIT MT safe-memory: on (guest TSO fences emitted)"
                        : "WASM JIT MT safe-memory: off (compiler barriers only)");
    });
}
#endif

#endif // BOXEDWINE_WASM_JIT
