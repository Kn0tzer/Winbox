# FD-271 — Chromium boot fault loop: loop fix + trigger fix

Filed by the FD-271 lane, 2026-11-10. Lane branch `lane-fd271`, tip
`69f2bfd5f`. Backport branch `lane-fd271-b14` (batch-14 base), tip
`970047718`. Record `docs/reports/fd271-chromium-boot-fault-loop-20261110.md`.

## What the user would see

Opening Chromium (the browser program in the live checks) sometimes never
showed a picture: the start died before the desktop was ready, over and
over, with the same fault. After this fix it starts reliably again.

## The defect, in two parts

The Chromium boot died in a repeated fault loop before the desktop was
ready. The loop and its trigger are two separate defects in the same
boot path; both are fixed here.

### Part 1 — the loop (fixed, commit `0647f59da`)

A fresh clone child's first fault was delivered on an alternate signal
stack that was never set up (a fresh pthread starts with SS_DISABLE,
sp=0, size=0), so every signal-frame write faulted at `0xfffffd08` and
re-delivered identically until FD-205 containment ended the process at
count 129. Fix: 32-bit `KThread::runSignal` falls back to the normal
stack when no alternate signal stack is configured. The 64-bit delivery
(`signalFrameBase64`) already guards on `alt.ssSp` and does not have
this defect.

Proof (preserved logs, `test-results/fd271/diag-run3/`): exactly ONE
fault event (addr `0x10584384` eip `0x10584384`, count=1), ZERO
`0xfffffd08` lines, ZERO containment lines. Unfixed `diag-run2`: 220
raw memfault lines (hundreds of deliveries, 2 containments).

### Part 2 — the trigger (fixed, commit `2b84f4ead`)

With the loop fixed, the session still died: after the single fault the
thread's eip moved to Wine's fault handler (`0x106d7af0`), the entry
path reported `Failed to get first op for thread 80 of process 77 at
address 106d7af0`, the process exited, and the session never showed a
picture (0 frames, 904 s timeout). The first fetch returned null even
though the page was valid and the operation was cached.

Root cause, proven by the diagnostic builds:

- The faulting instruction `0x10584384` is glibc `__clone`'s return
  point (`pop edi` immediately after `int 0x80`, clone syscall 120),
  at libc file offset `0x124384` (libc mapped at `0x10460000`).
  The fault is a FETCH fault (addr == eip), delivered via the
  `getNextOp`-returns-null path, not a data access.
- The diagnostic build's `fd271-firstop` line reads
  `tid=81 eip=10584384 csBase=00000000 eipReg=10584384 term=1 proc=1
  decOp=1 pageFlags=00000025 validExec=1`: the fresh clone child's
  `thread->terminating` is TRUE at its first fetch, even though the op
  IS cached (decOp=1), the page IS mapped+exec (pageFlags `0x25` =
  READ|EXEC|MAPPED, validExec=1), and the process is alive (proc=1).
  `CPU::getNextOp` returns null SOLELY because of its terminating
  check; `NormalCPU::getOp` has the same check. The caller then treats
  the null as a fault and calls `seg_mapper(eip)`, fabricating a
  SIGSEGV (err=0, "no page") for a perfectly valid address.
- The caller is `exit_group`: the termination-diagnostic build's
  console proves the chain (diag-run9):
  `[42.4s] TransactNamedPipe: Broken pipe. (0x6D)`;
  `[42.5s] fd271-exitgroup pid=77 tid=78 code=1` — the main thread
  calls `exit_group(1)`; `[42.5s] fd271-termother pid=77 tid=80
  found=1` — exit_group terminates the just-created clone child tid
  80; `[42.7s] fd271-firstop tid=80 eip=10584384 term=1 ...`;
  `[42.7s] guest-memfault32 pid=77 tid=80 addr=0x10584384
  eip=0x10584384 err=0 delivered=1 count=1`;
  `[42.7s] Failed to get first op for thread 80 of process 77 at
  address 106d7af0`; `[42.8s] KNativeSystem::exit(1)`.
  Exactly 1 memfault line, 0 `0xfffffd08` lines.
- `TransactNamedPipe: Broken pipe. (0x6D)` is NOT the cause: it
  appears exactly once in EVERY run, clean and firing alike. It is a
  standing Chrome-under-Wine warning.
- The process (pid 77) exits code 1 in CLEAN runs too
  (diag-run6: exitgroup pid=77 code=1 at 24.0 s; diag-run8: same at
  35.0 s), and the same exit_group terminates the same tid 80 in the
  clean run6 (`fd271-termother pid=77 tid=80 found=1` at 24.0 s) with
  NO firstop line and NO fault. So the difference between a clean run
  and a firing run is purely the RACE: whether tid 80's first fetch
  lands before or after exit_group sets the terminating flag. Pid 77
  is a short-lived Wine startup helper that exits(1) routinely; the
  window is mapped by a different, long-lived process (pid 10).
