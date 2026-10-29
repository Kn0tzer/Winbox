#!/usr/bin/env bash
# scripts/build-jit-wasm.sh — decoder/cache, emitted-module, and runtime correctness gates.
#
# Activate an Emscripten SDK before running the wasm gates.
#
# When run, it:
#   1. Runs native decoder/cache tests and emitted wasm modules under Node.
#      Any failure aborts before touching emcc.
#   2. Builds the wasm64-selftest recipe (project/emscripten/makefile), which
#      compiles with GUEST_X64 and BLOCK_EXEC enabled in an isolated directory.
#      source/emulation/cpu/jit64.cpp is
#      auto-globbed into SOURCES (BOXEDWINE_GUEST_X64-gated, so it is empty in
#      the 32-bit targets); source/emulation/cpu/tests/ is pruned from the
#      glob because the native harness has its own main().
#   3. Smoke-runs the result under node twice — BW64_JIT unset (interpreter
#      baseline) and BW64_JIT=1 (emitted-module path) — and diffs the outputs.
#      The generated modules report their own entries and retired instructions;
#      the gate rejects decoder-only or interpreter-block execution.
#
#   4. Runs the genuine 64-bit ELF fixture (tools/guest-fixtures/jitbench64.c,
#      built for x86-64) through the wasm64-runelf target twice — interpreter
#      (BW64_JIT=0) and emitted modules (BW64_JIT=1) — and requires the same
#      checksum from both, plus non-zero EMITTED-CODE counters (the modules
#      report their own entry/retired counts through Jit64WasmState). When a
#      qemu-x86_64 binfmt handler is present the same binary is also run
#      natively as an independent x86-64 reference.
#
# Usage: ./scripts/build-jit-wasm.sh [--selftest-only]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TMP="${BUILD_TMP:-/tmp/bw64-jit-build}"
SELFTEST_ONLY="${1:-}"
JOBS="${JOBS:-2}"
case "$SELFTEST_ONLY" in
    ''|--selftest-only) ;;
    *) echo "Usage: $0 [--selftest-only]" >&2; exit 2 ;;
esac
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: JOBS must be positive." >&2; exit 2; }

echo "=== [1/5] native phase-1 unit tests (g++ gate) ==="
mkdir -p "$BUILD_TMP"
BUILD_TMP="$BUILD_TMP/lifetime" bash "$REPO/scripts/test-jit-lifetime.sh"
g++ -std=c++17 -Wall -Wextra -DBOXEDWINE_GUEST_X64=1 \
    -I"$REPO/include" \
    "$REPO/source/emulation/cpu/jit64.cpp" \
    "$REPO/source/emulation/cpu/jit64wasm.cpp" \
    "$REPO/source/emulation/cpu/tests/jit64_tests.cpp" \
    -o "$BUILD_TMP/jit64_tests"
"$BUILD_TMP/jit64_tests"
# Byte-identical migration gate: the table-generated emitters must produce
# byte-identical wasm output to the hand-written ones they replace
# (see source/emulation/cpu/jit64table.mjs). The generated .inc must also
# be in sync with the table.
node "$REPO/scripts/gen-jit64.mjs" --check
g++ -std=c++17 -Wall -Wextra -DBOXEDWINE_GUEST_X64=1 -DJIT64_GEN_VERIFY=1 \
    -I"$REPO/include" \
    "$REPO/source/emulation/cpu/jit64.cpp" \
    "$REPO/source/emulation/cpu/jit64wasm.cpp" \
    "$REPO/source/emulation/cpu/tests/jit64_gen_tests.cpp" \
    -o "$BUILD_TMP/jit64_gen_tests"
"$BUILD_TMP/jit64_gen_tests"
# Plan section 14.3: block-overlap invalidation. Caches a block, rewrites
# guest memory inside / overlapping / across its range (plus a within-block
# self-modifying store and a cross-page block), and compares block execution
# against a separately compiled instruction interpreter. A sensitivity
# check (noteGuestWrite neutered) was verified to fail T1/T3/T4, so this gate
# is not vacuous.
echo "--- jit64 block-invalidation tests (section 14.3) ---"
# NOTE: -ffunction-sections/-fdata-sections are load-bearing here: fpu.cpp
# also implements 32-bit-KMemory FPU helpers (FSTENV/FRSTOR/...) that must be
# dead-stripped by --gc-sections at link time.
# cpu64.cpp's control-trace calls link ripSampler.cpp directly (in this
# non-multi-threaded configuration it provides the product's inert stubs).
# Its bw64Recover* calls are defined by the test harness instead: they live
# in source/util/log.cpp, whose logging core (klog/kpanic/KSystem::logFile)
# the standalone harness deliberately replaces with its own definitions.
g++ -std=c++20 -DBOXEDWINE_GUEST_X64=1 -ffunction-sections -fdata-sections -c \
    -I"$REPO/include" -I"$REPO/lib/simde" \
    "$REPO/source/emulation/cpu/common/fpu.cpp" -o "$BUILD_TMP/inv_fpu.o"
