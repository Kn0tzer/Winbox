# 500 ms Frame-Floor Investigation — Lane Report (2026-10-27)

Worktree: `/home/ubuntu/wt-framefloor` @ integ tip `87123df37` (the BEFORE
baseline; identical to what the live site serves). Branch: `lane-framefloor`.
Probe tooling: `web/tests/framefloor-probe.mjs` (new, committed by this lane).
Raw captures: `test-results/framefloor/` in this worktree.

## Bottom line

- The floor **reproduces in the rig for Doom**: on a quiet box Doom presents
  almost exactly **once per ~500 ms** for minutes at a time, and the guest
  executes only ~1.85 M instructions per 500 ms cycle — one frame of work —
  so the guest is **blocked waiting ~96 % of every cycle**. It is a wait,
  not compute.
- The floor is **not** in any shared host layer I could measure or read:
  the page tier, the emulator main loop (rAF = 60 Hz measured), the X11
  present flush (event-driven), the emulated clock (µs-grade), and the
  kernel's timed waits (true host timed waits) are all cleared. **Snake,
  running through the identical stack on the same night, presents at
  10–18 fps** — the shared path can deliver well above 2 fps.
- The wait therefore lives **inside the guest**, in the Doom engine's own
  pacing loop as serviced by Wine (its imports are exactly:
  `QueryPerformanceCounter`, `Sleep`, `PeekMessageA`, `StretchDIBits`,
  `waveOut*`). I could **not** convict the single responsible wait to a
  line of code: every attempt to capture steady-state guest thread dumps
  was defeated by the rig (see "Rig health" — guest boots wedged or the
  runtime crashed in 6 of 7 instrumented attempts). **No product fix is
  landed by this lane** — landing one without conviction would violate
  the campaign's verified-fix discipline. The discriminating experiment
  is built and ready (below).
- Portal's floor could **not** be reproduced or refuted in the rig: the
  only Portal capture ran at box load ~27 and present intervals were
  ≥ 1.15 s (contention-dominated). Portal's known profile (guest CPU
  ~76–90 % of frame time after perf rounds 1–2) is a *different*
  mechanism from Doom's wait; whether a guest-side wait also quantizes
  Portal in the user's unloaded browser remains open.

## BEFORE evidence

### Doom (integ dist, JIT=1, box load ~4 falling to ~3.5) — `doom-before/`

- Mapped at 85.6 s; record window 60 s in steady state (attract/demo mode).
- bw64 METRICS windows (the emitter is present-gated: a line is emitted on
  the first present ≥ 500 ms after the previous line, so window length +
  count jointly bound the true present period): **103 consecutive windows,
  winMs 497–509, present count = 1 in ~90 % of windows (2 in a few, 0 in a
  few)** → true present period ≈ 480–520 ms.
- JIT instructions retired per window: **1.69–2.01 M** (remarkably
  constant) → at any plausible interpreter/JIT throughput the guest is
  idle-blocked for the great majority of each cycle. One Doom frame of
  work ≈ this quantum.
- Canvas-change sampler (independent, frame-clock): 43 steady-state
  intervals, **min 559 ms**, p25 644, median 694, p75 754, max 1035 —
  nothing below ~550 ms. (Sampler can miss subtle changes, so treat the
  metrics bound as primary: period ≈ 500 ms, never materially below.)
- Metrics stage data per window: syscall calls ~2000–3100 (all guest
  processes), audio stage ~45–73 calls / ~5–7 ms / ~50 KB — audio is a
  small share; GL stage zero.

### Doom, second capture (load ~13) — `doom-ddraw3/` (partial, crashed)

- Sink-present timestamps (`?xwire=1`, `XWire SDL: presented 640x400`):
  presents arrive in **pairs ~13–21 ms apart** (≈ one rAF period —
  one guest frame marking the scene dirty across two main-loop ticks),
  pairs separated by **368–949 ms** at that load. Consistent with a
  ~500 ms guest frame period inflated by contention.
- The run later died with `RuntimeError: memory access out of bounds`
  in the runtime — one of several sick runs that night (see Rig health).

