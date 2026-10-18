// Exercise the actual emitter's cross-worker counter instruction under Node.
#include "boxedwine.h"
#include "../wasm/wasmEmitter.h"
#include <cstdio>

int main() {
    WasmEmitter emitter;
    auto type = emitter.addFuncType({WasmType::I32}, {});
    emitter.addMemoryImport("env", "memory");
    emitter.addExport("increment", emitter.addFunction(type));
    emitter.beginFunction({});
    emitter.emitLocalGet(0);
    emitter.emitI64Const(1);
    emitter.emitI64AtomicAdd(8);
    emitter.emitOp(WASM_DROP);
    emitter.endFunction();
    auto bytes = emitter.finalize();
    return fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size() ? 0 : 1;
}
