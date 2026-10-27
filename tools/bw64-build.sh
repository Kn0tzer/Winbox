#!/usr/bin/env bash
# One writer of dist/runtime and web/runtime, with matching WASM/JS provenance.
# Usage: tools/bw64-build.sh [--check|--export-only]
# EMSDK defaults to /opt/emsdk; an already active emcc is also supported.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMSCRIPTEN_DIR="$REPO/project/emscripten"
# BW64_BUILD_TARGET selects the makefile target (default unchanged). The JIT
# target compiles the same runtime with -DBOXEDWINE_WASM_JIT, so it writes to
# its own build directory and its build is NOT interchangeable with the
# non-JIT one (that is the point of the A/B comparison).
TARGET="${BW64_BUILD_TARGET:-wasm64-mt}"
case "$TARGET" in
    wasm64-mt)     BUILD_OUT="$EMSCRIPTEN_DIR/Build/Wasm64Mt" ;;
    wasm64-mt-jit) BUILD_OUT="$EMSCRIPTEN_DIR/Build/Wasm64MtJit" ;;
    *) echo "[bw64-build] FATAL: unknown BW64_BUILD_TARGET: $TARGET" >&2; exit 2 ;;
esac
LOCKDIR="${BUILD_LOCK_DIR:-/tmp/bw64-build.lock.d}"
INFO_FILE="$LOCKDIR/info"
PROBE_SLOT_DIR="${PROBE_SLOT_DIR:-/tmp/bw64-probe-slots}"
WAIT_SECONDS="${BW64_BUILD_WAIT_SECONDS:-600}"
OWNER="${BW64_BUILD_OWNER:-build:$$}"
# BW64_PROFILE=1 adds --profiling-funcs at link time so the CPU profiler can
# attribute samples to named wasm functions. It changes only the link flags and
# is recorded in BUILD_INFO.json; unset means the flags are identical to before.
PROFILE="${BW64_PROFILE:-0}"
case "$PROFILE" in 0|1) ;; *) echo "[bw64-build] FATAL: BW64_PROFILE must be 0 or 1" >&2; exit 2 ;; esac
MAKE_LDFLAGS=()
if [[ "$PROFILE" == 1 ]]; then
    MAKE_LDFLAGS+=("EXTRA_LDFLAGS=--profiling-funcs")
