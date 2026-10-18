// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
// Runtime module instantiation: the table-index glue for the wasm emitter.
// Compiled into every build, but only meaningful under __EMSCRIPTEN__: the
// native TU is empty and jit64wasm.h provides inline no-op stubs instead, so
// native harnesses (including the section-GC lifetime test) link without this
// file.
//
// Instantiation is synchronous: WebAssembly.Module + WebAssembly.Instance
// constructors, no ASYNCIFY needed. The emitted module imports only env.memory
// (satisfied with the host's own WebAssembly.Memory) plus the two memory
// helper imports, so the state pointer the module receives is an ordinary
// wasm32 linear-memory address.
//
// Two publication modes, selected once at the first instantiation:
//
//   "table"    (preferred) the emitted execute() is appended to the host's
//              WebAssembly.Table (the JIT build passes -sALLOW_TABLE_GROWTH)
//              and its index is returned. CPU64 calls that index through a
//              C++ function pointer, which compiles to call_indirect on the
//              same table: no JavaScript crossing per block entry.
//   "js-array" fallback used when the table cannot grow. The function is kept
//              in a JS array and reached through an EM_JS call, which costs
//              one JS->wasm transition per block. Correctness is identical;
//              only the call overhead differs. The metrics surface reports
//              which mode is live so a build mistake is visible rather than
//              silently slowing everything down.
//
// Table slots are worker-local (every wasm worker has its own table object),
// which is what jit64WasmOwnerId() tracks: Jit64State records the owner that
// created its indices and drops the cache on a mismatch, so a numeric slot is
// never reused across workers.
#include "jit64wasm.h"

// Guest-X64 targets only. This TU is auto-globbed into every emscripten build,
// so without the guest gate a 32-bit target would compile (and link against)
// CPU64-only symbols such as jit64GlobalStatsJson; the header mirrors this gate
// and hands out inline stubs everywhere else.
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
#include <emscripten.h>
#include <cstring>
#include "boxedwine.h"
#include "kmemory64.h"
#ifdef __EMSCRIPTEN_PTHREADS__
#include <pthread.h>
#endif

// Address space the emitted module's memRead/memWrite imports operate on.
// Set by CPU64::tryWasmExec around the synchronous jit64WasmCall; the module
// cannot re-enter the CPU, so the thread-local is always correct when an
// import runs.
namespace {
thread_local KMemory64* g_jitActiveMem = nullptr;
// 0 = not yet instantiated / unavailable, 1 = wasm table, 2 = JS array.
int g_wasmEngineMode = 0;
bool g_memOptionsKnown = false;
Jit64EmitOptions g_memOptions;
} // namespace

void jit64WasmSetActiveMem(KMemory64* mem) { g_jitActiveMem = mem; }

// Host memory helpers for the string/push/pop/call/ret imports. All
// params/results are 32-bit (no BigInt dependence): addresses and values are
// split to lo/hi, and memRead writes its 64-bit result to 8 bytes at retPtr (a
// wasm32 address, written with memcpy for alignment safety).
extern "C" {
EMSCRIPTEN_KEEPALIVE void jit64MemRead(U32 addrLo, U32 addrHi, U32 size, U32 retPtr) {
    U64 val = 0;
    if (KMemory64* mem = g_jitActiveMem) {
        U64 addr = ((U64)addrHi << 32) | addrLo;
        switch (size) {
            case 1: val = mem->readb(addr); break;
            case 2: val = mem->readw(addr); break;
            case 4: val = mem->readd(addr); break;
            default: val = mem->readq(addr); break;
        }
    }
    memcpy(reinterpret_cast<void*>(retPtr), &val, 8);
}

EMSCRIPTEN_KEEPALIVE void jit64MemWrite(U32 addrLo, U32 addrHi, U32 size, U32 valLo, U32 valHi) {
    if (KMemory64* mem = g_jitActiveMem) {
        U64 addr = ((U64)addrHi << 32) | addrLo;
        U64 val = ((U64)valHi << 32) | valLo;
        switch (size) {
            case 1: mem->writeb(addr, (U8)val); break;
            case 2: mem->writew(addr, (U16)val); break;
            case 4: mem->writed(addr, (U32)val); break;
            default: mem->writeq(addr, val); break;
        }
    }
}
} // extern "C"