### Snake (same night, same stack, demo-matrix) — the counter-proof

- `snake-matrix-diag/`: mapped, 1281 frames presented; steady-state
  METRICS windows show **present counts 2–9 per 500 ms window (≈ 4–18
  fps, typically 10–14)**. Snake's imports: `SetTimer`, `Sleep`,
  `PeekMessageA`, `BitBlt` — no QPC. Its waits complete briskly through
  the same wineserver, X server, sink, and page.
- Conclusion: there is **no universal ~500 ms quantizer** in the shared
  host path. The quantizer acts on a wait that Doom (and per the user,
  Portal and Chromium) uses and Snake's loop does not depend on in the
  same way.

### Portal (integ dist, load ~27) — `portal-before2/`

- 22 present intervals in the record window: **min 1152 ms, median
  3948 ms**. Contention-dominated; uninformative about a 500 ms floor
  either way. (The first attempt, `portal-before/`, caught only the
  asset-loading phase — zero presents in-window; the probe was fixed to
  wait for ≥ 30 presents before recording.)

## Eliminated layers (evidence, not vibes)

| Suspect | Verdict | Evidence |
|---|---|---|
| Page-tier present delivery (vkwebgpu pump/rAF throttle) | Cleared | Pump debounce is 8 ms; `createFramePacer` is measurement-only; Doom/Chromium don't even use this tier for display. |
| Emulator main loop cadence | Cleared | rAF inside the runtime frame measured over 20 000 samples: **median 16.7 ms** (60 Hz), tail-3000 median still 16.7 ms. The loop that flushes X11 presents ticks at 60 Hz. |
| X11 present/composite path | Cleared | Code: draw handlers only `schedulePresent()` (a dirty flag); `tickXWirePresent()` → `flushPendingPresent()` composites once per main-loop tick. No timer anywhere in `source/x11wire/`. Sink pair-spacing ≈ 1 rAF confirms tick-rate flushing. |
| Host shell timers | Cleared | Served launcher JS: intervals are 5000 ms (persist), 250 ms (boot-only isolation poll), and the startup progress timer. No 500 ms periodic event touches the guest. |
| Emulated clock granularity | Cleared | `Platform::getSystemTimeAsMicroSeconds` = `gettimeofday` (µs); `getMicroCounter` derives from it; `sys_clock_gettime64` returns it directly. QPC's ultimate source is µs-grade. (Caveat: what Wine 10's ntdll does between the syscall and the guest's QPC read was not independently measured.) |
| Kernel timed waits (MT build) | Cleared by reading | `BoxedWineCondition::waitWithTimeout` = host `condition_variable::wait_for(emulatedMilliesToHost(ms))` — wakes on time/signal; futex waits use the true remaining time (200 ms re-check cap for signal delivery only). `emulatedMilliesToHost` is 1:1 unless clock adjustment is enabled. |
| VK bridge readback/present waits | Cleared | Readback flag waits are readback-only (10 s timeout); present path is proxied MAIN_THREAD_EM_ASM calls; not on Doom's path anyway. |
| Content-hash costs (portal-d24 lane) | Not the floor | Different mechanism (per-submit hashing, since fixed: submit serialize 84.3 → 22.9 ms/frame). Two orders below 500 ms and Portal-only; Doom/Chromium don't hash. |

## What Doom actually does (binary evidence)

`doom.exe` (748 182 B, x86-64 PE, Chocolate Doom lineage, MinGW build,
extracted from `prefix64.zip`) imports, completely:

- GDI32: `StretchDIBits` (the frame blit — one per frame)
- KERNEL32: `QueryPerformanceCounter`, `QueryPerformanceFrequency`,
  `Sleep`, critical sections, `VirtualProtect/Query`
- USER32: `PeekMessageA`/`DispatchMessageA`/`TranslateMessage`, window calls
- WINMM: `waveOutOpen/Write/PrepareHeader/...` (audio out)
- No SDL, no DirectDraw, no Direct3D.

