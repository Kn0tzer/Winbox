# Found-defects register — the development queue

**What this is.** Every bug, defect, or gap that a lane of the recent
campaigns *found but did not fix*, collected in exactly one place, in
plain terms, with its seriousness and a pointer to the evidence. This
register is the project's development queue: a found problem is not
done when its lane's report is filed — it is done when it is closed
here, with proof. **Entries are never deleted.** An item that is
later fixed moves to the closed section with its proof; an item that
turns out not to be a defect is marked so, with the reason.

**Scope of this first sweep (2026-10-31).** The campaigns of
2026-10-29 → 2026-10-31: the DirectX 12 close-out aftermath, the
dead-start and Chromium hunts, the all-demos sweep and its three
class lanes, the broad-conformance analysis, and the cleanup /
logging / error-recovery pathfinder + ten-lane wave — plus the
remaining OPEN rows of the standing registers
(`docs/known-gaps/kernel.md`, `gl.md`, `coverage.md`). The DX12
campaign register and `docs/known-gaps/vulkan.md` closed at OPEN 0
and are not duplicated here. Where a standing register already
tracks an item in detail, this register carries the queue entry and
names the standing row as the evidence; the standing row remains
the detailed record.

**Severity, in plain terms.** *Critical* — stops people using the
site, or whole classes of programs cannot run. *High* — can kill
the whole emulator, or blocks a class of programs. *Medium* —
breaks a feature or some programs, or produces silently wrong
results. *Low* — narrow, cosmetic, developer-only, or a
verification debt (something unproven rather than known-broken).

**Maintenance rule.** When a lane finishes, any problem it found but
did not fix is added here before its report is considered filed.
Closing an entry requires naming the fixing commit and the proof.
Fixes that exist only on a lane branch are listed in section B and
do not close the entry until they land.

---

## A. Open defects and gaps

### Product-facing

- **FD-01 — First-time visitors could not reliably start a
  program on the first attempt. CLOSED 2026-10-31 (CL-06),
  REOPENED the same day — the closure's measurements are
  void (see FD-43 and the reopening note closing this
  entry). CLOSED AGAIN 2026-10-31 — see CL-17.** *(Critical)*
  Two impatience defects in the staged Windows system files
  (Wine 8.0's desktop initialization), invisible on real
  hardware: (1) a process that asks for the desktop starts the
  desktop process and waits only 10 seconds for it; on timeout a
  detached stand-in desktop is fabricated, the real desktop's
  window is refused, and the session's graphics-driver
  announcement never happens; (2) each process checks that
  announcement exactly once when it first loads its graphics
  driver — a miss installs the null (no-display) driver
  permanently for that process, which is the "explorer process
  failed to start" failure. Under emulation the desktop
  routinely needs longer than 10 seconds while the browser is
  still loading the ~210 MB runtime, so cold first visits lost
  both races; warm restarts usually won them (which is why the
  restart workaround helped). The fix is a backport patch to the
  staged system files (the wait raised to 300 s; the
  announcement check retries for up to ~300 s), proven
  before/after with fresh browser profiles and the restart
  workaround's budget exhausted: before, 0 of 6 started; after,
  6 of 6 (ready in 34–61 s); the project's suites pass on the
  lane's tree. This entry stays open until the fix is merged,
  deployed, and the live first-visit rate is re-measured through
  the public site — only then can the restart stopgap retire.
  Closure (2026-10-31): fix merged (merge commit d51fa408e),
  deployed in build 14a82a302, and the live first-visit rate
  re-measured through the public site with fresh profiles and
  the restart budget exhausted: 6/6 first-attempt starts
  (median ready 35.7 s), against 0/4 on the previous live build.
  Record: `docs/reports/deploy-20261031.md`; evidence:
  `test-results/deploy-20261031/live-boots-fresh/`.
  Evidence: `docs/reports/first-visit-start-reliability-20261031.md`
  (branch `lane-firstvisit`, fix commit `67f5fe1fd`); earlier
  measurements in `docs/reports/deploy-20261030-deadstart.md`
  and its addendum (integration tree); corroborated by the
  intermittent window-creation failures in
  `wt-firstframe/test-results/firstframe-20261030/REPORT.md`.
  REOPENING (2026-10-31): the closure paragraph above
  does not stand. The 6/6 measurements it rests on — the
  lane's "after" arm and the live re-measurement — were
  false positives of the readiness test: on every build
  carrying the win32u backport the session had no desktop
  at all, and an NTSTATUS error value was certified as a
  window handle (FD-43, attributed). The backport patch
  itself shipped in no binary — its functions live in the
  unix-side win32u.so, which was never rebuilt — so the
  two impatience defects described in this entry remain
  UNFIXED in the stock unix-side file and are in play
  again with builtin treatment restored by the FD-40
  fix. This defect is OPEN. Re-closure requires the
  hardened readiness test and the four-arm measurement
  named in FD-43. The restart stopgap stays in the
  launcher.
  UPDATE (2026-10-31, lane-readygate): both prerequisites
  are now met — the readiness test is hardened (FD-47)
  and the four-arm measurement has run (FD-43, closed
  CL-10). In the anchor-harness shape with the stopgap's
  budget exhausted, the true first-attempt start rate is
  0/10 on the stock build, 0/10 on the FD-40-stamped
  build, and 0/10 on the pre-rebuild baseline build
  (692875bd8): the race is exactly as alive as the
  pre-rebuild baselines measured, and the merge wave did
  not change it. This defect remains OPEN — re-closure
  still requires the unix-side fix itself, merged,
  deployed, and re-measured live. Record:
  `docs/reports/readiness-hardening-20261031.md`.
  UPDATE (2026-10-31, branch `lane-fd01-so`): the remedy
  FD-43 named is built and proven on the branch. The
  backport pipeline now builds the unix half of win32u
  (a third, x86_64-linux-gnu tree producing `win32u.so`
  from the same patched wine-8.0 source as the PE half)
  and both staging paths ship it next to `win32u.dll`;
  the completeness checker pins its bytes. Staged
  container member audit against the live base: exactly
  two content differences in 1,257 members — `win32u.dll`
  (unsigned -> stamped backport) and `win32u.so` (stock
  `5dac5821...` -> patched `bffd9036...`); against the
  container currently pinned on integration the only
  difference is `win32u.so`. Cold-start measurement
  through the project's probe harness (fresh profile
  each boot, restart-stopgap budget exhausted, verdicts
  classified offline under the hardened FD-43 rules —
  plausible handle + five X11 connections + explorer
  opened): with a 900 s observation window the fixed
  build certified **5 of 5** first-attempt starts (real
  handle `0x10048` every time); the pre-fix build
  (identical dist, `win32u.so` member swapped back to
  stock) certified **0 of 5** across both windows —
  four of its five boots died with this defect's own
  signature printed, the fifth never started anything.
  At the four-arm measurement's 360 s window the fixed
  build certified 0 of 3 but every boot was still
  advancing when the window closed (X11 connections
  3/3/1, explorer opened, workload ran, no failure
  signature) at box load 13-19: the fix's own 300 s
  waits plus the desktop parade exceed 360 s of wall
  time on a loaded box, while the longer window does
  not revive the pre-fix build at all (0 of 2, both
  signature deaths). This entry stays OPEN per its own
  re-closure terms: merge, deploy, and a live
  first-visit re-measurement through the public site
  remain. Record:
  `docs/reports/fd01-unix-half-fix-20261031.md`;
  evidence: `test-results/fd01so-20261031/` (worktree
  `wt-fd01so`).
  UPDATE (2026-10-31, final-batch merge): the remedy is
  MERGED (integ `ca4166040`) and the merge term of the
  re-closure conditions is discharged. At the merge gate
  the composed tree's cold-start measurement at the
  900 s window, fresh state, stopgap budget spent the
  same way, certified 3 of 3 first-attempt starts on
  x86-64 and 3 of 3 on x86 (handles `0x10048` / `0x10052`,
  five X11 connections, explorer opened, every time;
  two of the x86-64 starts began at box load 11-12).
  Remaining terms: deploy, and the live first-visit
  re-measurement through the public site. Gate evidence:
  `test-results/merge-finalbatch-20261031/`; record:
  `docs/reports/merge-finalbatch-20261031.md`.
  UPDATE (2026-10-31, deploy + closure decision): the
  remedy is DEPLOYED (build `fe4cfc265`; record:
  `docs/reports/deploy-finalbatch-20261031.md`) and the
  live first-visit rate is re-measured through the public
  site under the hardened FD-47 rules (fresh profile per
  boot, restart budget pre-exhausted, 900 s window,
  offline classification): the previous live build
  certified 0 of 6 (its own page rule said ready 6 of 6 —
  false-ready on every boot, deskpin value `0xC000000D`,
  explorer never opened); the deployed build certified
  6 of 6 on x86-64 (handle `0x10048`, median ready
  102.9 s, zero restarts, zero failure signatures) and
  3 of 3 on x86 (handle `0x10052`, median ready 31.2 s).
  Every re-closure term is now discharged; the
  coordinator's closure decision is recorded with the
  deploy record. This defect is CLOSED — see CL-17. The
  §4.68 restart stopgap remains in the shipped launcher
  by separate decision: it was needed zero times in the
  live measurement, but retiring it is its own case and
  is not part of this closure.
- **FD-02 — The 32-bit staged Windows system files carry the
  same two impatience defects, and the fix pipeline cannot
  reach them.** *(High)* The 32-bit set is a different vintage
  (WineHQ 10) whose desktop initialization has the same
  10-second wait and the same check-once latch that permanently
  installs the null driver on a miss — verified present in that
  version's source. The backport pipeline that produced the
  FD-01 fix builds only the 64-bit set, so the 32-bit side
  needs its own route (a backport build for the newer vintage,
  or a binary-level approach). Until then, cold starts of
  32-bit programs face the same race as the 64-bit side
  (FD-01, reopened 2026-10-31 — see FD-43). Evidence:
  `docs/reports/first-visit-start-reliability-20261031.md`
  (branch `lane-firstvisit`, "Residuals, stated plainly").
  Related: FD-35 (the vintage split itself).
  **Status 2026-10-31 (lane-fd02): FIXED ON BRANCH, proof
  below; entry stays OPEN under the same re-closure terms as
  FD-01 (merge -> deploy -> live re-measurement).** Fix commit
  `6f3d440ef`: the 32-bit side got its own pipeline route -
  `tools/rootfs32/build-wine10-win32u.sh` builds win32u.so
  from the pinned Wine 10.0 source with
  `tools/buildWine/patches/wine10-win32u-desktop-wait.patch`
  (10 s fuse -> 300 s; check-once latch -> 500 ms retries for
  ~300 s, preserving Wine 10's null-GUID fallback and callback
  convention), and the base build substitutes the committed
  module (sha256 `b2d9d135...`, hash-pinned, fail-closed) so
  all four shipped copies carry it. The regenerated wine32.zip
  (239,672,753 B, sha256 `7170f3cc...`) is member-identical to
  the shipped one except exactly those four members; the
  completeness checker pins the backport in source and zip.
  Proof (hardened rules, fresh state, 900 s windows, one boot
  per run, `tools/browser/startup-measurement.mjs --arch=x86`,
  evidence `test-results/fd02-20261031/`): fixed arm (FD-48
  signal fix composed + fixed payload) **5/5 certified**,
  median ready 74 s, under the heaviest mean load of the three
  arms; shipped-state arm (unmodified integ + stock payload)
  **0/3 certified** - all three boots were in fact healthy
  starts (plausible handle, 5 X11 connections, no doom line)
  that could not be certified because the FD-48 spawn signal
  was never emitted on the 32-bit paths, so the baseline zero
  is two stacked defects, stated as such; isolation arm
  (signal fix + stock payload) **3/3 certified** - at this
  harness/load profile the stock Wine-10 pair did not bite in
  three boots (Wine 10's null-GUID fallback, preserved by the
  patch, plausibly masks part of the latch's failure space;
  the 10 s fuse and the remaining latch paths are live code in
  the stock module regardless), so no B1->F rate delta is
  claimed. The FD-48 fix rides this branch as cherry-pick
  `23c87ec6c` for measurement only - drop it at composition;
  it lands via `lane-fd48-restart-cert`. Record:
  `docs/reports/fd02-win32u32-fix-20261031.md`.
  **Status 2026-11-01 (deploy): MERGED in the
  FD-02+FD-38 batch (integ `02bffb14a`) and DEPLOYED
  the same day (live build `02bffb14a`, wasm
  `c332d20a`); the shipped wine32.zip differs from the
  previous live one in exactly the four win32u members
  (member audit in the deploy record). Live
  re-measurement through the public route under the
  hardened rules (fresh profile per boot, restart
  budget pre-exhausted, 900 s window): x86 5/5
  certified, handle 0x10052 on every boot, median
  certified-ready 26.6 s, zero restarts, zero
  false-ready; the x64 control arm certified 3/3 on
  the same build. The merge -> deploy -> live
  re-measurement terms are discharged. This defect is
  CLOSED — see CL-19.**
- **FD-03 — The emulated machine is far smaller than modern
  programs expect, and no GPU-compute API exists.** *(High, class
  of programs)* Four independent floor failures, none of them a
  per-program bug: (a) no OpenCL ICD exists behind the staged
  `opencl.dll` and there is no CUDA at all, so GPU-compute
  programs have no API to call; (b) the CPU advertises at most
  SSE4.2 — no AVX/AVX2 executes; (c) the machine definition is
  1 GiB of RAM and 1 CPU; (d) the Vulkan bridge advertises a
  2 GiB device-local heap. This is the wall in front of the
  DaVinci Resolve class; it falls only to broad work (compute
  over the existing bridge, AVX2 proven family-by-family, a
  machine-definition raise), never to per-program workarounds.
  Evidence: `docs/reports/broad-conformance-analysis-20261030.md`
  §1.
- **FD-04 — Networking gaps (campaign parked by directive; gaps
  inventoried and verified).** *(Medium)* What works: opt-in IPv4
  TCP through the relay, connect-by-name. What does not exist:
  UDP at all; guest-side explicit DNS APIs are unverified; IPv6;
  inbound connections; networking is off by default. Evidence:
  `docs/reports/chromium-networking-20261017.md`; the deployed
  relay state recorded 2026-10-29.

### Emulator-killing and correctness defects (kernel / CPU / memory)

- **FD-05 — Running out of file descriptors panics and kills the
  whole emulator.** *(High)* `KProcess::getNextFileDescriptorHandle`
  panics at the 0x4000-descriptor ceiling. Answering EMFILE safely
  needs the allocation contract changed across ~50 call sites
  whose callers mostly dereference the result immediately — a
  dedicated lane, not a naive conversion (which would trade a loud
  failure for null dereferences). This is the one known remaining
  guest-reachable emulator-kill in the kernel-subsystems area.
  Evidence: `docs/reports/cleanlog-ksubsys-20261030.md`
  (branch `lane-cleanlog-ksubsys`, "deliberately left alone").