// Probe once whether the host wasm table can grow. When it can, reserve the
// probe slot so the first emitted function reuses it instead of growing again.
EM_JS(int, jit64_wasm_table_probe_impl, (), {
    try {
        if (typeof wasmTable === 'undefined' || !wasmTable ||
            typeof wasmTable.grow !== 'function') return 0;
        var before = wasmTable.length;
        wasmTable.grow(1);
        if (wasmTable.length !== before + 1) return 0;
        if (!Module.jit64WasmTableFree) Module.jit64WasmTableFree = [];
        Module.jit64WasmTableFree.push(before);
        return 1;
    } catch (e) {
        return 0;
    }
});

// Instantiate one emitted module and publish its execute().
// Returns a wasm table index (>= 0), -(2 + arraySlot) for the JS-array
// fallback, or -1 when the module cannot be instantiated at all.
EM_JS(int, jit64_wasm_instantiate_impl, (const unsigned char* bytes, unsigned size, int useTable), {
    try {
        // Copy: the caller's vector may be freed right after this returns.
        var copy = new Uint8Array(wasmMemory.buffer, bytes, size).slice();
        var module = new WebAssembly.Module(copy);
        var instance = new WebAssembly.Instance(module, { env: {
            memory: wasmMemory,
            // Memory helpers. All i32 params (addresses/values split lo/hi);
            // memRead writes 8 bytes to retPtr.
            memRead: function(addrLo, addrHi, sz, retPtr) {
                Module._jit64MemRead(addrLo, addrHi, sz, retPtr);
            },
            memWrite: function(addrLo, addrHi, sz, valLo, valHi) {
                Module._jit64MemWrite(addrLo, addrHi, sz, valLo, valHi);
            },
        } });
        var fn = instance.exports.execute;
        if (typeof fn !== 'function') return -1;
        if (useTable) {
            if (!Module.jit64WasmTableFree) Module.jit64WasmTableFree = [];
            if (Module.jit64WasmTableFree.length) {
                var reused = Module.jit64WasmTableFree.pop();
                wasmTable.set(reused, fn);
                return reused;
            }
            var idx = wasmTable.length;
            wasmTable.grow(1);
            wasmTable.set(idx, fn);
            return idx;
        }
        if (!Module.jit64WasmTable) Module.jit64WasmTable = [];
        if (!Module.jit64WasmJsFree) Module.jit64WasmJsFree = [];
        if (Module.jit64WasmJsFree.length) {
            var slot = Module.jit64WasmJsFree.pop();
            Module.jit64WasmTable[slot] = fn;
            return -(2 + slot);
        }
        Module.jit64WasmTable.push(fn);
        return -(2 + Module.jit64WasmTable.length - 1);
    } catch (e) {
        return -1;
    }
});

// JS-array fallback call path (used only in js-array mode).
EM_JS(void, jit64_wasm_call_impl, (int index, unsigned statePtr), {
    var slot = -2 - index;
    var fn = Module.jit64WasmTable[slot];
    if (typeof fn !== 'function') throw new Error('jit64: bad wasm js slot ' + slot);
    fn(statePtr);
});

EM_JS(void, jit64_wasm_table_release_impl, (int index), {
    if (index >= 0 && index < wasmTable.length && wasmTable.get(index)) {
        wasmTable.set(index, null);
        if (!Module.jit64WasmTableFree) Module.jit64WasmTableFree = [];
        Module.jit64WasmTableFree.push(index);
    }
});

EM_JS(void, jit64_wasm_js_release_impl, (int index), {
    var slot = -2 - index;
    if (Module.jit64WasmTable && Module.jit64WasmTable[slot]) {
        Module.jit64WasmTable[slot] = null;
        if (!Module.jit64WasmJsFree) Module.jit64WasmJsFree = [];
        Module.jit64WasmJsFree.push(slot);
    }
});

