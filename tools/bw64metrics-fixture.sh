#!/usr/bin/env bash
# Regenerate the writer-side metrics fixture used by
# web/tests/suite/unit/frame-metrics-contract.test.mjs.
#
# The fixture is the REAL output of source/util/bw64metrics.cpp (compiled
# natively, no Emscripten needed), so the reader-side test fails loudly whenever
# the JSON schema drifts from what the panel parses.
#
# Usage: tools/bw64metrics-fixture.sh
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/web/tests/fixtures/frame-metrics-snapshot.txt"
BIN="${TMPDIR:-/tmp}/bw64metrics-selftest-fixture"

mkdir -p "$(dirname "$OUT")"
# Same subsystem defines as the browser build, so the fixture reports the stages
# the real runtime reports instead of the native-only subset.
g++ -std=c++17 -O2 -Wall -Wextra -Wno-comment -I"$REPO/include" \
    -DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_VULKAN64 -DBOXEDWINE_OPENGL \
    "$REPO/source/util/bw64metrics.cpp" "$REPO/source/util/bw64jitstats.cpp" \
    "$REPO/tools/bw64metrics-selftest.cpp" -o "$BIN"
"$BIN" "$OUT"
echo "[bw64metrics-fixture] wrote $OUT"