So the engine's loop is: pump messages → check QPC → if due, run a tic
and `StretchDIBits` → otherwise `Sleep` (and/or block on waveOut buffer
recycling). With ~1.85 M instructions per cycle, the loop spends its
life inside one of those waits, and the wait's *effective* duration is
~500 ms instead of the intended ~1–35 ms.

## Ranked remaining suspects (all guest-side, Wine-mediated)

1. **Wineserver-serviced timeout wake** (`Sleep`/`NtDelayExecution` and
   message-wait timeouts are granted by the emulated wineserver). Guest
   profiles captured during boots show the wineserver taking 77–100 %
   of all sampled guest execution — it is by far the busiest guest
   process in this stack, and every frame-paced engine (Doom, Source,
   Chromium) blocks on it per frame, while Snake's timer/message loop
   tolerates/coalesces it. If the wineserver's own `epoll_wait` timeout
   servicing (our `kepoll`/`KThread::pollCond`) stretches its timer
   grants, all three user-observed programs inherit ~the same quantum.
   The Phase-2 wake-latency lane's `fc6818ec1` (poll deadlines across
   wakeups) touched exactly this code — it is the first place to audit.
2. **waveOut buffer-completion pacing** via the emulated DSP device:
   if the port syncs frames to audio-buffer recycling and our DSP only
   completes buffers on a coarse cadence, frames quantize to the buffer
   period. (Audio stage traffic is small but nonzero every window.)
3. **winex11 DIB flush synchronization**: `StretchDIBits` completion
   blocked on a wineserver round-trip per blit (see suspect 1 — same
   root, different call site).

## The experiment that convicts (built, ready, blocked only by the rig)

`web/tests/framefloor-probe.mjs` now supports `THREAD_DUMPS=1`: it
route-patches the stock launcher exactly like `demo-matrix.mjs` (sets
`BW_THREAD_DUMP=1` when the runtime URL carries `bw64diag=threads`) and
invokes `bw64_dump_guest_threads` from the page every
`DUMP_INTERVAL_S` seconds — zero guest-side diagnostic overhead between
dumps (unlike `?guestdiag=`, whose RIP sampler/memstats heartbeat
coincided with repeated boot wedges). One healthy Doom run with dumps
every 15–20 s through 2 minutes of steady state will show the frame
thread's `state=blocked syscall=… wait=<condition name>` distribution
across dumps — naming the wait directly. Command:

```
DEMO=doom DIST_DIR=<hardlinked integ dist, archiveBase→127.0.0.1:8901 mirror> \
SKIP_BUILD_WAIT=1 THREAD_DUMPS=1 DUMP_INTERVAL_S=20 \
URL_PARAMS="xwire=1&bw64diag=threads" \
node web/tests/framefloor-probe.mjs
```

## Rig health (why conviction is pending)

