#!/usr/bin/env bash
# One writer of dist/runtime and web/runtime, with matching WASM/JS provenance.
# Usage: tools/bw64-build.sh [--check|--export-only]
# EMSDK defaults to /opt/emsdk; an already active emcc is also supported.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
# Tree-scoped coordination (FD-224): a build replaces only its own tree's
# dist/, so it publishes its claim and its lock under this tree's identity.
# The claim file is named for the tree (sha256 of the canonical tree path,
# first 16 hex chars); probes in other trees ignore it. pwd -P matters: the
# probe side canonicalizes with realpath, so both sides must hash the same
# physical path.
TREE_HASH="$(printf '%s' "$REPO" | sha256sum | cut -c1-16)"
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
CLAIM_FILE="$PROBE_SLOT_DIR/build-pending-$TREE_HASH"
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
    lock_tree=""
    # A 4th field is the builder's tree (an absolute path). Only treat the
    # tail as a tree when it looks like one; an owner containing '|' stays
    # an owner.
    if [[ "$lock_owner" == *"|/"* ]]; then
        lock_tree="${lock_owner##*|}"
        lock_owner="${lock_owner%|*}"
    fi
    [[ ${#lock_pid} -le 10 && "$lock_pid" -le 2147483647 ]] || return 1
}
# Write the lock info: pid|started|owner|tree (tree omitted when it cannot
# be represented safely). Readers treat a missing tree as unknown and fail
# closed, so a build never looks idle to a probe it could corrupt.
write_lock_info() {
    if [[ "$REPO" == *"|"* ]]; then
        printf '%s|%s|%s\n' "$$" "$(date +%s)" "$OWNER" > "$INFO_FILE"
    else
        printf '%s|%s|%s|%s\n' "$$" "$(date +%s)" "$OWNER" "$REPO" > "$INFO_FILE"
    fi
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
# Tree scope (FD-224): coordination is per tree, because a build replaces
# only its own tree's dist/. The claim is published under a file named for
# this tree, the lock info records this tree, and the drain/validate scans
# consider only this tree's probes — a probe in another tree, or testing a
# remote dist, never blocks this build. Claim timing (FD-224): the claim is
# published only while draining for a write or holding the lock. A build
# merely queued for the lock publishes no claim, so probes in its tree can
# start while it waits — a relay of queued builds no longer holds the
# claim continuously and starves all probe starts.
# Invariants are unchanged: at most one build runs the write phases
# (bootstrap/compile/export), and a build enters them only after a probe
# scan under the lock shows no live probe in its tree.
acquire_lock() {
    local started_wait=$SECONDS
    local waited_logged=0
    while ! mkdir "$LOCKDIR" 2>/dev/null; do
        # A queued build must say so: waiting silently for up to
        # BW64_BUILD_WAIT_SECONDS reads as a hung build.
        if (( waited_logged == 0 )); then log 'waiting for build lock'; waited_logged=1; fi
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
    write_lock_info
    log "lock acquired (pid=$$)"
}
cleanup() {
    if read_owner && [[ "$lock_pid" == "$$" ]]; then
        rm -f "$INFO_FILE"
        rmdir "$LOCKDIR" 2>/dev/null || true
    fi
    unclaim_build_pending
    # The legacy single build-pending file is no longer written here, but an
    # older build may still have claimed it: remove it only when it names
    # us. (probe-slots.mjs also treats a dead pid in build-pending as no
    # build pending and unlinks it itself.)
    local bp
    bp=$(cat "$PROBE_SLOT_DIR/build-pending" 2>/dev/null) || bp=""
    if [[ "$bp" == "$$" ]]; then rm -f "$PROBE_SLOT_DIR/build-pending"; fi
    return 0
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Release the build lock without dropping the claim: after a slip the build
# is still draining for a write, so same-tree probes must stay held back.
release_lock() {
    if read_owner && [[ "$lock_pid" == "$$" ]]; then
        rm -f "$INFO_FILE"
        rmdir "$LOCKDIR" 2>/dev/null || true
    fi
}
claim_build_pending() {
    # Publish our pid so new same-tree probes hold back while we drain.
    # The claim names this tree only; probes in other trees ignore it. If
    # another live build in this tree already claims it, probes are held
    # back either way.
    mkdir -p "$PROBE_SLOT_DIR"
    local existing claim_pid
    existing=$(cat "$CLAIM_FILE" 2>/dev/null) || existing=""
    if [[ -n "$existing" ]]; then
        claim_pid="${existing%%|*}"
        if [[ "$claim_pid" == "$$" ]]; then return 0; fi
        if [[ "$claim_pid" =~ ^[1-9][0-9]*$ ]] && pid_alive "$claim_pid"; then return 0; fi
    fi
    printf '%s|%s\n' "$$" "$REPO" > "$CLAIM_FILE"
}
unclaim_build_pending() {
    local existing
    existing=$(cat "$CLAIM_FILE" 2>/dev/null) || existing=""
    if [[ "${existing%%|*}" == "$$" ]]; then rm -f "$CLAIM_FILE"; fi
}
# Prints the tree whose dist/ a probe tests, from its sidecar: the tree path,
# the word REMOTE (tree null: a remote dist no local build can replace), or
# the word UNKNOWN (no sidecar or no tree field). Always succeeds, so callers
# under `set -e` can read the result without the script dying on it.
slot_tree() {
    local sidecar="$PROBE_SLOT_DIR/info-$(basename "$1").json"
    if [[ ! -f "$sidecar" ]]; then printf 'UNKNOWN'; return 0; fi
    local tree
    tree=$(sed -n 's/.*"tree": *"\([^"]*\)".*/\1/p' "$sidecar" | head -n 1)
    if [[ -n "$tree" ]]; then printf '%s' "$tree"; return 0; fi
    if grep -q '"tree": *null' "$sidecar"; then printf 'REMOTE'; return 0; fi
    printf 'UNKNOWN'
}
probes_busy() {
    # A probe blocks this build only when it tests this tree's dist. The
    # legacy lock is global and still blocks (fail closed). A probe with
    # no readable tree is unknown and blocks (fail closed); a probe in
    # another tree, or testing a remote dist, never blocks.
    if [[ -f /tmp/bw64-probe.lock ]]; then
        local lpid
        lpid=$(cat /tmp/bw64-probe.lock 2>/dev/null) || lpid=""
        if [[ "$lpid" =~ ^[1-9][0-9]*$ ]] && pid_alive "$lpid"; then return 0; fi
    fi
    local slot pid tree
    for slot in "$PROBE_SLOT_DIR"/slot-* "$PROBE_SLOT_DIR"/light-*; do
        [[ -e "$slot" ]] || continue
        pid=$(cat "$slot" 2>/dev/null) || continue
        [[ "$pid" =~ ^[1-9][0-9]*$ ]] && pid_alive "$pid" || continue
        tree=$(slot_tree "$slot")
        if [[ "$tree" == REMOTE ]]; then continue; fi  # remote dist: never blocks
        if [[ "$tree" == UNKNOWN ]]; then return 0; fi # unknown tree: fail closed
        [[ "$tree" == "$REPO" ]] || continue # another tree: never blocks
        return 0
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
        write_lock_info
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
                write_lock_info
                log "lock acquired (pid=$$)"
                return 0
            fi
        fi
    fi
    return 1
}
validate_probe_slots() {
    # A live probe in this tree must die the build: the build must not
    # replace dist/ under it. A live probe in another tree, or testing a
    # remote dist, is left alone. A probe with no readable tree fails
    # closed — but its sidecar lands microseconds after its slot file, so
    # re-read once before dying on a probe that just started.
    local slot pid tree
    for slot in "$PROBE_SLOT_DIR"/slot-* /tmp/bw64-probe.lock; do
        [[ -e "$slot" ]] || continue
        pid=$(cat "$slot" 2>/dev/null) || die "cannot read probe slot: $slot"
        [[ "$pid" =~ ^[1-9][0-9]*$ ]] || die "invalid probe slot (investigate before building): $slot"
        [[ ${#pid} -le 10 && "$pid" -le 2147483647 ]] || die "invalid probe slot PID: $slot"
        if pid_alive "$pid"; then
            if [[ "$slot" == /tmp/bw64-probe.lock ]]; then
                die "probe activity detected ($slot held by live pid $pid)"
            fi
            tree=$(slot_tree "$slot")
            if [[ "$tree" == REMOTE ]]; then continue; fi  # remote dist: never blocks
            if [[ "$tree" == UNKNOWN ]]; then
                sleep 1
                tree=$(slot_tree "$slot")
                if [[ "$tree" == UNKNOWN ]]; then die "probe activity detected ($slot held by live pid $pid with no readable tree)"; fi
                if [[ "$tree" == REMOTE ]]; then continue; fi
            fi
            if [[ "$tree" == "$REPO" ]]; then die "probe activity detected ($slot held by live pid $pid)"; fi
            continue
        fi
        # A dead slot in this tree is stale and may be removed; a dead
        # slot in another tree is that tree's business.
        if [[ "$slot" == /tmp/bw64-probe.lock ]]; then rm -f "$slot"; continue; fi
        tree=$(slot_tree "$slot")
        if [[ "$tree" == "$REPO" ]]; then rm -f "$slot"; fi
    done
}
if [[ "${BW64_BUILD_FORCE:-0}" == 1 ]]; then
    acquire_lock
else
    drain_budget=$(( ${BW64_BUILD_WAIT_MIN:-60} * 60 ))
    lock_deadline=$(( SECONDS + WAIT_SECONDS ))
    slipped=0
    drain_logged=0
    lock_logged=0
    while :; do
        if probes_busy && (( drain_budget > 0 )); then
            # Drain WITHOUT the build lock. The claim is published here —
            # while same-tree probes are running and this build is draining
            # for a write — so no new same-tree probe starts. A contended
            # lock still fails on schedule even while only draining.
            claim_build_pending
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
            # Sole lock holder; the claim names this tree. Re-scan before
            # any write phase.
            claim_build_pending
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
        # Queued for the lock with no same-tree probe running: drop the
        # claim so probes in this tree can start while this build waits
        # (FD-224 — the claim must not be held for the whole queue).
        unclaim_build_pending
        # Same duty as the drain message above: a build queued behind
        # another build logs that fact once instead of sitting silent.
        if (( lock_logged == 0 )) && lock_contended; then
            if read_owner; then log "waiting for build lock (owner=$lock_owner pid=$lock_pid)"; else log 'waiting for build lock (owner still initializing)'; fi
            lock_logged=1
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
# Pin the verifier to the tree this run just exported. verify-dist.mjs
# otherwise resolves BW_TEST_DIST first — an env var the test suites use
# for a different purpose — and would verify a stale dist while this
# script reports BUILD COMPLETE for the new one.
EXPORT_OUT="${WINE_BUILD_OUT:-dist}"
[[ "$EXPORT_OUT" = /* ]] || EXPORT_OUT="$REPO/$EXPORT_OUT"
# A bare `cmd1 && cmd2` statement would swallow a failed export: the
# && list consumes the exit status, so set -e never fires and BUILD
# COMPLETE printed anyway (observed live 2026-10-30: stage-dependencies
# died on a missing payload, build.mjs exited 1, BUILD COMPLETE still
# printed). Both steps die loudly by name instead.
if ! node tools/browser/build.mjs; then
    die 'browser export failed (tools/browser/build.mjs exited non-zero); BUILD COMPLETE not claimed'
fi
if ! BW_TEST_DIST="$EXPORT_OUT" node tools/browser/verify-dist.mjs; then
    die 'browser export verification failed (tools/browser/verify-dist.mjs exited non-zero); BUILD COMPLETE not claimed'
fi
log 'BUILD COMPLETE'