- Why the session fails to map in firing runs: after the fabricated
  fault, `kpanic` calls `KNativeSystem::exit(msg, 1)` and then the
  host `exit(1)` (`source/util/log.cpp`), which stops the guest
  entirely. The console after 42.8 s in run9 shows NO guest activity
  at all (no syscalls, no X11, no Vulkan — only JS-side logging),
  while run6 after 24 s shows rich guest activity (X11 handshake,
  Vulkan traps). The fabricated fault kills the whole guest, not
  just pid 77.

Fix (commit `2b84f4ead`): in the three places a null `getNextOp` is
treated as a fault, check `thread->terminating` FIRST and retire
silently WITHOUT calling `seg_mapper`:

- `platformThread` first fetch
  (`source/emulation/cpu/normal/normalPlatformMultiThreaded.cpp`):
  skip seg_mapper and the second getNextOp/kpanic, fall through to
  the existing retirement block.
- `NormalCPU::run` (`source/emulation/cpu/normal/normalCPU.cpp`):
  guard before `seg_mapper`, return if terminating.
- `CPU::runNextSingleOp` (`source/emulation/cpu/common/cpu.cpp`):
  same guard, return false.

Rationale: on real Linux a thread killed by `exit_group`/exec never
takes a SIGSEGV for its next instruction; the null fetch of a
terminating thread is a retirement, not a fault. The guard does NOT
mask genuine decode failures on non-terminating threads (the guard is
false there, so `seg_mapper` still runs and the panic still fires).
The 64-bit path has no equivalent defect (`cpu64.cpp` has no
`seg_mapper` on a null fetch; the 64-bit platformThread just loops
`cpu64->run()` and breaks on terminating). The temporary
termination-caller diagnostics (`fd271-termother`, `fd271-onexec`,
`fd271-exitgroup`) were removed in the fix commit; the first-op
diagnostic was kept as the verification instrument and removed in
commit `69f2bfd5f` after it served its purpose.

## Proof

All runs use the local mirror (`http://127.0.0.1:8901/`, chromium.zip
200) and `DEMO=chromium JIT=0` via `web/tests/demo-matrix.mjs`.
The canonical clean-run picture is 1,273,748 lit pixels.

### Fix build (lane tip `2b84f4ead`, wasm `7afeabd8…`, 5,757,213 B)

- diag-run10: CLEAN — mapped, tMapped 64.384 s, 8 frames presented,
  overlay cleared, 1,273,748 lit pixels, 0 fault lines, 0 firstop
  lines (race did not fire).
- diag-run11: **RACE FIRED AND THE FIX HELD** — console line 938:
  `fd271-firstop tid=81 eip=10584384 csBase=00000000 eipReg=10584384
  term=1 proc=1 decOp=1 pageFlags=00000025 validExec=1` — the same
  tid (81) and eip as the firing diag-run5, term=1 — and the run
  MAPPED: tMapped 57.723 s, 7 frames, overlay cleared, 1,273,748 lit
  pixels, **0 fault lines** (no guest-memfault, no `0xfffffd08`, no
  "Failed to get first op").

### Final build (lane tip `69f2bfd5f`, wasm `05bc6962…`, 5,756,897 B)

- diag-run12: CLEAN — mapped, tMapped 56.520 s, 7 frames presented,
  overlay cleared, 1,273,748 lit pixels, 0 fault lines. The final
  build differs from the fix build only by removing the diagnostic
  log line.

### Backport build (batch-14 base `d3b9bb7f5` + loop fix
`685274969` + trigger fix `970047718`, wasm `14f26e7f…`, 5,752,528 B,
support archive `caf6b5aed680` = batch 14's)

- diag-run-b14-1: NON-RESULT — the test browser closed while the
  driver waited for "first window mapped"
  (`page.waitForFunction: Target page, context or browser has been
  closed`); guest console stopped at 24.7 s with 0 fault lines; box
  load 6.09 at the time. Harness/infrastructure failure, not a guest
  fault. Discarded.
- diag-run-b14-2: NON-RESULT — timed out unmapped (tMapped null, 0
  frames, stage "not-mapped") because the local games mirror had died
  (chromium.zip returned 000); the guest never received the archive
  and never started. The mirror (`/tmp/fd271-games-mirror.mjs`) was
  restarted and verified (chromium.zip 200). Discarded.
