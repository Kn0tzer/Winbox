#!/bin/bash
# Four-arm alternating start measurement (FD-43/FD-47). One boot per
# driver invocation; each boot takes and releases a probe slot, so
# other lanes interleave fairly. Rounds x arms, load logged per boot
# by the driver itself (boot.json loadStart/loadEnd).
set -u
cd /home/ubuntu/wt-readygate
LOG=/home/ubuntu/wt-readygate/test-results/readygate-progress.log
ROUNDS=${ROUNDS:-10}
declare -A DIST=(
  [A]=/home/ubuntu/readygate-arms/A
  [B]=/home/ubuntu/readygate-arms/B
  [C]=/home/ubuntu/readygate-arms/C
  [D]=/home/ubuntu/wt-readygate-d/dist
)
echo "== arms run started $(date -Is) rounds=$ROUNDS" >> "$LOG"
for i in $(seq 1 "$ROUNDS"); do
  for arm in A B C D; do
    if [ ! -f "${DIST[$arm]}/runtime/BUILD_INFO.json" ]; then
      echo "arm $arm #$i SKIPPED (no dist at ${DIST[$arm]})" >> "$LOG"
      continue
    fi
    echo "arm $arm #$i start $(date -Is) load=$(cut -d' ' -f1-3 /proc/loadavg)" >> "$LOG"
    WINE_DIST_DIR="${DIST[$arm]}" node tools/browser/startup-measurement.mjs \
      --arm="$arm" --index="$i" --window-ms=360000 >> "$LOG" 2>&1
    echo "arm $arm #$i done $(date -Is)" >> "$LOG"
    node tools/browser/startup-measurement.mjs --summarize >> "$LOG" 2>&1
  done
done
echo "== arms run complete $(date -Is)" >> "$LOG"
