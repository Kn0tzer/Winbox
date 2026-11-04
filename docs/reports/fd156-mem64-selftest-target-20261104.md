# FD-156 — the MEMORY64 variant of the Node self-test target compiles again and runs to full marks

Lane `lane-fd156` (worktree `wt-fd156`), based on the integration tip
`3ea2fd97d`. Commits: `3ee906b3a` (the fix), `b4d3f2e8f` (run-instruction
correction), plus the register and record commit that carries this file.
Evidence: `test-results/fd156/` in this worktree.

## The filing

FD-156 (filed by the FD-151 lane as its lane-local FD-155; settled at
the batch-6 composition) recorded that the MEMORY64 variant of the Node
self-test target, `make wasm64-selftest-mem64` in `project/emscripten`,
does not compile: the SDL audio bridge's EM_ASM call in
`platform/sdl/kdspaudio.cpp` cast the ring-buffer index addresses to
`int` (`(int)&bwRingWrite`, `(int)&bwRingRead`), which a 64-bit-pointer
build rejects ("cast from pointer to smaller type 'int' loses
information"). It was the build's only error site, pre-existing on the
integration tip by construction (the file is byte-identical there; the
FD-151 lane's diff does not touch it). Closing terms: the mem64 target
compiles and its run is reported.

Reproduction on this branch's base was exact (`build-base-mem64.log`):
the same two errors at line 371 in both compilations of the file, and
no other error anywhere in the log.

## The defect

`bwAudioEnsureBridge` arms the Web Audio bridge by handing JS three
addresses in wasm linear memory: the ring base (`bwRing`, $0) and the
producer/consumer frame-index atomics (`bwRingWrite` $1, `bwRingRead`
$2). The JS side uses all three purely as byte offsets — `ringOffset`,
`writeOffset`, `readOffset` in the worklet's processorOptions, and
`new Float32Array(buffer, $0, …)` / `new Uint32Array(buffer, $1, 1)` in
the ScriptProcessor fallback. The ring base was passed as a raw pointer
all along; the two index addresses were narrowed through `(int)` casts,
which is exactly the value on a wasm32 host and unrepresentable on a
wasm64 host.

The cast shape also matters, not just the width: Emscripten's
`__em_asm_sig` gives every pointer type the sig char `p`, and the
generated MEMORY64 runtime's `readEmAsmArgs` delivers a `p` argument as
`Number(HEAPU64[buf/8])` — a plain JS number holding the byte offset,
which is what the bridge's JS consumes. A `uintptr_t` cast would have
compiled but taken sig `j`, arriving as a raw BigInt, and the
typed-array constructions would have thrown the first time the bridge
armed. Passing the addresses as pointers (the treatment $0 already
gets) is the only form that is both compilable and behaviour-identical
on both host widths; the generated glue of the fixed build was read to
confirm the `p` → `Number(...)` path (the bridge's ASM_CONSTS entry is
`($0,$1,$2,$3,$4,$5) => …` with no other conversion).

Class check: the only other `(int)&` occurrences in the tree are
inside commented-out debug printfs in kunixsocket.cpp / knetlink.cpp;
the build itself confirms no other live site (one error site before,
zero after).

## Fix

`3ee906b3a`: the two casts are dropped — `&bwRingWrite`, `&bwRingRead`
go to EM_ASM as pointers, with a two-line comment recording why. One
file, +3/−1; nothing else in the bridge changes, and on wasm32 hosts
the delivered values are byte-identical to the old casts.

`b4d3f2e8f`: the target's section of
`project/emscripten/buildInstructions.md` still documented the `.js`
run (which the repository's `type: module` package.json refuses, the
same defect FD-151 corrected for the wasm32 target) and a stale pinned
case count; it now documents the `.cjs`-copy run and states the
measured Node-version behaviour below.

## The run

The box's Node is v22.22.3. Its V8 accepts a 64-bit memory
(`new WebAssembly.Memory({index:'i64'})` succeeds) but rejects this
module at instantiation: `CompileError: WebAssembly.instantiate():
invalid table elements limits flags` — the wasm64 output declares
64-bit table limits, which that V8 refuses (also under
`--experimental-wasm-memory64`; `run-mem64.log` preserves the refusal).
The target's documentation already required Node v24+, so the run used
an official Node v24.11.0 linux-arm64 build fetched into `/tmp`
scratch, its tarball verified against the release `SHASUMS256.txt`
(sha256 `33a6673b2c7bffeae9deec7f9f8b31aad9119b08f13d49b2ca3ee3bebfe8260f`,
exact match):

- mem64 build: `make wasm64-selftest-mem64` MAKE_EXIT=0
  (`build-fixed-mem64.log`); outputs `boxedwine64-selftest-mem64.js`
  (223,341 B) + `.wasm` (3,718,591 B).
- mem64 run (Node 24, `.cjs` copy, `--x64-selftest`):
  **`=== self-test summary: 953 passed, 0 failed ===`**, exit 0
  (`run-mem64-node24.log`) — the same standard as the other variants.

## Gates (all on this branch's tip)

- Standard variant regression: `make wasm64-selftest` MAKE_EXIT=0;
  run **953 passed, 0 failed**, exit 0 (`build-fixed-standard.log`,
  `run-standard.log`). The shared kdspaudio change is clean for
  wasm32 hosts too.
- Native suites, serial (1 worker): main **682 tests, 0 FAILED**
  (290 s, exit 0); x64 **108 tests, 0 FAILED** (24 s, exit 0)
  (`suite-main.log`, `suite-x64.log`).
- JS unit suite: **353 passed, 0 failed**, exit 0
  (`suite-js-unit3.log`). JS legacy: **115 passed, 0 failed**, exit 0
  (`suite-js-legacy2.log`).
- Browser build: `tools/bw64-build.sh` — "Browser export verified:
  34 .mjs syntax files; … dependency archive c4dbe35986a5 verified",
  BUILD COMPLETE, exit 0 (`build-browser.log`). BUILD_INFO: git_sha
  `b4d3f2e8f`, dirty 0, wasm sha256 `aba69204…` (5,684,501 bytes).
- Runtime assets bootstrap from `runtime-assets-20261104`: 13 assets
  restored and verified, `--check` 0 failures (`bootstrap` output in
  the lane transcript; the tree's `web/runtime/` matched every pin).

Two first-attempt JS results were this lane's own setup artifacts, not
product signals, and are recorded so the next lane does not repeat
them: the unit suite's first run (276 pass / 77 fail,
`suite-js-unit.log`) ran before `tools/browser/stage-vendor.mjs` had
staged `web/vendor/` in the fresh worktree — the failures were
`ERR_MODULE_NOT_FOUND` on vendor imports; and `provenance-artifacts`
fails until `dist/` exists, i.e. until the browser build has run.
With vendor staged and dist built, the suite is 353/0 as above. A
fresh worktree needs both steps before its JS results mean anything.

## Filed separately

Nothing. No new product defect was found: the only break was the
filed one, the run-environment gap is the target's already-documented
Node v24+ requirement, and the two JS artifacts were this lane's
setup omissions, corrected in-lane.