The box was saturated all night (load 4–44; two heavy probe slots +
near-constant `build-pending` from three other lanes' builds). Guest
boots in this environment are bimodal: healthy (map in ~90–200 s) or
wedged (boot processes exit, then total silence — the same signature
the GB lane documented). Of this lane's 7 Doom attempts: 1 clean
(doom-before, load ~4, pre-instrumentation), 1 mapped then crashed OOB
(ddraw3), 5 wedged/not-mapped (guestdiag ×2, winedbg ×2 counting the
300 s-timeout attempt, matrix ×1, final ×1). Snake and one Portal run
did complete through the same contention. Per-brief discipline, I
report the floor as **reproduced for Doom, mechanism localized to a
guest-side Wine-mediated wait, exact wait not yet convicted, no fix
landed** rather than guessing.

## Files

- Probe: `web/tests/framefloor-probe.mjs` (committed) — captures console
  stream (incrementally, crash-safe), METRICS windows, canvas-change
  sampler, in-frame rAF cadence, optional thread dumps. Analysis
  snippets were run from `/tmp/ff_analyze*.py` (not committed).
- Captures: `test-results/framefloor/{doom-before,doom-ddraw3,
  portal-before2,snake-matrix-diag}/` in this worktree.

---

## Continuation — 2026-10-27 (frame-floor lane, second session)

### Verdict up front

The wineserver-timeout suspect is **acquitted by direct measurement**,
and the floor's mechanism is convicted elsewhere: Doom's frame time is
its own per-frame instruction cost at the throughput the x64 JIT
actually delivers. The dominant fixable cause found: **any basic block
containing a DIV or IDIV was rejected by the JIT wholesale and
interpreted forever** — in the one healthy baseline run, ~49% of all
block entries system-wide (43.6M of 88.6M) fell back to the
interpreter. DIV/IDIV now have a JIT fast path whose bail cases
re-enter the interpreter with bit-exact state. In a DIV-dense guest
micro-probe the fix is worth **11.5×** (28,564 ms → 2,473 ms guest
time) with bit-identical results across a 200K-iteration divide
battery spanning all widths and signs. A before/after
Doom histogram could **not** be collected on the day: after the
baseline run, every windowed guest boot — on the shipped build as
control, identically — failed to map within timeout under all-day box
load 8–17 (see Rig health). The Doom claim is therefore mechanism +
census + micro-probe proof, not a measured Doom fps change.

### 1. Audit of the suspect (fc6818ec1, kepoll/KThread::pollCond)

- `fc6818ec1` is semantically sound: the poll deadline is preserved
  across spurious wakeups via `condStartWaitTime`; no path re-arms or
  extends a guest timeout toward 500 ms.
- Timeout primitives are exact: `wait_for` maps 1:1, the futex path
  uses true deadlines, epoll passes the guest timeout through.
- One deliberate 200 ms chunking exists (x64 signal re-checks in the
  wait loop). It can only *shorten* individual waits and re-check; it
  cannot stretch a 35 ms request to ~500 ms.
- Wart (opposite sign, not the floor): `condStartWaitTime` is shared
  between `internal_poll_impl` and `KThread::sleep` and is not reset
  on the `-K_CONTINUE` path → early returns / busy-spin. This may
  explain the wineserver's frequent `running` samples in thread dumps.
  Not changed: no measurement ties it to a user-visible defect.

### 2. The wait family, measured in-guest (clockprobe.exe)

A purpose-built guest micro-probe (windowed process, guest QPC
deltas): QPF = 10,000,000 (exact); Sleep(1) → 1–5 ms; Sleep(10) →
10–13 ms; Sleep(35) → 35–38 ms; MsgWaitForMultipleObjects(35) →
35–41 ms; MsgWait(100) → 101–103 ms. A 200M-iteration spin took QPC
69,873 ms vs host 70,082 ms — **no clock dilation**; execution is
simply slow. Every wait Doom's loop can block on is granted at the
requested granularity. (Guest console printfs block the caller
50–250 ms each — a proxy-cost curiosity on a path Doom does not use.)

### 3. Elimination rounds

- **Audio**: a full `-nosound` Doom run still quantizes frame bursts
  at the same ~650 ms (loaded) intervals and 1×/2×/3× multiples. The
  WINMM wait seen in dumps is the game pacing itself, not the cause.
- **Loop scaffolding**: guest benches of Doom's exact loop shape —
  message pump + Sleep(1) — run at 32.3 fps guest time; pump-only
  35.1; pump + Sleep(1) + StretchDIBits(640×400) 31.4; pump + DIB
  34.8. Everything around the game code sustains the 35-tic cap.
- Thread dumps in steady state agree: Doom's main thread cycles
  pump ↔ WINMM wait ↔ running with `doom.exe` frames on its stack;
  the wineserver when blocked waits `epoll_wait` with a 16 ms timeout
  (never ~500 ms). (Dumps remain usable only as short bursts: every
  THREAD_DUMPS run eventually froze the guest — the dump ccall runs
  on the runtime main thread and deadlocks against a guest thread
  holding a lock across a proxied call. Reported as a rig defect.)

### 4. Throughput evidence (doom-stats2, shipped build c01fdb666)

- METRICS windows during steady play: `instructionsRetired` grows
  only ~1.75–1.95M per 500 ms window (~3.4–3.7M inst/s system-wide).
- JIT counters (cumulative): blocksEntered 88.6M, fallbacks 43.6M
  (**49%**), blocksEmitted 6,540, ~4.0 instructions per block entry.
- Reject census (`rejectTop`, distinct blocks by first failing op):
  FF 363, 8B 162, 89 131, then an SSE cluster (0F 6F 87, 0F 10 54,
  0F EF 54, 0F 28 51, 0F 11 46, 0F 6E 40, 0F 29 20 ≈ 352), 0F 05
  (SYSCALL) 47, F7 15. Counts are per distinct block, not
  entry-weighted; the fallback ratio is the entry-weighted figure.

### 5. Mechanism (code)

`CPU64::tryWasmExec` rejects a whole block (≤24 ops) if **one** op's
plan is not JIT64_FAST, negative-cached by page generation; rejected
RIPs are interpreted forever (`insnsFallback` counts those entries).
DIV/IDIV (F6/F7 group 3, /6 and /7) never decoded at all — the
decoder's stated reason was that #DE delivery "cannot be represented"
in the straight-line wasm block model. That claim is wrong in this
codebase: the emitter already bails mid-block with exact state —
`emitRipAfter` keeps L_RIP at the next op after every instruction,
and `emitStoreGenCheck`/`emitLimitCheck` store a partial `execCount`
and branch to the epilogue. The kCovered table in `jit64.cpp`
recorded Doom's measured instruction mix all along (F7 0.32% +
F6 0.22% of instructions; "/6 DIV /7 IDIV stay interpreted") — each
such instruction poisoned its entire block (~24 ops) back to the
interpreter.

