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
// Generated exports use the host WebAssembly.Table and call_indirect, without
// a JavaScript crossing on block entry. ALLOW_TABLE_GROWTH is required; if a
// reservation/instantiation fails, CPU64 retains its interpreter fallback.
// The slot namespace is shared with the guest32 backend.
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
#include "cpu64.h" // full CPU64 for the K-M3 fault flag (memoryFaultPending)
#include "bwjittable.h"
#ifdef __EMSCRIPTEN_PTHREADS__
#include <pthread.h>
#endif

// Address space the emitted module's memRead/memWrite imports operate on.
// Set by CPU64::tryWasmExec around the synchronous jit64WasmCall; the module
// cannot re-enter the CPU, so the thread-local is always correct when an
// import runs.
namespace {
thread_local KMemory64* g_jitActiveMem = nullptr;
// K-M3: the CPU whose guest-access context the accessors enforce, and the
// staged state the imports flag when an access is denied. The emitted
// module reads Jit64WasmState::faultPending after every import call and
// stops at the faulting op; once the flag is set, later imports in the
// same call are suppressed so the faulting op cannot complete a second
// access on placeholder values (mirroring the interpreter, whose later
// accesses in the faulting instruction are dropped by the same gate).
thread_local CPU64* g_jitActiveCpu = nullptr;
thread_local Jit64WasmState* g_jitActiveState = nullptr;

void jit64NoteAccessFault() {
    if (g_jitActiveCpu && g_jitActiveCpu->memoryFaultPending && g_jitActiveState)
        g_jitActiveState->faultPending = 1;
}
bool jit64AccessSuppressed() {
    return g_jitActiveState && g_jitActiveState->faultPending;
}
// 0 = not yet instantiated / unavailable, 1 = wasm table.
thread_local int g_wasmEngineMode = 0;
thread_local bool g_memOptionsKnown = false;
thread_local Jit64EmitOptions g_memOptions;
} // namespace

void jit64WasmSetActive(KMemory64* mem, CPU64* cpu, Jit64WasmState* state) {
    g_jitActiveMem = mem;
    g_jitActiveCpu = cpu;
    g_jitActiveState = state;
}

// Host memory helpers for the string/push/pop/call/ret imports. All
// params/results are 32-bit (no BigInt dependence): addresses and values are
// split to lo/hi, and memRead writes its 64-bit result to 8 bytes at retPtr (a
// wasm32 address, written with memcpy for alignment safety).
extern "C" {
EMSCRIPTEN_KEEPALIVE void jit64MemRead(U32 addrLo, U32 addrHi, U32 size, U32 retPtr) {
    U64 val = 0;
    if (!jit64AccessSuppressed()) {
        if (KMemory64* mem = g_jitActiveMem) {
            U64 addr = ((U64)addrHi << 32) | addrLo;
            switch (size) {
                case 1: val = mem->readb(addr); break;
                case 2: val = mem->readw(addr); break;
                case 4: val = mem->readd(addr); break;
                default: val = mem->readq(addr); break;
            }
            // The accessors run under the guest-access context the host
            // armed in execWasmBlock: a denied access has just recorded a
            // fault on the active CPU. Surface it to the emitted module.
            jit64NoteAccessFault();
        }
    }
    memcpy(reinterpret_cast<void*>(retPtr), &val, 8);
}

EMSCRIPTEN_KEEPALIVE void jit64MemWrite(U32 addrLo, U32 addrHi, U32 size, U32 valLo, U32 valHi) {
    if (jit64AccessSuppressed()) return;
    if (KMemory64* mem = g_jitActiveMem) {
        U64 addr = ((U64)addrHi << 32) | addrLo;
        U64 val = ((U64)valHi << 32) | valLo;
        switch (size) {
            case 1: mem->writeb(addr, (U8)val); break;
            case 2: mem->writew(addr, (U16)val); break;
            case 4: mem->writed(addr, (U32)val); break;
            default: mem->writeq(addr, val); break;
        }
        jit64NoteAccessFault();
    }
}
} // extern "C"

