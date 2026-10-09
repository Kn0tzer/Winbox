#!/bin/bash
# Run the cpu-diff harness headless and drop the report in
# test-results/cpu-differential/.
set -e
cd "$(dirname "$0")"
[ -x build/cpu-diff ] || ./build.sh
OUT="../../test-results/cpu-differential"
./build/cpu-diff --out "$OUT"
echo "report: $OUT/REPORT.md"