Interpreter semantics pinned from `cpu64.cpp` (the JIT mirrors them
exactly): the **only** fault is divisor == 0 (#DE at the DIV's RIP);
quotient overflow is **not** checked for sizes 2/4/8 — results are
computed in wider C types and truncated (size-1 IDIV also truncates);
flags are never written.

### 6. The fix (this lane's commit)

- `jit64.cpp`: decoder and classifier admit F6/F7 /6 /7 as
  J64_GRP3/JIT64_FAST; kCovered and store-classification comments
  updated (DIV/IDIV write no memory — the existing predicates were
  already correct for them).
- `jit64wasm.cpp`: the emission validator admits /6 /7; the GRP3
  emit case routes them to a new hand-written `emitDivGen` (the
  generated table emitter has no op-index parameter, which the bail
  needs). Guards, evaluated before any state changes: divisor == 0
  bails (all sizes); for 16/32/64-bit forms, a high dividend half
  that is not the zero (DIV) or sign (IDIV) extension of the low
  half bails — the shape real code produces via CWD/CDQ/CQO or a
  zeroed high half always takes the fast path; 64-bit
  INT64_MIN / −1 bails (wasm `div_s` would trap; the interpreter's
  `__int128` math truncates). The 8-bit form needs no high-half
  bail: its 16-bit intermediate is computed and truncated exactly
  like the interpreter's. A bail stores `execCount = <op index>`
  and branches to the epilogue with L_RIP already at the DIV — the
  `emitLimitCheck` convention — so the interpreter re-executes the
  divide with bit-exact state, including exact #DE delivery.

### 7. Verification

- Native JIT unit gate (`jit64_tests`): **650 passed, 0 failed**.
  Sabotage check: reverting the decoder gate turns exactly the 20
  new DIV checks red (630 passed / 20 failed) — the guard bites.
- Emitted-module execution gate (`jit64_wasm_tests.mjs`, full
  file): **280 passed, 0 failed**, including a new DIV/IDIV section:
  targeted cases per width/sign, divide-by-zero and bad-high-half
  bails, a mid-block bail (earlier ops stand, RIP/execCount at the
  DIV), and a 160-case differential fuzz against a JS reference
  model that mirrors the interpreter. The byte-identical generated-
  emitter gate passes (compared=0, skipped=31304, failures=0) and
  the table generator reports in sync. The fuzz caught one real
  bug during development: the 64-bit sign guard used
  `shl 63`/`shr_s 63` (keeps only bit 0, so the expected high half
  was always 0); corrected to a lone `shr_s 63`.
- Native suite: `make -C project/linux test -j2` exits 0; the
  `__TEST` binary reported **641 tests OK, 0 FAIL** before ending
  inside its final crash-path test without printing a summary (the
  suite's designed crash test produces the trailing fault output).
- In-guest differential (`tools/guest-fixtures/divprobe.c`, new):
  checksum over a 200K-iteration battery (all widths, signed and
  unsigned, register forms) is **identical** with the JIT on the fixed build
  and with the interpreter: 16129164002239702816. The DIV-dense
  timing loop: **2,473 ms (JIT, fixed build) vs 28,564 ms
  (interpreter)** guest QPC — 11.5×, sink value identical.
- Portal smoke on the fixed build (ff-dist5 = this lane's dist):
  mapped at 152.6 s, **pageErrors 0**, 60 presents and 98 rendered
  frames in the record window, 0 pipeline failures (absolute fps
  is load-dominated — load 13.7 at end — and is not claimed).
- JS unit suite (`web/tests/suite/runner.mjs --unit`): **297
  passed, 0 failed**. This branch's suite has 297 tests; the
  integ-config baseline quoted for the merge tree is 299/0 — the
  delta is branch state, and there are no failures on either.
- Build: `BW64_BUILD_TARGET=wasm64-mt-jit tools/bw64-build.sh` →
  "Browser export verified" (wasm sha256 0263df64…). Note the
  branch's older build script does not fail loudly when
  `tools/browser/build.mjs` fails (it printed BUILD COMPLETE over a
  staging error twice); the verify-dist line is the only
  trustworthy signal. First build attempt also used the default
  non-JIT target and produced an interpreter-only runtime
  (`engine:"interp"` in the stats JSON is the tell).

### 8. Not collected, and what remains for the next lane

- **Doom before/after present-interval histograms and Snake fps on
  the fixed build**: not collected. After doom-stats2, windowed
  boots failed to map all afternoon on both builds (fixed:
  doom-after1/2/3; shipped control: doom-control3 — LAUNCH FAILED,
  302 console lines, same shape). Snake via demo-matrix hit the
  same wall (jit0 and jit1 attempts, not-mapped). One attempt at a
  time in a quiet window, first and alone, is the known-good
  recipe; re-run `ff-stats-probe` (Doom) when the box is quiet to
  get the after histogram and the post-fix census — F7 should be
  gone from `rejectTop` and the fallback share should drop.
- Residual reject leaders for follow-up, in census order: FF
  group-5 blocks (363 — attribution is per first-op vs failing-op,
  unverified; needs per-RIP reject logging to convict), the SSE
  cluster (~352 distinct blocks across MOVDQA/MOVUPS/PXOR/MOVAPS/
  MOVD forms), 0F 05 SYSCALL stubs (47; interpreter-only by
  design, cheap per entry but hot on wineserver paths), and x87
  (interpreter-only by design).
- The `condStartWaitTime` sharing wart (§1) and the boot-wedge
  family (§3) remain open, both unrelated to this fix.
- No known-gaps register row applied: JIT opcode coverage is
  tracked in-code by the kCovered table in `jit64.cpp`, which this
  fix updates in place.

### Files (continuation)

- Fix: `source/emulation/cpu/jit64.cpp`,
  `source/emulation/cpu/jit64wasm.cpp`; guards:
  `source/emulation/cpu/tests/jit64_tests.cpp`,
  `source/emulation/cpu/tests/jit64_wasm_tests.mjs`.
- Micro-probe: `tools/guest-fixtures/divprobe.c` (companion to
  jitbench64.c; build with `x86_64-w64-mingw32-gcc -O2`).
- Captures: `test-results/framefloor/{doom-stats2,doom-control3,
  div-on2,div-off2,portal-after1}/` in this worktree. Session
  scratch (ff-* probe variants, scratch dists) was removed; the
  worktree `dist/` is this lane's own build (it was a symlink to
  the integ dist at handoff — restored after verification).