// Free indices belong to this worker and were reserved from the shared
// guest32/guest64 namespace. Never allocate from wasmTable.length directly.
EM_JS(int, jit64_wasm_take_free_slot_impl, (), {
    var free = Module.jit64WasmTableFree;
    return free && free.length ? free.pop() : -1;
});

// Instantiate one emitted module and publish its execute().
// Returns the reserved index, or -1 when the module cannot be instantiated.
EM_JS(int, jit64_wasm_instantiate_impl,
      (const unsigned char* bytes, unsigned size, int slot, U32 readIndex, U32 writeIndex), {
    bytes >>>= 0;
    size >>>= 0;
    try {
        // Copy: the caller's vector may be freed right after this returns.
        var copy = new Uint8Array(wasmMemory.buffer, bytes, size).slice();
        var module = new WebAssembly.Module(copy);
        var instance = new WebAssembly.Instance(module, { env: {
            memory: wasmMemory,
            // Memory helpers. All i32 params (addresses/values split lo/hi);
            // memRead writes 8 bytes to retPtr.
            // Actual Wasm imports preserve exceptions and avoid a JavaScript
            // crossing for each guest memory operand.
            memRead: wasmTable.get(readIndex),
            memWrite: wasmTable.get(writeIndex),
        } });
        var fn = instance.exports.execute;
        if (typeof fn !== 'function') return -1;
        wasmTable.set(slot, fn);
        return slot;
    } catch (e) {
        return -1;
    }
});

EM_JS(void, jit64_wasm_table_release_impl, (int index), {
    if (index >= 0 && index < wasmTable.length) {
        wasmTable.set(index, null);
        if (!Module.jit64WasmTableFree) Module.jit64WasmTableFree = [];
        Module.jit64WasmTableFree.push(index);
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
    int slot = jit64_wasm_take_free_slot_impl();
    if (slot < 0) slot = bwJitReserveTableSlots(1);
    if (slot < 0) return -1;
    int index = jit64_wasm_instantiate_impl(bytes, (unsigned)byteCount, slot,
        (U32)(uintptr_t)&jit64MemRead, (U32)(uintptr_t)&jit64MemWrite);
    if (index < 0) jit64_wasm_table_release_impl(slot);
    else g_wasmEngineMode = 1;
    return index;
}

void jit64WasmCall(int tableIndex, Jit64WasmState* state) {
    if (tableIndex >= 0) {
        // A wasm table index used as a function pointer: with
        // -sALLOW_TABLE_GROWTH, function pointer values ARE table indices, so
        // this compiles to call_indirect on the host table (no JS per call).
        reinterpret_cast<void (*)(U32)>(tableIndex)((U32)(uintptr_t)state);
    }
}

void jit64WasmRelease(int tableIndex) {
    if (tableIndex >= 0) jit64_wasm_table_release_impl(tableIndex);
}

U32 jit64WasmOwnerId() {
#ifdef __EMSCRIPTEN_PTHREADS__
    return (U32)(uintptr_t)pthread_self();
#else
    return 0;
#endif
}

bool jit64WasmHostAbiOk() {
    // The size pin covers the whole staged state, including the XMM
    // staging area appended at offset 176 (256 bytes): 176 + 256 = 432.
    // include/jit64wasm.h static_asserts the same value; grow both together.
    return sizeof(void*) == 4 &&
           sizeof(Jit64WasmState) == 432 &&
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

#ifndef BOXEDWINE_WASM_JIT
// The combined build provides the same convenience entry points in the
// guest32 statistics TU; single-engine targets still expose both sections.
extern "C" EMSCRIPTEN_KEEPALIVE int bw64_jit_stats_json(char* buf, int size) {
    return bw64_jit_stats(buf, size);
}
extern "C" EMSCRIPTEN_KEEPALIVE const char* bw64_jit_stats_json_text() {
    static thread_local char text[3072];
    if (!jit64GlobalStatsJson(text, sizeof(text))) text[0] = 0;
    return text;
}
#endif

#endif // __EMSCRIPTEN__ && BOXEDWINE_GUEST_X64
