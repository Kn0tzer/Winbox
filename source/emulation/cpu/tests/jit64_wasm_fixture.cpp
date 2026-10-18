// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
// Native fixture for the emitted-module node gate (jit64_wasm_tests.mjs).
//
//   jit64_wasm_fixture <in.bin> <out.wasm> [page0] [page1] [shared]
//
// Decodes the byte stream with the JIT decoder, fills the block's page/slot
// metadata (so the emitted module's inline page-generation check addresses the
// same slot the runtime would), emits the wasm module and writes it out.
//
// Exit codes: 0 = emitted, 2 = the stream itself is not a single JIT block
// (decode/length mismatch, or the emitter refused it), 3 = emitter refused
// while the stream decoded cleanly, 1 = I/O error.
#include "jit64wasm.h"
#include <cstdlib>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 1;
    std::vector<U8> bytes((std::istreambuf_iterator<char>(input)), {});
    Jit64Op ops[24];
    U32 count = jit64CompileStream(0, bytes.data(), (U32)bytes.size(), ops, 24, true);
    U32 consumed = 0;
    for (U32 i = 0; i < count; i++) consumed += ops[i].len;
    if (consumed != bytes.size()) return 2;
    Jit64Block block;
    block.ops.assign(ops, ops + count);
    // Page/slot metadata: the module bakes the slot hash into its inline
    // invalidation check, so the harness must know which slot to poke.
    U64 page0 = argc > 3 ? std::strtoull(argv[3], nullptr, 0) : 0;
    U64 page1 = argc > 4 ? std::strtoull(argv[4], nullptr, 0) : page0;
    block.page0 = page0;
    block.page1 = page1;
    block.slot0 = jit64BlockSlot(page0);
    block.slot1 = jit64BlockSlot(page1);
    Jit64EmitOptions options;
    options.sharedMemory = argc > 5 && argv[5][0] == 's';
    std::vector<U8> module = {0xAA}; // failure must discard any prior module
    if (!jit64EmitWasm(block, options, module)) return module.empty() ? 2 : 3;
    std::ofstream output(argv[2], std::ios::binary);
    output.write((const char*)module.data(), module.size());
    return output ? 0 : 1;
}