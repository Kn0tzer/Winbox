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
