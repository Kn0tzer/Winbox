#!/usr/bin/env bash
# scripts/build-jit-wasm.sh — Phase-1 JIT wasm build driver.
#
# STATUS: PREPARED, NOT RUN. This host has no Emscripten SDK, so this script
# has never been executed. Run it on a host with emsdk activated.
#
# When run, it:
#   1. Builds + runs the NATIVE phase-1 unit tests (g++ -std=c++17) as a gate.
#      Any failure aborts before touching emcc.
#   2. Builds the wasm64-selftest target (project/emscripten/makefile), which
#      compiles with -DBOXEDWINE_GUEST_X64=1. source/emulation/cpu/jit64.cpp is
#      auto-globbed into SOURCES (BOXEDWINE_GUEST_X64-gated, so it is empty in
#      the 32-bit targets); source/emulation/cpu/tests/ is pruned from the
#      glob because the native harness has its own main().
#   3. Smoke-runs the result under node twice — BW64_JIT unset (interpreter
#      baseline) and BW64_JIT=1 (JIT fast path) — and diffs the outputs. Phase
#      1 executes JIT-compiled blocks via the interpreter's execBlock, so the
#      outputs must be byte-identical; any difference is a JIT bug.
#
# Phase 2 will extend this script: wasm opcode emitter build, module
# instantiation glue (boxedwine_wasm_instantiate_runtime_batch), and the
# node --x64-run-elf fixture with timing comparison.
#
# Usage: ./scripts/build-jit-wasm.sh [--selftest-only]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TMP="${BUILD_TMP:-/tmp/bw64-jit-build}"
SELFTEST_ONLY="${1:-}"

echo "=== [1/3] native phase-1 unit tests (g++ gate) ==="
mkdir -p "$BUILD_TMP"
g++ -std=c++17 -Wall -Wextra -DBOXEDWINE_GUEST_X64=1 \
    -I"$REPO/include" \
    "$REPO/source/emulation/cpu/jit64.cpp" \
    "$REPO/source/emulation/cpu/tests/jit64_tests.cpp" \
    -o "$BUILD_TMP/jit64_tests"
"$BUILD_TMP/jit64_tests"

if [ -n "$SELFTEST_ONLY" ]; then
    echo "selftest-only: skipping emcc build."
    exit 0
fi

echo "=== [2/3] wasm64-selftest build (emcc) ==="
command -v emcc >/dev/null || {
    echo "ERROR: emcc not found. Activate the Emscripten SDK first (emsdk_env.sh)." >&2
    exit 1
}
command -v node >/dev/null || { echo "ERROR: node not found." >&2; exit 1; }
make -C "$REPO/project/emscripten" wasm64-selftest
JS="$REPO/project/emscripten/Build/Wasm64SelfTest/boxedwine64-selftest.js"
[ -f "$JS" ] || { echo "ERROR: expected output $JS not produced." >&2; exit 1; }

echo "=== [3/3] smoke run: interpreter vs BW64_JIT=1 (outputs must match) ==="
node "$JS" --x64-selftest > "$BUILD_TMP/selftest_interp.log" 2>&1
BW64_JIT=1 node "$JS" --x64-selftest > "$BUILD_TMP/selftest_jit.log" 2>&1
if diff -u "$BUILD_TMP/selftest_interp.log" "$BUILD_TMP/selftest_jit.log"; then
    echo "OK: BW64_JIT=1 output identical to interpreter baseline."
else
    echo "FAIL: JIT output differs from interpreter baseline (see diff above)." >&2
    exit 1
fi
echo "build-jit-wasm.sh: all gates passed."
