# FD-154 adjudication — the five long-standing browser guest-suite failures

Lane record, 2026-11-04. Worktree `wt-fd154`, branch `lane-fd154`,
base: integration tip `3ea2fd97d`. Every failure below was first
reproduced stock on the unmodified base build (wasm
`9abd2f249d…`, the same build the live site serves), then proven
against fixed builds. Evidence: `test-results/fd154/` on this tree
(base/fixed run directories, focused probes, build logs, chain logs).

## 1. winsock-http — PRODUCT defect, fixed

Mechanism: in the browser build every AF_INET/SOCK_STREAM socket is
marked `loopbackStream` (`source/kernel/knativesocket.cpp`), and
`connect()` refused any loopback destination with no guest listener
internally — never using the socket's native transport. With the
relay enabled the native transport is the only path to the test
relay, and the fixture connects to 127.0.0.1:80 by design. Base
evidence: guest prints `BW_TCP connect=-1 error=10061`
(WSAECONNREFUSED), harness relay records accepted 0 / bytes 0/0.
Every other data-path predicate already treated such a socket as
native; connect() was the exception (regression shape: guest-local
loopback streams arrived 2026-10-21, after the mid-October
networking acceptance).

Fix `d26f606a1`: listener lookup first; no listener and a native
transport present → fall through to the native connect.
Proof: test passes on the fixed build; relay accepted 1,
76 bytes guest→TCP, 324 bytes back — the filing's closing term.

## 2. input-mouse-rel — PRODUCT defect, fixed

Mechanism: both relative-motion producers (SDL `forwardSdlEvent`,
`injectEvent`, in both sink classes of `xwirepresentSDL.cpp`)
advanced the sink's tracked pointer by the delta at queue time; the
delivering connection seeded its virtual position from that pointer
and added the delta again. First motion of every relative stream
arrived doubled: injected (100,50) → guest `RAW dx=200 dy=100`
(measured with the product's `BW64_XWIRE` log; the delivering
connection logged one core MotionNotify at (200,100)).

Fix `3cce91bcb`: producers no longer advance the tracked pointer
for relative events; the connection owns the virtual position and
reports each delivered position back through a new bookkeeping-only
sink method `noteDeliveredPointer()`.
Proof: test passes on the fixed build, both architectures
(re-verified on all three fixed builds).

## 3. input-fullscreen — HARNESS defect over three PRODUCT layers, all fixed

Filed failure (harness): the guest's ClipCursor makes the SDL layer
request pointer lock; a real browser grants lock only on a user
gesture, the headless harness grants it immediately. Under lock,
Playwright's hit test reports the body intercepting the canvas
while `document.elementFromPoint` at the click point returns the
canvas in every sample; dispatched clicks arrive at the canvas with
coordinates frozen at (0,0) and produce no guest button. A neutral
control page in the same harness browser clicks correctly, and
input-keyboard passes with the identical locator click because its
guest never clips. Fix `e482eb6a6`: the test stubs
`requestPointerLock` per page and delivers the press through the
`bw64_xwire_mouse_*` injection channel the rest of the test uses.

Product layer (i) — grab-time freeze: the GrabPointer handler set
`pointerFrozen` for GrabModeSync at grab time, but the delivery
model is deliver-then-freeze; with the queue frozen before any
delivery, no event ever reached the grabbing client (total pointer
blackout for a clipping guest; keys flowed). Fix `f610e90b2`.

Product layer (ii) — own-grab foreign drop: grab-routed events were
dropped as "foreign target" when the grab window belongs to another
connection. Wine grabs the pointer on the root window for clipping
(measured grab record: client 0x1000000, win 0x600001, mask 0x4c,
ownerEvents 0, pointerMode sync, confine 0x600001), and the root is
owned by the session's first connection; X reports grab events to
the grabbing client. Fix `dbe7705f3`: own-grab targets are exempt
from the foreign-target drop. The same commit logs grab parameters
and every pointer-path drop reason under `BW64_XWIRE` — the drops
were silent.