- diag-run-b14-3: CLEAN — mapped, tMapped 73.402 s, 5 frames
  presented, overlay cleared, 1,273,748 lit pixels, 0 fault lines.

Closing terms met: the consumer Chromium check passes on the fixed
build with no fault-driven restart and no `0xfffffd08` fault in its
preserved console, AND the same check passes on the previous
release's build with the fix applied (shared code).

### Diagnostic runs that established the mechanism (unfixed builds)

- diag-run4 (build `0aa1cb29e`, wasm `40ee700f…`): CLEAN — mapped,
  5 frames, 1,273,748 lit pixels, tMapped 73.834, 0 fault lines.
- diag-run5 (same build): FIRED — firstop `term=1`, exactly 1
  memfault, 0 `0xfffffd08`, `Failed to get first op`, `exit(1)`, not
  mapped (906.6 s timeout).
- diag-run6 (termination-diagnostic build `8ec72056b`, wasm
  `55e23dce…`): CLEAN — mapped, 6 frames, 1,273,748 lit pixels,
  tMapped 56.838, 0 fault lines, exactly 1 `fd271-termother` line
  (pid 77 tid 80 at 24.0 s).
- diag-run7 (same build): LOAD-STARVED NON-RESULT — not mapped,
  guest alive at 880 s but too slow to map in 904 s (box load 6.17).
- diag-run8 (same build): CLEAN — mapped, 4 frames, 1,273,748 lit
  pixels, tMapped 87.265, 0 fault lines.
- diag-run9 (same build): FIRED — caller proven (exitgroup →
  termother → firstop), not mapped, 0 `0xfffffd08` lines.

Trigger firing rate on unfixed builds: ~1 in 3–4 runs (run4 clean,
run5 fired, run6 clean, run7 non-result, run8 clean, run9 fired).

## FD-115: mechanism identified, tally re-settled

FD-115's original 2026-11-02 instance (eip `0x10584384`, addr
`0xfffffd08`) is this defect: the alt-stack delivery (loop) plus the
terminating-flag trigger. The batch-11 a4 and batch-12 a1/a2/b3
sightings are the same signature. The standing tally "3 in 60"
through batch 14 no longer describes the consumer series: the
batch-15 deploy consumer series went 1 of 4 mapped (the fault firing
far more often than the old tally), and the diagnostic runs above
show 2 fired of 5 decisive runs on the unfixed builds. On the fixed
builds the consumer check is 3 of 3 mapped with 0 fault lines
(run10, run11, run12), and the backport build is 1 of 1 mapped
(b14-3). The entry stays closed on its surfacing term (CL-127); this
update records that the mechanism behind the original instance is
now identified and fixed (see the FD-115 entry update).

## Notes and non-scope items

- The nested-signal alt-stack frame observation (32-bit
  `runSignal` has no `onAlt` guard; a nested delivery while on the
  alt stack resets the frame) was deliberately left unchanged.
- The stale Caddy configuration flagged by an earlier lane (the live
  server returns no `Access-Control-Allow-Origin` header for
  `/games/*` despite the Caddyfile setting it) was NOT fixed here —
  it needs a separate permission (server restart). That is why the
  diagnostic runs used the local mirror.
- Disk: the lane worked under the 5 GB guardrail the whole time
  (7.8 G at the start, 4.5 G at the lowest — a transient dip caused
  by another tenant's writing, recovered to 5.7–5.8 G without any
  lane deletion). No retained runtime archive was deleted by this
  lane.
- The `fd271-firstop` diagnostic line is preserved in the run logs
  (`test-results/fd271/diag-run5/`, `diag-run9/`, `diag-run11/`);
  the diagnostic commits themselves (`0aa1cb29e`, `8ec72056b`) remain
  in the lane branch history below the fix.

## Evidence

- `test-results/fd271/diag-run2/` — unfixed loop (220 memfault lines)
- `test-results/fd271/diag-run3/` — loop-fix proof (1 fault event)
- `test-results/fd271/diag-run4/` … `diag-run9/` — trigger mechanism
  + caller proof
- `test-results/fd271/diag-run10/`, `diag-run11/`, `diag-run12/` —
  fix-build proof (run11 = race fired, mapped, 0 faults)
- `test-results/fd271/diag-run-b14-1/`, `diag-run-b14-2/` (non-results),
  `diag-run-b14-3/` — backport proof
- Builds: `build-fix.log`, `build-fix-trigger.log`,
  `build-final.log`, `build-b14.log` (all "Browser export verified" +
  "[bw64-build] BUILD COMPLETE")
- Commits: loop fix `0647f59da`, trigger fix `2b84f4ead`,
  diagnostic removal `69f2bfd5f`; backport loop fix `685274969`,
  backport trigger fix `970047718`.
