#!/bin/bash
# Run the cpu-diff harness headless and drop the report in
# test-results/cpu-differential/.
set -e
cd "$(dirname "$0")"
REPO="$(cd ../.. && pwd)"
# A cached executable must not certify source changed since it was built.
if [ ! -x build/cpu-diff ] || [ ! -x runner/runner.elf ] ||
   [ build.sh -nt build/cpu-diff ] || [ harness.cpp -nt build/cpu-diff ] ||
   [ runner/runner.cpp -nt runner/runner.elf ] ||
   [ -n "$(find "$REPO/include" "$REPO/lib/simde" "$REPO/lib/softfloat/source" \
       "$REPO/source/emulation/cpu" "$REPO/source/kernel/kmemory64.cpp" \
       "$REPO/source/util/bstring.cpp" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.c' \) \
       -newer build/cpu-diff -print -quit)" ]; then
    ./build.sh
fi
OUT="../../test-results/cpu-differential"
./build/cpu-diff --out "$OUT"
echo "report: $OUT/REPORT.md"