Product layer (iii) — sync refreeze: the delivery loop re-froze
after the first delivered event under a sync grab, but this client
never sends XAllowEvents (zero opcode-35 requests in full captured
sessions), so every later core pointer event stalled in the frozen
queue for the life of the grab — the button release measurably
never left the emulator. The project's native X11 layer settled
this policy already (`x11common.cpp`: "XGrabPointer GrabModeSync is
treated as GrabModeAsync: pointer events are not frozen until
XAllowEvents"). Fix `25471296c`: the XWire loop matches it; the
AllowEvents handler and frozen queue remain for clients that drive
the sync protocol explicitly.

Proof: the test passes on the final fixed build, both
architectures; the guest prints the moved pointer (240,180),
BUTTON UP, and the clipped 1500-count move exactly once.

Two unit contracts in `xwire-pointer-grab.test.mjs` encoded the
pre-fix semantics (grab freezes; sink advances at queue time) and
were updated to the measured contracts, with the grab handler's new
diagnostic call stubbed in that test's standalone harness
(commit `58f759879`).

## 4. jit64 — HARNESS defects, fixed

The wineboot exit wait demanded the substring `wineboot.exe--init`;
`KProcess::commandLineForLog()` renders argv space-joined by
construction (`wineboot.exe --init`) — the demanded substring
occurs zero times in the full gate console while the boot completes
correctly. Fix `189f6b302` (pattern). The test's generated-code
assertion also demanded JIT output from the suite's interpreter
build (see item 5); gated on the stats' availability report in
`41de3c327`.
Proof: the test passes end to end on the fixed build (both modes,
four samples per mode, checksum `8b527b4986bdaccf`). Its benchmark
waits remain load-paced on the shared box, as the filing noted.

## 5. jitbench — HARNESS defects, fixed

(1) `bootBenchmarkGuest` passed only `jit`; the launcher maps
`?jit=1` to CPU64 and latches CPU32 off — CPU32 follows `?jit32`,
never passed, so the x86 mode-1 latch failed by construction.
Fix `b99c92154` (drive `jit32` from the mode).
(2) In the suite's build there is no engine to latch: the suite
runs the default interpreter browser target (`wasm64-mt`; the JIT
is the separate `wasm64-mt-jit` variant), and the 32-bit JIT is not
compiled into the x64 target at all. The product's own stats say
so (`available` 0; cpu32 carries `"reason":"32-bit JIT not compiled
in this target"`; cpu64 reports `engine:"interp"`, zero blocks
entered; `jit64WasmAvailable()` is compile-time false without
`BOXEDWINE_BLOCK_CACHE_INFRA`). Fixes `260545f5b`, `1d2c059d8`,
`87d9e021b`: the JIT assertions are gated on the stats reporting
the engine available; otherwise the test asserts no generated code
is reported entered (checksum stability still covers the
interpreter). JIT-variant builds keep the full assertions.
Proof: focused run (both fixtures, modes 0,1) passes on the fixed
build. Whether the shipped build should switch to the JIT variant
is a build decision outside this filing. Benchmark wall-times
remain load-gated by the test's own guard
(`BW_TEST_JITBENCH_MAX_LOAD`, default 4).

## Verification

- Base build (unmodified `3ea2fd97d`): all five failures reproduced
  stock; per-item evidence in `test-results/fd154/base-*`.
- Fixed builds: build 1 (product fixes 1–2) — winsock + mouse-rel
  pass. Build 2 (+ fullscreen harness/grab fixes, jit harness
  fixes) — winsock, mouse-rel, jit64 pass; fullscreen button layer
  isolated by the new drop logging. Build 3 (+ sync-as-async) —
  fullscreen, mouse-rel, jitbench pass.
- Final build stamp: git_sha `87d9e021b`, wasm `3b3fd4f9a8…`
  ("Browser export verified" in `test-results/fd154/build-fixed3.log`).
- JS unit suite: 353 passed / 0 failed. Emulator native suites, gate-standard serial mode: main 682 tests 0 FAILED, x64 108 tests 0 FAILED. (Threaded x64 mode shows the known pre-existing topology failure and teardown crash other lanes have recorded; serial is the gate standard.)
- Register: FD-154 annotated with these verdicts in this commit's
  companion change to `docs/defect-register.md`.
