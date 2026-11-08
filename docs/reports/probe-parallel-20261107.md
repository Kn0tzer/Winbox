# Probe parallelism on the 3-core split — measurement attempt and the build-relay starvation finding (FD-196)

- **Branch:** `lane-probe-parallel` off integration tip `faf95d26c`.
- **Worktrees:** `wt-probe-parallel` (the branch) and
  `wt-probe-parallel-2` (detached at the same commit; created so a
  third concurrent probe could be a fully independent
  process/tree/result-dir). 422 MB each, no generated state.
- **Test target:** every probe ran against the live site's build
  read-only (`WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox`;
  BUILD_INFO git_sha `d50bcc02f`, wasm sha256 `92b36180…`).
  `git diff d50bcc02f..faf95d26c` touches only `.woodpecker.yml`
  and docs, so the product code under test is the tip's;
  `ProbeTest.setup` hash-verifies the wasm/JS against BUILD_INFO
  on every run, and the one executed run's evidence BUILD_INFO
  confirms it.
- **Conditions:** project work on cores 0–2 (a Minecraft server
  owns core 3); ~20 lanes active; 1-minute load average between
  ~7 and ~25 during the attempts; free disk fell from 12 GB to
  3.9 GB over the same period from the lane wave's builds.

## The question

Can browser test probes run concurrently on the box — at minimum
two at once, aiming for three — with the same reliability they
have one at a time, under today's conditions (cores 0–2 only,
many lanes running)?

## Verified before running (re-read at the tip, not assumed)

- `web/tests/probe-slots.mjs` already defaults to **2 heavy +
  2 light** slots (`PROBE_SLOTS` / `PROBE_LIGHT_SLOTS`), FIFO,
  with a build-pending drain. Suite browser tests use the light
  pool via `ProbeTest`. The one-at-a-time rule is an older
  project convention, not what the code enforces.
- `web/tests/parallel-probe-README.md` records the previous
  verification: 2× concurrent novideo probes and 2× concurrent
  SwiftShader boot probes all reached READY — proven when all
  four cores were free for project work. It attributes the
  original flakiness to CPU contention between two SwiftShader
  instances, with mitigations (graphics probes ≤2, small
  viewports, staggered launches).
- Probe isolation is per-test by design (`suite/lib/probe-base.mjs`):
  own slot, own `test-results/suite-<tag>` dir, own mkdtemp serve
  root (the dist is symlinked in, never copied), own Chromium
  server on an ephemeral port with its own user-data dir. No
  shared file/port collision vector; the contended resources are
  CPU, RAM, and disk.
- Only two suite tests boot a guest while staging nothing from
  the tree: `boot64-smoke` and `boot32-vkboot`. A third,
  `d3d11-triangle`, also boots fixture-free by the guest-fixture
  definition but stages tree-built DXVK DLLs
  (`tools/dxvk/build.w64/…`), so it is **not** runnable from an
  unbuilt worktree (see serial-3). The 3-probe design therefore
  runs boot64 + boot32 from the branch tree and a second,
  independent boot32 from the detached second tree.

## Method