// True when the host linear memory is a SharedArrayBuffer-backed (pthreads)
// memory. Shared memories can only be satisfied by a shared import, and the
// opposite mismatch is also fatal, so the emitter has to know (see the import
// section comment in jit64wasm.cpp).
EM_JS(int, jit64_wasm_mem_shared_impl, (), {
    try {
        return (wasmMemory.buffer instanceof SharedArrayBuffer) ? 1 : 0;
    } catch (e) {
        return 0;
    }
});

int jit64WasmInstantiate(const U8* bytes, U32 byteCount, const Jit64EmitOptions& options) {
    if (!bytes || !byteCount) return -1;
    // The emitted module's memory import must match the host memory's type
    // (shared vs plain) or instantiation fails with a LinkError. Bail out on
    // a caller that emitted with the wrong descriptor instead of paying a
    // guaranteed module compile + throw.
    if (options.sharedMemory != jit64WasmMemoryOptions().sharedMemory) return -1;
    if (g_wasmEngineMode == 0) {
        g_wasmEngineMode = jit64_wasm_table_probe_impl() ? 1 : 2;
    }
    int index = jit64_wasm_instantiate_impl(bytes, (unsigned)byteCount, g_wasmEngineMode == 1 ? 1 : 0);
    if (index >= 0) g_wasmEngineMode = 1;
    else if (g_wasmEngineMode != 1) g_wasmEngineMode = 2;
    return index;
}

void jit64WasmCall(int tableIndex, Jit64WasmState* state) {
    if (tableIndex >= 0) {
        // A wasm table index used as a function pointer: with
        // -sALLOW_TABLE_GROWTH, function pointer values ARE table indices, so
        // this compiles to call_indirect on the host table (no JS per call).
        reinterpret_cast<void (*)(U32)>(tableIndex)((U32)(uintptr_t)state);
    } else if (tableIndex <= -2) {
        jit64_wasm_call_impl(tableIndex, (unsigned)(uintptr_t)state);
    }
}

void jit64WasmRelease(int tableIndex) {
    if (tableIndex >= 0) jit64_wasm_table_release_impl(tableIndex);
    else if (tableIndex <= -2) jit64_wasm_js_release_impl(tableIndex);
}

U32 jit64WasmOwnerId() {
#ifdef __EMSCRIPTEN_PTHREADS__
    return (U32)(uintptr_t)pthread_self();
#else
    return 0;
#endif
}

bool jit64WasmHostAbiOk() {
    return sizeof(void*) == 4 &&
           sizeof(Jit64WasmState) == 176 &&
           offsetof(Jit64WasmState, rip) == 128 &&
           offsetof(Jit64WasmState, genBase) == 152 &&
           offsetof(Jit64WasmState, limit) == 168;
}

bool jit64WasmAvailable() {
#ifdef BOXEDWINE_BLOCK_CACHE_INFRA
    // The inline invalidation checks read KMemory64's shared page-generation
    // table, which only exists with the block-cache infrastructure compiled in.
    return jit64WasmHostAbiOk();
#else
    return false;
#endif
}

const char* jit64WasmEngineMode() {
    if (!jit64WasmAvailable()) return "unavailable";
    switch (g_wasmEngineMode) {
    case 1: return "table";
    case 2: return "js-array";
    default: return "idle";
    }
}

Jit64EmitOptions jit64WasmMemoryOptions() {
    if (!g_memOptionsKnown) {
        g_memOptions.sharedMemory = jit64_wasm_mem_shared_impl() != 0;
        g_memOptionsKnown = true;
    }
    return g_memOptions;
}

// Metrics bridge for the live panel (docs/reports/winbox-jit64-metrics.md):
// writes the process-wide CPU64 JIT aggregate as JSON and returns its length,
// or 0 when the buffer is too small / unavailable. Read-only; safe to call at
// any time from the viewer thread.
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_jit_stats(char* buf, int size) {
    if (!buf || size <= 0) return 0;
    return (int)jit64GlobalStatsJson(buf, (U32)size);
}

#endif // __EMSCRIPTEN__ && BOXEDWINE_GUEST_X64