mkdir -p "$BUILD_TMP/softfloat"
for sf in "$REPO"/lib/softfloat/source/*.c "$REPO"/lib/softfloat/source/8086-SSE/*.c; do
    obj="$BUILD_TMP/softfloat/$(basename "$sf" .c).o"
    [ -f "$obj" ] || gcc -O1 -c -I"$REPO/lib/softfloat/source/include" \
        -I"$REPO/lib/softfloat/source" -I"$REPO/lib/softfloat/source/8086-SSE" \
        "$sf" -o "$obj"
done
for mode in interp blocks; do
    INV_FLAGS=()
    if [[ "$mode" == blocks ]]; then INV_FLAGS=(-DBOXEDWINE_BLOCK_EXEC=1); fi
    g++ -std=c++20 -Wall -Wextra -DBOXEDWINE_GUEST_X64=1 "${INV_FLAGS[@]}" \
        -DBOXEDWINE_BLOCK_CACHE_INFRA=1 -ffunction-sections -fdata-sections \
        -I"$REPO/include" -I"$REPO/lib/simde" -I"$REPO/lib/softfloat/source/include" \
        "$REPO/source/emulation/cpu/cpu64.cpp" \
        "$REPO/source/emulation/cpu/jit64.cpp" \
        "$REPO/source/emulation/cpu/jit64wasm.cpp" \
        "$REPO/source/emulation/cpu/common/common_lock.cpp" \
        "$REPO/source/emulation/cpu/normal/ripSampler.cpp" \
        "$REPO/source/kernel/kmemory64.cpp" \
        "$REPO/source/util/bstring.cpp" \
        "$BUILD_TMP/inv_fpu.o" "$BUILD_TMP"/softfloat/*.o \
        "$REPO/source/emulation/cpu/tests/jit64_invalidate_tests.cpp" \
        -Wl,--gc-sections -o "$BUILD_TMP/jit64_invalidate_$mode" > "$BUILD_TMP/inv_build_$mode.log" 2>&1 || \
        { echo "FAIL: $mode invalidate-test build; see $BUILD_TMP/inv_build_$mode.log" >&2; exit 1; }
    "$BUILD_TMP/jit64_invalidate_$mode" > "$BUILD_TMP/inv_$mode.log"
done
cmp -s "$BUILD_TMP/inv_interp.log" "$BUILD_TMP/inv_blocks.log" || { echo "FAIL: block invalidation differs from the instruction interpreter" >&2; exit 1; }
echo "OK: block-invalidation tests match the independent instruction interpreter."
command -v node >/dev/null || { echo "ERROR: node not found." >&2; exit 1; }
g++ -std=c++17 -Wall -Wextra -DBOXEDWINE_GUEST_X64=1 \
    -I"$REPO/include" \
    "$REPO/source/emulation/cpu/jit64.cpp" \
    "$REPO/source/emulation/cpu/jit64wasm.cpp" \
    "$REPO/source/emulation/cpu/tests/jit64_wasm_fixture.cpp" \
    -o "$BUILD_TMP/jit64_wasm_fixture"
JIT64_WASM_FIXTURE="$BUILD_TMP/jit64_wasm_fixture" node --test \
    "$REPO/source/emulation/cpu/tests/jit64_wasm_tests.mjs"

if [ -n "$SELFTEST_ONLY" ]; then
    echo "selftest-only: skipping emcc build."
    exit 0
fi

echo "=== [2/5] wasm64-selftest build (emcc) ==="
command -v emcc >/dev/null || {
    echo "ERROR: emcc not found. Activate the Emscripten SDK first (emsdk_env.sh)." >&2
    exit 1
}
# Command-line MAKEFLAGS wins over the makefile's automatic CPU-count setting.
# Separate objects prevent stale files compiled without BLOCK_EXEC from passing.
make -C "$REPO/project/emscripten" Build/Wasm64JitSelfTest/boxedwine64-selftest.js MAKEFLAGS="-j$JOBS" \
    BUILD_DIR=Build/Wasm64JitSelfTest \
    TARGET_EXEC=boxedwine64-selftest.js \
    EXTRA_LD_FLAGS='-sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_TABLE_GROWTH=1' \
    EXTRA_CPP_FLAGS='-DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_BLOCK_EXEC=1 -DBOXEDWINE_BLOCK_CACHE_INFRA=1'
JS="$REPO/project/emscripten/Build/Wasm64JitSelfTest/boxedwine64-selftest.js"
[ -f "$JS" ] || { echo "ERROR: expected output $JS not produced." >&2; exit 1; }
# The repository is type=module, but Emscripten's Node shell is CommonJS.
# Keep the copy alongside the wasm so its __dirname asset lookup still works.
JS_CJS="${JS%.js}.cjs"
# Emscripten 6 does not mirror Node's environment unless NODE_HOST_ENV is set.
# Forward only these two test controls before the guest calls getenv().
{
    printf '%s\n' 'var Module = { preRun: function() { ENV.BW64_JIT = process.env.BW64_JIT || "0"; ENV.BW64_JIT_VERIFY = process.env.BW64_JIT_VERIFY || "0"; ENV.BW64_JIT_ENGINE = process.env.BW64_JIT_ENGINE || ""; ENV.BW64_JIT_HOT = process.env.BW64_JIT_HOT || ""; } };'
    cat "$JS"
} > "$JS_CJS"

echo "=== [3/5] smoke run: interpreter vs BW64_JIT=1 (outputs must match) ==="
if ! BW64_JIT=0 BW64_JIT_VERIFY=0 node "$JS_CJS" --x64-selftest > "$BUILD_TMP/selftest_interp.log" 2>&1; then
    echo "FAIL: interpreter self-test; see $BUILD_TMP/selftest_interp.log" >&2
    exit 1
fi
if ! BW64_JIT=1 BW64_JIT_VERIFY=1 BW64_JIT_HOT=0 node "$JS_CJS" --x64-selftest > "$BUILD_TMP/selftest_jit.log" 2>&1; then
    echo "FAIL: JIT self-test; see $BUILD_TMP/selftest_jit.log" >&2
    exit 1
fi
# Emitted-execution evidence: engine=wasm plus non-zero EMITTED-CODE counters
# (Jit64WasmState::blocksEntered / execCount, written by the generated modules
# themselves) and equality between the module-reported entry count and the
# host's call count.
grep -E '^JIT64: engine=wasm blocks=[1-9][0-9]* insns=[1-9][0-9]*$' "$BUILD_TMP/selftest_jit.log"
grep -E '^WASM64: blocks=[1-9][0-9]* insns=[1-9][0-9]* entered=[1-9][0-9]* ' "$BUILD_TMP/selftest_jit.log"
sed -e '/^JIT64:/d' -e '/^WASM64:/d' -e '/^BW64JITSTATS:/d' -e '/JIT64-CASE:/d' "$BUILD_TMP/selftest_jit.log" > "$BUILD_TMP/selftest_jit_results.log"
if diff -u "$BUILD_TMP/selftest_interp.log" "$BUILD_TMP/selftest_jit_results.log"; then
    echo "OK: BW64_JIT=1 output identical to interpreter baseline."
else
    echo "FAIL: JIT output differs from interpreter baseline (see diff above)." >&2
    exit 1
fi
echo "=== [4/5] genuine 64-bit ELF fixture (interpreter vs emitted modules) ==="
FIXTURE_SRC="$REPO/tools/guest-fixtures/jitbench64.c"
ELF="$BUILD_TMP/jitbench64.elf"
if command -v x86_64-linux-gnu-gcc >/dev/null; then
    x86_64-linux-gnu-gcc -O2 -fno-tree-vectorize -static -nostdlib -fno-builtin \
        -fno-stack-protector -fno-pie -no-pie -DJITBENCH_FREESTANDING \
        -o "$ELF" "$FIXTURE_SRC"
else
    echo "WARN: x86_64-linux-gnu-gcc not found; skipping the ELF fixture gate." >&2
    echo "build-jit-wasm.sh: all runnable gates passed (ELF fixture skipped)."
    exit 0
fi
checksum_of() { grep -o 'checksum=[0-9a-f]*' "$1" | head -1; }
# Independent x86-64 reference when a qemu binfmt handler is registered.
NATIVE_SUM=""
if [ -e /proc/sys/fs/binfmt_misc/qemu-x86_64 ] || [ "$(uname -m)" = x86_64 ]; then
    "$ELF" > "$BUILD_TMP/fixture_native.log" 2>&1 || true
    NATIVE_SUM="$(checksum_of "$BUILD_TMP/fixture_native.log")"
fi

make -C "$REPO/project/emscripten" Build/Wasm64JitRunElf/boxedwine64-runelf.js MAKEFLAGS="-j$JOBS" \
    BUILD_DIR=Build/Wasm64JitRunElf \
    TARGET_EXEC=boxedwine64-runelf.js \
    EXTRA_LD_FLAGS='-sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_TABLE_GROWTH=1 -sNODERAWFS=1' \
    EXTRA_CPP_FLAGS='-DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_BLOCK_EXEC=1 -DBOXEDWINE_BLOCK_CACHE_INFRA=1'
RUNJS="$REPO/project/emscripten/Build/Wasm64JitRunElf/boxedwine64-runelf.cjs"
[ -f "${RUNJS%.cjs}.js" ] || { echo "ERROR: run-elf target produced no JS." >&2; exit 1; }
{
    printf '%s\n' 'var Module = { preRun: function() { ENV.BW64_JIT = process.env.BW64_JIT || "0"; ENV.BW64_JIT_VERIFY = process.env.BW64_JIT_VERIFY || "0"; ENV.BW64_JIT_ENGINE = process.env.BW64_JIT_ENGINE || ""; ENV.BW64_JIT_HOT = process.env.BW64_JIT_HOT || ""; } };'
    cat "${RUNJS%.cjs}.js"
} > "$RUNJS"

BW64_JIT=0 BW64_JIT_HOT=0 node "$RUNJS" --x64-run-elf "$ELF" > "$BUILD_TMP/fixture_interp.log" 2>&1 || true
BW64_JIT=1 BW64_JIT_VERIFY=1 BW64_JIT_HOT=0 node "$RUNJS" --x64-run-elf "$ELF" > "$BUILD_TMP/fixture_jit.log" 2>&1 || true
INTERP_SUM="$(checksum_of "$BUILD_TMP/fixture_interp.log")"
JIT_SUM="$(checksum_of "$BUILD_TMP/fixture_jit.log")"
[ -n "$INTERP_SUM" ] || { echo "FAIL: interpreter run produced no checksum; see $BUILD_TMP/fixture_interp.log" >&2; tail -20 "$BUILD_TMP/fixture_interp.log" >&2; exit 1; }
[ "$INTERP_SUM" = "$JIT_SUM" ] || {
    echo "FAIL: JIT checksum $JIT_SUM != interpreter $INTERP_SUM (bit-exactness gate)" >&2
    exit 1
}
if [ -n "$NATIVE_SUM" ]; then
    [ "$NATIVE_SUM" = "$INTERP_SUM" ] || {
        echo "FAIL: native/qemu checksum $NATIVE_SUM != interpreter $INTERP_SUM" >&2
        exit 1
    }
fi
grep -E '^WASM64: engine=wasm blocks=[1-9][0-9]* insns=[1-9][0-9]* entered=[1-9][0-9]* ' "$BUILD_TMP/fixture_jit.log"
# The emitted modules must be reached through the wasm TABLE (call_indirect);
# "js-array" mode would still be correct but crosses JavaScript per block, which
# means -sALLOW_TABLE_GROWTH did not take effect and the measurement is wrong.
grep -q '"mode":"table"' "$BUILD_TMP/fixture_jit.log" || {
    echo "FAIL: emitted modules are not in table mode; see $BUILD_TMP/fixture_jit.log" >&2
    grep -o '"mode":"[a-z-]*"' "$BUILD_TMP/fixture_jit.log" | head -2 >&2
    exit 1
}
grep -q '"mode":"table"' "$BUILD_TMP/selftest_jit.log" || {
    echo "FAIL: self-test emitted modules are not in table mode" >&2
    exit 1
}
if grep -qE '^WASM64: engine=wasm blocks=[1-9]' "$BUILD_TMP/fixture_interp.log"; then
    echo "FAIL: BW64_JIT=0 run reported emitted blocks" >&2
    exit 1
fi
printf 'OK: fixture %s identical across interpreter%s and emitted modules (.elf=%s bytes)\n' \
    "$INTERP_SUM" "${NATIVE_SUM:+, qemu-x86_64 reference $NATIVE_SUM}" "$(wc -c < "$ELF")"

echo "=== [5/5] opt-in engine comparison (BW64_JIT_ENGINE=interp vs wasm) ==="
BW64_JIT=1 BW64_JIT_VERIFY=1 BW64_JIT_ENGINE=interp node "$JS_CJS" --x64-selftest > "$BUILD_TMP/selftest_interpengine.log" 2>&1
grep -E '^JIT64: engine=interp blocks=[1-9]' "$BUILD_TMP/selftest_interpengine.log"
sed -e '/^JIT64:/d' -e '/^WASM64:/d' -e '/^BW64JITSTATS:/d' -e '/JIT64-CASE:/d' "$BUILD_TMP/selftest_interpengine.log" > "$BUILD_TMP/selftest_interpengine_results.log"
if diff -u "$BUILD_TMP/selftest_interp.log" "$BUILD_TMP/selftest_interpengine_results.log"; then
    echo "OK: phase-1 interpreter engine also matches the interpreter baseline."
else
    echo "FAIL: BW64_JIT_ENGINE=interp output differs from the baseline." >&2
    exit 1
fi

echo "build-jit-wasm.sh: all gates passed."