- **FD-06 — 32-bit port-IO instructions panic and kill the whole
  emulator. FIXED 2026-10-31 on branch `lane-fd06-portio`
  (commit `d88fdc61a`) — see Section B.** *(Was High)* The twelve INS-string handlers in the
  32-bit interpreter are bare panics on unimplemented port IO, and
  `int99` panics on an unregistered callback index — both
  reachable by ordinary guest execution. No graceful answer was
  invented on the spot: the 32-bit core has no port-IO convention
  to degrade to (0xFF-fill vs fault delivery is a design decision
  that lane must make). Evidence:
  `docs/reports/cleanup-cpu-core-20261030.md`
  (branch `lane-clean-cpu`, "handed off"). Correction
  (2026-10-31): the panics named above live in
  `source/emulation/cpu/ops.h`, which no translation unit in
  the tree includes; the live sites were the interpreter's
  missing port-op handlers (SIGILL delivered instead of #GP)
  and `callOpenGL`'s panic on an unregistered int 0x99 index.
  The fix landed at the live sites. Record:
  `docs/reports/fd06-portio-20261031.md`.
- **FD-07 — K-M2: in interpreter mode, control flow can reach
  data on multiple threads.** *(High)* Observed as thousands of
  unimplemented-opcode faults inside a mapped image plus
  stack-overflow exceptions across threads; best-fit hypothesis is
  a shared corrupted indirect target. Next step on record: a
  traced probe at a merge gate. Evidence: `docs/known-gaps/kernel.md`
  (K-M2 row).
- **FD-08 — Exclusive epoll wake-one is incomplete, with a live
  field symptom.** *(High)* Edge-triggered and one-shot epoll
  semantics are largely implemented, but EPOLLEXCLUSIVE wake-one
  behavior remains incomplete and a "possible starvation in epoll"
  warning already fires in the native tests. The registered field
  symptom: a real game (Quake III) presents early frames, then
  stops presenting while its calls continue at a low rate and
  guest syscall time saturates — two spin hypotheses are on
  record. Evidence: `docs/known-gaps/kernel.md` (epoll row);
  `docs/known-gaps/gl.md` (fixed-function row, same symptom from
  the graphics side).
- **FD-09 — `KThread::modify_ldt` panics on invalid indexes.**
  *(Medium)* Guest input reaching an internal-invariant panic in
  the thread-object layer; flagged by the fault-delivery lane as
  outside its area. Evidence:
  `docs/reports/cleanup-fault32-20261030.md`
  (branch `lane-cleanlog32`, "deliberately left alone").
- **FD-10 — Some X11 callers use memory pointers without checking
  for failure.** *(Medium)* The guest-memory lane made a
  cross-page pointer request return "no pointer" instead of
  panicking the whole emulator; some X11 callers dereference the
  result without a null check, so those specific requests can
  still crash. A caller-side audit is owed. Evidence:
  `docs/reports/cleanmem-guest-memory-20261030.md`
  (branch `lane-cleanmem`, "residual risks / notes for the
  merge").
- **FD-11 — Splitting a string with an empty separator loops
  forever.** *(Medium)* `BString::split("")` never advances its
  cursor, so any caller that reaches it with an uninitialized
  separator hangs the process. Found via a debugger stack of a
  hung test process; the filesystem layer now guards its own
  entry point, but the utility itself is unfixed. Owner: the
  util layer. Evidence: `docs/reports/cleanup-fs-20261031.md`
  (branch `lane-cleanfs`, "found outside this layer").
- **FD-12 — A 32-bit futex lost-wakeup flaw is proven, its fix
  is unmerged, and a related park is unconvicted.** *(Medium)*
  A deterministic native test proves the lost-wakeup flaw; the
  fix exists on the unmerged branch `lane-wake32`
  (commit `c66a35684`). Separately, demo builds in a certain
  range repeatedly (not always) map a window and then park
  before the first frame; this flaw is the documented suspect
  with a matching stall signature, but the park was never
  isolated and may have another cause. Evidence: integration
  tree `test-results/site-chromium-20261030/REPORT.md`
  ("the post-window park").
- **FD-13 — Two small socket behaviours deviate from the
  contract.** *(Low)* On unix sockets, `MSG_NOSIGNAL` is
  accepted through the write path but not honored (no SIGPIPE
  suppression plumbing there yet), and `MSG_DONTWAIT` is
  governed by the socket's blocking flag. Both are noted in
  code comments at the site. Evidence:
  `docs/reports/cleanlog-ksubsys-20261030.md`
  (branch `lane-cleanlog-ksubsys`, "residuals").
- **FD-14 — Fault reporting has two blind spots left.**
  *(Low)* `CPU64::raiseSyncFault` logs nothing itself, so
  direct kernel-side callers remain unlogged at the delivery
  point (deliveries routed through the interpreter's funnel do
  log); and the 32-bit core's two gate sites that mirror the
  64-bit ones were not confirmed in their own context.
  Evidence: `docs/reports/cleanup-cpu-core-20261030.md`
  (branch `lane-clean-cpu`, "handed off").

### Graphics

- **FD-15 — The OpenGL bridge gives five fabricated or dropped
  answers. CLOSED 2026-10-31 — see CL-07.** *(Was Medium)*
  The five cases are fixed on branch `lane-fd15` (commit
  `f8bedecdb`): the backend now honors the sampler settings it
  claimed to accept, unknown display/sync queries refuse
  instead of answering 0, two more legacy vertex-array types
  have real decoders and unmodelled types refuse the draw
  rather than fabricating blank geometry, the two packed
  pixel formats unpack correctly, and the dropped
  texture-coordinate call is recorded and replayed on every
  path. One part of case (a) could not be made honest without
  risking programs that work today; it stays open, recorded
  as FD-39. Evidence: `docs/reports/cleanup-bridges-20261030.md`
  (the original find) and
  `docs/reports/fd15-honest-gl-answers-20261031.md` (the fix).
- **FD-39 — Some desktop-only OpenGL texture settings still
  have no honest answer (the FD-15 residue).** *(Medium)*
  Settings such as texture LOD bias, mipmap-generation hints,
  border color, depth-texture mode, and texture swizzle exist
  on desktop OpenGL but have no model in either graphics
  backend here (WebGPU or the legacy GL layer). Both available
  answers are wrong in different ways: accepting the setting
  and silently dropping it (what happens today, logged once
  per case) pretends support that is not there; refusing it
  with the standard error is also unsafe, because Wine's
  Direct3D layer reads the GL error slot while creating its
  device, so a stray refusal can fail device creation for
  programs that start fine today. This is an explicit
  honesty-vs-compatibility conflict, not a settled item: the
  calls stay dropped and once-logged until a backend can
  actually honor the settings, or the bridge's error reporting
  can be scoped so a refusal reaches only the call that asked.
  Found alongside by the same lane: the packed pixel format
  5_6_5_REV (0x8364) likewise has no unpack model (the two
  formats FD-15 named were fixed); it is mechanical to add
  with the same table. Evidence:
  `docs/reports/fd15-honest-gl-answers-20261031.md`.
  RESOLVED IN PART 2026-10-31 on branch `lane-fd39` (commit
  `ad93c01ea`): the silent drop is gone. The bridge now owns the
  state for this closed set (LOD bias, GENERATE_MIPMAP,
  border color, depth-texture mode, texture swizzle): sets
  are validated -- an invalid value records the standard
  error a real driver raises, a valid value never touches
  the error slot Wine's Direct3D layer polls -- stored per
  texture, and queries answer from the store with spec
  defaults. The WebGPU backend validates and stores the same
  set, GENERATE_MIPMAP actually regenerates the mip chain
  when the base level is redefined, and the CLAMP_TO_BORDER
  wrap reaches the sampler as WebGPU clamp-to-border.
  5_6_5_REV gained its unpack model in the same table.
  REMAINING (effect-only residue, still open): no backend
  here can express the *rendering effects* of LOD bias in
  sampling, a non-default border color (WebGPU samplers
  have no borderColor field), or texture swizzle /
  depth-texture mode in shader sampling. Those values are
  now honest state -- set, validated, queryable -- whose
  sampling effect is not yet modelled; closing the residue
  needs sampler/shader support in a backend, not a
  different answer at the API surface. Evidence:
  `docs/reports/fd39-texparam-store-20261031.md`.
- **FD-40 — The 64-bit boot smoke test never reaches the
  window station. FIXED 2026-10-31 (commit `5e4bab8f2`,
  merged into the integration tree as `6b9c834d7`).**
  *(Characterized 2026-10-31 in
  `docs/reports/fd40-boot-smoke-characterization-20261031.md`;
  that record's mechanism is CORRECTED below — its
  observations were accurate, its convicted mechanism was
  not. Full fix record: `docs/reports/fd40-fix-20261031.md`.)*
  The test's own workload completes: the booted program
  prints its echo and exits with status 0. After the last
  boot process terminates there is a ~16 s total guest
  silence, then the boot thread (the one that created
  `\KernelObjects\__wineboot_event`) opens the session's
  named-object directory, creates `DBWinMutex`, and begins
  reopening the section `DBWIN_BUFFER` on an exact ~30 s
  cadence, getting `OBJECT_NAME_NOT_FOUND` every time. That
  retry is thereafter the ONLY request the whole guest
  issues (observed continuously for 570 s); the window
  station is therefore never created and the test's 240 s
  watch expires. `DBWIN_BUFFER` is the shared-memory half
  of the Windows debug-output (DBWIN) protocol: by that
  protocol it exists only while a debug listener (a
  DebugView-class program) is running, and a sender whose
  open fails is supposed to abandon the send. No component
  of this session ever creates it — in every captured log,
  passing or failing, the count of `DBWIN_BUFFER`
  creations is zero — so once the boot thread enters this
  wait it can never be satisfied, at any machine load, for
  any length of time. Reproduced 5/5 in this environment
  (the FD-15 lane's three captures across its fix build and
  an unmodified `c4bab5e07` baseline, plus two fresh
  instrumented runs on the FD-15-tip build, one started at
  load 1.9); the one passing capture on record (2026-10-26,
  older build) takes a different boot path entirely — the
  window station is created during initialization and the
  DBWIN objects are never touched. DISPROVEN: the original
  unix-socket suspicion — the threads seen waiting in
  `KUnixSocketObject::lockCond` are a service process in
  its ordinary idle read, and the request stream accounts
  for the non-progress completely without them; no socket
  waiter is blocked on undelivered data. Machine load is
  at most the trigger (a boot slow enough that the boot
  thread reaches its report path — entry followed a ~16 s
  silence in both instrumented runs), not the mechanism:
  during the wait the guest issues nothing else for the
  load to starve. STILL OPEN inside the mechanism: what
  the boot thread was waiting for during the ~16 s silence
  (its report was never delivered, so its content is
  unknown), and whether taking this path is a regression
  since 2026-10-26 or was always latent behind boot speed.
  The fix lever is not the emulator's socket layer: it is
  the staged Wine's debug-output path abandoning the send
  when no listener exists (the documented behaviour), the
  session providing a listener, or the boot wait that
  times out at ~16 s being satisfied. Evidence: the
  characterization record above; fresh captures in this
  lane's tree at `test-results/fd40-run1/` and
  `test-results/fd40-run2/`; the FD-15 lane's captures at
  `wt-fd15/test-results/suite-boot64-smoke/` and
  `wt-fd15/test-results/baseline-c4bab5e07-boot64-smoke/`;
  the passing capture in the integration tree at
  `test-results/suite-boot64-smoke/`.
  CORRECTION AND CLOSURE (2026-10-31, `lane-fd40-fix`): the
  DBWIN wait this entry convicted is not a wait and not the
  boot thread. The ~30 s `DBWIN_BUFFER` reopens are the
  session anchor's designed heartbeat (deskpin sleeps 30 s,
  then calls `OutputDebugStringA`, forever; Wine's sender is
  single-attempt and abandons the send when no listener
  exists — the correct documented behaviour, unchanged by
  the fix). The test fails on its fourth watch because
  session initialization never runs in any process: the
  staged win32u.dll is a from-source backport rebuild that
  lacks the 32-byte "Wine builtin DLL" signature a full Wine
  build stamps at file offset 64, so the wineserver does not
  flag it as a builtin (image_flags 0x04, not 0x44) and its
  syscall table is never registered; user32's
  initialization syscall therefore reaches no implementation
  and silently does nothing — no font load, no window
  station, in any process, at any load. Three-arm A/B, one
  file different: unstamped rebuild — no winstation ever;
  stock Debian DLL — winstation at 106.4 s; the same rebuild
  with only the signature stamped — winstation at 102.0 s.
  Fix (commit `5e4bab8f2`): the backport build script now
  stamps every rebuilt DLL the way winebuild does, the
  shipped win32u.dll is stamped in place (sha256
  `49b87438…`, code bytes untouched) and everything is
  re-pinned (completeness checker 0 failures). Proof:
  boot64-smoke passes through the project's own suite path
  three times in a row (89 s, 82 s, 91 s); a new dbwin-echo
  fixture + suite test proves the guest debug-output channel
  still delivers `OutputDebugStringA` text to a real
  listener (suite pass, 93 s). The other four backport DLLs
  lack the signature too but are not syscall DLLs and show
  no symptom; the build script stamps them on their next
  rebuild.
- **FD-16 — One class of shader still cannot build a pipeline:
  texel buffers reached through a descriptor-heap array.
  CLOSED 2026-10-31 — see CL-13.**
  *(High for the D3D12 class)* A guest's shader module whose
  texel buffer is reached through a descriptor-heap runtime
  array is rejected, because the serving classifiers cannot
  prove that shape servable — the same family the D3D12
  campaign closed for storage images, not yet extended to
  sampled/texel heap arrays. This is what still blocks the
  Godot demo's first frame after six earlier walls were fixed
  on a branch (section B). Evidence:
  `wt-firstframe/test-results/firstframe-20261030/REPORT.md`
  (branch `lane-firstframe`, "what still blocks the first
  frame").
- **FD-17 — Unigine Tropics aborts on a missing DirectX
  function, and which graphics library the guest loads is
  unproven.** *(Medium)* The engine aborts on
  `dxgi.dll.DXGID3D10CreateDevice`: the staged DXVK library
  does not export it, Wine's built-in one does. The decisive
  observation (a run with library-load tracing) was never
  captured, so the fix location — staged-library machinery or
  override defaults — is undecided. Evidence:
  `wt-firstframe/test-results/firstframe-20261030/REPORT.md`.
- **FD-18 — Quake III renders garbled or not at all, and the
  rejected texture format is still uncaptured.** *(Medium)*
  The sweep's second attempt showed a frozen, garbled smear;
  the bridge rejects some texture-upload format, but the
  tracing run that would capture the exact format never mapped
  a window (it hit FD-01-class startup flakiness). The handler
  logs the exact tuple, so a rerun on a current build settles
  it. Evidence: integration tree
  `test-results/demo-sweep-20261030/REPORT.md`;
  `wt-firstframe/test-results/firstframe-20261030/REPORT.md`.
  (The separate presents-stop symptom is FD-08's field
  symptom, not this entry.)
- **FD-19 — The 64-bit Vulkan dispatcher is missing one entry
  its 32-bit sibling has.** *(Low)* There is no
  `vkGetPhysicalDeviceProperties2KHR` case in the 64-bit
  dispatcher. No rejection has been observed in any run (the
  known consumer calls the core entry point), so this is a
  latent asymmetry, not a proven failure. Evidence:
  `wt-firstframe/test-results/firstframe-20261030/REPORT.md`
  ("also observed, not a blocker").

- **FD-65 — Wine Gecko aborts with "unable to find a usable
  font (serif)"; the MSHTML fixture never reports.**
  *(Medium — embedded-HTML rendering class: any guest
  program that renders through Wine's Gecko engine.
  Surfaced by the FD-02+FD-38 deploy gate, 2026-11-01;
  proven pre-existing — the identical failure, including
  the Gecko abort line and the fixture's wedged stack
  signature, reproduces against the previous live build
  `fe4cfc265`, and this batch's inputs to the path (x64
  payloads, fonts, support archive) are byte-identical,
  so the batch did not cause it.)* In the
  common-dependencies browser test the guest boots
  cleanly and `mshtml-document.exe` spawns, but Wine
  Gecko 2.47.3 aborts at startup —
  `###!!! ABORT: unable to find a usable font (serif):
  file /build/wine-gecko-2.47.3/gfx/thebes/gfxTextRun.cpp,
  line 1875` — and the fixture exits without ever
  printing `MSHTML: RESULT 0` (the test's 180 s wait
  expires; a second fixture process exits with status
  0xC0000005). Gecko's font enumeration is finding no
  usable serif font in the guest at that moment; whether
  the guest font set is genuinely absent on this path
  or enumeration races first-boot font-cache generation
  is not yet characterized — that determination is the
  entry's first step, and neither the test's wait nor
  its assertions may be loosened to pass it.
  *(Numbered FD-65 on integ: the FD-50 lane's branch
  record earmarked this number for its readback-leak
  entry; that entry renumbers to FD-66 at its
  composition.)* Evidence:
  `test-results/deploy-fd02-fd38-20261101/js-commondep-rerun.log`,
  `test-results/deploy-fd02-fd38-20261101/js-commondep-live-baseline.log`,
  `test-results/suite-common-dependencies/console.log`.

### Programs that hang, exit, or crash (cause uncharacterized)

- **FD-20 — Gang Beasts' engine process exits during startup
  for an unknown reason.** *(Medium)* The Unity process exits
  with status 1, its crash handler launches, and no
  graphics-device lines appear; sessions were repeatedly eaten
  by startup flakiness before a thread-dump + player-log
  capture could be taken. Next step on record: that capture
  against a current build. Evidence: integration tree
  `test-results/demo-sweep-20261030/REPORT.md`;
  `wt-firstframe/test-results/firstframe-20261030/REPORT.md`.
- **FD-21 — A Java program's runtime executes indefinitely
  without ever showing a window; loop or crawl unproven.**
  *(Medium)* The Java runtime's main thread stays running
  inside its own library for 15 minutes with the game archive
  still being read near the end; helpers wait normally. Ruled
  out with evidence: lost-wakeup deadlock, server churn,
  loader problems. Whether the execution is a loop (an
  emulator-contract bug) or extremely slow linear startup is
  the open question, and it decides where the fix belongs.
  Resume steps are recorded in the evidence, in order.
  Evidence: `docs/reports/mindustry-jvm-stall-20261030.md`
  (branch `lane-mindustry`).
- **FD-22 — Reading the loaded-module list directly faults on
  a null entry.** *(Medium)* Reachable only after the FD-class
  interrupt fix on branch `lane-alkhaser-int2d` let the
  Al-Khaser checker survive long enough to get here: while
  "enumerating the process LDR directly" it prints its own
  error reading an entry, then faults reading address 0 and
  exits 0xC0000005. Unproven: whether the bad list entry
  originates in the emulator's loader data, in the staged
  Wine's own list maintenance, or in the checker's error path.
  Evidence: `test-results/alkhaser-int2d/REPORT.md`
  (branch `lane-alkhaser-int2d`, "what remains").

### Coverage and machine surface

- **FD-23 — Large parts of the system-call surface have no
  dispatch at all, and the disposition pass has not been
  done.** *(Medium)* Counted by census: on the 64-bit side,
  168 of 363 named system calls have no dispatch case; on the
  32-bit side, 223 table slots are null and numbers 440–461
  (including `futex_waitv`) fall off the end of the table.
  Most of the list is legacy surface nothing calls — now a
  stated, checkable property instead of an assumption — but
  modern calls real libraries issue go first; the standing
  hypothesis is `epoll_pwait` (281), which is undispatched
  while its siblings are, and which common libraries issue.
  Evidence: `docs/reports/broad-conformance-analysis-20261030.md`
  §3; the census tool `tools/conformance/syscall_census.py`.
  **Disposition pass done 2026-10-31 (lane `lane-fd23-syscalls`;
  full record `docs/reports/fd23-syscall-census-20261031.md`).**
  The census is now three-way (implemented / deliberately
  stubbed with a logged -ENOSYS / absent) on both ABIs, and a
  dynamic census mined the preserved run logs for what guests
  actually issue: `rseq` dominates (273 processes asked; glibc
  falls back cleanly every time), everything else undispatched
  was asked for at most once, by a self-test that probes on
  purpose. Implemented and proven on the branch: `epoll_pwait`
  (281), `accept4` (288), `dup3` (292) on the 64-bit side —
  absent count 168 -> 165. What remains open in this entry:
  the per-number disposition of the long legacy tail;
  `rseq` (a real implementation must first land abort-on-signal
  handling in the 64-bit signal-delivery path — a pinned-cpu_id
  registration without it would silently corrupt per-cpu data
  on signal delivery, which is worse than the current loud
  refusal); `select` (23; route: extract the `pselect6` core);
  `pidfd_getfd` (438; one probe hit, no real consumer yet);
  `futex_waitv` (449, both ABIs; no observed asks). `io_uring`
  (425-427) is dispositioned ENOSYS-correct (a subsystem, not a
  call; guests probe and fall back by design).
- **FD-24 — No game controller exists for guests.** *(Medium)*
  There is no guest joystick device at all: controller queries
  return "device not connected" for every slot. The entry
  waits on a kernel deliverable (a guest joystick device with
  an event queue, plus a host-feed entry point); the page/SDL
  wiring follows on that landing. One tested game is proven
  not to need it (keyboard is a first-class join device
  there); the class of controller-first games does.
  Evidence: `docs/known-gaps/coverage.md` (gamepad / XInput
  row).
- **FD-25 — The instruments that would find unknown gaps are
  only partly built.** *(Medium)* Proposed by the
  broad-conformance lane and not yet in place: a full-spec
  census of both graphics bridges against the official
  function lists; a per-library export-completeness census as
  a standing test; expansion of the CPU differential harness
  from curated sets to generated fuzzing; and semantic probe
  families written from contracts (synchronisation, timers,
  waitable objects, file/section semantics) — the territory
  where this project's worst failures have lived. The loader
  and system-call censuses exist and have already caught real
  items (FD-22's class; FD-23). Evidence:
  `docs/reports/broad-conformance-analysis-20261030.md` §3.
- **FD-26 — Interpreter throughput is the frame-rate ceiling,
  and the instrument to target the work does not exist.**
  *(Medium)* Measured: about 90% of a Portal-class frame is
  guest execution in the interpreter; the fast engine measured
  *worse* for the 32-bit case and stays off. Material gain
  needs interpreter-throughput work, and the op-mix histogram
  that would aim it has not been built. Evidence:
  `docs/reports/frame-floor-investigation-20261027.md`;
  `docs/reports/portal-perf-profile-baseline-20261026.md`.

- **FD-38 — Directory-notification (inotify) delivery:
  semantics fixed, the enablement park found (a poll/sweep
  lock-order deadlock) and removed at its source, delivery
  enabled end-to-end. CLOSED 2026-10-31 — see CL-18.**
  *(Medium)*
  FD-38 lane, 2026-10-31 (branch `lane-fd38`): the semantics
  remainder is isolated and fixed — the name field is padded
  to a multiple of 16; a plain re-add replaces the watch mask
  (IN_MASK_ADD merges); a guest rename emits exactly the move
  pair plus IN_MOVE_SELF (it previously also emitted a phantom
  delete+create pair and destroyed any watch on the renamed
  node); unlinking a watched file reports IN_ATTRIB before
  IN_DELETE_SELF; IN_ISDIR is set on directory self events
  except IN_DELETE_SELF and IN_MOVE_SELF; guest utimensat
  generates IN_ATTRIB; inotify_add_watch honors IN_DONT_FOLLOW.
  Each behavior was measured against the host kernel first and
  is pinned by the inotify native tests, which now construct
  their instances directly (the guest-visible gate below stays
  closed) and drive the same machinery through the syscall and
  KProcess paths. Enabling delivery still parks the Chromium
  site session at the same startup point, and the lane's site-
  rig bisection isolates the park to enablement itself, not to
  any event semantics: the pristine base with the refusal
  passes (window mapped + frame, control run 2026-10-31);
  enablement alone on the pristine machinery parks; forcing
  instances non-blocking parks; the full semantics fix parks.
  In the traced run the watched directories generated no
  events during the boot and no queued event was lost (two
  events total, both rm_watch IN_IGNORED, read back correctly);
  per the Wine 8 server source, the directory waits the server
  registers pend identically under the refusal, whose dnotify
  fallback this emulator also does not implement. That
  remainder is now resolved (lane `lane-fd38park`,
  2026-10-31): the mechanism was an ABBA lock-order
  deadlock — the poll scan probes readiness while holding
  the polling thread's poll condition and
  KInotify::isReadReady took the instance wait condition
  (pollCond -> waitCond), while the notify sweep holds
  that wait condition and signals parent poll conditions
  at its end (waitCond -> pollCond). Merely possessing a
  working inotify fd put the wineserver's poll scan inside
  the cycle; any other process's file churn could close
  it, and the instance lock held by the wedged sweep then
  froze guest file activity session-wide at ~0 CPU — the
  frozen-log signature. Proven by a minimal stress
  composition that wedges pre-fix with the two worker
  stacks crossed in KInotify::isReadReady and
  KInotify::sweep -> BoxedWineCondition::signalAll (gdb
  capture preserved with the lane evidence). Fixed in
  `1e8151d2a` by making isReadReady lock-free over an
  atomic mirror of the queue state (the unix socket
  object's established pattern); the entry point is
  enabled, and the site-rig consumer proof the gate
  demanded passes 3/3 (fresh-profile Chromium sessions,
  window-mapped+frame, delivery live). Evidence:
  `docs/reports/fd38-enablement-park-20261031.md`;
  `docs/reports/fd38-inotify-semantics-20261031.md`; preserved
  rig runs under `test-results/fd38/` on the lane branch;
  original disposition `c9259ec42`, integration tree
  `test-results/site-chromium-20261030/REPORT.md`.

- **FD-59 — Two inotify event kinds are never generated:
  IN_ACCESS, and IN_MODIFY for writes through memory
  mappings.** *(Low. Filed provisionally as FD-56 on the
  FD-38 lane branch; renumbered at this composition.)*
  No read-path hook generates IN_ACCESS (deliberate: the read
  path is too hot for per-read event sweeps), and writes
  through memory mappings have no per-write funnel that could
  generate IN_MODIFY. Both are documented at the KInotify
  implementation. add_watch cannot refuse individual kinds
  without breaking IN_ALL_EVENTS consumers, so the honest
  treatment is the header documentation plus this entry.
  Found by the FD-38 lane, 2026-10-31.

### Tooling, build, and records

- **FD-27 — The Vulkan marshal generator still emits
  guest-reachable panics for two more union types.**
  *(Medium)* The pattern fixed for one type (CL-section,
  branch `lane-tooling-honesty`) remains in the same generator
  for `VkIndirectExecutionSetCreateInfoEXT` and
  `VkDescriptorGetInfoEXT`, plus panic emissions in the
  next-pointer writers. Their unions carry handles/pointers,
  so the degrade needs its own stride/ownership check; it was
  recorded, not silently dropped. Evidence:
  `docs/reports/tooling-honesty-20261030.md`
  (branch `lane-tooling-honesty`, item 6 follow-ups).
- **FD-28 — One generated graphics file has silently diverged
  from its generator.** *(Medium)* `vk_host.cpp` carries
  hand-written fixes (error propagation, map-failure handling,
  log-once unsupported operations) that a wholesale
  regeneration would destroy. It needs an owner: either the
  generator learns those implementations, or the file stops
  presenting itself as generated. Evidence:
  `docs/reports/tooling-honesty-20261030.md`.
- **FD-29 — A fresh checkout cannot fetch its runtime
  assets.** *(Medium)* The runtime-assets release address in
  `tools/browser/runtime-assets.json` returns 404, so every
  new worktree bootstraps by hand-copying 13 manifest files
  from the integration tree and hash-verifying them. The
  retained candidate archive exists on the box; publishing it
  needs an authenticated release step the box cannot perform.
  Evidence: `docs/reports/cleanup-bridges-20261030.md`
  (branch `lane-cleanup-bridges`, verification section).
- **FD-30 — Five legacy test failures reproduce identically
  on the pristine integration tree.** *(Medium)* One spirv
  corpus conversion and four vkwebgpu frame/sampler cases fail
  the same way on the untouched base — pre-existing, in the
  graphics area, currently unowned. Evidence:
  `docs/reports/tooling-honesty-20261030.md`
  ("for the coordinator").
- **FD-31 — The 64-bit native test binary dies partway
  through a full run.** *(Low)* A single-process full run
  segfaults (exit 139) after ~33 passes; the crash
  neighbourhood is the clone/fork tests. A/B-proven
  pre-existing at the base commits, unrelated to the lanes
  that tripped over it. The main native suite's serial runs
  are unaffected. Evidence:
  `test-results/alkhaser-int2d/REPORT.md`
  (branch `lane-alkhaser-int2d`, tests section).
- **FD-32 — Multi-worker native suite runs die on the loaded
  box.** *(Low)* Runs at the harness's default worker count
  die with exit 139 inside the first few tests, in a
  thread-dump path no recent lane modified; single-worker runs
  are the trustworthy mode and every lane now uses them. Not
  A/B-tested against a pristine-tip binary by the lane that
  documented it most recently. Evidence:
  `docs/reports/cleanup-cpu-core-20261030.md`
  (branch `lane-clean-cpu`, verification).
- **FD-33 — The native release build does not build.**
  *(Low)* Two pre-existing failures in the same build family:
  (a) the release application binary fails to link with
  undefined symbols (`msgScratch`, `tickXWirePresent`,
  `hasMainThreadWork`); (b) the release configuration has one
  compile error — a JIT source uses a threshold function whose
  declaration is not visible outside the WASM JIT
  configuration. Neither affects the shipped browser build;
  both mean a whole build configuration is unusable and
  untested. Evidence: (a) the dead-start lane's record,
  `wt-deadstart3/test-results/INTERIM-deadstart2.md`; (b)
  `docs/known-gaps/gl.md` (row for the release-target guard
  fix, "adjacent non-GL failure").
- **FD-34 — The debug (`-D_DEBUG`) compile of the instruction
  decoder is broken at baseline.** *(Low)* It fails on a
  switch/`-Werror` conflict, verified byte-identical on the
  pristine integration-tip file; untouched by the lane that
  found it. Evidence:
  `docs/reports/cleanup-cpu-core-20261030.md`
  (branch `lane-clean-cpu`, "handed off").
- **FD-35 — The staged Windows libraries are two different
  vintages.** *(Low)* The 64-bit set is Debian bookworm
  Wine 8.0; the 32-bit set is WineHQ 10.0. Targeted backports
  closed every observed load failure, but the split remains a
  standing source of missing-export and behaviour-difference
  risk (it is also why FD-02 needs its own route). Evidence:
  `docs/reports/loader-gaps-20261030.md`.
- **FD-36 — Verification debts (unproven, not known-broken).**
  *(Low)* (a) Keyboard input at the Celeste title screen was
  never directly exercised in the live proof. (b) 39 exotic GL
  function names remain unsupported — proven non-fatal, not
  proven harmless for every program. (c) Whether 32-bit guests
  share the interrupt-delivery defect class fixed on 64-bit
  (section B, `lane-alkhaser-int2d`) is unproven either way.
  (d) The 32-bit fault-delivery fix's own noted residual: its
  no-handler page-fault path now throws even when called in a
  no-throw mode — argued safe by its lane (the old path could
  not return either), recorded here so the argument is not
  lost. Evidence: `docs/reports/deploy-20261030-celeste.md`;
  the Celeste lane record (branch `lane-celeste`);
  `test-results/alkhaser-int2d/REPORT.md`;
  `docs/reports/cleanup-fault32-20261030.md` ("residual
  risks").
- **FD-37 — A recorded "safe to drop" proof for a saved work
  stash has rotted.** *(Low, operations)* The 2026-10-25 proof
  that a particular stash could be dropped relied on a backup
  directory that no longer exists, and the stash list has
  since shifted — what the proof described is no longer what
  the label points at. No stash may be dropped on the old
  proof; any future drop needs a fresh export first.
  Evidence: `docs/reports/docs-cleanup-20261030.md`
  (branch `lane-docsclean`).
- **FD-58 — chmod reports success without changing
  anything.** *(Medium. Filed provisionally as FD-55 on
  the FD-38 lane branch; renumbered at this composition —
  FD-55 on integration is the Godot presentation entry.)* `KProcess::chmod` resolves the node and returns 0
  without applying any mode change; the 32-bit fchmod path
  logs "fchmod not implemented" and marks the call IGNORED. A
  guest is told a metadata change happened when none did (and
  no IN_ATTRIB can be generated for a change that never
  occurred). Found by the FD-38 lane's funnel audit,
  2026-10-31 (kprocess.cpp `KProcess::chmod`; syscall.cpp
  `syscall_fchmod`).

- **FD-60 — link() copies file content instead of creating
  a hard link.** *(Low. Filed provisionally as FD-57 on
  the FD-38 lane branch; renumbered at this composition.)* `KProcess::link` creates a new node holding a copy
  of the content (its own debug line says "Hard link not
  implemented"), so inode identity and link counts observably
  differ from Linux, and inotify sees a creation for the new
  name but no link-count IN_ATTRIB on the source. Found by
  the FD-38 lane, 2026-10-31 (kprocess.cpp `KProcess::link`).

- **FD-64 — Five legacy-suite subtests fail against the
  current V2 protocol fixtures.** *(Low, test-suite health.
  Surfaced by the FD-02+FD-38 merge gate, 2026-11-01;
  proven pre-existing — the identical failures reproduce
  on the pre-batch tree `fe4cfc265`, so this batch did not
  cause them.)* The legacy group of `tools/run-tests.mjs`
  (`node --test web/tests/*.test.mjs`) fails 5 subtests:
  four in `web/tests/vkwebgpu-v2.test.mjs` throw
  `vk64: v2 DESC_SET has 132 trailing bytes` from the
  protocol reader, and the corpus-conversion subtest in
  `web/tests/spirvfix.test.mjs` fails. The gate-standard
  unit suite is unaffected (344/0 on this batch). Either
  the legacy fixtures predate a bridge protocol change
  (the descriptor-set encoding grew during the FD-16 /
  FD-49 work) or the bridge writes bytes the reader
  rightly rejects; a lane must determine which side is
  authoritative before anything is edited — neither side
  may be "fixed" by weakening the other. *(Numbered
  FD-64: FD-61/62/63 are already taken by parallel lanes'
  branch records not yet composed.)* Evidence:
  `test-results/merge-fd02-fd38-20261031/js-suite.log`.

- **FD-41 — The native suite aborts mid-run in the clone
  test group when the syscall-ABI tests run first.
  CLOSED 2026-10-31 — see CL-08.** *(Was Medium)*
  *(Numbered FD-39 on the lane branch; renumbered at
  integration — FD-39 was independently taken by the
  FD-15 lane's texture-settings entry.)*
  Running tests 610-621 in one process aborted with an
  uncaught `int` throw (`terminate called`, rc 134) in the
  clone tests after the syscall-ABI block; the identical
  window aborted on the pristine base tip, and every test
  in the window passes standalone. Characterized and fixed
  on branch `lane-fd41-fd42` (commit `dc5f96fa8`): the
  abort was a guest thread's first-op fetch unwinding out
  of a bare thread entry point with no handler, and the
  broader mid-run instability (segfaults, a tail wedge)
  was threads leaked by earlier tests acting underneath
  later ones in the shared process — see CL-08. Evidence:
  `docs/reports/fd06-portio-20261031.md`;
  `docs/reports/fd41-fd42-suite-containment-20261031.md`.
- **FD-42 — A suite run can flood gigabytes of crash
  diagnostics in the tail tests. CLOSED 2026-10-31 — see
  CL-09.** *(Was Medium)*
  *(Numbered FD-40 on the lane branch; renumbered at
  integration — FD-40 was independently taken by the
  FD-15 lane's boot-smoke entry.)* After ~650 passes a
  full serial run could enter an unbounded diagnostic
  loop ("probably about to crash, could not find
  emulation instruction that caused exception") and grow
  its log by gigabytes until killed; the same flood
  aborted a deploy-gate attempt on the shipped tree, and
  all tail tests pass standalone. Fixed on branch
  `lane-fd41-fd42` (commit `dc5f96fa8`): the diagnostic
  now follows the project's counting-cap standard in both
  host handlers — see CL-09. Evidence:
  `docs/reports/fd06-portio-20261031.md`;
  `docs/reports/deploy-20261031.md`;
  `docs/reports/fd41-fd42-suite-containment-20261031.md`.
- **FD-43 — The FD-01 desktop-wait fix is present in no
  shipped binary; the measured first-visit "improvement"
  was a readiness false positive — ATTRIBUTED 2026-10-31; CLOSED 2026-10-31 (CL-10) once the hardening and the four-arm measurement it demanded were delivered (FD-47, lane-readygate).** *(Medium — a closed fix's evidence chain)*
  FD-01's patch edits `get_desktop_window` and
  `load_desktop_driver`, functions that exist only in the
  unix-side `win32u.so` — in neither PE win32u.dll, stock or
  rebuilt (no strings, no 300 s constant). The shipped
  `win32u.so` is byte-identical to the container staging
  image's (sha256 `5dac58217e6b6203…`) and its single
  `NtUserWaitForInputIdle` call site passes the stock 10 s
  timeout, so the patch's code shipped nowhere. FD-01's live
  measurements (0/4 → 6/6 first-attempt starts) stand as
  measured, but the cause is not the patch as shipped; the
  only win32u change in that build was the (independently
  defective, see FD-40) unsigned PE rebuild.
  ATTRIBUTION (2026-10-31, frontier review + independent
  spot-checks:
  `test-results/fd43-attribution-review-20261031/REVIEW.md`):
  without the builtin signature the rebuild's syscalls are
  never dispatched and return the NTSTATUS error
  `0xC000000D`; the session anchor's window creation
  "returns" that error value in place of a handle, its
  non-NULL check passes, it prints READY, and the
  launcher's readiness test accepts any hex value — so
  sessions were certified started with no desktop at all
  (the desktop process never launches; zero X11 clients
  connect). Every start measurement taken on a build
  carrying the unsigned DLL is VOID as a start
  measurement: the first-visit lane's "after" 6/6, the
  live 6/6 behind CL-06, the merge/deploy gate boots, and
  the FD-15/FD-06 deploy's live 6/6 all carry the same
  fingerprint (`hwnd=0xc000000d`, 0 X11 connections).
  Scope, stated plainly: proven for the anchor-based
  harness session shape those measurements used; a
  full-Wine session on the same class of build
  demonstrably runs a real GUI (the Celeste boot test:
  5 X11 connections, a pixel-verified frame), so there is
  no evidence the live site's real demos are broken —
  what is void is the anchor-based start measurement,
  not the site. CONSEQUENCE: CL-06 rests on void
  measurements and FD-01 is REOPENED (see its entry and
  the annotation on CL-06); the impatience defects in the
  stock unix-side win32u.so were never fixed and are in
  play again now that the FD-40 fix restores builtin
  treatment. The restart stopgap stays in the launcher;
  it is inert on the current live build, because the
  failure line it watches for is printed by unix-side
  code that never runs there. Before any build carrying
  the FD-40 fix is deployed, or FD-01 is closed again,
  the readiness test must be hardened (reject
  NTSTATUS-shaped handles; require the X11/desktop
  handshake) and the review's four-arm measurement
  (stock / stamped / unsigned control / pre-merge
  baseline, under the corrected criterion) must be run;
  this entry stays open until that measurement re-earns
  a start-reliability verdict. Evidence: the review
  above (claims L1–L12 spot-checked on the box; the
  Celeste qualification is the reviewing lane's);
  `docs/reports/fd40-fix-20261031.md` ("Found on the
  way").
  RESOLUTION (2026-10-31, lane-readygate): the two things
  this entry demanded now exist. The readiness
  certification is hardened (FD-47): an NTSTATUS-shaped
  handle is rejected loudly, and certification requires
  the five-connection X11 handshake and the explorer
  launch. The four-arm measurement has run under the
  hardened certification — 10 boots per arm, alternating,
  fresh profile each, machine load logged per boot,
  verdicts classified offline from the complete console
  stream: stock 0/10 first-attempt starts, stamped
  (FD-40 fix) 0/10, pre-rebuild baseline (692875bd8)
  0/10, and the unsigned control false-ready 10/10 under
  the old certification against 0/10 under the new,
  offline and in-page alike. The true first-attempt rate
  in the anchor-harness shape is at the floor on every
  healthy build; the merge wave changed nothing (the
  question this entry's review left undecidable is
  answered: the baseline arm matches the merged arms);
  the race's remedy remains the unix-side win32u.so
  backport build. CLOSED — see CL-10. Record:
  `docs/reports/readiness-hardening-20261031.md`; data:
  `test-results/readygate-summary.json` and the per-boot
  `test-results/suite-readygate-<arm>-<i>/boot.json`.
- **FD-44 — Guest platform threads can outlive the
  thread state they execute on. CLOSED 2026-10-31 — see CL-14.** *(Medium, emulator
  thread lifetime)* Characterized by the FD-41/FD-42 lane
  (2026-10-31), not fixed there: tests that leak guest
  threads leave host platform threads executing guest
  code indefinitely, and those threads fault, deliver
  signals, and retire on state other threads are freeing
  — a core dump shows a platform thread dying inside
  guest-signal delivery (`KThread::runSignal` ->
  `CPU::fillFlags`) on concurrently torn-down state, and
  a full run wedged with a leaked thread blocked in
  `KProcess::removeThread` while the main thread spun in
  a later test's JIT code. The suite now isolates tests
  so this cannot hurt a run, but the product has no such
  boundary: a long-running guest that clones and exits
  threads aggressively exercises the same lifetime
  hazards. Evidence:
  `docs/reports/fd41-fd42-suite-containment-20261031.md`
  (worktree `wt-fd41fd42`, `test-results/fd41fd42/`,
  incl. the captured core + debugger stacks).
  *(Numbered FD-43 on the lane branch; renumbered at
  integration — FD-43 was independently taken by the
  FD-40 fix lane's attribution entry above.)*
  FIXED 2026-10-31 on branch `lane-fd44` (fix
  `d52d839ac`; regression test `76407729e`, hardened by
  `84d430548`/`03f259dc1`): thread lifetime in the
  emulator is now reference counted. A `KThread` starts
  with one reference owned by the process thread table;
  `KProcess::deleteThread` releases it instead of
  deleting the object, and the object (with the CPU it
  owns) is destroyed when the last reference drains —
  safe because `deleteThread` runs `cleanup()` first,
  which disarms the memory-touching teardown. Every
  table lookup that uses the thread beyond the lookup
  itself now takes the reference under the table lock
  (`KProcess::getThreadByIdRef`/`getThreadRef`/
  `tryGetThreadByIdRef`, `KSystem::getThreadByIdRef`/
  `tryGetThreadByIdRef`): the `signalProcess` iteration,
  `KSystem::tgkill`, `terminateOtherThread`, the
  syscall64 `tgkill`/`rt_tgsigqueueinfo`/
  `pidfd_send_signal` queueing paths, and the
  guest-thread dump; `signalProcess` also skips threads
  already in teardown. Proof: new suite test
  `testThreadLifetimeTeardown` — under an
  AddressSanitizer build of the lifetime units it fails
  pre-fix with heap-use-after-free in
  `KThread::signal` (kthread.cpp:205) on a thread freed
  by `deleteThread` (kprocess.cpp:410), and passes
  post-fix with zero sanitizer reports; full native
  suite serial 662/662, x64-test 59/59 on the branch.
  The `removeThread` wedge in the original report was
  not reproduced in any lane run and is consistent with
  a downstream symptom of executing on freed state; if
  it recurs on a tree carrying this fix it is an
  independent defect. Two same-class residues found on
  the way stay open below: FD-51 (sampler) and FD-52
  (process lifetime under cross-thread delivery).
  Evidence: `docs/reports/fd44-thread-lifetime-20261031.md`
  (worktree `wt-fd44`, `test-results/fd44/`).
- **FD-51 — The RIP sampler's background thread
  dereferences published CPU state with no lifetime
  hold.** *(Low–Medium, emulator thread lifetime; found
  by the FD-44 lane, 2026-10-31.)* With `BW64_RIPSAMPLE`
  set, a detached sampler thread
  (`ripSamplerLoop`, `source/emulation/cpu/normal/
  ripSampler.cpp`) walks slots holding raw `CPU*` /
  `CPU64*` pointers published by the platform threads
  and dereferences them: registers and instruction
  counters for the spin detector, and in heartbeat mode
  full traversals (`thread->process`, module lookups,
  guest-memory reads). Owners free that state on
  teardown (`~KThread` deletes the CPU; execve frees
  the pre-exec CPU mid-run) — slot release at platform
  exit and per-iteration republication narrow the
  window but do not close it: a sample in flight during
  a free reads torn-down state, the FD-44 class in a
  diagnostic path. Diagnostic mode only (not enabled by
  default). A fix publishes value samples (rip /
  instruction count copied into slot atomics by the
  owner) for the spin path and gives the heartbeat
  traversal a lifetime hold (an FD-44 thread reference
  plus a generation check) or drops the deep traversal.
  *(Numbered FD-47 on the lane branch; renumbered
  FD-51 at integration — FD-47 was taken on the
  integration branch by the readiness-certification
  entry.)*
- **FD-52 — Cross-thread delivery pins the thread, not
  the process: the shared guest memory can still be
  freed under an in-flight frame build.** *(Low,
  emulator thread lifetime; characterized by the FD-44
  lane, 2026-10-31.)* FD-44's references keep the target
  `KThread` and its CPU alive for the whole delivery,
  but a frame build also writes through the process's
  shared `KMemory`, whose lifetime is the process's:
  the last thread's `deleteThread` frees it (and a
  `KSystem`-level sender such as `KSystem::tgkill`
  holds no `KProcessPtr` at all) while another host
  thread's delivery against a thread of that process
  can still be in flight. Not observed in the FD-44
  lane's measurements (an ASan build with the memory
  unit instrumented ran the teardown race clean, twice:
  deliveries drain in microseconds and product process
  teardown waits out thread removal, so the window is
  narrow); recorded so the fix, if taken, starts from
  the fact. Fix shape: the reference-taking lookups
  also retain the process (the syscall64 sites already
  hold the `KProcessPtr`), and the memory free in
  `deleteThread` drains in-flight deliveries (a
  per-process transient-reference count waited on under
  `threadExitMutex`, which delivery paths never take).
  *(Numbered FD-48 on the lane branch; renumbered
  FD-52 at integration — FD-48 was taken on the
  integration branch by the restart-certification
  entry.)*
- **FD-47 — The session-readiness certification accepted
  any hex value as a window handle, certifying sessions
  with no desktop as started. FIXED ON BRANCH
  lane-readygate 2026-10-31 — see CL-11.** *(High — it
  silently voided every start measurement taken on a
  build carrying the unsigned win32u.dll since 14a82a302,
  including a closure of FD-01)*
  The launcher's session observer
  (`project/emscripten/wine64-launcher.js`,
  `observeSessionOutput`) set desktop readiness from the
  desktop pin's `deskpin: READY hwnd=0x...` line alone,
  accepting any hex value. On a build whose win32u
  syscalls are never dispatched (FD-40), the pin's window
  creation "returns" the NTSTATUS error `0xC000000D`; the
  pin's own NULL-only check passes it through, and the
  launcher certified the session started with no desktop
  at all — no explorer, zero X11 connections, and the
  failure signature unprintable because the code that
  prints it never runs (attribution: FD-43). Every
  consumer of `bwRuntime.ready()` inherited the false
  verdict: the suite's `bootGuest`,
  `web/tests/boot-rate.mjs`'s "started" class, and
  `web/tests/boot-diag.mjs`. The fix: certification now
  requires the whole startup handshake, in any order —
  a plausible handle (nonzero, top bit clear; the
  NTSTATUS warning/error classes `0x8.......` /
  `0xC.......` are rejected and logged loudly, once per
  distinct value, as `BW64-FAIL
  component=launcher.session`), at least five accepted
  X11 client connections (a healthy boot makes exactly
  five on both arches; dead boots stall at two), and
  explorer.exe actually opened. The §4.68 restart
  watcher is untouched. `boot64-smoke` never used the
  flag (it asserts server-stream evidence) and is
  unaffected; `boot-rate.mjs`'s own pin-line signal is
  negative-only (it can withhold a "race-failed" label,
  never certify a start). Proof: the readiness unit
  contract tests are red/green — four new scenarios fail
  against the old launcher, all pass against the new;
  full JS unit suite 340/0. The four-arm measurement
  (FD-43 resolution) is the sabotage pair: the
  broken-dispatch control is false-ready 10/10 under the
  old certification and certified 0/10 under the new —
  offline, and in-page through the hardened launcher in
  the control's own dist — while genuine starts certify
  under the new rule on the stamped arm's dist (the
  suite's boot32-vkboot passes through the hardened
  predicate: handle `0x10052`, five X11 connections,
  winstation reached; celeste-boot passes with a
  pixel-verified frame; boot64-smoke passes; a genuine
  32-bit start also certified in calibration on the live
  dist, handle `0x1004c`). The pin fixture itself still
  prints whatever non-NULL value creation returns;
  hardening the consumer rather than the canary is the
  fix — rebuilding the committed pin blobs to change
  their printout would court exactly the source/binary
  divergence FD-40 was. Record:
  `docs/reports/readiness-hardening-20261031.md`.
  *(Numbered FD-44 on the lane branch; renumbered
  FD-47 at integration — FD-44 was already taken on
  the integration branch by the thread-lifetime entry
  above.)*
- **FD-53 — The shipped support runtime archive contains
  two different files under one member name.
  CLOSED 2026-11-01 — see CL-24.** *(Medium)*
  The support archive produced by the dependency staging
  step — byte-identical at every recent merge/deploy
  gate (sha256 prefix `de592964f772`) — contains
  `usr/lib/x86_64-linux-gnu/libvulkan.so.1` twice, with
  different bytes (185,464 B, sha256 `60b89e5afc7c...`;
  457,224 B, sha256 `91dd2cb10ee4...`). Which copy a
  reader gets depends on how that reader resolves
  duplicate zip entries (Python's zipfile returns the
  last one; other extractors differ), so the Vulkan
  library the guest actually loads is not determined by
  the pinned archive hash alone. Found 2026-10-31 by
  branch `lane-fd01-so` from the staging step's
  duplicate-name warning during its dist build;
  pre-existing (the archive is the canonical one every
  recent gate verified) and NOT fixed there. Next step:
  find which staging input writes the second copy,
  decide which bytes are the intended ones, and make
  the staging step fail loudly on any duplicate member
  name. Evidence:
  `docs/reports/fd01-unix-half-fix-20261031.md`
  (section "Found along the way").
  *(Numbered FD-45 on the lane branch; renumbered
  FD-53 at integration — FD-45 was taken on the
  integration branch by the texture-parameter
  entry.)*
  Fixed on lane-fd53 in `1e228a7ed` — see CL-24.


- **FD-45 — Texture-parameter queries have no answer on the
  vector entry points or the WebGPU path (found by the FD-39
  lane, uncharacterized).** *(Low)* The bridge ABI traps only
  the scalar texture-parameter entry points (glTexParameteri /
  glTexParameterf in, glGetTexParameteriv out), so vector-form
  calls (glTexParameterfv / glTexParameteriv /
  glGetTexParameterfv -- the form that carries a full border
  color) never reach the bridge; and the WebGPU GL backend
  implements no texture-parameter query at all, so how a guest
  query is answered on that path (host-side tracking or not at
  all) is unverified. FD-39's store covers the scalar path;
  this entry tracks the rest of the query surface. Evidence:
  `docs/reports/fd39-texparam-store-20261031.md`.
- **FD-48 — After a stopgap session restart, a genuinely
  started session may never be certified by the hardened
  readiness test. CLOSED 2026-10-31 — see CL-15
  (fixed on branch lane-fd48-restart-cert).** *(Medium — the FD-47 certification is
  fail-closed here: it withholds a true verdict rather
  than issuing a false one)* Found at the batch merge
  gate (lane-readygate + lane-fd39 + lane-fd23-syscalls,
  2026-10-31): when the FD-01 race kills the first boot
  attempt and the restart stopgap starts a clean session,
  the restarted session can reach the full handshake in
  the guest stream — a plausible deskpin handle
  (`0x10054`), the restarted session's five X11 accepts,
  explorer running (server traces) — and still never be
  certified: `boot32-vkboot` timed out inside bootGuest
  at 600 s twice with identical anatomy, and the 32-bit
  D3D12 oracle's session likewise never reported ready,
  while the 64-bit shape certified in the same gate (the
  64-bit oracle's SESSION READY). The one handshake
  element with no counterpart in the persisted guest
  stream of this shape is the explorer-open line the
  observer matches; whether it is emitted before the
  restarted observer consumes, or not emitted into the
  observed stream in this shape after a reload, is not
  decidable from the persisted artifacts (the in-page
  observer's state is not logged). Single-session boots
  certify (the readiness lane's own boot32-vkboot pass,
  handle `0x10052`). The certification must not be
  weakened to close this: the failure direction is a
  withheld true verdict, surfaced loudly by every
  consumer's timeout, never a false certification.
  Evidence: `test-results/merge-batch3-20261031/`
  (boot32-vkboot.log, boot32-vkboot2.log,
  vkboot-attempt1-artifacts/, d3d12-32.log) and
  `docs/reports/merge-batch3-20261031.md`.
  RESOLUTION (lane-fd48-restart-cert, fix `509c26752`): the
  undecidable point is settled — the explorer-open line the
  observer matches is the 64-bit kernel's process-spawn signal
  (`sys_openat64: OPENED '<path>' -> fd <n>`, printed on every
  successful .exe open), and the 32-bit open paths
  (`syscall_open` / `syscall_openat` in
  `source/kernel/syscall.cpp`) never emitted an equivalent
  line, so the handshake's explorer element had no counterpart
  in ANY 32-bit session stream, single-session or restarted.
  (The readiness lane's preserved single-session console
  contains the full boot and zero such lines; how that run's
  in-page `ready()` returned true cannot be reconstructed
  from the preserved artifacts.) The fix emits the same signal
  from both 32-bit open entry points, successful opens only,
  in the same format after the syscall name. No readiness
  rule was changed — the launcher and observer are
  byte-identical to the integrated tree; the fix is entirely
  on the evidence side. Proof on the branch's dist (Browser
  export verified): 18 boot32-vkboot runs — 14 single-attempt
  certifications; TWO restart-shape certifications (run 10:
  dead first attempt, restarted session handle `0x10054`, 5
  X11, explorer OPENED, suite PASS; run 18: two dead attempts
  spending the full restart budget, third session certified
  the same way); two genuinely dead restart runs (both
  attempts killed by the FD-01 race) certified zero times in
  the full 600 s budget although the explorer signal fired on
  every attempt. Sabotage control on this branch's dist
  (unsigned win32u member): the pin printed `0xc000000d`, the
  old certification fires, the hardened certification refused
  in-page for the whole window (0 X11, no explorer). JS unit
  342/0; completeness 0 failures / 1 skip. Record:
  `docs/reports/fd48-restart-certification-20261031.md`.
- **FD-50 — The native Vulkan bridge test harness aborts at
  the pipeline-cache assertions, so every later section of
  the harness never runs. CLOSED 2026-11-01 — see CL-20.** *(Medium, test harness)* Found
  by the FD-16 lane (2026-10-31): `tools/vkd3d/tests/
  run-native.py` aborts on the pristine integration tip —
  the fn-921 `vkGetPipelineCacheData` test call makes the
  bridge write through a guest address (`0xFFFF00000040`)
  that the harness's bounded guest-memory model rejects
  with an assertion, killing the process before the
  sections after it (including the SPIR-V audit guards)
  execute. The abort is identical on unmodified
  `f3fd57d58`, so it is not a regression from any lane's
  fix; with only the two fn-921/922 assertions skipped in
  a scratch copy the full harness passes end to end
  (52 PASS lines). Either the bridge's host-pointer
  marshalling for the pipeline-cache functions must
  respect the harness's memory model, or the harness must
  give those calls guest memory they may legally write;
  until one of those lands, `run-native.py` cannot serve
  as a gate for anything past fn-921. Evidence:
  `docs/reports/fd16-texel-heap-arrays-20261031.md`.
  *(Numbered FD-48 on the lane branch; renumbered
  FD-50 at integration — FD-48 was taken on the
  integration branch by the restart-certification
  entry above.)*
  Re-confirmed 2026-11-01 by the FD-62 lane, on this
  register's base and on the then-current integration
  tip: the standard harness aborts identically (the
  fn-921 handler writing 8 bytes through a
  host-pointer-range value as a guest address; gdb
  backtrace in the FD-62 record). The FD-62 native
  guards sit behind this abort in the harness and were
  proven via a scratch classifier harness; they run in
  the standard harness once this entry lands.
  Composition (2026-11-01, merge lane): the fix landed
  as `165a1308f`. In the composed tree the harness now
  passes the pipeline-cache section (`PASS: former
  stubs (device image sizing, queue2, granularity,
  sparse, pipeline cache, buffer-view destroy)`) and
  executes 43 sections, including FD-16's SPIR-V audit
  guards, before stopping at FD-62's native guard — a
  separate, later defect registered as FD-67, which
  this abort had masked. End-to-end completion is
  FD-67's closing term, not this entry's; this entry's
  defect — the fn-921 abort and the silent skipping of
  every later section — is discharged. Evidence:
  `test-results/merge-fd50-fd55-fd62-20261101/`.
- **FD-49 — A class of captured Godot shader modules fails
  translation on format-less storage images. CLOSED 2026-10-31 — see CL-16.** *(Severity
  unmeasured)* Found by the FD-16 lane (2026-10-31) while
  replaying the modules the Godot demo submits: five
  modules fail in the vendored translator with
  `UnsupportedCapability(StorageImageReadWithoutFormat)`
  and one with `InvalidStorageImageWithoutFormat` — storage
  images used without a format qualifier, which WebGPU
  cannot describe as-is. No sampled/image-buffer capability
  is involved; this is a different class from FD-16, and it
  is not what blocked the demo's first frame (the guest log
  shows exactly one pipeline failure, the FD-16 one). It
  will block whichever guest pipelines use those modules
  once they are reached. Evidence: the module dumps and
  per-module verdicts in `wt-fd16/test-results/fd16/
  godot-capture/` (modules `21fc48e3`, `40b0cb95`,
  `9347f1b2`, `ba9896b5`, `c9c003a7`, `e1ca507c`), recorded
  in `docs/reports/fd16-texel-heap-arrays-20261031.md`.

- **FD-54 — With `BW64_DLLTRACE` set, the 64-bit
  process-spawn signal changes dialect and 64-bit session
  certification can never complete.** *(Low — behind a
  diagnostic environment variable no session harness
  currently sets; found by the FD-48 lane)* In
  `sys_openat64` (`source/kernel/syscall64.cpp`) the default
  spawn-signal line (`OPENED '<path>' -> fd <n>` on a
  successful .exe open) is explicitly suppressed when
  `BW64_DLLTRACE` is set, replaced by a `DLLTRACE … OPEN
  '<path>'` line. The readiness observer matches the default
  line, so in that configuration a 64-bit session's explorer
  launch leaves no matching trace and the FD-47 handshake
  can never complete — the same withheld-true-verdict shape
  as FD-48, behind a flag. Fix direction (for its own lane,
  since it touches the 64-bit path): emit the spawn signal in
  the matched format regardless of the trace flag, or keep
  the `OPENED` token in the trace line. Evidence: the
  `sys_openat64` source itself (the `!getenv("BW64_DLLTRACE")`
  condition on the default line) and
  `docs/reports/fd48-restart-certification-20261031.md`.
---

  *(Numbered FD-53 on the lane branch; renumbered
  FD-54 at integration — FD-53 is assigned in this
  batch to the support-archive duplicate-library
  entry.)*
- **FD-55 — The Godot demo presents no frame in the
  browser path: the guest never reaches its first
  frame. Its startup is still inside engine
  initialization — in-engine shader compilation — when
  every observation window ends, and no surface or
  swapchain is ever requested.** *(Medium — one demo's
  rendering path; CHARACTERIZED 2026-11-01 by the
  FD-55 lane; the present path itself is not
  convicted)* The merge gate's question — 'the guest
  never presents' vs 'it presents and the page never
  shows it' — is answered: the guest never presents,
  because it never gets there. Across five
  instrumented runs on the merged build (vktrace +
  drvinit milestones + guest thread dumps + shader
  dumps, the longest a full 1,752 s), the bridge —
  whose surface, swapchain and present handlers all
  log unconditionally — records ZERO vkCreateWin32
  SurfaceKHR, ZERO vkCreateSwapchainKHR, ZERO
  vkAcquireNextImage and ZERO vkQueuePresentKHR. The
  work that does flow is startup work: resource
  uploads only (61 buffer-to-image copies, zero draws,
  zero render passes in the longest run), ~213
  pipeline-layout pairs and ~94 shader modules created
  at a decelerating rate, the guest's main thread
  parked on a futex hand-off while exactly one worker
  thread runs engine code — the classic shape of
  Godot compiling its built-in shader set on a worker
  while the main thread waits, at interpreter speed.
  A --verbose run's engine output stops advancing
  after the D3D12 device line (155 s): no scene-load
  lines ever appear. The work is finite and
  deterministic (distinct module hashes, new ones
  still arriving at 1,738 s) but exceeds the demo
  window by an order of magnitude; nothing in the
  emulator's present stack is implicated (the D3D12
  oracle programs present and read back on the same
  build, both word sizes). What could close this
  entry: the initialization workload finishing inside
  a demo window — a pre-warmed shader/pipeline cache
  shipped for the demo, or a step change in guest
  execution speed. Both are product-level decisions
  outside a fix lane; the demo check must not be
  weakened to accommodate the wait. Caveat for future
  investigators: late-session emulator METRICS
  snapshots in these runs are a frozen instrument —
  see FD-63. Evidence: `test-results/fd55/` (char1,
  long1, verb1, cap1, cap2 — consoles, thread dumps,
  extracted guest shader dumps). Two sub-defects were
  found on the way: FD-61 (a shader-audit rejection of
  servable modules — fixed on this lane's branch) and
  FD-62 (a genuinely unservable writable-image array
  shape, still open).
  **Update (2026-11-01, `lane-fd55prewarm`): the cause is
  narrowed, and this entry's title claim is corrected.**
  Three full captures through the standard demo path
  (3,322 s cold; 3,330 s and 4,227 s with a pre-warmed
  demo archive) all present zero frames, and the guest's
  own engine log shows why: every D3D12 graphics
  pipeline-state creation fails with error 0x80004005
  (E_FAIL) inside the guest translation layer
  (`Create(Graphics)PipelineState failed`,
  `rendering_device_driver_d3d12.cpp:5360`); the engine
  continues with null pipelines
  (`scene_shader_forward_clustered.cpp:506`) and can
  never draw. The bridge census agrees: 239 shader
  modules and ~1,100 pipeline layouts created, zero
  `vkCreateGraphicsPipelines`, zero swapchains, zero VK
  presents. So the pipelines do NOT compile — their
  creation is refused — and the block sits inside
  pipeline-state creation: not downstream in the present
  path, not an observation gap, and not startup cost.
  The entry stays open on that defect; closing needs the
  pipeline-creation failure fixed and a presented frame
  proven. The same lane built the repeatable pre-warm
  packaging step (`tools/demo-packaging/`, capture driver
  `web/tests/godot-warm-capture.mjs`); the demo entry now
  names the warmed archive it produces, and the cacheable
  part of startup is paid at packaging time (66 engine
  shader-cache files, stable across captures; the seeded
  cache phase completes in ~213 s) — which is what will
  let the demo fit the standard window once pipeline
  creation works. Raw cold startup speed is registered
  separately as FD-71. Record:
  `docs/reports/fd55-prewarm-packaging-20261101.md`;
  evidence: `test-results/fd55prewarm/`.

  Pipeline-creation lane annotation (2026-11-01, branch
  lane-fd55pipe): on the composed tree (FD-61 and the FD-62
  hardening landed) this entry's blocker is pipeline creation,
  proven directly. Two full standard-path captures with the warmed
  archive (1,863 s and 1,923 s; evidence
  test-results/fd55pipe/diag1 and diag2) show the engine reaching
  pipeline creation and being refused there: the FD-61-fixed
  vertex module (fixture godot-vs-heap-plus-cbv-heap.spv, bridge
  djb2 a61ba068136e1191) now creates successfully, and its
  fragment partner — FD-62's captured module, byte-identified by
  the bridge's new refusal logging (5,704 bytes, djb2
  0b3b4d8cc939485a, the godot-fs-atomic-texel-heap.spv fixture) —
  is refused at creation for ImageBuffer and
  RuntimeDescriptorArray, each refusal landing in the same second
  as the guest translation layer's hresult_from_vk_result(-8) and
  the engine's Create(Graphics)PipelineState 0x80004005. Five such
  failures per run; zero vkCreateGraphicsPipelines, surface,
  swapchain or present calls reach the bridge in either run. A
  second, later wave of refusals (three creations at ~1,881 s) is
  filed as FD-70. This entry stays open: its closing condition is
  now FD-62's closing condition, plus FD-70's class. Record:
  docs/reports/fd55-pipeline-creation-20261101.md.

  **Update 2026-11-01 (FD-62 serving lane):** FD-62 is
  CLOSED — see CL-25. In vivo on the warmed archive
  (build 2e890f315, wasm a2e1059d): the byte-identified
  module (5,704 B, djb2 0b3b4d8cc939485a) was created
  twice with no refusal; across 254 vkCreateShaderModule
  calls there were zero audit refusals and zero
  CreateGraphicsPipelineState E_FAILs (the two baseline
  runs had five E_FAILs each, each in the same second
  as this module's refusal). The guest completed its
  PSO warm-up and reached vkQueueSubmit2. The demo
  still presented nothing (0 guest presents at the
  1,500 s deadline): no swapchain was created,
  submissions dropped at the bridge's
  `!view || !view->image` guard (8,386 drops, logged),
  FD-70's two modules were never submitted this run
  (status untested in vivo), and FD-72's
  HelperInvocation blocker still stands in front of
  any actual draw using this module. This entry stays
  open on FD-70 + FD-72 and that downstream behavior.
  Record: `docs/reports/fd62-texel-serving-20261101.md`;
  evidence `test-results/fd62serve/warm-20261101-110605/`.

- **FD-61 — The shader-capability audit rejected
  modules whose descriptor arrays are servable,
  because buffer-content runtime arrays survived its
  specialization sweep. CLOSED 2026-11-01 — see CL-21.** *(Medium — graphics
  correctness; FIXED on lane-fd55, awaiting merge.
  Numbered FD-56 on the lane's register;
  renumbered FD-61 in the composed numbering (the
  FD-38 lane's provisional FD-56 is FD-59).)* The bridge audit admitted the
  RuntimeDescriptorArray capability only when its
  simulation of the spirvfix orphan sweep left NO
  OpTypeRuntimeArray alive. vkd3d-proton emits, in the
  same modules, runtime arrays that are buffer
  CONTENT — its SSBO-offsets block (a runtime array
  inside a storage-buffer struct) and its CBV heap (a
  bare storage-buffer variable of runtime-array-of-
  struct type). Specialization correctly leaves those
  in place (the tier serves them as ordinary buffers,
  and naga accepts the retained capability declaration
  above them), so the audit vetoed modules the tier
  demonstrably serves. Two real Godot vertex modules
  (vkd3d dumps 480d68cc811ceb30, 29caf5794322da20)
  were rejected at creation with
  VK_ERROR_FEATURE_NOT_PRESENT in every demo run,
  each killing a Godot pipeline (0x80004005). Fix
  (lane-fd55): the sweep's verdict now counts a
  surviving runtime array against the capability only
  when it is a descriptor array type — the direct
  pointee of a UniformConstant pointer; comments in
  both mirrors aligned; register citations re-pinned
  (+14 lines). Proof: the extracted C++ classifier on
  the captured modules flips both from skip to admit
  while a genuinely unservable module stays rejected;
  new unit test `spirvfix-r15-buffer-arrays` (3/3,
  sabotage-checked); in-vivo demo run on the fixed
  build — capability rejections 3 → 2 and both
  modules now create successfully (the remaining
  failures are FD-62). Bounded residue: a module
  declaring the capability with ONLY buffer-content
  arrays and no descriptor array still rejects; no
  captured instance exists. Evidence:
  `test-results/fd55/` (cap1 vs cap2 consoles),
  fixtures `testdata/spirv/godot-vs-heap-plus-*.spv`.
  Composition (2026-11-01, merge lane): landed as
  `7c3fc661b`. On the composed tree the new unit suite
  `spirvfix-r15-buffer-arrays` passes inside the full
  JS unit suite (346 passed, 0 failed), the vulkan
  citation audit reports 0 drift on the re-pinned
  composed bridge, the dist build verifies, and
  packaging completeness reports 0 failures. The
  bounded residue above (the capability declared with
  only buffer-content arrays) is registered as FD-68
  so it stays tracked. Evidence:
  `test-results/merge-fd50-fd55-fd62-20261101/`.

- **FD-63 — The emulator's periodic METRICS emission
  stops a few minutes into a session, freezing the
  metrics snapshot for the rest of the run.** *(Low —
  developer observability; open. Numbered FD-58 on
  the lane's register; renumbered FD-63 in the
  composed numbering.)* Found by the FD-55 lane
  (2026-11-01): in a 1,752 s demo run the console
  carries exactly three `bw64: METRICS` lines (226.9 s,
  227.4 s, 228.4 s) and none after, while the session
  runs on for another ~1,500 s; the harness's
  metrics.jsonl rows repeat identical counters
  (syscall calls=239, ms=46108.4 in every row).
  Anything computed from late-session metrics
  snapshots — stage timings, present counts — is
  reading a frozen instrument. Evidence:
  `test-results/fd55/long1/` (console.txt,
  metrics.jsonl).

- **FD-66 — The native bridge harness's sanitizer flavour
  cannot finish clean: readback blocks the fixture leaves
  pending are reported as leaks, and the bridge drops
  pending readbacks without freeing them.** *(Low —
  developer-only verification debt, plus a latent bridge
  sharp edge no production path currently reaches)* Found
  by the FD-50 lane (2026-11-01) once the harness could
  run to completion: under
  `VK64_TEST_SANITIZERS=address,undefined` every section
  executes and passes and no memory or undefined-behaviour
  error is reported, but LeakSanitizer reports 1,132 bytes
  in 5 allocations at exit, all from the bridge's
  `setupReadbacks` (`source/vulkan/vk64bridge.cpp` — the
  image-readback and buffer-readback mallocs). Three
  harness sections call `setupReadbacks` directly to
  check readback planning and never call
  `completeReadbacks`; the production submit paths always
  pair the two, and `completeReadbacks` frees every
  block, so no production leak is proven. The latent
  half: `setupReadbacks` opens by clearing both pending
  vectors without freeing any blocks still pending, so a
  second setup after an unpaired first one loses the
  blocks silently — today reachable only from the
  fixture. Either the fixture completes the pairing after
  its planning checks, or setup learns to free what it
  discards; until one lands, the sanitizer flavour cannot
  serve as a clean gate. Measured alongside: the
  instrumented suite needs ~35 s wall on a loaded box,
  over the runner's 30 s execution cap, so through the
  runner it currently ends as a timeout rather than the
  leak report. Evidence:
  `test-results/fd50/run-san-manual.log`,
  `test-results/fd50/run-san-noleakcheck.log` (control:
  leak check off, all 52 sections pass, exit 0), and
  `docs/reports/fd50-native-harness-completion-20261101.md`.
  *(Numbered FD-61 on this lane branch; renumbered
  FD-66 in the composed numbering — FD-61/FD-62/FD-63
  belong to the FD-55/FD-57 lanes' items, and FD-64
  and FD-65 were taken on the integration branch
  meanwhile.)*
- **FD-62 — A class of real shader modules is refused
  over atomic operations performed through image texel
  pointers; the refusal now rests on the true, named
  blocker, but the modules remain unserved because the
  translation stack rejects OpImageTexelPointer
  outright. CLOSED 2026-11-01 — see CL-25.** *(Medium — the affected pipelines fail
  creation outright; measured in the FD-55 proof run
  as two 0x80004005 pipeline failures from the captured
  Godot module)* Found by the FD-55 lane (2026-11-01;
  numbered FD-57 on that lane's register, renumbered
  FD-62 in the composed numbering). The captured
  fragment module `6f52b154d73c804f` (fixture:
  `testdata/spirv/godot-fs-atomic-texel-heap.spv`)
  indexes a writable storage-image heap array (set 4,
  binding 0) and runs `OpAtomicOr` through
  `OpImageTexelPointer` on the elements. Both audit
  mirrors (spirvfix `r15PointerUses`, bridge
  `r15::pointerUses`) treated forming a texel pointer
  from an array element as a generic pointer escape
  (`array element pointer escape (op60)`) and never
  audited the pointer's uses — the refusal was correct
  only by accident, and this register's originally
  suggested fix (follow the pointer and admit its
  uses) would have flipped the modules to served at
  the audit while the translation stack cannot serve
  them: measured 2026-11-01 against the vendored
  translator (naga 30.0.1), ANY module containing an
  OpImageTexelPointer fails translation
  (UnsupportedStorageClass(Image) for 2D storage
  images on every backend; Buffer-dim images fail
  earlier with UnsupportedCapability(ImageBuffer)) —
  admission would have moved the failure later and
  quieter, the FD-49 failure shape. The FD-62 lane
  (branch `lane-fd57`) hardened the audit instead, in
  both mirrors, instruction-for-instruction: the uses
  analysis now audits THROUGH OpImageTexelPointer —
  the texel pointer's loads, stores and atomics
  classify as image-content reads/writes, counted
  separately from writes to the descriptor slot
  itself; the texel pointer VALUE escaping (stored,
  passed on) stays an escape; the array classifier
  refuses any texel formation through an element with
  a named reason (`texel-pointer atomic write through
  an array element is not served (the translation
  stack rejects OpImageTexelPointer)`, with store/use
  variants); the root-descriptor mirrors account
  texel writes as rejections on the same terms. Net
  behaviour change: zero modules change verdict —
  every module served today stays served, every
  refused module stays refused; only the
  classification content and the refusal reasons
  changed. Proof: JS suite
  `spirvfix-texel-pointer-arrays.test.mjs` (5 tests
  over the captured module itself plus three
  hand-authored fixtures — minimal reduction,
  OpCopyObject sabotage twin, two-array attribution,
  stored-pointer escape) fails 5/5 before the change
  and passes after; the native mirror guard in
  `tools/vkd3d/tests/vk64_native_test.cpp` is proven
  via a scratch classifier harness (the standard
  harness cannot reach it until FD-50 lands); one
  pre-existing r15 test pin moved from the old escape
  wording to the named reason, its refusal assertions
  kept and strengthened. Closing this entry requires
  a translation stack that ingests OpImageTexelPointer
  (translator support for the Image pointer storage
  class and Buffer-dim storage images), or a sound
  pre-translation rewrite eliminating texel pointers
  (loads/stores have image read/write equivalents;
  atomics through texel pointers have no WGSL
  equivalent and need a different serving strategy).
  Until then the named refusal is the correct end
  state. Record:
  `docs/reports/fd62-texel-pointer-atomics-20261101.md`.
  Composition correction (2026-11-01, merge lane):
  with FD-50 landed, this entry's native guard now
  executes in the standard harness — and fails at its
  first bridge-level assertion: the bridge admits the
  guard's texel-atomic heap module at
  vkCreateShaderModule (VK_SUCCESS) instead of
  refusing it. The classifiers record the texel
  refusal in the array verdict, but the audit's
  admission path never consults it, so the two
  mirrors disagree (the JS mirror refuses the class
  by name). Registered as FD-67; the lane record's
  claim of a completed standard-harness run including
  this guard is not reproducible from the committed
  tree and is superseded by FD-67's evidence. This
  entry stays open on its original terms.
  Demo impact measured (2026-11-01, FD-55 pipeline-creation lane):
  this entry's captured module is the Godot demo's FIRST
  pipeline-creation blocker on the composed tree — the first two
  of five 0x80004005 failures in each of two full standard-path
  captures, byte-identified in vivo through the bridge's refusal
  logging (see the FD-55 annotation and
  docs/reports/fd55-pipeline-creation-20261101.md). Closing terms
  unchanged.

- **FD-70 — A second class of real Godot shader modules
  is refused at creation: a descriptor runtime array that
  specialization cannot remove, combined with
  physical-storage-buffer addressing in its USED form. *(Medium —
  the affected pipelines fail creation outright; three of the five
  0x80004005 failures in each FD-55 pipeline-creation capture, in
  a late wave at ~1,819 s (diag1) and ~1,881 s (diag2). Filed
  provisionally as FD-70 by the FD-55 pipeline-creation lane,
  2026-11-01; numbered FD-70 at composition on 2026-11-01 —
  the number was free on integ.)* In the
  diag2 capture (build 72360dda5; evidence
  test-results/fd55pipe/diag2) the engine's scene-shader retries
  hit two further modules the bridge audit refuses with
  VK_ERROR_FEATURE_NOT_PRESENT: 23,440 bytes (djb2
  28a2394720432fda, one creation) and 16,304 bytes (djb2
  3eccbc1e60e9f7a4, two creations), refused for
  spirv-capability:RuntimeDescriptorArray (the descriptor array is
  not specializable, so the R15a exemption does not apply) and
  spirv-capability-unknown:5347 (PhysicalStorageBufferAddresses in
  its used form — neither the unused-declaration exemption nor R15
  root-descriptor servability applies). The second 16,304-byte
  creation's log shows only the 5347 reason; whether the aggregate
  refusal logger coalesced the repeated array reason or the two
  creations differ below the djb2 hash was not chased — the
  verdict and the layer are the same either way. No fixtures exist
  for these modules yet (nothing in testdata/spirv/ matches their
  sizes); the bridge's refusal logging (lane-fd55pipe 72360dda5)
  now names size+hash for every refused module, so a future
  capture can be matched against fixtures byte-for-byte once they
  are taken. Closing this entry requires the descriptor-heap
  serving path to serve non-specializable descriptor arrays with
  used physical addressing, or proof — once FD-62's first wave is
  served — that these modules are never reached on the demo path.
  Record: docs/reports/fd55-pipeline-creation-20261101.md.

- **FD-67 — The bridge's shader audit admits modules
  whose descriptor-heap array elements form texel
  pointers, although its own classifiers record them
  as refused and the JS mirror refuses them by name.
  CLOSED 2026-11-01 — see CL-22.**
  *(High — graphics correctness / mirror divergence;
  FIXED on lane-fd67fix; composed into integ on 2026-11-01 by the FD-67 guard-correction merge. Found by the FD-50+FD-55+FD-62 merge lane's gate,
  2026-11-01.)* FD-62's hardening taught both
  descriptor-array audit mirrors to follow
  OpImageTexelPointer and to record, per array
  variable, the texel-pointer uses the translation
  stack cannot serve (`texelAtomicByVar` in the array
  verdict). On the JS side the refusal takes effect:
  spirvfix refuses the class with the named reason
  ('the translation stack rejects OpImageTexelPointer').
  On the bridge side nothing consumes the recorded
  verdict: `vk64AuditSpirv` consults the array verdict
  only for texel-buffer servability and the
  RuntimeDescriptorArray capability strip, so
  `vkCreateShaderModule` returns VK_SUCCESS for a
  module in the refused class (measured directly on a
  diagnostic build: verdict 0, and no capability
  rejection is counted for it). The failure surfaces
  exactly where FD-50's fix lets the standard native
  harness reach FD-62's guard: `run-native.py`
  executes 43 sections — including the pipeline-cache
  section FD-50 repaired and FD-16's guards — then
  aborts at the guard's first bridge-level assertion
  (`vk64_native_test.cpp:3481`: the module must come
  back VK_ERROR_FEATURE_NOT_PRESENT). Proven not to
  be a composition interaction: the identical abort
  occurs with FD-61's bridge delta reverted (FD-62's
  own bridge in an isolated harness build), and
  FD-62's lane tree cannot reach the guard at all
  (its harness still dies at FD-50's fn-921 call).
  The FD-62 lane record's claim of a completed
  standard-harness run (53 PASS lines including this
  guard, exit 0) is not reproducible from the
  committed tree, and its cited evidence directory is
  absent from that tree; the register's Section B row
  for the lane correctly described the guard's proof
  as a scratch harness. Closing terms: the bridge
  audit refuses the recorded texel class at
  shader-module creation, by name, mirroring the JS
  verdict (or the sweep simulation becomes texel-aware
  so the capability path refuses); FD-62's native
  guard then passes as written, or is corrected to
  the implemented attribution with the correction
  recorded here; and `run-native.py` runs to
  completion end to end. Evidence:
  `test-results/merge-fd50-fd55-fd62-20261101/`
  (`run-native.log`,
  `run-native-direct-unmodified.log`).

  Correction (2026-11-01, FD-67 lane, branch
  `lane-fd67`): the mechanism above is not the
  operative cause of the harness abort, measured on
  the unmodified composed tree (evidence:
  `test-results/fd67/`). The guard's three fixtures
  (`heapTexelAtomic`, `heapTexelAtomicCopy`,
  `heapTexelAtomicTwo` in
  `tools/vkd3d/tests/vk64_native_test.cpp`) — and
  the hand-authored JS fixtures they transcribe
  (`testdata/spirv/atomic-texel-heap{,-copy,-twoarrays}.spv`)
  — declare an `OpEntryPoint` of GLCompute
  (model 5). `vk64AuditSpirv` accepts compute-only
  modules at creation by deliberate, documented
  policy (the capability and storage checks protect
  the graphics translation path; a compute module
  the tier cannot serve fails later at pipeline
  translation, permanently and counted) and returns
  before the R15 section is reached: an instrumented
  build shows no audit verdict at all for the
  guard's first module call, and the acceptance is
  counted as `spirv-compute-entry` — matching this
  entry's "verdict 0, no capability rejection
  counted" measurement. On the graphics path the
  recorded class IS refused at creation by name
  today, with no bridge change: a probe through the
  harness's own create-module path on the pristine
  tree returns VK_ERROR_FEATURE_NOT_PRESENT with
  `spirv-capability:RuntimeDescriptorArray` +1 for
  (a) the captured module itself
  (`testdata/spirv/godot-fs-atomic-texel-heap.spv`,
  entry model 4 — the FD-62 instance is a fragment
  module), and (b) each hand-authored fixture with
  its entry-model word flipped 5 -> 4, the only word
  changed; as authored (model 5) each returns
  VK_SUCCESS. The refusal runs through the
  capability path: texel formation fails the
  per-chain servability check closed, so no array is
  servable, the 5302 strip does not apply, and the
  capability is refused by name; no consultation of
  `texelAtomicByVar` is needed for the graphics
  outcome, and refusing the class on the compute
  path as well would override the compute-accept
  policy and change verdicts FD-62's hardening
  promised to leave unchanged. Disposition: this
  entry's closing terms reduce to their second
  branch — FD-62's native guard is corrected to the
  implemented attribution (its bridge-level fixtures
  authored as graphics modules, as FD-16's refusal
  fixtures already are: entry model 4), after which
  the guard passes as written and `run-native.py`
  completes end to end; the bridge requires no
  change. This entry stays open until that guard
  correction lands and the harness completes.
  Record: `docs/reports/fd67-guard-attribution-20261101.md`.
  That correction landed on `lane-fd67fix` and the
  harness completes end to end there — see CL-22.

- **FD-68 — A module that declares
  RuntimeDescriptorArray but carries only
  buffer-content runtime arrays is still rejected by
  the shader audit.** *(Low — graphics correctness;
  no captured instance; open. Recorded by the FD-55
  lane as FD-61's bounded residue; numbered at
  FD-61's closure so it stays tracked.)* FD-61's fix
  counts a surviving runtime array against the
  capability only when it is a descriptor array type;
  a module whose arrays are all buffer content and
  which declares the capability still rejects, and no
  captured module is known to hit the case. Closing
  terms: a captured instance, or an audit change that
  admits the case with the mirror updated in step and
  a unit fixture proving it.

- **FD-69 (provisional number) — One FD-16 native
  guard fixture is a compute module, so its
  bridge-level assertions pass through the
  compute-accept path instead of the R15a admission
  they narrate. CLOSED 2026-11-01 — see CL-23.**
  *(Low — test attribution; FIXED on lane-fd67fix; composed into integ on 2026-11-01 by the FD-67 guard-correction merge. Found by the FD-67 lane,
  2026-11-01.)* The FD-16
  guard's multi-signature fixture
  (`heapTexelMultisig` in
  `tools/vkd3d/tests/vk64_native_test.cpp`) declares
  an `OpEntryPoint` of GLCompute (model 5); its
  assertions (module admitted, SampledBuffer and
  RuntimeDescriptorArray counts unchanged) are
  satisfied by `vk64AuditSpirv`'s compute-accept
  return, not by the R15a specialization admission
  its PASS line describes. The class's servability
  is genuinely proven on the JS mirror
  (`spirvfix-texel-heap` suite) and by the bridge
  classifier over the guard's graphics fixtures, so
  no product behaviour is mis-stated — only the
  native guard's attribution for this one fixture
  is vacuous. Closing terms: the fixture is
  re-authored as a graphics module (or the guard's
  claim narrowed to the classifier level) in the
  same guard-correction pass that closes FD-67,
  and the guard then passes with the audit's R15
  section demonstrably reached. Evidence:
  `test-results/fd67/`. Same-pass find (FD-67
  guard-correction lane, 2026-11-01): the FD-16
  section's storage fixture (`heapTexelStorage`,
  likewise distilled from a compute shader) carried
  the identical vacuity — its admission assertion
  was satisfied through the compute-accept return
  (instrumented run: `spirv-compute-entry`, no R15
  verdict). Re-authored as a Fragment module in the
  same pass, it reaches the R15 section, classifies
  servable, and is admitted through R15a with the
  guard's assertions untouched, so CL-23 covers
  both fixtures.

- **FD-71 — The Godot demo's raw cold first-run startup
  is far too slow to fit the standard demo window.**
  *(Medium — first-visit experience for cache-heavy
  D3D12 programs)* Registered by `lane-fd55prewarm`
  (2026-11-01; filed provisionally as FD-67 on that
  branch — FD-67 and FD-68 were both already taken on
  integ — and numbered FD-71 at composition on
  2026-11-01) to hold the startup-speed truth separately
  from FD-55's pipeline-creation defect. Measured: cold,
  the engine is still compiling built-in shaders well
  past 1,700 s (the sibling characterization lane saw
  new shader hashes at 1,738 s), and a full 3,322 s cold
  capture on this lane never reached a first frame. The
  cacheable portion is now known and bounded: 66 engine
  shader-cache files plus the translation layer's
  pipeline cache (~2.6 MB in total), and with that state
  seeded the cache phase completes in ~213 s. What
  cannot yet be measured is true cold time-to-first
  frame: every cold run is also blocked by FD-55's
  pipeline-creation failure, so this entry can only be
  sized — and closed — after that defect is fixed. The
  pre-warm packaging step (`tools/demo-packaging/`) is
  the standing mitigation for the demo itself; this
  entry tracks the underlying speed for programs that
  arrive without a warmed archive. Evidence:
  `test-results/fd55prewarm/`; record:
  `docs/reports/fd55-prewarm-packaging-20261101.md`.

- **FD-72 — The vendored shader translator cannot
  ingest a BuiltIn HelperInvocation variable in any
  spelling, which blocks the captured Godot fragment
  module one step past its (now served) texel
  class.** *(Medium — the module's remaining
  translation blocker; found by the FD-62 serving
  lane, 2026-11-01)* The captured module
  (`testdata/spirv/godot-fs-atomic-texel-heap.spv`)
  loads the HelperInvocation builtin (BuiltIn 23) at
  two sites: a ballot election
  (`laneId == broadcastFirst && !helper`) and
  `select(helper, 0, bit)`, masking helper lanes out
  of the second texel atomic's OR value. Measured
  against the vendored translator (naga 30.0.1): a
  BuiltIn HelperInvocation variable fails with
  `UnsupportedBuiltIn(23)`; `OpIsHelperInvocationEXT`
  with its capability fails with
  `UnsupportedCapability(DemoteToHelperInvocation)`;
  the EXT opcode without the capability is invalid
  SPIR-V and is rejected as an unsupported
  extension. Substituting a constant for the helper
  flag would change the atomics' input bits — the
  wrong-data outcome — so no honest local rewrite
  exists today. It is the only corpus module
  carrying the builtin. Closing this entry requires
  translator support for helper invocation (a WGSL
  is-helper-invocation path, or a demote-based
  lowering whose masked-atomic semantics are
  proven), after which the captured module should
  translate end to end. FD-55 stays open on this
  entry and FD-70. (Numbering per the merge lane's
  settled audit on integration: FD-70 confirmed,
  the pre-warm speed entry is FD-71; this entry is
  the next free number there and is filed on this
  branch for the composing merge to place.)

- **FD-73 — The pinned runtime-assets release
  archive disagrees with the manifest's file pins
  for both Wine archives, so a fresh checkout
  cannot bootstrap. CLOSED 2026-11-01 — see
  CL-26.** *(Low — masked in every tree
  that already carries the files; found by the
  FD-62 serving lane, 2026-11-01, during its
  worktree bootstrap)* `tools/browser/bootstrap.py
  --local` against the pinned archive (sha256
  `9978166f…`, matching the manifest's archive pin)
  fails: the archive's `web/runtime/wine64.zip`
  hashes to `b7476541…` against a file pin of
  `53d77f33…`, and its `web/runtime/wine32.zip` to
  `955cc41c…` against `7f2209cf…`; the other eleven
  files match their pins. Trees that already carry
  the pinned files pass `bootstrap.py --check`
  without ever reading the archive, which is how
  the disagreement survived a full merge gate. A
  fresh worktree must currently restage from a
  verified tree instead (this lane did, per-file
  sha256 against the manifest). Closing this entry
  requires the release archive to be regenerated
  from the pinned files and re-uploaded (a
  user-gated release action), or the manifest
  re-pinned to the archive with a completeness
  proof of the older Wine builds — the former,
  since the file pins are what shipped builds
  verify against.

## B. Fixed on a lane branch — awaiting merge

These are findings whose fixes are written and proven on their
branches but are **not** in the integration tree. The entries
they answer stay open until the branch lands and, where the fix
is behavioural, is re-proven there.

| Branch | What it fixes | Proof on the branch |
| --- | --- | --- |
| `lane-firstvisit` | FD-01: the desktop-initialization impatience defects (Wine win32u backport — wait raised, announcement check retries) | Fresh-profile before 0/6 → after 6/6; JS 333/0; native 648/0; completeness 0 failures |
| `lane-fd55` | FD-61: the shader-capability audit's specialization sweep counted buffer-content runtime arrays against the RuntimeDescriptorArray capability and rejected servable modules (fix in the bridge audit + mirror comments; vulkan register citations re-pinned) | Standalone C++ classifier on the captured modules: both flip admit 0→1, a genuinely unservable module stays 0; new unit test spirvfix-r15-buffer-arrays 3/3 (sabotage-checked); in-vivo demo run: capability rejections 3 → 2 and both modules now create (the 2 remaining failures are FD-62); dist 'Browser export verified', completeness 0 failures; JS suite 345/0 |
| `lane-cleanlog` (pathfinder) | The campaign pattern standard; guest memory faults were logged nowhere; 11 system calls panicked on unsupported arguments by default; launcher downloads did not retry | Native 648/0 serial |
| `lane-cleanweb` | Page-layer silent failures: session-start load failures left visitors on an endless spinner; save-sync, shader-cache, and clipboard failures vanished | JS suite accounted 333/4 (all four classified) |
| `lane-cleanup-bridges` | Graphics-bridge fabricated/dropped answers were invisible (now logged once per case — the answers themselves are FD-15); shader warming could fail silently; the silent-failure scanner miscounted loud rejections | Guard tests 16/16; graphics subset 47/47 |
| `lane-cleanlog32` | 32-bit fault delivery: faults went silent after 10, handler-less faults exited the whole emulator, one fault class (BOUND) had no delivery at all | Native 649/0 serial; sabotage-proven |
| `lane-cleanlog-ksubsys` | Kernel subsystems: guest-reachable panics across event/wait/socket/process areas degraded to honest answers; two silent lies (a send that sent nothing, a receive that read nothing) fixed | Native 649/0 serial; sabotage-proven |
| `lane-cleanmem` | Guest memory: three 32-bit panics on ordinary requests; silent denials on both word sizes now log with reasons | Native 652/0 serial; red/green proof |
| `lane-cleanwave-kernelgate` | Reporting hidden behind an off-by-default switch (two real cases); adds a mechanical audit that fails if reporting is ever gated again | Native 648/0; audit sabotage-proven both ways |
| `lane-clean-cpu` | Silent 64-bit instruction faults now log; a live debug-build kill in the instruction-log formatters degrades; fast-engine startup failures no longer discard their reason | Native 648/0 single-worker |
| `lane-cleanfs` | Filesystem: a repeated error-code-as-length bug shape (one case could corrupt memory); discarded zip integrity verdicts; partial extractions served forever; a test that existed but was never registered | Native 654/654 serial; sabotage-proven |
| `lane-tooling-honesty` | The build script printed success on a failed export (mechanism found and fixed); harnesses that failed silently now refuse loudly or return real verdicts; the marshal-generator panic referred by the bridges lane, fixed in the generator | JS 333/0 with own-tree build; the previously "broken" oracle ran green end-to-end (CL-03) |
| `lane-docsclean` | Status and compatibility documents rewritten from current measurements (several rows had been inverted); disused planning files deleted with disuse proven | Link check: zero new breakage |
| `lane-fd06-portio` | FD-06: 32-bit port instructions and int 0x99 with an unregistered callback index now deliver #GP through the core's exception machinery, instead of the wrong fault (SIGILL) and an emulator-killing panic — the frontier review's D9 disposition, after settling that ioperm/iopl grant nothing | New testPortIoFault32 pins signal/trap/error for all six port forms, REP INSB, and int 0x99; sabotage-proven (port cases deliver SIGILL and the int 0x99 case kills the test binary with the old panic text on pre-fix code); native suite 661/661 accounted across the serial run and the standalone tail window |
| `lane-alkhaser-int2d` | Interrupt-delivery class: software interrupts delivered with the wrong saved code so the guest ignored them; the breakpoint instruction was never delivered and parked the thread | Proven through the website path (the checker completes whole sections it previously died in) |
| `lane-firstframe` | Six shared D3D12 first-frame walls (shader-capability dispositions, subgroup serving + advertisement timing, 23 shim-stubbed entry points, a second whitelist, 8-bit integer render targets) | JS 334/0; native 0 failed serial; the affected demo's D3D12 driver fully initializes through the website path |
| `lane-fd23-syscalls` | FD-23 (in part): the undispatched-syscall disposition pass — three-way census tool, dynamic census of preserved run logs, and implementations of `epoll_pwait` (281), `accept4` (288), `dup3` (292) on the 64-bit side; FD-46: `accept4`'s `SOCK_CLOEXEC` silently dropped by a flag-encoding mismatch, fixed by validating/translating in `kaccept` for both ABIs | New dispatch-level tests fail before (logged -ENOSYS for 281/292; all three FAILED) and pass after; x64 suite 61/61, 0 FAILED, gate-standard serial mode; census re-run: implemented 193 -> 196, absent 168 -> 165. Main 661 suite NOT run on the branch (disk pressure; `kaccept` change is the identity for flags == 0) — merge gate must run the full native gate |
| `lane-readygate` | FD-47: readiness certification hardened (plausible handle + five-X11 handshake + explorer launch); FD-43 closed by the four-arm measurement it demanded (CL-10/CL-11) | Sabotage pair: broken-dispatch control false-ready 10/10 under the old certification, 0/10 under the new (offline + in-page); genuine starts certify on the stamped arm (boot32-vkboot, celeste-boot, boot64-smoke pass); readiness unit tests red/green; JS 340/0; four-arm true rates A 0/10, B 0/10, D 0/10 |
| `lane-fd48-restart-cert` | FD-48: the 32-bit open paths now emit the process-spawn signal (`OPENED '<path>' -> fd <n>` on successful .exe opens) that the FD-47 handshake's explorer element matches — the line existed only on the 64-bit path, so no 32-bit session could ever be certified; FD-54 registered (DLLTRACE dialect) | boot32-vkboot x18 on the branch dist: 14 single-attempt certifications, 2 restart-shape certifications (one after the full restart budget), 2 genuinely dead runs certified zero times; unsigned-control sabotage boot: old cert fires on `0xc000000d`, new cert 0, page cert 0; JS 342/0; no readiness rule changed |
| `lane-fd49` | FD-49: format-less storage images at multi-type heap bindings — six captured Godot compute modules failed translation (the R14 signature keyed one format per binding while two storage image types share it; specialized clones of both types then collide at one binding in the translator; one empty shader poisoned by unreferenced format-less type declarations). Fixed in `df68b0d92`: per-type signature entries (`set:binding@typeKey=fmt`), specialized-binding-split (surplus clones move to fresh bindings, origin kept in the clone name and honoured as `origBinding`), and prune-dead-storage-image-types for the unreferenced declarations; every refusal left in place stays fail-closed | New suite `vk-storage-format-multi-type.test.mjs` (10 tests) over the six captured modules themselves through the tier path: all translate post-fix (fail-before on the pristine tree: `UnsupportedCapability(StorageImageReadWithoutFormat)` x5, `InvalidStorageImageWithoutFormat` x1), sabotage set holds (legacy/partial/kind-swapped signatures still rejected; live types never pruned). Full JS unit suite 339 passed / 4 failed: provenance-artifacts + runtime-completeness are dist-less-worktree audits (this lane builds no dist); vk-descriptor-binding-remap + vk-register-citations were broken by the change (source pin, citation drift) and fixed in the same commit, re-verified green standalone. Measured impact: in the captured Godot run nothing user-visible was blocked (modules accepted at creation, demo presented; no dispatch reached translation of them) — the class is latent: the first dispatch building a pipeline on one would have dropped its work silently. Record: `docs/reports/fd49-storage-image-formats-20261031.md`. NOTE for the merge lane: FD-49's open entry lives in this register on `lane-fd16`'s branch; this row is the fixing half — compose A->C at merge |
| `lane-fd50` | FD-50: the native bridge harness aborted at the fn-921 pipeline-cache assertions — the test called the trapping wrapper with a host pointer, so the bridge wrote its answer through a host stack address as a guest address and the fixture's bounds-checked memory model killed the process, silently skipping every later section (including the FD-16 SPIR-V audit guards). The call now follows the fixture's own convention for trapping wrappers: the size slot is guest memory, the result is read back from guest memory, and the trap target is pinned | Pristine base: SIGABRT at the fn-921 call (gdb backtrace to the bridge's cache-data write, vk64bridge.cpp fn-921 case); fixed: `run-native.py` runs to completion, 52 PASS lines, exit 0; sabotage (the bridge's fn-921 write withheld) aborts at the new read-back assertion; sanitizer flavour: all 52 sections execute and pass, the only finding is the readback-leak report registered as FD-66. Record: `docs/reports/fd50-native-harness-completion-20261101.md` |
| `lane-fd57` | FD-62 audit hardening (the defect itself stays OPEN in Section A — this row is the classification half awaiting merge): both descriptor-array audit mirrors now audit through OpImageTexelPointer and refuse texel-pointer forms through an array element with a named reason naming the true blocker (the translation stack rejects OpImageTexelPointer), instead of an incidental generic pointer escape; zero modules change verdict | JS suite `spirvfix-texel-pointer-arrays.test.mjs` 5/5 (red 5/5 pre-fix) over the captured Godot module + reduction/copy-sabotage/attribution/escape fixtures; native classifier guard proven via a scratch harness (the standard harness is blocked by FD-50's abort, re-confirmed by this lane); full JS unit suite green except the dist-state audits of an unbuilt lane tree; vulkan citation register re-pinned through the exact diff line map (citations suite green, incl. the pre-existing reg:410 dual-cite drift). Record: `docs/reports/fd62-texel-pointer-atomics-20261101.md` |

Merge-composition notes that belong with this table: the
kernelgate and kernel-subsystems branches treat one system call
(signalfd4) two ways — the queue's recorded decision is the
kernel-subsystems treatment (the graceful answer unconditional);
the filesystem branch contains one cherry-picked pathfinder
commit that will duplicate when the pathfinder lands first;
`lane-cleanweb` is based on the pathfinder's tip and merges
after it; `lane-firstframe` is based on a tip from before the
dead-start fix and needs composition care.

---

## C. Closed — fixed (or disproven) with proof

- **CL-01 — The dead-start defect that killed program starts
  outright.** A change to guest page permissions turned three
  ordinary writes during early startup into fatal faults with
  no recovery machinery behind them, so startup died silently.
  Fixed in commit `692875bd8`, deployed live, and verified:
  across all 22 measured live starts after the deploy, zero
  guest memory-fault lines in any log. Proof:
  `docs/reports/deploy-20261030-deadstart.md` (integration
  tree). What remains of start reliability is FD-01/FD-02.
- **CL-02 — Two directory-notification defects that stalled a
  real browser demo.** Notification records for unnamed events
  carried a wrong length that corrupted the record stream, and
  pre-existing files were fabricated as newly created. Both
  fixed in commit `9617317fe` (in the integration tree) and
  verified 3/3 through the real website. Proof: integration
  tree `test-results/site-chromium-20261030/REPORT.md`. The
  uncharacterized remainder of that feature is registered
  separately as FD-38.
- **CL-03 — NOT-A-DEFECT: the "DirectX 12 test oracle is
  broken box-wide" verdict.** The oracle was never broken:
  unstaged trees fail an invisible staging precondition and
  guests die in the spawn path, which is what the first-frame
  lane observed. The tooling lane added loud preflights naming
  the missing pieces and, once staged, ran the 64-bit oracle
  end-to-end green (verdict passed, result 0). Proof:
  `docs/reports/tooling-honesty-20261030.md`
  (branch `lane-tooling-honesty`).
- **CL-04 — The standing register's open "Celeste visual
  verification" row.** Closed by live proof on 2026-10-30: the
  demo reaches its animated title screen through the live
  website on the deployed build. Proof:
  `docs/reports/deploy-20261030-celeste.md`. (The
  `docs/known-gaps/gl.md` row itself is stale and should be
  updated the next time that register is touched; the
  remaining Celeste debts are FD-36.)
- **CL-05 — The pathfinder's four deferred audit areas.**
  Multi-line recovery-gated reporting blocks, 32-bit CPU fault
  delivery, kernel-subsystem silent branches, and the
  page-side catch audit were deferred by the pathfinder to
  the wave; each was taken by its wave lane and closed on its
  branch (section B: `lane-cleanwave-kernelgate`,
  `lane-cleanlog32`, `lane-cleanlog-ksubsys`,
  `lane-cleanweb`). Proof: those lanes' records. They close
  fully when the branches merge.


- **CL-06 — First-time visitors could not reliably start a
  program on the first attempt (was FD-01). VOID —
  REOPENED 2026-10-31 (see FD-43); retained as the record
  of what was believed.** Two impatience
  defects in the staged Wine 8.0 desktop initialization: a
  10-second desktop wait whose timeout fabricated a stand-in
  desktop that poisoned the real one, and a check-once graphics
  announcement latch that permanently installed the null driver
  on any miss. Fixed by the win32u backport (wait and retry
  horizons raised to ~300 s) in lane-firstvisit commits
  `67f5fe1fd` + `5306152b6`, merged as `d51fa408e`, deployed in
  build `14a82a302` (2026-10-31). Proof: live re-measurement
  through the public site, fresh browser profile per boot,
  restart budget exhausted — 6/6 first-attempt starts, median
  ready 35.7 s, zero failure signatures; the same measurement
  on the previous live build was 0/4. Record:
  `docs/reports/deploy-20261031.md`. The 32-bit vintage of the
  same pair stays open as FD-02.
  VOID ANNOTATION (2026-10-31): the proof above does not
  stand. The six live boots it cites each carry the
  readiness false positive convicted in FD-43 — an
  NTSTATUS error value (`0xc000000d`) accepted as the
  desktop window handle, no desktop process launched,
  zero X11 connections: the sessions certified as started
  had no desktop. FD-01 is reopened in Section A. This
  entry is retained unrewritten as the record of what was
  believed, and why, on the evidence then available.

- **CL-07 — The OpenGL bridge gave five fabricated or dropped
  answers (was FD-15).** Fixed on branch `lane-fd15`, commit
  `f8bedecdb`. Per case: (a) the four sampler settings the
  bridge claimed to accept (minimum/maximum LOD, depth-compare
  mode and function) were rejected by the WebGPU backend and
  never reached its sampler — the backend now validates,
  stores, and wires them into sampler creation; the rest of
  the desktop-only set has no backend model and stays open as
  FD-39 (a loud refusal would poison the error slot Wine's
  Direct3D layer polls during device creation). (b) Unknown
  GLX query attributes now return the GLX bad-attribute error
  instead of 0; the drawable query, whose real form returns
  nothing, leaves the caller's memory untouched; unknown sync
  queries record the standard invalid-enum error and write
  nothing. (c) Half-float and fixed-point legacy vertex-array
  types gained real decoders; a genuinely unmodelled type now
  refuses the draw instead of rendering fabricated zeroed
  vertices. (d) The 4_4_4_4 and 5_5_5_1 packed pixel formats
  unpack with correct layouts. (e) The texture-coordinate call
  that vanished on the legacy-GL path is recorded on every
  path and re-emitted per vertex by the block replay (through
  the client-active-unit mechanism the WASM GL library can
  actually link — it has no multi-texture entry point of its
  own, which the lane's first full build proved at link time).
  Proof: new tests fail on the old code (23 behavioral checks
  across all five cases) and pass on the fix
  (`web/tests/suite/unit/gl-fd15-honest-answers.test.mjs`,
  `gl-fd15-texparams.test.mjs`); full build "Browser export
  verified"; JS unit suite 340 passed / 0 failed against the
  lane build; site-path regression probe on the same build —
  Celeste boot PASS (the OpenGL program that works on the live
  site still starts and draws). Record:
  `docs/reports/fd15-honest-gl-answers-20261031.md`.

- **CL-08 — The native suite aborted mid-run in the clone
  test group (was FD-41).** Fixed on branch
  `lane-fd41-fd42`, commit `dc5f96fa8`. Mechanism, found
  by reproduction on the pristine base (the registered
  window aborted 3 of 10 runs, rc 134): a guest thread's
  platform thread fetched its first decoded instruction
  before the run loop, in a segment with no exception
  handler, so the segment mapper's cannot-start unwind
  escaped a bare thread entry point into
  `std::terminate` — the trigger was a cloned child's
  first fetch faulting on its shared spin page. Two
  sibling holes: the test-mode completion check
  dereferenced a next-op the run loop's handler had just
  cleared, and the harness invoked test bodies with no
  containment at all. Beyond the abort, full runs died
  three other ways (host segfault in guest-signal
  delivery on freed thread state, a wedge behind a leaked
  thread's mutex, an occasional clean pass) because
  threads leaked by earlier tests keep executing
  underneath later ones in the shared process. Fix:
  the first-op fetch is contained and a thread that
  cannot start retires through normal end-of-thread
  accounting; the harness catches any throw out of a
  test body and records that one test FAILED with its
  diagnostic; serial runs execute each test in a forked
  child (same order, code, and assertions; the
  pre-created context inherited copy-on-write), so a
  crashing child fails only itself — named by its killing
  signal — and leaked threads die with the child's
  address space. `BW64_TEST_NO_FORK=1` keeps the
  in-process path for bisection. Proof: the repro window
  completed 20/20 post-fix with the exact child fault
  firing (and contained) in 9 of the 20 logs; a sabotage
  test throwing out of its body is reported FAILED with
  its diagnostic while the suite continues; three
  consecutive full serial runs each executed all 661
  tests, 0 failed, exit code 0, ~25 KB logs. The
  underlying thread-lifetime defect the isolation exposed
  stays open as FD-44. Record:
  `docs/reports/fd41-fd42-suite-containment-20261031.md`.
- **CL-09 — A suite run could flood gigabytes of crash
  diagnostics (was FD-42).** Fixed on branch
  `lane-fd41-fd42`, commit `dc5f96fa8`. The host
  exception handler's "could not find emulation
  instruction that caused exception" line was an uncapped
  plain log call in both host handlers (armv8 and
  x86-64); any fault-looping thread wrote it without
  limit. It now follows the project's counting-cap
  standard: a `BW64-FAIL` line, the first 64 occurrences
  in full, then every 1024th, always with the cumulative
  count — the first occurrence is always reported in
  full, repetitions are counted and summarized, never
  unbounded, never silently dropped. Proof: the capped
  line observed firing with its count in a real run, and
  the three consecutive full-suite proof runs for CL-08
  produced ~25 KB logs each with no flood; the
  fault-looping leftover that drove the original floods
  no longer exists under test isolation. Record:
  `docs/reports/fd41-fd42-suite-containment-20261031.md`.
- **CL-10 — The FD-01 "improvement" was a readiness false
  positive and the fix shipped in no binary (was
  FD-43).** Closed by its own named conditions being
  met, not by a fix to the race: the hardened readiness
  certification (FD-47 / CL-11) and the four-arm
  measurement are delivered on `lane-readygate` — the
  broken-dispatch control false-ready 10/10 under the
  old certification against 0/10 under the new; true
  first-attempt rates 0/10 stock, 0/10 stamped, 0/10
  pre-rebuild baseline. FD-01 remains open on the race
  itself. Record:
  `docs/reports/readiness-hardening-20261031.md`.
- **CL-11 — The readiness certification accepted an
  NTSTATUS error value as a window handle (was
  FD-47).** Fixed on `lane-readygate`: the launcher
  requires the plausible-handle + five-connection X11 +
  explorer handshake before certifying a session
  started; NTSTATUS-shaped values are refused loudly.
  Unit contract tests red/green; JS suite 340/0;
  sabotage pair in the four-arm measurement (control
  10/10 false-ready under the old rule, 0/10 under the
  new, offline and in-page; genuine starts certify).
  Record: `docs/reports/readiness-hardening-20261031.md`.

- **CL-12 — `accept4`'s close-on-exec flag was silently
  dropped (was FD-46).** Fixed in `98dee4d6f` on
  `lane-fd23-syscalls`: `kaccept` validates the guest
  flags (EINVAL for unknown bits, before any connection
  is consumed) and translates them at the single entry
  point both ABIs share; plain accept (flags 0) is
  unaffected. The entry's own closing condition — the
  branch lands and is re-proven at the merge gate — was
  discharged at the batch gate (2026-10-31): the full
  native gate ran on the merged tree (main flavour
  661/661 serial; x64 flavour 61/61, including the new
  dispatch-level `accept4` test). Record:
  `docs/reports/fd23-syscall-census-20261031.md`; gate:
  `docs/reports/merge-batch3-20261031.md`.
- **CL-13 — A texel buffer reached through a descriptor-heap
  runtime array was rejected at shader creation (was
  FD-16).** Fixed on branch `lane-fd16`, commits
  `9d7b86428` (spirvfix, tier, fixtures) and `e7377591b`
  (bridge audit mirror, native guards). The two serving
  classifiers disagreed with the shape in two layers: the
  translator specialized descriptor arrays only for a
  single index signature and never re-ran its texel
  conversion on the clones, and the bridge audit recognized
  texel servability only on direct variables. Both sides
  now serve the class in step: specialization produces one
  scalar clone per classifiable index signature (the real
  Godot heap uses five over one base; clones past the first
  take synthetic slots in the same set and resolve against
  the guest heap binding), the texel conversion — including
  the bare-image form the real modules use — runs on the
  clones, Coherent decorations that the translator rejects
  on clones are dropped, pre-1.4 entry-point interfaces are
  left untouched, and neither side's dead-type sweep counts
  a constant's value as an id reference any more. Along the
  way the lane also fixed three silent mis-translations in
  the same machinery (array loads traced onto a direct
  variable sharing the type; deferred texel variables split
  as combined samplers; dangling names/copies for removed
  variables). Proof: eight hand-authored fixtures in the
  vkd3d-proton heap idiom pinned on both sides — the JS
  suites (88 pass / 0 fail over the five SPIR-V suites,
  sabotage-verified) and native audit guards over the same
  fixture bytes (52 PASS via the FD-48 scratch method,
  sabotage-verified); all five texel-heap modules captured
  from the real Godot demo translate after the fix; and in
  the product path itself the fixed dist runs the Godot
  demo through the real launcher with zero capability
  rejections and zero pipeline failures where the previous
  build logged both rejections and the
  `Create(Graphics)PipelineState failed 0x80004005` error,
  vkd3d's pipeline cache flushing 100+ compiled pipelines
  (`wt-fd16/test-results/fd16/godot-proof5/`). Record:
  `docs/reports/fd16-texel-heap-arrays-20261031.md`.

---
- **CL-14 — Guest platform threads could outlive the
  thread state they executed on (was FD-44).** Fixed in
  `d52d839ac` on `lane-fd44` (tests `76407729e`,
  `84d430548`, `03f259dc1`): `KThread` lifetime is
  reference-counted — the object is destroyed when the
  last reference drains, after `cleanup()` has disarmed
  the memory-touching teardown — and every cross-thread
  user takes the reference under the table lock. Proof
  on the branch: an AddressSanitizer build fails pre-fix
  with heap-use-after-free in `KThread::signal` on a
  thread freed by `deleteThread`, and is clean post-fix
  (x3); native 662/662 serial, x64-test 59/59. Re-proven
  at the final-batch merge gate on the composed tree:
  native main 662/662 full serial (246 s), x64 flavour
  62/62; the JS source-model test for process-memory
  release across concurrent exits was updated at
  composition to model the reference surface and now
  also pins thread destructions at exactly one per
  thread (JS suite 344/344). Records:
  `docs/reports/fd44-thread-lifetime-20261031.md`,
  `docs/reports/merge-finalbatch-20261031.md`.

- **CL-15 — No 32-bit session could ever be certified
  started (was FD-48).** Fixed in `509c26752` on
  `lane-fd48-restart-cert`: both 32-bit open paths emit
  the process-spawn signal (`OPENED '<path>' -> fd <n>`)
  that the FD-47 readiness handshake matches — the line
  existed only on the 64-bit path, so the handshake's
  explorer element had no counterpart in any 32-bit
  session stream; no readiness rule was changed. Proof
  on the branch: 18 classified boot32 runs (14
  single-attempt certifications, 2 restart-shape
  certifications — one after the full restart budget —
  2 genuinely dead runs certified zero times, 0 false
  certifications) and the sabotage pair in both
  directions. Re-proven at the final-batch merge gate:
  boot32-vkboot PASS with a certified start (handle
  `0x10052`); the 32-bit D3D12 oracle — the run this
  defect blocked at the batch-3 gate — VERDICT passed,
  RESULT 0; cold-start measurement x86 3/3 certified at
  the 900 s window. Correction carried in the FD-48
  entry: the readiness lane's 'single-session 32-bit
  boots certify' line is not supported by its own
  preserved console. Records:
  `docs/reports/fd48-restart-certification-20261031.md`,
  `docs/reports/merge-finalbatch-20261031.md`.

- **CL-16 — A class of captured Godot shader modules
  failed translation on format-less storage images (was
  FD-49).** Fixed in `df68b0d92` on `lane-fd49`:
  per-type format signature entries for bindings whose
  storage images carry more than one type, a
  specialized-binding split so two clones no longer
  collide at one binding, and pruning of unreferenced
  format-less type declarations; live format-less types
  still fail loudly. Proof: the new suite over the six
  captured modules themselves translates all six
  post-fix (fail-before on the pristine tree recorded:
  5x `StorageImageReadWithoutFormat`, 1x
  `InvalidStorageImageWithoutFormat`); sabotage set
  holds; JS suite 344/344 at the merge gate; the merged
  tree's Godot capture shows the same modules accepted
  with zero translation rejections. Impact, corrected
  at composition: the lane's 'the demo presented'
  clause is NOT supported by the preserved evidence —
  no presented frame of this demo has been verified by
  any harness (that gap is registered as FD-55). The
  supported statement is narrower: in the captured run
  the guest accepted all six modules at creation and no
  dispatch reached their translation, so the class was
  latent. Records:
  `docs/reports/fd49-storage-image-formats-20261031.md`,
  `docs/reports/merge-finalbatch-20261031.md`.

- **CL-17 — First-time visitors could not reliably start
  a program on the first attempt (was FD-01).** Fixed in
  `7cbbb3e2d` on `lane-fd01-so`: the backport pipeline
  now builds the unix half of win32u (`win32u.so`, the
  patched 300 s desktop waits) from the same patched
  wine-8.0 source as the PE half, both staging paths
  ship it, and the completeness checker pins its bytes.
  The earlier closure (CL-06) was void: its measurements
  were readiness false positives (FD-43) and the patch
  then shipped in no binary. Proof: branch measurement
  5/5 certified vs 0/5 pre-fix at the 900 s window under
  the hardened FD-47 rules; merged at the final batch
  (`ca4166040`; gate cold-start measurement x64 3/3 and
  x86 3/3 certified); deployed as build `fe4cfc265` and
  live re-measured through the public route (deploy
  record `06571b2b8`): the previous live build certified
  0/6 (false-ready 6/6 under its own page rule), the
  deployed build certified x64 6/6 (handle `0x10048`,
  median ready 102.9 s, zero restarts) and x86 3/3
  (handle `0x10052`). Closure decision recorded by the
  coordinator, 2026-10-31; this row was written at the
  following merge batch's composition. The §4.68 restart
  stopgap remains in the shipped launcher by separate
  decision. Records:
  `docs/reports/fd01-unix-half-fix-20261031.md`,
  `docs/reports/merge-finalbatch-20261031.md`,
  `docs/reports/deploy-finalbatch-20261031.md`.

- **CL-18 — Directory-notification (inotify) delivery
  disabled: enabling it parked the consumer session (was
  FD-38's open remainder).** Fixed in `1e8151d2a` on
  `lane-fd38park`: the park was an ABBA lock-order
  deadlock between the poll scan (which holds the polling
  thread's poll condition while probing readiness) and
  the inotify notify sweep (which holds the instance wait
  condition while signaling parent poll conditions);
  KInotify::isReadReady taking the instance wait
  condition was the edge that closed the cycle.
  isReadReady is now lock-free over an atomic mirror of
  the queue state, maintained at every queue mutation —
  the unix socket object's established pattern — and
  KProcess::inotify_init1's ENOSYS refusal is removed,
  its implementation live. Proof: the new
  testInotifyPollNotifyStress wedges pre-fix (gdb
  capture: one worker blocked in KInotify::isReadReady,
  the other in BoxedWineCondition::signalAll under
  KInotify::sweep) and completes post-fix; the three pins
  that asserted the refusal now assert the enabled
  contract (EINVAL for invalid flags, working
  descriptors, add_watch through the KProcess path);
  native suites 663/663 + 62/62 full serial, JS 344/344,
  dist verified (completeness 0 failures); the site-rig
  consumer proof — fresh-profile Chromium sessions
  through the real website path with delivery live —
  passes 3/3 window-mapped+frame, against the FD-38
  bisect's park on every enablement form. The semantics
  half of FD-38 was fixed on `lane-fd38` (`0ddb6f31a`,
  carried here as cherry-pick `04b32c1ac`; the merge lane
  drops one copy at composition). Numbering note: CL-17
  (FD-01's closure row) was assigned at the following
  merge batch's composition, per the deploy record.
  Records:
  `docs/reports/fd38-enablement-park-20261031.md`,
  `docs/reports/fd38-inotify-semantics-20261031.md`.

- **CL-19 — The 32-bit staged system files carried the
  same two impatience defects as the 64-bit set, and
  the fix pipeline could not reach them (was FD-02).**
  Fixed in `6f3d440ef` on `lane-fd02` (composed into
  integ as `dc225bd93`): the 32-bit side got its own
  pipeline route — `tools/rootfs32/build-wine10-win32u.sh`
  builds the unix half from the pinned Wine 10.0 source
  with `wine10-win32u-desktop-wait.patch` (the 10 s
  input-idle fuse becomes 300 s; the check-once
  graphics-driver latch becomes 500 ms retries for
  ~300 s, preserving Wine 10's null-GUID fallback), and
  the base build substitutes the committed module
  (sha256 `b2d9d135...`, hash-pinned, fail-closed) into
  all four shipped copies. Deployed 2026-11-01 in build
  `02bffb14a` (deploy record
  `docs/reports/deploy-fd02-fd38-20261101.md`): the
  shipped wine32.zip differs from the previous live
  archive in exactly those four members (1,926 members,
  name set identical). Proof: branch measurement under
  the hardened rules 5/5 certified (median 74 s);
  merged-tree measurement 3/3 certified (median
  58.2 s); live re-measurement through the public
  route after the deploy — x86 5/5 certified, handle
  0x10052 on every boot, certified-ready 25.5–27.0 s
  (median 26.6 s), zero restarts, zero false-ready,
  zero readiness rejections — with the x64 control arm
  3/3 certified on the same build. The stock Wine-10
  pair's defect presence was proven at source level on
  the branch; no stock-vs-fixed rate delta is claimed
  (the isolation arm certified 3/3 — the fallback masks
  part of the latch's failure space at this profile),
  so the closure rests on the fix being present in the
  shipped bytes and the fixed build certifying live,
  the same terms as FD-01's closure (CL-17).

## Appendix — examined and deliberately NOT registered

Recorded so the examination is not lost; each was audited by
its lane and is not a defect, for the stated reason:

- The shared-memory index-space panic: guards against a
  wrap-around loop past 2^31 live segment indexes; practically
  unreachable; a genuine internal-invariant panic.
  (`lane-cleanlog-ksubsys` record.)
- The unbounded blocking file-lock wait: matches the real
  system's semantics (the equivalent real call is likewise
  unbounded); not a silent-failure path. (`lane-cleanfs`
  record.)
- Directory-notification queue-overflow coalescing: matches
  the real system's behavior; overflow is reported via the
  designated event. (`lane-cleanlog-ksubsys` record.)
- The page-fault logger's terminal diagnostic panic: loud by
  design, at the end of an already-unrecoverable path.
  (`lane-cleanmem` record.)
- ~120 frozen historical probe scripts importing a helper by
  an absolute path: frozen evidence scripts, deliberately
  untouched. (`lane-tooling-honesty` record.)
- The graphics bridges' remaining counted default branches,
  other than FD-15's five: audited case by case as
  fail-closed or correct-with-loud-callers (classifiers whose
  "cannot prove" answer makes the audit reject, predicates
  whose false is correct, an honest "UNKNOWN" string).
  (`lane-cleanup-bridges` record.)

- **CL-20 — The native Vulkan bridge test harness
  aborted at the pipeline-cache assertions, so every
  later section never ran (was FD-50).** Fixed in
  `bb8d3bf72` on `lane-fd50` (composed into integ as
  `165a1308f`): the fn-921/922 pipeline-cache test
  calls now pass a guest-memory size slot to the
  trapping wrapper, pin that the call actually trapped
  to the bridge, and read the answer back from guest
  memory, instead of handing the bridge a host pointer
  it wrote through as a guest address. Proof: on a
  tree whose base predates the fix the harness still
  dies inside the pipeline-cache section (re-verified
  2026-11-01 on `wt-fd57` — the fixture's
  bounds-checked memory assertion fires before that
  section's PASS line; the FD-50 and FD-62 records
  carry the gdb backtrace to the bridge's fn-921
  write); on the composed tree the same section
  passes (`PASS: former stubs (device image sizing,
  queue2, granularity, sparse, pipeline cache,
  buffer-view destroy)`) and the harness executes 43
  sections, including FD-16's SPIR-V audit guards,
  before stopping at FD-62's guard — a separate
  defect registered as FD-67 that this abort had
  masked. The closure covers FD-50's defect (the
  fn-921 abort and the silent skipping of later
  sections); end-to-end harness completion is
  FD-67's closing term. Evidence:
  `test-results/merge-fd50-fd55-fd62-20261101/`,
  record `docs/reports/merge-fd50-fd55-fd62-20261101.md`.

- **CL-21 — The shader-capability audit rejected
  servable modules because buffer-content runtime
  arrays survived its specialization sweep (was
  FD-61).** Fixed in `b8508856a` on `lane-fd55`
  (composed into integ as `7c3fc661b`): the sweep's
  verdict counts a surviving runtime array against
  the RuntimeDescriptorArray capability only when it
  is a descriptor array type (the direct pointee of a
  UniformConstant pointer); buffer-content arrays are
  served as ordinary buffers. Proof: branch evidence
  (the extracted classifier flips both captured Godot
  modules from reject to admit while a genuinely
  unservable module stays rejected; in-vivo demo run
  with capability rejections 3 -> 2 and both modules
  creating) plus the composed tree's gate — the new
  unit suite `spirvfix-r15-buffer-arrays` passes
  inside the full JS unit suite (346 passed, 0
  failed), the vulkan citation audit reports 0 drift
  against the composed bridge, the dist build
  verifies ('Browser export verified', support
  archive `de592964f772`), and packaging completeness
  reports 0 failures. The entry's bounded residue (a
  module declaring the capability with only
  buffer-content arrays still rejects; no captured
  instance) is registered as FD-68 and stays open.
  Evidence: `test-results/fd55/`,
  `test-results/merge-fd50-fd55-fd62-20261101/`.

- **CL-22 — FD-62's native audit guard aborted the
  native harness because its fixtures were compute
  modules, not graphics modules (was FD-67).**
  Fixed in `5e3d1e2c7` on `lane-fd67fix` (built on
  the FD-67 investigation branch `lane-fd67`, whose
  register correction is preserved verbatim):
  the guard's three fixtures (`heapTexelAtomic`,
  `heapTexelAtomicCopy`, `heapTexelAtomicTwo` in
  `tools/vkd3d/tests/vk64_native_test.cpp`) are
  re-authored with a Fragment entry point — one
  word each, entry model 5 -> 4 — matching the
  stage of the captured module they stand for
  (`testdata/spirv/godot-fs-atomic-texel-heap.spv`,
  model 4). No assertion, bridge code, or JS file
  changed. Proof: on the unmodified composed tree
  `run-native.py` dies with SIGABRT at the guard's
  first bridge-level assertion
  (`vk64_native_test.cpp:3481`); on this branch it
  runs to completion end to end — exit 0, 53 PASS
  lines, including the guard's own PASS line (each
  module refused with
  VK_ERROR_FEATURE_NOT_PRESENT, the
  RuntimeDescriptorArray counter advancing exactly
  +1 per module, the two-array per-array attribution
  intact). An instrumented scratch build (since
  reverted) shows each fixture's create-module call
  now reaching the audit's R15 section
  (`skip5302=0`, `arrServable=0`) instead of
  returning through the compute-accept path. The
  bridge requires no change: on the graphics path
  the texel class refuses at creation by name, as
  the FD-67 lane's probe measured. Composed into integ on 2026-11-01 by the FD-67 guard-correction merge — see docs/reports/merge-fd67-20261101.md. Evidence:
  `test-results/fd67fix/` (`run-native-before.log`,
  `run-native-final.log`, `run-native-diag.log`),
  record
  `docs/reports/fd67-guard-correction-20261101.md`.

- **CL-23 — FD-16 native guard fixtures whose
  admission assertions were satisfied through the
  compute-accept return instead of the R15a
  admission they narrate (was FD-69).** Fixed in
  `5e3d1e2c7` (`heapTexelMultisig`) and `8cc080568`
  (`heapTexelStorage`, the same-pass find recorded
  in FD-69's entry) on `lane-fd67fix`: both
  fixtures re-authored with a Fragment entry point,
  one word each; assertions untouched. Proof: an
  instrumented scratch build (since reverted) shows
  both create-module calls previously returning
  through `spirv-compute-entry` with no R15 verdict,
  and after the correction reaching the audit's R15
  section and classifying servable (multisig:
  nw=309, storage: nw=213; both `skip5302=1`,
  `arrServable=1`), admitted with the
  SampledBuffer / RuntimeDescriptorArray rejection
  counters unchanged — inside the completed harness
  run named in CL-22 (exit 0, 53 PASS lines). No
  product code changed. Composed into integ on 2026-11-01 by the FD-67 guard-correction merge — see docs/reports/merge-fd67-20261101.md. Evidence: `test-results/fd67fix/`
  (`run-native-diag.log`,
  `storage-flip-probe.log`, `run-native-final.log`),
  record
  `docs/reports/fd67-guard-correction-20261101.md`.

- **CL-24 — The shipped support archive contained
  two different files under
  `usr/lib/x86_64-linux-gnu/libvulkan.so.1` (was
  FD-53).** Fixed in `1e228a7ed` on `lane-fd53`.
  The second copy was the distro Vulkan loader
  1.3.239 (457,224 B, `91dd2cb10ee4...`) riding the
  amd64 GStreamer closure through the `libvulkan1`
  deb; the intended copy is the project shim
  (185,464 B, `60b89e5afc7c...`) the staging step
  adds deliberately with its digest recorded as
  the `vulkan` provenance — the pack holds no ICD
  manifests a distro loader could use, and
  wine64.zip carries no library at that path at
  all. The staging walk now excludes the loader
  under both its names, `add()` refuses any
  duplicate member name with `SystemExit` instead
  of a zipfile warning, and a new completeness
  check pins member-name uniqueness archive-wide
  plus the four `libvulkan.so.1` members hashing
  to the provenance digests; the runtime-artifact
  manifest was regenerated for the new pack.
  Proof: the new check fails on the previously
  shipped pack (duplicate named) and on a sabotage
  pack carrying the loader's bytes once under the
  soname (hash-pin mismatch); staging with the
  exclusion disabled fails loudly with the
  duplicate-name `SystemExit`. Standalone staging
  and two standard builds produce the
  byte-identical pack `support-c4dbe35986a5...`
  (517,564,189 B, 3,542 members, zero duplicates);
  the second build completes (`Browser export
  verified` / `BUILD COMPLETE`) with the gate at
  0 failures, and a member-level audit against
  the old pack shows the only name removed is
  `libvulkan.so.1.3.239` and the only member whose
  effective bytes changed is the formerly
  duplicated soname, now always the pinned shim.
  Evidence: `test-results/fd53-20261101/`
  (`archive-audit.txt`, `build1-gate-manifest-drift.log`,
  `build2-complete.log`), record
  `docs/reports/fd53-support-archive-libvulkan-20261101.md`.

- **CL-25 — Shader modules performing atomics
  through image texel pointers are served by
  rewriting the texel variable to a storage buffer
  (was FD-62).** Fixed in `24becdb53` on
  `lane-fd62serve`: spirvfix gains R12A
  (`texelPointersToStorageBuffers`) — a Dim=Buffer
  storage image is guest buffer memory, so a
  variable whose every derived value flows only
  into OpImageTexelPointer formations, and whose
  texel pointers reach only loads, stores and
  WGSL-expressible integer atomics, becomes a
  storage buffer of texels at the same
  set/binding, each formation becoming an
  OpAccessChain into it. One cone analysis
  (`analyzeTexelCone`) is shared by the rewrite,
  the JS classifier and both bridge mirrors
  (r15::analyzeArrays; vk64TexelStorageServable's
  R12A coverage), so the audit admits exactly what
  the rewrite converts, in both directions. The
  page tier completes the serving: a storage texel
  view record (kind 6) at a storage-buffer
  declaration binds the view's buffer range itself
  (`texelBufferBind`), registered for readback
  with its binding batch — it previously fell
  through to a zero dummy buffer, which would have
  silently swallowed the shader's writes. Served
  class, in exact terms: 32-bit-component texel
  formats (R32f/R32i/R32ui scalar for atomics;
  2/4-component forms for loads/stores), atomics
  Load/Store/Exchange/CompareExchange/IIncrement/
  IDecrement/IAdd/ISub/SMin/UMin/SMax/UMax/And/Or/
  Xor. Still refusing by name:
  OpAtomicCompareExchangeWeak (the translator
  rejects the opcode itself), sub-word/packed and
  64-bit texel formats, mixed image/buffer use of
  one element, any texel-pointer escape, and texel
  forms through root-descriptor slots (unchanged).
  The same work closes the FD-62 hardening's blind
  spot: VALUE-form formations (through an OpLoad
  of the element) were invisible to the uses
  analysis and classified servable by accident —
  they are now attributed and genuinely served.
  Proof: the captured module's transformed output
  is spirv-val clean, carries both atomics as
  buffer atomics, and its translation stops only
  at FD-72's HelperInvocation blocker; the JS
  suite pins served and refused shapes in both
  directions; the native guards flip to VK_SUCCESS
  with per-array attribution and gain boundary
  guards (value form served; weak/R8ui/R64ui/
  mixed/escape refuse); page-tier tests pin the
  buffer binding and readback arbitration; the
  in-vivo demo-path result is recorded in the lane
  record. Record:
  `docs/reports/fd62-texel-serving-20261101.md`.
  (Composition note: on integration this closure
  follows FD-53's CL-24; this branch's register
  predates that merge, so the composing merge
  places CL-25 after CL-24.)

- **CL-26 — The pinned runtime-assets release
  archive disagreed with the manifest's file pins
  for both Wine archives, so a fresh checkout
  could not bootstrap (was FD-73).** Fixed in
  `cc2e3a220` on `lane-fd73`. The retained archive
  (built 2026-10-30) matched the manifest's archive
  pin but carried the pre-FD-40/FD-01/FD-02 Wine
  builds while the compositions re-pinned the
  files — member census: its wine64.zip differs
  from the pinned zip in exactly five members
  (stock `win32u.so` `5dac5821...` vs the FD-01
  patch `bffd9036...`; `win32u.dll` `643b7623...`
  vs the FD-40 stamp `49b87438...`; the
  pre-restage Vulkan shim `ae96105b...` vs
  `03edc142...` under its three names) and its
  wine32.zip in exactly the four win32u copies
  (`ca5dd9c5...` vs the FD-02 patch
  `b2d9d135...`). The pins are the shipped bytes
  (live and rollback trees carry them
  bit-identically), so the archive was regenerated
  from the pinned files rather than the manifest
  re-pinned to the archive: every staged file was
  verified against its file pin before packing,
  and the new archive
  `browser-runtime-20261101.tar` (512,829,440 B,
  sha256 `23e8c861...`) is retained under
  `/home/ubuntu/backups/winbox-runtime-assets-20261101/`
  (the 20261026 archive untouched).
  `runtime-assets.json` now describes release
  `runtime-assets-20261101` with the 13 file pins
  unchanged; the GitHub upload remains user-gated.
  A new completeness check — `retained
  runtime-assets archive agrees with the file pins
  (FD-73)` — verifies the retained archive end to
  end (its own pin, member set, every member's
  size/SHA256) inside `bootstrap.py --check` and
  the `build.mjs` gate, so a future re-pin that
  leaves the archive stale fails the standard
  verification. Proof: the defect reproduces from
  a clean worktree (`--local` fails on
  `web/runtime/wine64.zip`, nothing installed);
  the new check fails under the previous manifest
  naming that member's drift and passes on the
  regenerated archive; in a fresh worktree with
  none of the 13 files present, `--local` restores
  and verifies all 13 and `--check` passes with
  the completeness checker at 0 failures.
  Evidence: `test-results/fd73-20261101/`
  (`archive-audit-old.txt`, `wine-census-diff.txt`,
  `negative-check-old-manifest.txt`,
  `cleanroom-local.log`, `cleanroom-check.log`),
  record
  `docs/reports/fd73-runtime-assets-archive-20261101.md`.
