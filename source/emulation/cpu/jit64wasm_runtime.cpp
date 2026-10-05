// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
// Runtime module instantiation: the table-index glue for the phase-2 wasm
// emitter. Compiled into every build, but only meaningful under
// __EMSCRIPTEN__: the native TU is empty and jit64wasm.h provides inline
// no-op stubs instead, so native harnesses (including the section-GC
// lifetime test) link without this file.
//
// Instantiation is synchronous: WebAssembly.Module + WebAssembly.Instance
// constructors, no ASYNCIFY needed. The emitted module imports only
// env.memory, which is satisfied with the host's own WebAssembly.Memory, so
// the state pointer the module receives is an ordinary wasm32 linear-memory
// address. Table slots are reused on release (free-list); a failed
// instantiation returns -1 and leaves no slot behind.
#include "jit64wasm.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(int, jit64_wasm_instantiate_impl, (const unsigned char* bytes, unsigned size), {
    try {
        if (!Module.jit64WasmTable) Module.jit64WasmTable = [];
        // Copy: the caller's vector may be freed right after this returns.
        var copy = new Uint8Array(wasmMemory.buffer, bytes, size).slice();
        var module = new WebAssembly.Module(copy);
        var instance = new WebAssembly.Instance(module, { env: { memory: wasmMemory } });
        var fn = instance.exports.execute;
        if (typeof fn !== 'function') return -1;
        var table = Module.jit64WasmTable;
        if (Module.jit64WasmFree && Module.jit64WasmFree.length) {
            var reused = Module.jit64WasmFree.pop();
            table[reused] = fn;
            return reused;
        }
        table.push(fn);
        return table.length - 1;
    } catch (e) {
        return -1;
    }
});

EM_JS(void, jit64_wasm_call_impl, (int index, unsigned statePtr), {
    var fn = Module.jit64WasmTable[index];
    if (typeof fn !== 'function') throw new Error('jit64: bad wasm table index ' + index);
    fn(statePtr);
});

EM_JS(void, jit64_wasm_release_impl, (int index), {
    var table = Module.jit64WasmTable;
    if (table && index >= 0 && index < table.length && table[index]) {
        table[index] = null;
        if (!Module.jit64WasmFree) Module.jit64WasmFree = [];
        Module.jit64WasmFree.push(index);
    }
});

int jit64WasmInstantiate(const U8* bytes, U32 byteCount) {
    if (!bytes || !byteCount) return -1;
    return jit64_wasm_instantiate_impl(bytes, (unsigned)byteCount);
}

void jit64WasmCall(int tableIndex, U32 statePtr) {
    jit64_wasm_call_impl(tableIndex, (unsigned)statePtr);
}

void jit64WasmRelease(int tableIndex) {
    jit64_wasm_release_impl(tableIndex);
}

#endif // __EMSCRIPTEN__