fi
mode="${1:-}"
case "$mode" in ''|--check|--export-only) ;; *) echo "Usage: $0 [--check|--export-only]" >&2; exit 2 ;; esac
[[ "$WAIT_SECONDS" =~ ^[0-9]+$ ]] || { echo 'BW64_BUILD_WAIT_SECONDS must be nonnegative' >&2; exit 2; }
log() { echo "[bw64-build] $*"; }
die() { echo "[bw64-build] FATAL: $*" >&2; exit 1; }
if [[ "$PROFILE" == 1 ]]; then log 'profiling functions enabled (--profiling-funcs)'; fi
pid_alive() { kill -0 "$1" 2>/dev/null || ps -p "$1" -o pid= >/dev/null 2>&1; }
read_owner() {
    local raw
    raw=$(cat "$INFO_FILE" 2>/dev/null) || return 1
    [[ "$raw" =~ ^([1-9][0-9]*)\|([0-9]+)\|(.*)$ ]] || return 1
    lock_raw="$raw"
    lock_pid="${BASH_REMATCH[1]}"
    lock_started="${BASH_REMATCH[2]}"
    lock_owner="${BASH_REMATCH[3]}"
    [[ ${#lock_pid} -le 10 && "$lock_pid" -le 2147483647 ]] || return 1
}
if [[ "$mode" == --check ]]; then
    if [[ ! -d "$LOCKDIR" ]]; then log 'BUILD IDLE'; exit 0; fi
    if ! read_owner; then log 'BUILD LOCK INITIALIZING OR INVALID'; exit 10; fi
    if pid_alive "$lock_pid"; then log "BUILD IN PROGRESS (pid=$lock_pid owner=$lock_owner)"; exit 10; fi
    log "STALE BUILD LOCK (pid=$lock_pid dead)"; exit 11
fi
# --- Build lock + probe drain ----------------------------------------------
# Ordering (changed 2026-10-27): a build no longer holds the build lock
# while it waits for probes to drain. The loop below interleaves the two
# waits. While probes are busy it drains WITHOUT the lock (publishing
# build-pending so new probes hold back); it attempts the lock only once
# probes look clear, then re-scans under the lock before any write phase:
# a probe may have slipped in during lock acquisition, and if so the lock
# is released and the drain resumes. Lock contention keeps the historical
# semantics: a live owner is authoritative, and the build dies with
# "build lock busy or invalid" once BW64_BUILD_WAIT_SECONDS has elapsed,
# whether it is draining or waiting. The drain budget is
# BW64_BUILD_WAIT_MIN minutes of probe-busy waiting in total across all
# rounds; if it runs out (or a probe slips in three times) the build
# falls back to the historical behaviour: drain the remainder under the
# lock, then validate (dying on a live probe) exactly as before.
# Invariants are unchanged: at most one build runs the write phases
# (bootstrap/compile/export), and a build enters them only after a probe
# scan under the lock shows no live probe.
acquire_lock() {
    local started_wait=$SECONDS
    while ! mkdir "$LOCKDIR" 2>/dev/null; do
        # A live owner remains authoritative regardless of lock age. Missing info
        # can be the mkdir-to-write interval of a new owner: fail closed and wait.
        if read_owner && ! pid_alive "$lock_pid"; then
            if [[ "$lock_raw" == "$(cat "$INFO_FILE" 2>/dev/null)" ]]; then
                rm -f "$INFO_FILE"
                rmdir "$LOCKDIR" 2>/dev/null || die "stale lock contains unexpected files: $LOCKDIR"
                continue
            fi
        fi
        if (( SECONDS - started_wait >= WAIT_SECONDS )); then die "build lock busy or invalid: $LOCKDIR"; fi
        sleep 1
    done
    printf '%s|%s|%s\n' "$$" "$(date +%s)" "$OWNER" > "$INFO_FILE"
    log "lock acquired (pid=$$)"
}
cleanup() {
    if read_owner && [[ "$lock_pid" == "$$" ]]; then
        rm -f "$INFO_FILE"
        rmdir "$LOCKDIR" 2>/dev/null || true
    fi
    # build-pending is claimed by builds that do not hold the lock yet, so
    # remove it only when it names us. (probe-slots.mjs also treats a dead
    # pid in build-pending as no build pending and unlinks it itself.)
    local bp
    bp=$(cat "$PROBE_SLOT_DIR/build-pending" 2>/dev/null) || bp=""
    if [[ "$bp" == "$$" ]]; then rm -f "$PROBE_SLOT_DIR/build-pending"; fi
    return 0
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
release_lock() { cleanup; }
claim_build_pending() {
    # Publish our pid so new probes hold back while we drain. If another
    # live build already claims the file, probes are held back either way.
    mkdir -p "$PROBE_SLOT_DIR"
    local bp
    bp=$(cat "$PROBE_SLOT_DIR/build-pending" 2>/dev/null) || bp=""
    if [[ "$bp" == "$$" ]]; then return 0; fi
    if [[ "$bp" =~ ^[1-9][0-9]*$ ]] && pid_alive "$bp"; then return 0; fi
    echo "$$" > "$PROBE_SLOT_DIR/build-pending"
}
probes_busy() {
    # Probe slots contain exactly one PID, never a concatenation of arbitrary digits.
    local slot pid
    for slot in "$PROBE_SLOT_DIR"/slot-* "$PROBE_SLOT_DIR"/light-* /tmp/bw64-probe.lock; do
        [[ -e "$slot" ]] || continue
        pid=$(cat "$slot" 2>/dev/null) || continue
        [[ "$pid" =~ ^[1-9][0-9]*$ ]] && pid_alive "$pid" && return 0
    done
    return 1
}
lock_contended() {
    # True while another build owns the lock: a live owner, or an owner
    # still in its mkdir-to-write interval (fail closed, as the old spin).
    [[ -d "$LOCKDIR" ]] || return 1
    if read_owner; then pid_alive "$lock_pid"; else return 0; fi
}
try_acquire_lock() {
    if mkdir "$LOCKDIR" 2>/dev/null; then
        printf '%s|%s|%s\n' "$$" "$(date +%s)" "$OWNER" > "$INFO_FILE"
        log "lock acquired (pid=$$)"
        return 0
    fi
    # Reclaim a validated stale lock and retry once, as the historical
    # spin did; a live or still-initializing owner stays authoritative.
    if read_owner && ! pid_alive "$lock_pid"; then
        if [[ "$lock_raw" == "$(cat "$INFO_FILE" 2>/dev/null)" ]]; then
            rm -f "$INFO_FILE"
            rmdir "$LOCKDIR" 2>/dev/null || die "stale lock contains unexpected files: $LOCKDIR"
            if mkdir "$LOCKDIR" 2>/dev/null; then
                printf '%s|%s|%s\n' "$$" "$(date +%s)" "$OWNER" > "$INFO_FILE"
                log "lock acquired (pid=$$)"
                return 0
            fi
        fi
    fi
    return 1
}
validate_probe_slots() {
    local slot pid
    for slot in "$PROBE_SLOT_DIR"/slot-* /tmp/bw64-probe.lock; do
        [[ -e "$slot" ]] || continue
        pid=$(cat "$slot" 2>/dev/null) || die "cannot read probe slot: $slot"
        [[ "$pid" =~ ^[1-9][0-9]*$ ]] || die "invalid probe slot (investigate before building): $slot"
        [[ ${#pid} -le 10 && "$pid" -le 2147483647 ]] || die "invalid probe slot PID: $slot"
        if pid_alive "$pid"; then die "probe activity detected ($slot held by live pid $pid)"; fi
        rm -f "$slot"
    done
}
if [[ "${BW64_BUILD_FORCE:-0}" == 1 ]]; then
    acquire_lock
else
    drain_budget=$(( ${BW64_BUILD_WAIT_MIN:-60} * 60 ))
    lock_deadline=$(( SECONDS + WAIT_SECONDS ))
    slipped=0
    drain_logged=0
    while :; do
        claim_build_pending
        if probes_busy && (( drain_budget > 0 )); then
            # Drain WITHOUT the build lock. A contended lock still fails
            # on schedule even while we are only draining.
            if lock_contended && (( SECONDS >= lock_deadline )); then
                die "build lock busy or invalid: $LOCKDIR"
            fi
            if (( drain_logged == 0 )); then
                log 'waiting for probes to drain (build lock not held)'
                drain_logged=1
            fi
            sleep 15
            drain_budget=$(( drain_budget - 15 ))
            (( drain_budget < 0 )) && drain_budget=0
            continue
        fi
        if try_acquire_lock; then
            # Historical state at this point: sole lock holder,
            # build-pending names us. Re-scan before any write phase.
            echo "$$" > "$PROBE_SLOT_DIR/build-pending"
            if ! probes_busy; then
                validate_probe_slots
                break
            fi
            slipped=$(( slipped + 1 ))
            if (( drain_budget <= 0 || slipped >= 3 )); then
                # Historical fallback, still under the lock: wait out
                # whatever budget remains, then validate exactly as
                # the original did.
                while probes_busy && (( drain_budget > 0 )); do
                    sleep 15
                    drain_budget=$(( drain_budget - 15 ))
                done
                validate_probe_slots
                break
            fi
            log 'probe activity slipped in during lock acquisition; releasing lock to drain again'
            release_lock
            continue
        fi
        if (( SECONDS >= lock_deadline )); then die "build lock busy or invalid: $LOCKDIR"; fi
        sleep 1
    done
fi
python3 "$REPO/tools/browser/bootstrap.py"
if [[ "$mode" != --export-only ]]; then
    if ! command -v emcc >/dev/null; then
        sdk="${EMSDK:-/opt/emsdk}"
        [[ -f "$sdk/emsdk_env.sh" ]] || die 'activate Emscripten 6.0.9 or set EMSDK to its SDK directory'
        # shellcheck disable=SC1090
        source "$sdk/emsdk_env.sh" >/dev/null 2>&1
    fi
    version=$(emcc --version)
    [[ "$version" == *" 6.0.9 "* ]] || die 'the pinned build requires Emscripten 6.0.9'
    make -C "$EMSCRIPTEN_DIR" "$TARGET" MAKEFLAGS="-j${JOBS:-2}" "${MAKE_LDFLAGS[@]}"
fi
WASM="$BUILD_OUT/boxedwine64.wasm"
JS="$BUILD_OUT/boxedwine64.js"
[[ -f "$WASM" && -f "$JS" ]] || die 'missing compiled runtime; run the full build first'
magic=$(od -An -tx1 -N4 "$WASM" | tr -d ' \n')
[[ "$magic" == 0061736d ]] || die "invalid WASM magic: $WASM"
export BW64_PROVENANCE_OWNER="$OWNER" BW64_PROVENANCE_PID="$$" BW64_PROFILE="$PROFILE"
python3 - "$REPO" "$BUILD_OUT" "$mode" <<'PYCODE'
import hashlib, json, os, pathlib, subprocess, sys
from datetime import datetime, timezone
root, build = map(pathlib.Path, sys.argv[1:3])
mode = sys.argv[3]
def git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()
wasm_hash = hashlib.file_digest((build / 'boxedwine64.wasm').open('rb'), 'sha256').hexdigest()
js_hash = hashlib.file_digest((build / 'boxedwine64.js').open('rb'), 'sha256').hexdigest()
if mode == '--export-only':
    info = None
    for record in [build / 'BUILD_INFO.json', root / 'web/runtime/BUILD_INFO.json']:
        if record.is_file():
            candidate = json.loads(record.read_text())
            if candidate.get('wasm_sha256') == wasm_hash and candidate.get('js_sha256') == js_hash:
                info = candidate
                break
    if info is None:
        raise SystemExit('Existing binaries have no matching BUILD_INFO.json; run a full build')
    info['exported_at_utc'] = datetime.now(timezone.utc).isoformat()
else:
    info = {
    'built_at_utc': datetime.now(timezone.utc).isoformat(),
    'git_sha': git('rev-parse', 'HEAD'),
    'git_dirty_files': len(git('status', '--porcelain').splitlines()),
    'wasm_sha256': wasm_hash,
    'wasm_bytes': (build / 'boxedwine64.wasm').stat().st_size,
    'js_sha256': js_hash,
    'profiling': os.environ.get('BW64_PROFILE') == '1',
    'builder': os.environ['BW64_PROVENANCE_OWNER'] + ' (pid ' + os.environ['BW64_PROVENANCE_PID'] + ')',
}
(build / 'BUILD_INFO.json').write_text(json.dumps(info, indent=2) + '\n')
PYCODE
# Retain the generated web/runtime mirror used by provenance/packaging verifiers.
mkdir -p "$REPO/web/runtime"
for asset in boxedwine64.wasm boxedwine64.js BUILD_INFO.json; do cp "$BUILD_OUT/$asset" "$REPO/web/runtime/$asset"; done
cd "$REPO"
node tools/browser/build.mjs && node tools/browser/verify-dist.mjs
log 'BUILD COMPLETE'