Rounds driven by a preserved apparatus
(`test-results/probepar/apparatus/` — `probepar-driver.sh` v3.1 +
`run-round.sh`): a serial baseline per test, then three
2-concurrent rounds, then three 3-concurrent rounds (light cap
raised to 3 only in the round's own processes), each round gated
on disk ≥7 GB free and no live probe holders, launches staggered
3 s, per-test logs + slot-status snapshots every 15 s + evidence
copies preserved under `test-results/probepar/<round>/`.

## What actually happened — probe starts starved behind a build relay

Every round was decided before any browser started, by the build
coordination, not by the slot counts:

| Attempt (UTC) | Test(s) | Outcome |
|---|---|---|
| 04:30:46–~04:35 (attempt 0) | boot64-smoke | Queued behind `build-pending` (5 waiting notices); stopped by the lane when its SSH link dropped and the attempt was restructured. Never acquired. |
| 04:53:42–05:08:51 (serial-1) | boot64-smoke | **FAIL 901729 ms** — `timed out waiting for a probe slot`, never acquired, nothing tested. |
| 05:08:51–05:23:54 (serial-2) | boot32-vkboot | **FAIL 900490 ms** — same slot timeout (`probe-slots.mjs:254`), nothing tested. |
| 05:37:18–05:44:47 (serial-3) | d3d11-triangle | **Acquired** light-0 at 05:41:40Z in a handoff gap; passed the build-idle wait; booted the guest (see adjudication); **FAIL 443911 ms** on a missing tree-built DLL. |
| 05:56:51–06:11:54 (serial-1b) | boot64-smoke | **FAIL 901735 ms** — same slot timeout, nothing tested. |

The `build-pending` claim was held **continuously from at least
04:24Z to past 06:11Z (≥107 minutes)** by a relay of at least six
builds — observed claimants: pid 1203100 (wt-dnsresolve),
1399646 (wt-merge-batch10), 1446143, 1477753 (wt-fd26-base,
running `make wasm64-mt-jit`), 1518740 (wt-fd194), 1660403 —
with only sub-2-second handoff gaps. Other lanes' probes queued
behind the same relay (live waiters from wt-dnsresolve and
wt-fd26-base in slot status); one of theirs (sseprobe-bench) did
get through a gap and ran, as did serial-3. The 2- and
3-concurrent rounds never started: first the relay, then the
disk gate (free disk crossed the ~6 GB guardrail floor at
~05:47Z and fell to 3.9 GB; driver v3.1's rounds abort rather
than add browser scratch to a disk emergency owned by other
lanes' builds).

### Mechanism (read from the tip's source)

1. `probe-slots.mjs` `acquireProbeSlot`: acquisition requires
   `!legacyLockHeld() && !buildPending() && queueIndex < free`.
   A live `build-pending` blocks **both** pools even with slots
   free and queue position 1.
2. `tools/bw64-build.sh` publishes its pid to `build-pending`
   when it starts *waiting* — its own comment: "build-pending is
   claimed by builds that do not hold the lock yet" — and removes
   it only at exit, only if it names itself. A queued build
   therefore holds probes back for the entire build queue, not
   just for active builds.
3. `ProbeTest.setup` then calls `waitForBuildIdle()`
   (`web/tests/build-lock.mjs`, 600 s default), which waits for
   the build **lock** — a second, serial build gate after
   acquisition, there to stop a probe launching against a
   half-replaced dist.

Running probes are never displaced: builds drain-wait for live
slot holders (`probes_busy()` covers both pools). The starvation
is purely about *starting*.

### Serial-3 adjudication

The failure is a lane error in test selection, not a product or
coordination failure: `d3d11-triangle`'s `run()` first completes
`bootGuest()` and the `bwRuntime.ready()` wait, and only then
stages files; it threw `ENOENT … tools/dxvk/build.w64/src/d3d11/
d3d11.dll` at the first generated file (`probe-base.mjs:181`).
The preserved console (1,641 lines) and evidence BUILD_INFO
(`d50bcc02f` / wasm `92b36180…`) show a real boot of the live
build. So the one probe that started **worked** — acquisition,
build-idle wait, browser launch, guest boot — during the relay,
on a loaded 3-core box. It is a sample of one, alone; it says
nothing about concurrency.

## Conclusions

- **No slot default was changed.** Tonight produced no
  multi-probe concurrency measurement — the probes could not
  start — so there is no evidence on which to raise the light
  pool to 3, and the README's 2× result remains the highest
  proven concurrency. Defaults stay **heavy 2 / light 2**;
  `docs/standards/checks.md` §6 now records this measurement
  and that the 3× measurement is still owed.
- **The binding constraint on browser-test parallelism under
  today's conditions is the build relay, not the slot system.**
  Filed as **FD-196** (lane-local number) in the register's
  tooling section: while any build is queued, no probe can
  start, for an unbounded time; suite tests convert that into
  900 s failures that test nothing. Any future fix belongs to
  the build/probe coordination (e.g. claiming `build-pending`
  only while holding the build lock, or a bounded probe
  window), and only then does raising slot counts mean
  anything.
- **Still owed:** the actual 2×/3× concurrency measurement
  under the 3-core split. The apparatus is preserved verbatim
  (`test-results/probepar/apparatus/`) and reruns the full
  round plan unchanged. Preconditions for a meaningful rerun:
  `build-pending` absent for several minutes (no build relay)
  and ≥10 GB free disk. The laptop was not used: running the
  suite there would need installs this lane is barred from
  making, and the user's own browsers are not test browsers —
  as a capacity route it remains a workaround, not the fix.
- This lane created no private stores and ran no builds; the
  only processes it stopped were its own (attempt 0's runner
  and its round drivers). No other lane's work was touched.

## Evidence index (all under `test-results/probepar/` in the branch tree)

- `driver.log` — the full round-driver timeline, including the
  v1 strict-gate wait (16 min, no window), the v3 disk-gate
  design slip and its v3.1 correction.
- `serial-1-boot64/` — attempt 0's preserved log (build-pending
  wait, SSH drop).
- `par-serial-1-boot64/`, `par-serial-2-boot32/`,
  `par-serial-1b-boot64/` — per-round logs, slot snapshots
  before/during/after; the FAIL lines and the slot-timeout
  error text are in each test's own log.
- `par-serial-3-triangle/` — the executed run, including
  `evidence-d3d11-triangle/console.log` and `BUILD_INFO.json`.
- `apparatus/` — the exact driver and round scripts used.
