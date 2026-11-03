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
  whole emulator. FIXED 2026-11-02 on branch `lane-fd05` —
  see CL-45.** *(Was High)* `KProcess::getNextFileDescriptorHandle`
  panics at the 0x4000-descriptor ceiling. The filed estimate —
  that answering EMFILE needs the allocation contract changed
  across ~50 call sites — did not survive audit: the panicking
  function has exactly one caller, `allocFileDescriptor`, so the
  contract changed at that single point (the scan's exhaustion
  result is now a sentinel the allocator turns into a null
  descriptor), and the 22 production callers of the allocator
  were made safe one by one; two of them (the pidfd paths)
  already carried the null check and were unreachable only
  because the panic fired first. The guest now gets the
  kernel's answers — EMFILE, with EINVAL/EBADF at the two
  boundary cases the host distinguishes — and survives. This
  was the one known remaining guest-reachable emulator-kill
  in the kernel-subsystems area. Evidence:
  `docs/reports/cleanlog-ksubsys-20261030.md`
  (branch `lane-cleanlog-ksubsys`, "deliberately left alone");
  fix record `docs/reports/fd05-fd-table-exhaustion-20261102.md`;
  evidence `test-results/fd05/`.

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
  is unmerged, and a related park is unconvicted. CLOSED
  2026-11-01 (the flaw) — see CL-28; the park is split out as
  FD-77.** *(Was Medium)*
  The flaw half is fixed on branch `lane-fd12` in `e8644f814`:
  the 32-bit futex now carries the lost-wakeup protections the
  64-bit path already had — wakes flag registered slots
  unconditionally under the pending-wake lock, and a wake that
  lands before its waiter registers is recorded and consumed
  by the waiter's first check, the case Wine's
  alert-by-thread-id futex (whose word never changes) could
  never recover from. Proof: the deterministic native test
  ported with it (`72114f9ea`) fails on the unfixed code (the
  deterministic scenario times out at the full 400 ms; the
  race scenario loses 1 of 120 wakes) and passes on the fixed
  code; full native suite 664/664, x64 suite 63/63. The demo
  park that made this flaw the documented suspect was
  reproduced and captured separately; it is not this flaw's
  closing proof and is tracked, with its own mechanism, as
  FD-77. Evidence: `docs/reports/fd12-park-lane-20261101.md`;
  integration tree `test-results/site-chromium-20261030/
  REPORT.md` (the original suspicion).
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

- **FD-76 — The guest-thread dump silently covered nothing
  in 32-bit sessions. CLOSED 2026-11-01 — see CL-29.**
  *(Was Medium. Filed provisionally as FD-74 by the FD-12
  park lane, 2026-11-01; numbered FD-76 at composition on
  2026-11-01 — FD-74 was taken on integ by the FD-72
  lane's SampleMask entry.)* `bw64_dump_guest_threads()` enumerated
  threads up to a bound only ever raised on the 64-bit
  syscall path, so in a 32-bit session every dump reported
  maxTid=0 / dumped=0 while dozens of guest threads were
  running — a successful-looking empty diagnostic, hit live
  when this lane's baseline reproductions of the FD-77 park
  each carried 20 such vacuous dumps. Fixed on `lane-fd12` in
  `80062923f` (the tid note is now shared by both syscall
  dispatchers); 32-bit dumps now enumerate the real threads
  with state, syscall, args and wait condition, which is how
  the FD-77 capture was taken. Evidence:
  `docs/reports/fd12-park-lane-20261101.md`.

- **FD-77 — A fully booted session can still freeze solid
  before its first window and never say why; the freeze
  amplifier is fixed, the stall behind it is not.
  CLOSED 2026-11-01 — see CL-34.** *(Was High)*
  *(Filed provisionally as FD-75 by the FD-12
  park lane, 2026-11-01; numbered FD-77 at composition on
  2026-11-01 — FD-75 was taken on integ by the FD-72
  lane's helper-invocation entry.)* Split from FD-12 —
  the futex flaw that was the documented suspect is
  closed separately (CL-28). Signature, identical across
  every capture: the session boots completely (desktop,
  display connections, the guest browser fetching its
  archive lazily), reaches a late fetch milestone
  (192 MB in most captures, 112 MB in one), and falls
  totally silent at ~76-78 s — no window, no error, no
  BW64-FAIL — and never recovers. A parked-state thread
  capture exists (`test-results/fd12/consumer-hunt-4/`,
  taken with the FD-76-fixed dump): over 90+ seconds
  every one of the 69 guest-thread states is
  byte-identical; threads sit in ordinary waits
  (wineserver-socket reads, per-thread alert futexes,
  one timed poll) except ten threads of the main
  process frozen at guest faulting instructions with
  no wait condition at all. The amplifier is proven in
  code: `FilePage::onDemmand` performed the backing
  read — for a lazily fetched archive a network fetch
  proxied to the browser main thread — while holding
  the per-process memory mutex, so one unsettled fetch
  stranded every thread that faulted or needed that
  mutex. That half is FIXED on `lane-fd12` in
  `6ddd9cdd9` (read outside the critical section,
  re-validated on re-entry; native 664/664 + 63/63
  serial, JS 346/0; consumer runs on that build:
  5 mapped / 1 parked in 6, against 1 mapped / 3
  parked in 4 uninstrumented on the futex-only-fixed
  build). What remains open: a fetch sometimes never
  settles — no stall-watchdog line, no retry, no
  failure logged in any parked run although the fetch
  layer's documented worst case is ~3-4 minutes
  (60 s stall window, retries, 240 s waiter deadline),
  so the guest thread never entered the retry loop:
  it was stuck before or inside the proxied fetch
  call whose main-thread service never arrived. Which
  sub-mechanism of that service path fails
  (proxy-queue servicing on the runtime main thread
  vs the fetch promise vs the watchdog timer) cannot
  be separated from outside the page. Next capture:
  the armed dump configuration (FD-76's fix) against
  a fault-path-fixed build that parks — the dumps
  will show whether the frozen constellation changed
  (one stranded fetcher: the service itself) or
  persisted (a further blocker). Evidence:
  `docs/reports/fd12-park-lane-20261101.md`,
  `test-results/fd12/consumer-hunt-4/`,
  `test-results/fd12/consumer-verify-*/`,
  `test-results/fd12/consumer-posthunt-*/`.
  **Update (FD-77 follow-up lane, 2026-11-01; record
  `docs/reports/fd77-frozen-constellation-20261101.md`;
  evidence `test-results/fd77/`):** the specified capture
  was taken on the shipped FD-12 build through the public
  route (deploy lane, `test-results/deploy-fd12-20261101/
  chromium-consumer-b1/`): the constellation PERSISTED —
  13 main-process threads running with no wait condition,
  byte-identical from the second dump (123.6 s) through
  the fifteenth (903.6 s); it did not collapse to a single
  stranded fetcher. This lane then resolved what those
  threads are, and taught the dump to see the rest. Three
  parked captures (hunt-4, b1, and this lane's
  loader-traced park) freeze the SAME guest threads at
  the SAME guest addresses; with chrome.dll's base
  measured at 0x6EA80000 the frozen instruction pointers
  are basic-block entries in chrome.dll's text (two are
  exact function prologues; none is a wait, a syscall, or
  a spin loop), the main thread's is in user32.dll, one
  is in shell32.dll, and one sits at the Wine syscall
  stub — and in b1 three threads were seen joining the
  frozen set after completing wineserver round-trips,
  i.e. each thread froze at the next block it entered.
  Two corrections to how such captures may be read, both
  proven by this lane's healthy control run (mapped
  41.8 s, input completed): byte-identical "running"
  snapshots are NOT park-specific (the healthy session
  holds 24 threads, 16 of them at the same syscall stub,
  at identical snapshots for 300 s — a normal thread-pool
  wait the dump cannot see into), so register stillness
  convicts nothing. The dump now measures executed work
  instead (this lane's commits): each running thread
  reports `noprogress_ms` from its CPU's executed-op
  counter, each dump prints a per-process
  `min_noprogress_ms`, the x86 line no longer masks EAX
  for running threads, and the lazy-fetch proxy call
  counts its in-flight callers into every dump footer
  (`proxyfetch inflight/oldest`) — the step the fetch
  layer's "settles within 60 s" assumption never
  watched. On the instrumented build the demo parked
  again (`test-results/fd77/run-instr-3/`, 30 dumps) and
  the new instruments closed the two questions they were
  built for: `proxyfetch inflight=0` at every dump of
  the freeze — NO thread was inside the lazy-fetch proxy
  call, and with zero STALL lines ever logged the whole
  fetch layer (proxy, watchdog, waiter deadline) is
  cleared by measurement; and all 9 running threads of
  the main process sat at `noprogress_ms` growing to
  810–870 s — ZERO guest ops executed for the whole
  freeze, so the frozen threads are host-asleep in a
  wait, not spinning in guest code. FD-77 STAYS OPEN on
  the narrowed remainder: the wait sits in the
  demand/fault path AROUND the fetch machinery — the
  per-process memory mutex (a plain recursive mutex;
  held across a remaining slow section, or the frozen
  set piled behind one holder), the RAM-page allocator's
  mutex, or a proxied filesystem call on a non-archive
  backing file. Specified next step, small: a per-thread
  fault-stage marker in the demand path (memory-mutex
  acquire / alloc / backing read / install) printed by
  the dump, plus an owner field on the memory mutex so a
  pile-up names its holder — one parked capture then
  separates the three. Still owed after that: the
  launcher treating "booted, no window, no guest output"
  as a reported failure in the default session instead
  of an endless quiet page (specified in the lane
  record, not built there).

  **Update — CONVICTED AND FIXED (FD-77 next-step lane,
  2026-11-01; record
  `docs/reports/fd77-msync-abba-20261101.md`; evidence
  `test-results/fd77b/`).** The specified instrument
  was built (`3ca9decf4`): a per-thread fault-stage
  marker across the demand path (memory-mutex acquire
  and work, RAM allocation, file-position lock,
  backing read, install) with the faulted page — and
  for backing reads the file, offset and node type —
  printed by the thread dump; an owner record on the
  per-process memory mutex; an owner record on the
  RAM allocator's mutex. The FIRST armed run on the
  instrumented build parked, and the capture convicted
  one mechanism in a single reading
  (`test-results/fd77b/run-stage-1/`): the memory
  mutex stood owned for 635 s by a thread at stage
  mem-work inside `FilePage::onDemmand` — in
  `KProcess::getMappedFile`, waiting for the
  mapped-files mutex — while the thread holding that
  mutex sat at stage mem-lock inside an `msync`
  writeback, faulted on a page in the middle of its
  range, waiting for the memory mutex; the remaining
  running threads piled behind the memory mutex at
  block entries. The RAM allocator was unheld
  (owner 0) and the fetch proxy empty (inflight=0) in
  the same dumps: of the three named candidates, the
  memory mutex is convicted — as an ABBA deadlock,
  not a slow section — and the allocator candidate is
  cleared for this capture. THE MECHANISM:
  `KProcess::msync` held the mapped-files mutex
  across its writeback, and the writeback reads the
  guest range, demand-faulting any page not yet
  resident — memory mutex first, mapped-files mutex
  second — while the demand path takes the pair in
  that same order and waits for the mapped-files
  mutex while holding the memory mutex. Code audit:
  msync was the sole inversion (mmap/mremap take the
  memory mutex before their mapped-files sections;
  the lookups are leaves). FIX (`52ee264c7`): msync
  snapshots the containing mapping under the mutex
  and writes back after releasing it; the lock order
  is documented at the mutex declaration. PROOF:
  deterministic test `testMsyncLockOrder`
  (`5436ead2b`) forms the interleave without timing
  races — the fault-stage marker is its sync point —
  RED on the unfixed code (the lookup stayed blocked
  the full 3 s window; the test then unwinds itself
  and fails with the diagnosis), GREEN with the fix,
  in both suites; full native suites serial main
  666/666 and x64 65/65, 0 FAILED; JS unit 348/348;
  browser build on the fixed tree compiles (wasm
  `646f85cc…`); consumer series on the fixed build:
  6 of 6 runs mapped (45.8–71.4 s), zero parks,
  against 2 parks in 7 comparable pre-fix runs and
  the convicted capture above. The fault-stage
  instrument and owner records stay in the tree as
  permanent dump tooling. Still owed, unchanged: the
  launcher surfacing "booted, no window, no output"
  as a reported failure (specified in the previous
  lane record, not built here). FD-77 CLOSED on this
  branch — see CL-34.
  UPDATE 2026-11-01 (`lane-fd77-stall-surface`): the
  owed surfacing is BUILT. Default (unarmed) sessions
  now run the thread dump on the launcher's own
  cadence (first at 90 s after runtime start, then
  every 60 s) and convict only on the dump's measured
  evidence: the FD-47 desktop handshake complete,
  no window mapped, one process with `running` >= 3
  and `min_noprogress_ms` >= 60,000 in two consecutive
  dumps, and no proxy fetch in flight in either (a
  fetch in flight, a dump that cannot be taken, a
  process that progresses or vanishes each reset the
  count). On conviction the held per-thread dump
  lines are flushed to the session log, one
  `BW64-FAIL component=launcher.session
  reason=booted-no-window-no-progress` line is
  emitted, and the page reports the session as
  failed through the existing error surface with the
  log preserved. Slow-but-alive starts cannot
  convict: any thread of the process executing keeps
  `min_noprogress_ms` small, and mapping a window
  disarms the watchdog. Armed sessions (`?guestdiag`,
  `?bw64diag=threads`) and `?stallwatch=0` behave
  exactly as before; no readiness rule was touched.
  Proof: unit tests drive the shipped watchdog code
  (park convicts; slow-but-alive, fetch-in-flight,
  pre-handshake and thin-process shapes do not),
  red 8/8 on the unpatched launcher; a shaped-session
  browser probe drives the built page end to end
  (report shown with evidence in the log; the alive
  and pre-handshake shapes raise no alarm). Record:
  `docs/reports/fd77-stall-surfacing-20261101.md`;
  evidence `test-results/fd77-stall-surfacing/`.
  FD-77's register debt is now fully discharged: the
  freeze is fixed (CL-34) and its silence is
  surfaced. FD-77 stays CLOSED.

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
  font (serif)"; the MSHTML fixture never reports.
  CLOSED 2026-11-02 — see CL-41.** *(Was Medium —
  embedded-HTML rendering class: any guest
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
  **Closed 2026-11-02 — see CL-41.** The determination
  the entry asked for: the font set was absent AND the
  enumeration machinery itself was missing — the
  64-bit rootfs carried no faces, and the shipped
  64-bit `win32u.so` was built without freetype or
  fontconfig, so GDI enumerated zero families and
  could realize none, whatever files were present.
  Both layers are fixed on `lane-fd65`; the test's
  Gecko stage passes unloosened, and its remaining
  failure is the separate audio-capture stage filed
  as FD-85.

- **FD-85 — The common-dependencies audio stage: the
  guest's OpenAL tone never reaches the browser's
  audio capture.** *(Medium — guest audio output
  class: any guest program whose sound must reach the
  page. Surfaced 2026-11-02 by the FD-65 lane: the
  stage was unreachable while the same test died at
  its Gecko stage.)* On the FD-65-fixed build the
  test's guest stages complete — `MSHTML: RESULT 0`,
  then the OpenAL fixture prints its device, vendor,
  version and renderer lines, `OPENAL: PLAYSTART` and
  `OPENAL: RESULT 0` — but the page-side capture the
  test installed (`__audioCapture`, counting non-zero
  samples through the AudioContext script processor)
  records no non-zero sample within the test's 30 s
  window, deterministically (two runs, the same
  failure at the same wait, ~264 s each; the runner
  failure is the `waitForPageFn` timeout at
  `common-dependencies.test.mjs:60`). The guest side
  of the chain is proven by its own prints; the break
  is downstream — emulator audio output/pump or the
  page capture path — and is not characterized
  further here. Neither the wait nor the assertions
  may be loosened to pass it. *(Numbered FD-85
  against the composed register: the fd79-80
  branch's FD-82…FD-84 precede it in the composed
  numbering; the composing merge verifies by
  audit.)* Evidence: `test-results/fd65/`
  (`commondep-v2-console.log`, `commondep-v3-console.log`,
  `commondep-v2.log`, `commondep-v3.log`).

  **Update 2026-11-02 (lane-fd85): the filed failure is
  TWO defects, not one.** (1) The page capture itself was
  blind — split off as FD-87, fixed and closed there
  (CL-42): since the bwPump bridge moved guest audio onto
  an AudioWorkletNode (`13b33d3e1`), this test's
  ScriptProcessor-only capture watches a path whose only
  output is SDL's bypassed silent node; in a control run a
  real tone played at full scale (peak 0.3662; 261,528
  non-zero samples at a worklet tap) while the shipped
  capture counted 0 non-zero in 266,240 samples. The
  capture now taps the worklet output (the
  `audio-tone.test.mjs` pattern); no wait or assertion
  changed. (2) With a capture proven able to hear, the
  OpenAL tone STILL does not arrive — and the break is
  proven at the emulator's audio INPUT, not its
  output/pump: across the FD-65 lane's two runs and this
  lane's runs, `KDspAudioSdl::writeAudio` never received
  an S16 sample above its standing diagnostic threshold
  from any Wine-path device (the `S16 input first
  non-zero` line fires only for the direct-DSP control),
  and the first converted push is 898 frames at peak
  0.0000, while byte volume flows at real-time rate and
  the guest completes normally. The leg below the break
  is proven good in the same sessions: a Linux ELF
  writing the same 440 Hz tone straight to `/dev/dsp`
  reaches the page at peak 0.3662 (12000/32768). The leg
  was exercised three more ways, all failing at the same
  boundary: OpenAL forced to its winmm backend (guest
  completes, silence at the input); OpenAL forced to
  dsound (the backend's event waits time out —
  `(EE) WaitForSingleObjectEx error: 0x102` — and the
  source never finishes); a raw WASAPI fixture filling
  the `IAudioClient` shared buffer with the same sine
  in-process (silence at the input; consumption stalls
  after 26,737 ring frames and the client hangs on full
  padding — OpenAL is exonerated); a raw WinMM fixture
  (`waveOutWrite` never returns, WHDR_DONE never fires —
  the waveOut face of the same leg). mmdevapi selects
  the alsa driver (wineoss.drv is absent from the staged
  Wine — load error 126; winepulse reports Unavailable);
  OpenAL's WASAPI log is error-free (Stereo Float32
  48000 Hz, buffer 480/1536). **The break point is
  therefore proven between a Windows audio client's
  successful API calls in the guest and the emulator's
  audio input — inside Wine's mmdevapi → winealsa →
  ALSA(OSS-plug) leg or its contract with the emulator's
  OSS device. The open remainder is the sub-split
  (shared-buffer handoff vs driver feeder vs ALSA plug
  conversion); the next bisection needs an ALSA-client
  probe on the guest's Linux side — the dsptone fixture
  bypasses ALSA, which is exactly why it works — or a
  Wine-side content trace of the feeder's periods.**
  The shipped test, with the FD-87 fix in, still fails
  at this entry's wait (now line 90) on this lane's
  build — Gecko green, `OPENAL: RESULT 0`, first push
  peak 0.0000 — which is this entry's standing proof.
  Neither the wait nor the assertions were loosened.
  Fixtures committed for the continuation:
  `tools/guest-fixtures/wasapi-tone.c`,
  `tools/guest-fixtures/winmm-tone.c`. Record:
  `docs/reports/fd85-audio-leg-20261102.md`; evidence:
  `test-results/fd85/`.
  **Update 2026-11-02 (lane-fd85b — the continuation this
  entry named): the sub-split is measured, and the break is
  convicted to the 64-bit Wine payload's mmdevapi → winealsa
  render path — above libasound, below the Windows client.**
  Method: a native ALSA-client fixture
  (`tools/guest-fixtures/alsatone.c`) playing the control
  tone through the same libasound → plug → oss → /dev/dsp
  chain winealsa uses, and a libasound file-plugin dump
  (`tools/guest-fixtures/alsadump-alsa.conf`,
  `alsadump-wine.conf`, `alsadump-null.conf`, `dumpstat.c`)
  capturing the exact bytes a client hands to libasound.
  Findings, each re-read from preserved logs
  (`test-results/fd85b/`):
  (1) The ALSA leg is GOOD for writei clients: alsatone's
  S16_LE and Float32 writei modes both reached the
  emulator's audio input at full scale (`bwPump: S16 input
  first non-zero peak=12000`; page capture peak 0.4550 in
  both modes) — the plug format conversion and the OSS
  contract serve real data.
  (2) What winealsa hands libasound is an unbroken stream
  of zeros: the dump of the OpenAL run over the stock
  chain is 72,192 Float32 stereo frames, every sample
  0.0000; over libasound's `null` device (no emulator
  device in the path at all) 100,224 frames, every sample
  zero; on a traced rerun 33,792 more frames of zero. The
  same dump mechanism captured the native client's tone
  intact in the same sessions (1,536,000 bytes, peak
  0.3662), so the zeros are the driver's output, not a
  measurement artifact.
  (3) The client side of the driver is healthy by Wine's
  own tracing (WINEDEBUG=+mmdevapi,+alsa): a single
  AudioClient (Initialize → Start), 26 GetBuffer / 24
  ReleaseBuffer cycles of the full 1,536-frame MMDevice
  buffer with flags 0, padding oscillating 0 ↔ 1536, and
  `alsa_write_data` running 118 times with avail 1920.
  The data is lost between ReleaseBuffer and the driver's
  write to libasound, inside the prebuilt driver.
  (4) The A/B that convicts the payload: the identical
  fixture on the 32-bit Wine stack (WineHQ Wine 10 — a
  different build from this project's 64-bit Wine 8
  payload) IS HEARD through the same emulator and chain
  in the same harness: page capture 26,763 non-zero
  samples, peak 0.2269; non-zero S16 at the input;
  `OPENAL: RESULT 0`. The emulator, libasound, the OSS
  device and the capture are therefore all exonerated;
  the defect is specific to the 64-bit Wine build's
  audio data path, and every x64 client face tried
  (OpenAL/WASAPI, OpenAL/winmm, raw WASAPI, raw WinMM)
  fails the same way.
  What remains open, stated exactly: which side of the
  driver's internal GetBuffer→writei copy drops the data
  is not reachable by emulator-side measurement — the
  driver is a prebuilt payload and both sides' entry
  points succeed under tracing. The fix this points to
  is a payload fix (the 64-bit mmdevapi/winealsa pair
  rebuilt or replaced — the 32-bit stack proves a Wine
  10 build works in this harness — and re-pinned by the
  FD-73 procedure), a payload lane of its own;
  deliberately not attempted in this lane because it
  re-pins wine64.zip against the pending batch-2
  composition. FD-85 stays OPEN on those terms; the
  shipped common-dependencies test still fails at this
  entry's audio wait on the 64-bit stack until it lands.
  The chain's mmap face is separately defective — that
  is FD-90, and it is not this entry's cause (finding 2:
  forcing the Wine stream through writei changes
  nothing). Record:
  `docs/reports/fd85b-audio-bisection-20261102.md`;
  evidence: `test-results/fd85b/`.

  **Addendum 2026-11-03 (lane-fd85pay, second attempt):
  the break is located, and it is not in the audio
  chain.** An instrumented self-built mmdevapi/winealsa
  pair (PE-side and unix-side buffer scans at every
  release) plus raw WASAPI fixtures prove the whole
  audio path sound on this stack: polling, event-
  callback and worker-thread clients, including a
  full-ring 1536-frame client, all deliver their
  samples through the shipped driver to the page
  capture. A native ALSA client through the exact
  winealsa chain is likewise heard. The OpenAL run is
  the only silent one, and the instrumented driver
  reads its render buffer as zeros at release on the
  client's own thread. An instrumented OpenAL Soft
  1.25.2 build then shows why: its mixer renders
  zeros because the buffer it uploads from is zeros —
  the upload copy's source span (52,920 bytes, the
  fixture's own sine table, pointer identical on both
  sides of the call) reads as all zeros inside
  `alBufferData`, and the fixture's own readback of
  its table is likewise all zeros. The samples never
  persist in the client's own memory; that loss is
  the emulator defect filed as FD-108, with a pure
  Win32 minimal repro needing no audio at all. Every
  payload layer is thereby exonerated — driver,
  winealsa/ALSA/OSS chain, bwPump, page capture, the
  OpenAL DLL (stock MSVC binary, a config-forced
  scalar run, and a from-source scalar rebuild all
  behave identically) — and no payload rebuild or
  replacement can close FD-85, because no audio
  module can recover samples that were never in the
  client's buffer. FD-85 stays open, gated on FD-108.
  The identical fixture on the 32-bit stack is heard,
  matching FD-108's 64-bit scope. Evidence:
  `test-results/fd85pay/` (diag runs e31-e56, the
  instrumented-driver and instrumented-OpenAL
  records) and
  `docs/reports/fd85pay-audio-conviction-20261103.md`.

- **FD-86 — No Arial family on the 64-bit stack:
  family-name lookups for Arial find nothing.
  FIXED 2026-11-03 — see CL-64.** *(Was
  Low–Medium — Chromium-class text fallback:
  Blink's Windows last-resort list includes Arial,
  not DejaVu Sans. Surfaced 2026-11-02 by the FD-65
  lane's win32-fonts probe on x64.)* The fixture
  prints `GDI=1 FAMILIES=10 TAHOMA=1
  MAPPING=00000000 ARIAL=0 METRICS=0` on the
  FD-65-fixed build: enumeration and metrics work,
  but no family named Arial exists, because the
  64-bit prefix carries no
  `[Software\Wine\Fonts\Replacements]` entry — the
  32-bit stack serves Arial through exactly that
  mechanism (`tools/browser/stage-wine32-fonts.py`
  writes `"Arial"="DejaVu Sans"` into prefix32's
  user.reg at dist export, with checker coverage).
  The mirror fix is mechanical: a prefix64 staging
  step on the same pattern, completeness coverage
  for it, and the shipped win32-fonts suite test
  extended to x64 as the standing guard (today it
  runs x86 only, which is why the gap was
  invisible). Gecko does not need Arial — FD-65
  closed without this — so it is filed, not fixed,
  here. *(Numbered FD-86 against the composed
  register, as FD-85.)* Evidence:
  `test-results/fd65/fonts-x64-v2-console.log`
  (fixed build), `test-results/fd65/fd65-fontstepsx86-console.log`
  (the 32-bit enumeration, Arial present).
  **Closed 2026-11-03 — see CL-64.** The mirror fix
  the entry specified, with one strengthening: the
  replacement is baked into the canonical
  `prefix64.zip` by the new staging step
  `tools/rootfs64/stage-font-alias64.py` (the 32-bit
  edit itself, imported, so the stacks cannot drift),
  not added as an export-time step, so the re-pinned
  retained archive (`runtime-assets-20261103`)
  reproduces it from a clean bootstrap. The checker
  carries the matching drift check, and the shipped
  win32-fonts test — extended to x64, assertion
  unchanged — passes on both architectures: x64 reads
  `ARIAL=1 METRICS=1 RESULT 0`. The Gecko stage is
  unregressed (shipped test, load-gated third run:
  `MSHTML: RESULT 0`, zero abort lines; two earlier
  runs contention-bound at the stage's 180 s window
  — see the record).

- **FD-87 — The common-dependencies audio capture
  watched a path guest audio no longer uses.**
  **CLOSED 2026-11-02 — see CL-42.** *(Medium —
  test infrastructure: the suite's only end-to-end
  OpenAL check could never pass, whatever the guest
  played. Found 2026-11-02 by the FD-85 lane.)* The
  test counted non-zero samples only in
  ScriptProcessorNode output buffers; since
  `13b33d3e1` the bwPump bridge carries guest audio on
  an AudioWorkletNode straight to the destination, so
  the capture's only source was SDL's bypassed node,
  silent by construction. Measured: a direct-DSP tone
  audible at full scale (peak 0.3662) in the same
  session scored 0 non-zero in 266,240 captured
  samples. Fixed in `65cabcd67` (the capture taps the
  worklet output, waits and assertions unchanged);
  full account in CL-42. What the test still fails on
  after this fix is FD-85's remaining defect, a
  different layer.
- **FD-108 — 64-bit guest writes interleaved with
  repeated cross-module calls are lost to later
  reads: the memory reads back as zeros although
  immediate in-loop readbacks verify every write.**
  *(High — silent data loss in the 64-bit memory
  serving; any guest program that fills a buffer
  across repeated library calls can silently read
  back zeros. Filed 2026-11-03 by lane-fd85pay as
  the convicted blocker behind FD-85. The repro is
  a pure Win32 console program: no audio, no Wine
  audio modules, nothing downstream of the writes
  themselves.)* Minimal repro (sources committed as
  `tools/guest-fixtures/fpprobe6.c` and
  `fpprobe7.c`; built binaries and run records in
  `test-results/fd85pay/fixtures/` and
  `test-results/suite-fd85pay-diag25/`,
  `test-results/suite-fd85pay-diag26/`): a static
  double array is filled `db[i] = i*0.01` in a
  call-free loop and verified; a second loop
  computes `s = sin(db[i])` (msvcrt), stores `s`
  into a second static array, and reads it back
  immediately — the readback matches `s` for all
  1000 iterations, mismatch count 0, for both a
  direct store and a memcpy store — yet after the
  loop every stored value reads as 0.0, and a
  `.data` accumulator summed over the same calls
  reads back 0.0000 against a true sum of
  184.1776. Controls in the same fixtures:
  individual `sin` calls with memory arguments are
  correct (including sin(100.0) = -0.506366, so
  range reduction works); integer and double
  stores in call-free loops persist and read back
  correctly after the loop; an accumulator across
  calls to a local noinline function, and across
  `fabs`, is exact; per-iteration `sin` values
  passed straight to printf are correct; constants
  from the pool, `.rdata` and `.data` all load
  exactly. The loss needs the combination —
  repeated cross-module calls interleaved with the
  writes — and is shape-sensitive rather than a
  clean page discard: with a printf inside the
  loop the accumulator survives partially (71.8690
  of 184.1776). The 32-bit stack does not show it
  (the identical OpenAL fixture's tone is heard
  there). This one mechanism explains FD-85 end to
  end. Closing terms: the 64-bit memory serving
  fixed such that the repro's post-loop readbacks
  return the stored values, FD-85's audio stage
  then passes unloosened, and the native and JS
  suites stay green. *(Numbered FD-108 on
  lane-fd85pay against this branch's register,
  whose highest entry was FD-86; the number is
  claimed on sibling branches for unrelated items,
  so the composing merge renumbers by audit.)*

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
  done.** *(Medium)* **CLOSED 2026-11-02 — the residue left
  open below is closed on `lane-fd23res`; see CL-65
  (provisional number, pending the merge audit).** Counted by census: on the 64-bit side,
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
  **Residue closed 2026-11-02 (lane `lane-fd23res`;
  record `docs/reports/fd23-residue-20261102.md`; closure
  CL-65).** The three named routes are implemented to
  host-measured contracts: `select` (23) via the core
  factored out of `pselect6` — with the descriptor and
  readability pre-checks both calls were missing (which
  also corrects `pselect6`), timeval validation, and the
  remaining-time write-back the host performs; `pidfd_getfd`
  (438) in the host's check order; `futex_waitv` (449) on
  both ABIs over the futex machinery (one slot per entry on
  a shared condition, registration under the pending-wake
  lock, the woken index as the answer), with the i386 table
  extended to 462 slots so the number dispatches at all.
  The per-number disposition of the long tail is done:
  `tools/conformance/syscall_dispositions.json` carries a
  category, a reason and a route for every unimplemented
  number on both ABIs (404 entries), and the census tool
  audits it — undispositioned 0, stale 0. Census on the
  fixed tree: x86-64 implemented 202, stubbed {334 `rseq`}
  only, missing 160; i386 dispatched 206, gap 244.
  `rseq` itself is unchanged — the loud refusal stands —
  and its prerequisite is filed as FD-109. Two findings
  from the lane are filed as FD-110.
- **FD-109 — `rseq` cannot be served honestly until the
  64-bit signal-delivery path honours the abort
  contract.** *(Medium)* FD-23's entry records why `rseq`
  (334) stays a loud `-ENOSYS`: a registration without
  abort-on-signal handling would silently corrupt per-cpu
  data on signal delivery. This entry names the missing
  machinery so the refusal has a closing condition, not
  just a rationale. What is absent: when a signal is
  delivered to a 64-bit thread that has a registered rseq
  area, nothing inspects whether the interrupted program
  counter sits inside the registered critical section,
  and nothing redirects execution to the section's abort
  handler with the abort signature in place; the same
  inspection is owed on preemption and on any
  thread-migration point the emulator introduces. A guest
  (glibc registers rseq unconditionally) that is resumed
  inside an aborted critical section would read and write
  per-cpu state under a stale cpu id with no fault and no
  log — exactly the silent-corruption shape the refusal
  exists to prevent. Closing terms: the 64-bit signal
  path (frame setup in the syscall/signal layer) checks
  the registered rseq area's critical-section range on
  delivery, and on a hit redirects to the abort IP with
  the kernel's abort signature and clears the section;
  proven by a native test that registers rseq, enters a
  critical section in guest code, delivers a signal, and
  observes the abort handler running with the signature
  — failing on the current code because delivery ignores
  the area entirely. Only then may 334 be reconsidered,
  as its own lane with its own host-measured contract.
- **FD-110 — The 64-bit `pselect6` ignores its signal-mask
  argument entirely, and never validates its timespec.**
  *(Medium)* Found on the FD-23 residue lane
  (2026-11-02), which factored `pselect6`'s body into the
  core `select` now shares. The dispatch case reads five
  arguments; the sixth (the `{sigset_t*, size_t}` pair)
  is never touched: the mask is not applied for the
  duration of the wait, not restored after it, and its
  pointer and size are never validated — a guest that
  blocks a signal around the wait (the standard
  race-free pattern pselect exists for) runs the whole
  wait with the signal unblocked, so the very race the
  call exists to prevent can fire. The timespec's
  nanosecond field is likewise never range-checked
  (values >= 1e9 are folded into the wait instead of
  answering `-EINVAL` as the host does). Closing terms:
  apply the guest mask for the wait and restore the
  previous mask on every exit, in the signal layer's own
  terms; validate the mask pointer and size with the
  host's answers (`-EFAULT` / `-EINVAL` as the host
  does); range-check the timespec; prove with a native
  test that delivers the blocked signal during the wait
  and observes it pending until return, and that a bad
  mask size and an out-of-range timespec answer as the
  host's do — failing on the current code because the
  argument is never read.
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
  mappings. CLOSED 2026-11-01 — see CL-32.** *(Low. Filed provisionally as FD-56 on the
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
  anything. CLOSED 2026-11-01 — see CL-30.** *(Medium. Filed provisionally as FD-55 on
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
  a hard link. CLOSED 2026-11-01 — see CL-31.** *(Low. Filed provisionally as FD-57 on
  the FD-38 lane branch; renumbered at this composition.)* `KProcess::link` creates a new node holding a copy
  of the content (its own debug line says "Hard link not
  implemented"), so inode identity and link counts observably
  differ from Linux, and inotify sees a creation for the new
  name but no link-count IN_ATTRIB on the source. Found by
  the FD-38 lane, 2026-10-31 (kprocess.cpp `KProcess::link`).
  Update 2026-11-01 (FD-81): the refusal this entry's
  fix specified also fired, in the browser build, on
  ordinary files whose only problem is that the backing
  store cannot hard-link at all - 57 times per 32-bit
  boot on fontconfig's cache temp-to-lock links. FD-81
  serves that case at the emulator layer with true
  shared content; the refusal stands for the sources it
  was written for. FD-60 stays closed by CL-31.

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

  Update 2026-11-02 (FD-64 lane, branch `lane-fd64`,
  fix `9d132afc7`): CLOSED on branch — see CL-44
  (provisional). The corpus subtest had already been
  resolved by the FD-62 lane's measured corpus
  re-baseline (`ea7283041`) and kept green by the
  FD-70 composition's fixture enrollment
  (`bf2ce6bbf`); the four DESC_SET subtests are
  resolved here. Determination: the fixture was the
  stale side. The current contract — `u8 setIndex,
  u8 pad, u16 nBind, u64 layoutId`, one entry per
  written (binding, arrayElement) with count 1 — is
  stated in docs/vulkan-protocol.md §0x08, written
  by the bridge's emitDescSet, read by
  web/vk-protocol.mjs, and pinned by the suite's
  vk-desc-set-wire.test.mjs; the legacy fixture
  still wrote the superseded u8-count header and a
  single array entry with count 3, so the reader
  rightly reported the 132 binding bytes as
  trailing. The correction is test-file only
  (fixture header + per-element array entries +
  the decode assertions updated to the same
  contract; a second masked staleness, the decoded
  sampler object's shape, likewise updated to the
  current decoder contract — the ADDR mapping plus
  the raw addrMode* fields from `26ffedefa`).
  Proof: legacy group 115/0 (was 110/4; the
  standing failing set is empty), suite-unit group
  348/0 on the built tree (its provenance test
  requires one; browser build in-lane, wasm
  d631d254, byte-identical to the shipped
  build's), sabotage re-run returns the four
  failures. (The lane's initial message quoted
  first-run figures from a garbled delivery
  summary; the record's Correction section gives
  the log-verified account.) Record:
  `docs/reports/fd64-legacy-descset-20261102.md`;
  evidence `test-results/fd64/`.

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
  certification can never complete.
  CLOSED 2026-11-01 — see CL-35.** *(Low — behind a
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

  **Resolution (2026-11-01, `lane-fd54`; record
  `docs/reports/fd54-dlltrace-spawn-signal-20261101.md`):**
  the mechanism is exactly the one filed, verified at both
  ends: the emitter gated the line on
  `!getenv("BW64_DLLTRACE")`, and the observer
  (`observeSessionOutput` in the launcher) matches
  `OPENED '<path>explorer.exe'`, which the DLLTRACE line's
  `OPEN  '<path>'` dialect never satisfies — it was the
  only `BW64_DLLTRACE` suppression of a default line in
  the tree. Fixed in `e3660ab57`: the signal is emitted
  unconditionally on a successful .exe open, exactly like
  the 32-bit paths; the DLLTRACE line is unchanged, so a
  traced session now logs both. Proof: `bc2cfd046` adds
  `testExeSpawnSignal64`, which drives the real dispatcher
  (openat64 on a fixture .exe, klog captured) — RED on
  the unfixed code ("spawn signal suppressed under
  BW64_DLLTRACE=1"), GREEN after; it also pins the
  control (signal present untraced), the trace line's
  presence (the flag still works), and the failure
  discipline (a failed .exe open emits no signal in
  either configuration). Suites on the fixed tree,
  serial: x64 67/67 (66 + the new test), main 667/667,
  0 FAILED; run-native 56 PASS; the browser build
  compiles with the fix in shared code ("Browser export
  verified", wasm `35302eba…`, support archive
  `c4dbe35986a5…` unchanged).
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

  FD-72 lane update, 2026-11-01: FD-72's boundary
  is proven (its entry now states the exact terms:
  no WGSL helper-invocation query exists, helper
  subgroup participation is implementation-defined
  there, and upstream naga's IR has no variant for
  the builtin — closing waits on the translation
  stack, not on a local fix). This demo's FD-72
  module no longer reaches pipeline translation:
  the bridge audit refuses it at module creation
  by name (FD-75), so its in-vivo failure shape
  changes from dropped draws to a named creation
  refusal the guest can see. FD-74 (SampleMask)
  waits behind FD-72 for the same module. This
  entry stays open on FD-70 + FD-72 and the
  downstream submit/swapchain behaviour.

  FD-70 lane update, 2026-11-01: FD-70's pair is
  proven genuinely demanded on the current build
  (its absence from the FD-62/FD-75 runs was their
  1,500 s deadlines, not renderer state); two of
  its refusal layers were a sweep literal-reference
  defect, fixed on lane-fd70 with guards in both
  mirrors (the pair's RuntimeDescriptorArray
  refusals: 3 -> 0 in vivo). FD-70 stays open on
  its restated term (used-form physical addressing
  with data-dependent integer offsets -- see its
  entry). This entry stays open on FD-70 + FD-72
  and the downstream submit/swapchain behaviour.
  Record: `docs/reports/fd70-second-wave-20261101.md`.

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
  metrics snapshot for the rest of the run.
  CLOSED 2026-11-01 — see CL-36.** *(Was Low —
  developer observability.)* *(Numbered FD-58 on
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
  **Update — CONVICTED AND FIXED (FD-63 lane,
  2026-11-01; record
  `docs/reports/fd63-metrics-emission-20261101.md`).**
  The three emitted windows in the filed evidence
  are all SDL-present-driven (`present.path` "sdl",
  sdl=1/2/1, gl=0, audio=0): presents were the only
  activity the emission gate recognised in them.
  The gate (`bw64_metrics_maybe_emit`) counted
  presents, Vulkan frame completions, GL calls and
  audio calls as activity — but not the syscall
  stage, which every 64-bit syscall dispatch feeds.
  Once the session stopped presenting, every later
  window held only syscall activity and fell into
  the gate's no-activity branch, which discarded
  the accumulated counters unreported and restarted
  the window clock — every interval, for the rest
  of the run. The panel froze at the last
  present-driven snapshot (syscall calls=239,
  ms=46108.4) while the session executed on. Fix
  `6d711e6a6`: the gate now counts syscall dispatch
  as activity. Proof: a new section of the native
  metrics selftest drives successive syscall-only
  intervals against the real registry — RED on the
  unfixed code (exactly its two assertions fail),
  GREEN with the fix; the browser build compiles
  on the fixed tree (wasm `1809b7d7…`). Stated
  boundary, unchanged by design: Vulkan trap
  traffic that never completes a frame still feeds
  no counter — pre-existing, and not this entry's
  evidence, whose session dispatched syscalls
  throughout.

- **FD-66 — The native bridge harness's sanitizer flavour
  cannot finish clean: readback blocks the fixture leaves
  pending are reported as leaks, and the bridge drops
  pending readbacks without freeing them. CLOSED
  2026-11-01 — see CL-40.** *(Low —
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
  **Update (2026-11-01, `lane-fd66`): CLOSED — see
  CL-40.** A census of every mention of the two pending
  vectors confirms the filed reachability exactly: the
  only exits for a pending entry are `completeReadbacks`
  (which frees every block) and `setupReadbacks`' own
  prologue, and all three production submit paths pair
  the calls — so no production leak is proven, and the
  discard was reachable with a non-empty plan only from
  the harness's planning sections. The latent half is
  removed by construction: a new `freePendingReadbacks()`
  frees every pending block before clearing, and setup
  replans through it. Frees go through a new `heapRaw`
  field holding the allocation's true host pointer —
  `heapPtr`/`flagPtr` are page-tier handles narrowed to
  32 bits by the bridge/page ABI (exact in the browser
  build), and the first version of the fix, which freed
  through the handle, died in the sanitizer harness with
  a wild free under the host's 64-bit allocator. The
  harness's three planning sections dispose through the
  same helper (two ended in raw clears of the vectors;
  the third abandoned its block), a new section plans
  twice without completing and pins the counts, and the
  runner's sanitizer flavour now gets a 180 s run budget
  (plain stays 30 s) so it reports a verdict instead of
  a timeout. Proof: the sanitizer flavour on the base
  reproduced the filed measurement exactly (1,132 bytes
  in 5 allocations from `setupReadbacks`, exit 1); on
  the fixed tree it completes at 57 PASS, exit 0, zero
  leak reports, through the standard runner as well;
  sabotage (prologue alone reverted) reopens exactly
  the new section's first plan — 328 bytes in 2
  allocations. Native serial 667/667 + 66/66, JS 348/0,
  citations 352 rows 0 drift after re-pinning the
  citations the fix's insertions shifted, browser build
  BUILD COMPLETE (wasm `df27c5e0…`). Record:
  `docs/reports/fd66-readback-disposal-20261101.md`.
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
  the number was free on integ.)* CLOSED 2026-11-01 —
  see CL-37 (provisional, filed on branch `lane-fd70int`). In the
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

  **Update 2026-11-01 (FD-70 lane, branch
  `lane-fd70`): the pair is genuinely demanded; two
  of its three refusal layers were a sweep defect,
  now fixed; the entry stays open on the restated
  physical-addressing term.** The artifact
  hypothesis is disproven in vivo: on the current
  integration build (38085a142, wasm 04e73f59 -- the
  live bytes), with FD-62 served and FD-75's
  creation refusal active, a standard demo-path run
  submitted both modules and saw them refused --
  the 23,440-byte module at 2,556.7 s, the
  16,304-byte module at 2,557.6 s and 2,558.5 s,
  five Create(Graphics)PipelineState 0x80004005 in
  total (two for FD-72's module, three for this
  pair), diag2's exact shape (evidence
  `test-results/fd70/cap-long1/`). The pair's
  absence from the FD-62/FD-75 runs is explained by
  those runs' 1,500 s deadlines ending before the
  engine's scene-shader wave arrives, not by
  renderer state. Fixtures:
  `testdata/spirv/godot-vs-4000ddfd359efb05.spv`
  (23,440 B, djb2 28a2394720432fda) and
  `testdata/spirv/godot-vs-ebac7e2d70a20d62.spv`
  (16,304 B, djb2 3eccbc1e60e9f7a4), harvested from
  the run's vkd3d dumps and re-verified against the
  refusal log's size+hash.

  The filed decomposition was wrong about which
  array gates the capability. The descriptor image
  array (set 2 / binding 0) IS R15a-servable in
  both modules (one load signature each) and
  spirvfix specializes it. The
  RuntimeDescriptorArray co-refusal came from the
  descriptor-array orphan sweep's liveness scan, in
  both mirrors: the scan counts every non-constant
  operand word as an id reference, and each module
  carries exactly one root-pointer OpLoad whose
  MemoryAccess group holds the Aligned literal 16
  -- numerically the descriptor array type's id
  (%16). The literal kept the specialized-away
  type, and through it its pointer type, alive
  forever, so the sweep never collected it:
  spirvfix kept capability 5302 over the module,
  and the bridge audit's skip5302 simulation
  refused the module for it. The same false
  survival gated the SampledBuffer layer: the
  post-specialization texel pass saw the image type
  as still array-owned and declined its clone, so
  the tier kept SampledBuffer and the translator
  rejected that capability first. Proven by a
  keeper census run under the sweeps' exact rules:
  the only words keeping %16 alive are its pointer
  type's definition and the load's Aligned word;
  three control modules show no such keeper.

  Fix (this branch, both mirrors): the sweeps'
  liveness scans now bound the MemoryAccess operand
  group (OpLoad / OpStore / OpCopyMemory /
  OpCopyMemorySized -- group contents name
  constants, never the types the sweep deletes) and
  skip the literal-only OpTypeInt / OpTypeFloat
  words. Proof: JS unit `spirvfix-fd70.test.mjs`
  (7/7; 5/7 red with the JS fix reverted), native
  guard in `vk64_native_test.cpp` (admits the
  colliding and twin fixtures, keeps the unservable
  negative's named refusal; the harness aborts at
  the guard with the bridge fix reverted); full JS
  unit suite 348/348 entries (347 baseline + this
  lane's test file), with the 51 citations in
  `docs/known-gaps/vulkan.md` that the bridge
  edit's +17 lines drifted re-pinned on this
  branch (citation audit back to its baseline
  profile: 352 rows, 232 MATCH, 0 DRIFT);
  run-native 56 PASS; corpus
  byte-check over all 145 pre-existing
  `testdata/spirv` fixtures -- only this pair's
  spirvfix output changed. In vivo on the fixed
  build (wasm 16f43514; evidence
  `test-results/fd70/cap-long2/`): the pair was
  refused again at 2,088.3 s / 2,089.3 s /
  2,090.4 s with `spirv-capability-unknown:5347` as
  the ONLY reason -- RuntimeDescriptorArray refusal
  count 3 -> 0; FD-72's module refused twice by
  name as before; five E_FAILs, unchanged.

  What remains, stated exactly: capability 5347 in
  its used form, over root-descriptor loads whose
  addresses are computed in integer space --
  push-constant base VA plus a data-dependent
  offset (instance-derived index x element stride +
  field offset, via OpIAddCarry), bitcast to a
  physical pointer for a single load. R15b's root
  cone admits only a zero-addend push-block root
  with the element index in the access chain and
  declines this form by name (`moduleServable`
  false on both fixtures); one of each module's
  address materializations does match the R15b
  form, but the verdict is per-module and the
  majority do not. With the false layers removed,
  spirvfix serves everything else in the modules
  (the served outputs pass spirv-val under
  vulkan1.1), and the vendored translator's
  remaining rejection of the served outputs is the
  capability itself:
  `UnsupportedCapability(PhysicalStorageBufferAddresses)`
  -- the same shape as FD-72's boundary. No local
  rewrite is sound: serving this form means
  re-rooting a data-dependent byte offset as typed
  accesses over a byte view of the resolved root
  buffer, a new serving design across the fixup
  layer, the bridge's binding form, and the
  readback story; a partial version would compute
  on wrong data silently, which is worse than the
  loud refusal. FD-70 therefore STAYS OPEN.
  Closing terms, restated: the integer-offset
  root-addressing serving design (the two fixtures
  above are its specification), proven on the
  fixtures through translation in both mirrors and
  in vivo with the pair's creations succeeding; or
  an upstream translator change that accepts the
  capability, re-vendored. Record:
  `docs/reports/fd70-second-wave-20261101.md`.

  **Update 2026-11-01 (FD-70 integer-offset lane, branch
  `lane-fd70int`): the integer-offset root-addressing
  design is implemented in both mirrors and proven on
  the fixtures and the corpus; the entry CLOSES
  provisionally — see CL-37 — with the in-vivo
  pair-creations check on a shipped build named as
  the deploy-side proof still owed.** The restated
  remaining term — root loads whose addresses are
  computed in integer space — is served by a word
  view (R15c in `tools/spirvfix/spirvfix.mjs`, mirrored
  in `source/vulkan/vk64bridge.cpp`): when any
  materialization of a root slot carries an affine
  integer offset (push-block base VA plus a
  data-dependent byte offset folded into the low half
  through OpIAddCarry, the high half consuming only
  the carry), the slot is re-rooted at the same
  `bw_root_p<off>` name and binding as a storage
  struct wrapping a runtime array of 32-bit words, and
  every load through the slot is rewritten as word
  loads at its total affine byte offset divided by
  four, reassembled by bitcast. The class boundary is
  exact in both mirrors: the offset must classify
  affinely (constants, adds, subtractions without a
  dynamic subtrahend, multiplication by a positive
  constant; any other 32-bit value is an opaque term
  the rewrite recomputes from the same SSA value);
  every use of every materialized pointer must be a
  load of a 32-bit scalar or 2-4-component 32-bit
  vector reached through access chains and copies,
  with every chain index folding into the affine
  offset (struct members by Offset decoration, array
  elements at ArrayStride, vector components at 4
  bytes); every coefficient and the constant
  remainder of every load's total offset must be
  divisible by 4; a store through any cone refuses
  the slot by name. Proof: both specification
  fixtures take `root-descriptor-bda`, pass spirv-val
  and translate through the vendored translator;
  `spirvfix-fd70.test.mjs` 11/11 including the three
  mutation boundaries pinned on the captured module
  (stride 176 -> 174 refuses, field offset 48 -> 46
  refuses, an inserted store refuses naming the
  loads-only word view) and a sabotage pass; corpus
  over all 148 `testdata/spirv` fixtures — 146
  byte-identical, only the pair's output changed;
  native guard in `vk64_native_test.cpp` (the audit
  admits the synthetic integer-offset module end to
  end and keeps the mutations' refusals; run-native
  57 PASS); citations re-pinned (352 rows, 0 DRIFT);
  browser build on the staged branch (wasm
  `1cdc208e`) with the full JS unit suite 348/348.
  The mirrors caught each other once during the lane:
  the bridge's first word-cone struct step used the
  index constant's id where the JS resolves its
  value — the new native guard reddened on it before
  any fixture proof ran, and the fix is in the same
  commit. What remains owed is only the deploy-side
  confirmation the closing terms name — the pair's
  creations succeeding in vivo on a shipped build —
  which belongs to the deploy gate that carries this
  branch; this closure is provisional in exactly that
  sense. Record:
  `docs/reports/fd70-intoffset-20261101.md`.

  **Update 2026-11-02 (deploy of the FD-54 + FD-63 +
  FD-70 + FD-79/80 batch, shipped build `9e37e5640`,
  wasm `d631d254`): the deploy-side proof CL-37 owed
  was run in vivo and FAILED on its pipeline clause —
  the pair's module creations succeed and FD-70's
  refusal is extinct in vivo, but the pair's pipeline
  creations still fail, on sibling modules in the
  same pipelines.** Standard demo path against the
  shipped build (warmed archive, vktrace on, 3,300 s
  deadline; evidence
  `test-results/deploy-fd54-63-70-7980-20261102/godot-proof/console.txt`,
  14,880 lines): the pair was submitted in the second
  wave and CREATED — 23,440 bytes (djb2
  28a2394720432fda) at 2,047.0 s, 16,304 bytes
  (3eccbc1e60e9f7a4) at 2,073.3 s and 2,074.4 s, all
  plain `vkCreateShaderModule` lines; the run
  carries ZERO `spirv-capability-unknown:5347` lines
  and five REFUSED lines total, none of them the
  pair. The three second-wave pipeline creations
  nevertheless failed with
  `Create(Graphics)PipelineState 0x80004005`
  (2,072.4 s, 2,073.6 s, 2,074.6 s; the run's other
  two E_FAILs are FD-72's, at 236.9/237.4 s), each
  killed by another module in the same pipeline: a
  369,400-byte module (djb2 d4da162f9cbdb948)
  refused at 2,072.4 s for
  `spirv-capability-unknown:29`,
  `spirv-capability:RuntimeDescriptorArray` and
  FD-72's `spirv-builtin-helper-invocation`; and a
  2,340-byte module (djb2 fe166085df72b93f) refused
  at 2,073.6 s and 2,074.6 s for
  `spirv-capability:SampledBuffer` and
  `spirv-capability:RuntimeDescriptorArray`. CL-37's
  owed clause — the pair's PIPELINE creations
  succeeding in vivo — is therefore NOT met and
  stays owed, now gated on those sibling classes
  (one of them FD-72's). The sibling modules are
  recorded here and in the deploy record as found
  in vivo; filing them is the register owner's.
  Record:
  `docs/reports/deploy-fd54-63-70-7980-20261102.md`.

  **Update (FD-87 lane, 2026-11-02): the sibling
  classes this update recorded are now filed and
  dispositioned.** The 2,340-byte module's classes
  and the 369,400-byte module's SampledBuffer and
  capability-29-name classes are served under FD-89
  (CLOSED — CL-43); the same module's remaining
  RuntimeDescriptorArray share is the data-dependent
  array filed as FD-88; the helper-invocation share
  was already FD-72's. CL-37's owed pipeline clause
  now gates only on FD-88 and FD-72.

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
  exactly two sites, both feeding subgroup
  collectives whose results steer the atomics: a
  ballot built from
  `laneId == broadcastFirst && !helper`, observed
  only through its popcount (== 0 tests) gating the
  second atomic region; and
  `select(helper, 0, bit)`, subgroup-reduced (OR)
  into the second atomic's value operand. Helper
  lanes must contribute nothing at either site, or
  the atomics fire on wrong bits. It is the only
  corpus module carrying the builtin.

  **STILL OPEN — the boundary is now proven, in
  these exact terms (FD-72 lane, 2026-11-01; full
  evidence in
  `docs/reports/fd72-helper-invocation-boundary-20261101.md`).**
  (1) *The vendored translator rejects the builtin
  at its SPIR-V frontend, on the decoration's mere
  presence.* Measured against the vendored naga
  30.0.1, called exactly as production calls it:
  the captured module (post-FD-62 form) fails with
  `UnsupportedBuiltIn(23)` on every backend
  (wgsl/glsl/hlsl/msl). Minimal modules prove the
  gate is presence, not use: a BuiltIn 23 variable
  declared but never loaded fails identically, as
  do load-and-use forms. The EXT spellings fail
  too: `OpIsHelperInvocationEXT` with its
  capability →
  `UnsupportedCapability(DemoteToHelperInvocation)`;
  without the capability →
  `UnsupportedExtension("SPV_EXT_demote_to_helper_invocation")`.
  Replacing both loads with a constant still fails
  (the declaration alone blocks); only deleting the
  decoration, the variable and its interface entry
  clears the frontend — and the module then stops
  at FD-74 (SampleMask), not at anything
  helper-shaped.
  (2) *The target language has no value to
  translate the builtin to, and its semantics
  forbid the predicate-free rewrites.* The current
  WGSL specification contains no
  helper-invocation query — no `@builtin` value
  and no function (full-text check, 2026-11-01) —
  and its §15.5 states helper invocations "may
  participate in subgroup operations" on some
  devices and not on others, warning that assuming
  either "will yield non-portable results". Both
  of the captured module's uses are exactly the
  poisoned shape: on a spec-permitted
  implementation where helpers DO participate,
  constant substitution, dropping the `!helper`
  ballot term, or dropping the select each change
  the ballot/reduce inputs and so the atomics'
  behaviour — silently wrong data, the one outcome
  worse than refusal.
  (3) *Upgrading the translator does not change
  this today:* the current upstream naga's IR
  `BuiltIn` enum (50 variants, checked 2026-11-01)
  has no HelperInvocation variant at all — the IR
  cannot represent the value, and its WGSL writer
  would have nothing legal to emit regardless.

  **What changing the translation stack would
  entail, in dependency order:** a WGSL
  helper-invocation query (specification change,
  then implementation in the browser's WGSL
  compiler); naga IR + SPIR-V frontend + WGSL
  writer support built on that query; then
  re-vendoring naga here and re-proving the
  corpus. None of the three exists today, and the
  first is outside this project. **Closing this
  entry requires that chain (or an equivalent
  serving stack) to exist, and the captured module
  to translate end to end through it** — noting
  FD-74 waits immediately behind it for the same
  module. Until then the module is refused by
  name at creation (FD-75), not dropped at
  pipeline time. FD-55 stays open on this entry
  and FD-70.

  **Re-measured link by link on the current
  base, 2026-11-02 (lane `lane-fd72b`, base
  `cc1d89ab3`; record
  `docs/reports/fd72b-helper-invocation-recheck-20261102.md`;
  evidence `test-results/fd72b/`). STILL OPEN —
  the boundary above stands.** The blocker is
  one component: the vendored translator's
  SPIR-V frontend. Called in stages on this
  base, its parse stage fails with
  `UnsupportedBuiltIn(23)` — for the captured
  module (post-pre-pass), for a minimal module
  that declares the variable but never loads
  it, for a load-and-use minimal, and for a
  glslangValidator-compiled textbook
  `gl_HelperInvocation` module — identically
  for the wgsl/glsl/hlsl/msl targets, so the
  failure precedes every writer. With ONLY the
  HelperInvocation decoration, variable and
  interface entry removed from the captured
  module (its two loads replaced by a constant;
  a measurement probe, not a serving
  candidate), the frontend ingests the whole
  real module; its next failure is the
  validator's SampleMask error — FD-74, a
  different stage of the same component.
  Every other link measured ready on this
  base: the pre-pass applies the captured
  module's four standing rewrites and passes
  the decoration through untouched (no rule
  rewrites the BuiltIn-variable form); the
  creation audit refuses both spellings by
  name, neighbouring FragCoord admitted
  (native guard green in a 57-PASS run);
  downstream, the twin module's production
  WGSL compiles in the probe browser with zero
  errors and renders its required value at
  every pixel, a subgroup-ballot WGSL module
  compiles and renders its required value
  likewise, and the browser's WGSL compiler
  rejects both conceivable helper-query
  spellings in its own words (its builtin list
  has no helper entry; `isHelperInvocation`
  is an unresolved call target). Upstream has
  not moved: the WGSL specification (TR and
  living draft, fetched 2026-11-02) still has
  no helper-invocation query and still carries
  the subgroup-participation non-portability
  warning verbatim; the newest published naga
  is 30.0.1 — the vendored version — and its
  published source contains `HelperInvocation`
  nowhere (IR `BuiltIn` enum: 49 variants,
  none for it), so no re-vendor changes the
  verdict. One refinement of (1): the EXT
  opcode spelling IS translated translator-
  side on this base — the pre-pass's discard
  lowering (in the tree since 2026-10-20)
  rewrites each `OpIsHelperInvocationEXT` read
  into a load of a private flag that only the
  module's own demote / lowered-discard sites
  set. That value is the self-demotion
  component of helper state only;
  rasterization helper state is unrepresented
  (a module with no demote sites emits WGSL
  reading a constant false, measured rendering
  accordingly). The guest path still refuses
  the spelling at creation (FD-75), and
  admitting it on the flag's terms would serve
  exactly the collective-steering uses (2)
  shows are poisoned — so it is not a serving
  of this class, and this entry's closing
  terms are unchanged.

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

- **FD-74 — The vendored shader translator's
  validator rejects fragment SampleMask builtins,
  which blocks the captured Godot module one step
  past FD-72 and any fragment shader that reads or
  writes the sample mask.** *(Medium — blocks a
  shader class (SV_SampleMask / gl_SampleMask
  users); found by the FD-72 lane, 2026-11-01)*
  With the HelperInvocation declaration removed,
  the captured Godot module's next translator
  failure is `Built-in type for SampleMask is
  invalid. Found Array {…, size: Constant(1),
  stride: 4}`: the vendored naga's validator
  insists the fragment SampleMask entry-point
  argument have a shape SPIR-V cannot legally give
  it (the builtin is specified as an array).
  Measured general, not module-specific (vendored
  naga 30.0.1, production call shape): three
  minimal hand-written modules (access-chain load;
  the same with Flat; whole-array load + extract)
  all fail identically, and so does a textbook
  glslangValidator-compiled GLSL fragment shader
  reading `gl_SampleMaskIn[0]` — with and without
  the SampleRateShading capability. The write
  direction fails too, with a different validator
  error (`Initializer doesn't match the variable
  type`, on a glslang module writing
  `gl_SampleMask`). The captured module is the
  only corpus module carrying a SampleMask input.
  No spirvfix rewrite can change the variable's
  spec-mandated type, and no other SPIR-V source
  for the mask exists, so there is no honest local
  fix; closing requires translator-stack movement
  (a validator/frontend fix in naga and a
  re-vendor) — the same dependency family as
  FD-72 but a distinct mechanism. Evidence:
  `docs/reports/fd72-helper-invocation-boundary-20261101.md`.
  (Numbered provisionally as FD-74 by the FD-72
  lane, 2026-11-01; numbered FD-74 at composition
  on 2026-11-01 — the number was free on integ.)

- **FD-75 — The bridge's SPIR-V audit admitted
  helper-invocation modules that the page tier
  could never translate, so their draws were
  dropped at pipeline time after the guest had
  been told module creation succeeded. CLOSED
  2026-11-01 — see CL-27.** *(Medium — a
  silent-failure / mirror-discipline gap of the
  FD-49/FD-67 shape; found by the FD-72 lane,
  2026-11-01)* `vk64AuditSpirv` had no rule for
  BuiltIn 23 or OpIsHelperInvocationEXT, so a
  module carrying either was admitted at
  `vkCreateShaderModule`; the failure surfaced
  later, per pipeline, as `vk64: page tier cannot
  translate the fragment SPIR-V module (…) — frame
  skipped` in the page tier, with the guest
  believing creation had succeeded (the Godot
  demo's FD-72 module took exactly this path in
  the FD-62 lane's run). The fix makes the audit
  refuse any module that declares a BuiltIn
  HelperInvocation variable or contains an
  OpIsHelperInvocationEXT instruction, at
  creation, by name (`spirv-builtin-helper-invocation`,
  counted like every audit refusal, and the
  creation path's refusal log names the module's
  size + djb2 hash), returning
  `VK_ERROR_FEATURE_NOT_PRESENT` — the boundary
  is presence-based because the translator's
  rejection is (FD-72 evidence item 1). The
  demote WRITE side is untouched (capability 5379
  stays admitted; only the read refuses). Proof:
  native GUARD in `vk64_native_test.cpp` (both
  spellings refuse + counter; a FragCoord
  neighbour module admits; sabotage-verified —
  with the audit triggers neutered the guard
  aborts at its own first assertion); full native
  run green; JS unit suite at the integration
  baseline; legacy suite 110/4 (the standing
  vkwebgpu set); citation audit zero drift after
  re-pinning through the change's line map; a new
  REJECTED-LOUDLY row in
  `docs/known-gaps/vulkan.md`. In vivo (warmed
  archive, standard demo path, lane build
  `69bb72126` / wasm `04e73f59`): of 256
  `vkCreateShaderModule` calls the only 2 refusals
  are the byte-identified Godot module (5,704 B,
  djb2 `0b3b4d8cc939485a`), each logged by name
  (`spirv-builtin-helper-invocation`) + size +
  hash with `result=-8`; zero `page tier cannot
  translate` lines in the run; the guest reached
  `vkQueueSubmit2` with 0 pipeline-creation
  E_FAILs; the demo still presented nothing
  (FD-55's downstream blockers unchanged).
  Record:
  `docs/reports/fd72-helper-invocation-boundary-20261101.md`.
  (Numbered provisionally as FD-75 by the FD-72
  lane, 2026-11-01; numbered FD-75 at composition
  on 2026-11-01 — the number was free on integ.)

- **FD-78 — The orphan sweep's
  literal-reference defect class is fixed only at
  the operand positions proven on a captured
  module; the same raw-word liveness idiom survives
  at sibling positions and in sibling sweeps.
  CLOSED 2026-11-02 — see CL-46 (provisional
  number).
  *(Low — no captured module is known to be gated
  by the unfixed positions; filed by the FD-70
  lane, 2026-11-01. Numbered FD-78 at composition on
  2026-11-01 — FD-76/FD-77 were taken on integration by the
  FD-12 composition after this branch forked, as
  filed; the number had not moved.)* FD-70's fix teaches the descriptor-array
  orphan sweep (spirvfix) and its bridge-audit
  mirror to bound the MemoryAccess operand group
  and to skip OpTypeInt / OpTypeFloat literal words
  when scanning for id references. The scan idiom
  itself — every non-constant operand word counts
  as a reference — survives unchanged in two
  sibling sweeps in the fixup layer (the
  storage-image type prune and the R15b root-cone
  sweep), and other literal operand positions the
  operand-aware walker already documents remain
  unhandled by the fixed sweeps: the ExtInst
  instruction number, VectorShuffle components,
  CompositeExtract / CompositeInsert indices,
  OpSwitch case literals, OpLine words, the leading
  literals of OpFunction / OpVariable, and the
  OpArrayLength structure index. The class is
  proven live in the small: FD-70's first synthetic
  guard fixture was defeated by the type-width
  position alone (a width literal 32 equal to the
  swept type's id) before that position was fixed.
  Any of the remaining positions can, in a module
  whose ids align, keep a swept type alive and
  reproduce FD-70's failure shape — a refusal one
  layer earlier than the true blocker, or a
  silently retained capability. Closing terms:
  route the sweeps' liveness through operand-aware
  positions (an enumerating sibling of the existing
  per-op walker) in both mirrors, with a guard
  fixture per position; or prove per position that
  no module shape can align it with a swept type
  id. Evidence: `docs/reports/fd70-second-wave-20261101.md`;
  `test-results/fd70/` (keeper census).
  Update (FD-78 lane, 2026-11-02): CLOSED on branch
  `lane-fd78` — see CL-46. The operand-aware walker
  gained an enumerating sibling in both mirrors
  (`operandRefIds` in spirvfix, `r15::operandRefIds`
  in the bridge) and the five remaining raw-word
  liveness scans in the fixup layer plus the
  bridge's skip-5302 simulation now route through
  it. Every position named at filing is covered by
  a guard that is RED on the base scans and GREEN
  with the fix; the storage-image prune's constant
  and width positions, which the filing did not
  name because that scan excluded nothing at all,
  are covered by two new assembled fixtures.
  `stripDeadUndefs`, the fail-closed veto scans and
  the OpGroupDecorate family were audited and
  deliberately left unchanged, with reasons, in the
  record:
  `docs/reports/fd78-literal-positions-20261102.md`.

- **FD-79 — IN_CLOSE_WRITE is reported at fd close even
  when a writable mapping of the file is still open; the
  host defers it until the mapping is unmapped.
  CLOSED 2026-11-01 — see CL-38.** *(Low.
  Filed as FD-79 on the FD-58..60 lane branch,
  2026-11-01; the number was verified free on the
  integration register and settled at composition.)* Measured on the host kernel
  (6.17.0-1020-oracle) by the FD-59 reference programs:
  a file closed while a MAP_SHARED writable mapping of
  it is still open produces no IN_CLOSE_WRITE at close;
  the event arrives at munmap. The emulator's
  notifyClosed fires from the open node's close path
  with no mapping-liveness input, so it reports the
  close at fd close in that case. Found by the
  FD-58..60 lane, 2026-11-01. Evidence:
  `test-results/fd58-60/` (host reference programs and
  outputs).
  Update (FD-79/80 lane, 2026-11-01): measured on the
  emulator, the behaviour was worse than the filed
  text — on the demand-paged path the event was not
  merely early but lost in-session: the open node's
  close is destructor-driven, and a mapped file's
  KFile is pinned by its mapping entries and by the
  system file cache, so neither close nor munmap
  delivered the event; it surfaced only at process
  teardown. The host matrix was also widened by
  re-measurement: any live mapping of the description
  (shared or private, read or write) defers either
  close event to the last unmap. CL-38 covers the
  demand-paged path; the 64-bit path, whose file
  mappings are materialized copies with no
  open-description accounting, is filed as FD-82.

- **FD-80 — The 64-bit dispatcher's preadv (295) and
  preadv2 (327) have name-table entries but no dispatch
  case, so they return ENOSYS. CLOSED 2026-11-01 —
  see CL-39.** *(Low. Filed
  as FD-80 on the FD-58..60 lane branch, 2026-11-01;
  the number was verified free on the integration
  register and settled at composition.)*
  pread64 itself is implemented on the 64-bit side;
  only the vectored positioned reads are missing.
  Found by inspection during the FD-59 read-path work,
  2026-11-01 (syscall64.cpp name table vs dispatch
  switch).

- **FD-81 — In the browser build, link() refuses every
  link the backing store cannot hard-link - which is all
  of them: the temp-file-to-lock-name pattern
  (fontconfig's cache locks) failed 57 times per 32-bit
  boot. CLOSED 2026-11-01 — see CL-33.** *(Medium - a mainstream library startup
  pattern's step fails on every 32-bit boot; fontconfig
  copes, but any guest code that requires the link
  breaks.)* Filed 2026-11-01 from the FD-58..60 deploy
  record's in-vivo observation (285 BW64-FAIL lines, all
  this refusal, exactly 57 per x86 boot, 0 on x64).
  FD-60 (CL-31) made link() a true hard link via a host
  ::link() and specified refusal with EPERM and a
  BW64-FAIL line for sources that are not host files. In
  the browser build the "host" is the Emscripten
  filesystem: its MEMFS and IDBFS backings provide no
  hard-link operation at all, and its FS.link answers
  every attempt with EMLINK (errno 34 in this build's
  numbering - the value on all 285 deploy lines). The
  refused sources are ordinary files (fontconfig's
  /root/tmp/fontconfig/*.TMP-* temp files linked to
  *.LCK lock names in the same directory); what refuses
  them is the backing store's missing operation, not the
  FD-60 scope. On the host the same pattern succeeds, so
  the refusal is not the host-correct answer for it.
  The fix (b0b3a70d3): when the host link fails with
  EMLINK in the browser build, KProcess::link serves the
  link at the emulator layer (KProcess::linkSharedBacking):
  the new name is a second node with the source's id in
  the same link group, pointing at the SAME backing
  path, so content identity is real - every open of
  either name opens the one backing file, and writes
  through one name are visible through the other. The
  link group gains a sharedBackingPath flag, and
  FsFileNode consults it: link counts come from the
  group's live membership (the backing's own count is
  1), unlinking one name detaches only (the last name's
  unlink deletes the backing), and renaming one name
  moves only the guest tree entry (POSIX rename moves
  an entry, not the file). Each served link logs a
  capped BW64-INFO line; a served link is a success, not
  a BW64-FAIL. Native builds are unchanged - there
  EMLINK is the host's real link-count limit and stays a
  refusal - as do symlink sources in every build
  (FD-60's sidecar semantics) and non-file sources.
  Proof: new test testTempFileLinkPattern in both
  native suites runs the pattern end to end (shared
  inode, nlink 2, exactly one backing file on the
  host-side census - a copy would show two - writes
  visible through both names, content survives
  unlinking the temp name, the backing is deleted with
  the last name, a rename keeps the sharing);
  sabotage-checked both ways (a distinct backing path
  fails the shared-content assertions; disabling the
  unlink guard fails the survival assertions); full
  native suites serial: main 666/666, x64 65/65,
  0 FAILED (baselines 665/64 + the new test). Browser
  build of the branch: 'Browser export verified' (dist
  git_sha b0b3a70d3, wasm 8303eff4); in vivo
  boot32-vkboot PASS on that dist - the console carries
  0 BW64-FAIL lines of any kind and 5 BW64-INFO
  hard-link lines, on exactly the fontconfig files the
  deploy record lists (CACHEDIR.TAG and the four
  le32d4 cache files; this boot shape runs one
  fontconfig pass - the deploy's full-session count of
  57 is the merge/deploy gates' figure to re-measure).
  Residuals, stated: the group liveness count can lag
  while an unlinked name's node is still open (FD-60's
  stated class, now applying to file nodes on this path
  too); after a cold rescan of the backing tree the two
  names load as separate nodes (FD-60's stated class -
  the backing holds one file); the served link exists
  in the guest tree only, never as a second name in the
  backing store itself.
  Record: docs/reports/fd81-temp-link-pattern-20261101.md;
  evidence test-results/fd81/ (on the lane tree).
- **FD-82 — A 64-bit guest's file-backed mappings do
  not participate in FD-79's close-event deferral: the
  64-bit memory path materializes a file mapping as
  copied/shared pages keyed by node, with no
  open-description accounting, so a 64-bit guest still
  sees IN_CLOSE_WRITE / IN_CLOSE_NOWRITE at fd close
  while such a mapping is live, where the host defers
  the event to the last unmap.** *(Low. Filed by the
  FD-79/80 lane, 2026-11-01; the number was verified
  free against the register at filing and is
  provisional until the merge lane's audit.)* The
  demand-paged path is exact after CL-36; closing
  this entry needs per-description mapping accounting
  on the 64-bit path or an equivalent liveness input
  to its close path. Evidence:
  `docs/reports/fd79-fd80-kernel-semantics-20261101.md`.
  FIXED 2026-11-02 on branch `lane-fd82` -- see
  CL-48 (provisional). One correction to the text
  above: the demand-paged path's deferral closure
  is CL-38, not CL-36 as filed.

- **FD-83 — readv generates no IN_ACCESS when it
  succeeds with 0 bytes (at EOF, or with iovcnt 0);
  the host generates exactly one. FIXED 2026-11-02
  on branch `lane-fd83-84` — see CL-49.** *(Was Low.
  Filed by
  the FD-79/80 lane, 2026-11-01; the number was
  verified free against the register at filing and is
  provisional until the merge lane's audit.)*
  Measured on the host kernel (6.17.0-1020-oracle):
  the vectored reads generate IN_ACCESS once per
  successful call including 0-byte successes, where
  plain read/pread stay silent at 0 bytes. The
  emulator's readv notifies per segment through the
  plain read path, whose bytes>0 rule is correct for
  read/pread but not for the vectored calls.
  preadv/preadv2 follow the host rule since CL-39.
  Evidence: `test-results/fd79-80/` (host reference
  programs and outputs).

  *Update 2026-11-02 (lane-fd83-84): the host
  contract was re-measured in full before fixing —
  exactly one IN_ACCESS per successful readv (one
  segment or three, data or 0 bytes at EOF / iovcnt
  0 / all-zero-length segments; a call that reads
  bytes before a later segment fails returns the
  bytes as a success and generates the event), none
  on failure. The emulator's multi-segment count
  held only by accident of the inotify queue
  coalescing consecutive identical events; its
  0-byte successes generated nothing, and its walk
  carried no iovcnt range validation (an
  out-of-range count was never EINVAL) nor a mode
  check at iovcnt 0. Fixed in commits `0332ee82f`
  (tests) and `3459a7d20` (fix) — see CL-49.
  Record:
  `docs/reports/fd83-fd84-read-family-20261102.md`;
  evidence `test-results/fd83-84/`.*

- **FD-84 — The positioned/plain read family answers
  -EINVAL on a descriptor that cannot be read; the
  host answers -EBADF. FIXED 2026-11-02 on branch
  `lane-fd83-84` — see CL-50.** *(Was Low. Filed by
  the FD-79/80
  lane, 2026-11-01; the number was verified free
  against the register at filing and is provisional
  until the merge lane's audit.)* Measured on the
  host: preadv on an O_WRONLY descriptor returns
  EBADF. The emulator's positioned-read core rejects
  a non-readable file with -EINVAL, and pread64 and
  (since CL-39) preadv inherit that answer;
  KProcess::read makes the same choice for plain
  reads. Found by the FD-79/80 lane while pinning
  preadv's validation matrix; left as found rather
  than changed inside a wiring fix. Evidence:
  `test-results/fd79-80/` (host reference programs
  and outputs).

  Addendum (FD-100's lane, 2026-11-02): in
  the 64-bit dispatcher's vectored arms the
  mode answer comes from the walk's
  per-segment core, behind the iovcnt range
  and iovec array checks, so a write-only
  descriptor with an out-of-range iovcnt or a
  bad array answers EINVAL / EFAULT where the
  host answers EBADF -- one more combination
  governed by wherever this entry's mode gate
  lands when it composes.

  *Composition note (batch-3 merge): the mode gate the
  addendum above leaves open lands in the vectored
  wrappers themselves -- sys_preadv64 and sys_pwritev64
  now answer the mode EBADF with the descriptor check,
  ahead of the iovcnt range and iovec array checks --
  settled with FD-95, which see.*

- **FD-102 — The launcher UI test's demo-card count no
  longer matches the demo catalog.** *(Low. Filed as
  FD-82 on `lane-fd77-stall-surface`, 2026-11-01;
  renumbered FD-102 at composition — FD-82 on the
  composed register is the FD-79/80 lane's
  close-event entry.)* `web/demos.mjs`
  lists 15 demos and `web/app.mjs` `renderDemos()` renders
  one `[data-game]` card per demo unconditionally, while
  `web/tests/launcher-ui.mjs` asserts exactly 8 cards
  (line 31) — so the test fails at that assertion
  (15 !== 8) on a fresh build of the integration tip,
  before any of its UI checks run. Found by
  `lane-fd77-stall-surface` while regression-checking an
  unrelated launcher change: the failure is independent
  of that change (the test replaces the runtime page with
  a fixture, so the changed launcher script never loads;
  the count comes only from `demos.mjs`/`app.mjs`, both
  untouched by the lane) and reproduces from the
  branch-point sources. Either the catalog grew without
  the test following, or the test's 8 encodes a filtered
  view the renderer no longer applies; an owner should
  decide which side is wrong. Evidence: this lane's
  healthy-path log,
  `test-results/fd77-stall-surfacing/healthy.log`
  (branch `lane-fd77-stall-surface`); record:
  `docs/reports/fd77-stall-surfacing-20261101.md`.
  **Update (2026-11-02, batch-2 merge lane): RESOLVED at
  composition.** The catalog side is right and the test's
  expectation was stale: `web/demos.mjs` lists 15 demos
  and `renderDemos()` renders one `[data-game]` card per
  demo unconditionally, so the filtered-view alternative
  in the filing does not hold. Test-only correction
  `8ac504abf` (`web/tests/launcher-ui.mjs` line 31:
  8 -> 15), following the FD-64 stale-fixture precedent.
  Proof at this composition's gate:
  `node web/tests/launcher-ui.mjs` against the composed
  build returns `pass:true` with all 13 checks (evidence:
  `test-results/merge-fd66-65-85-fd77surface-20261102/launcher-ui.log`).
  Closed inline at composition; no CL number consumed,
  so the CL-42/CL-43 split coordinated for the two FD-87
  claims is undisturbed.
- **FD-89 — The Godot demo's second-wave pipeline
  creations fail on sibling shader modules:
  capability 29 refused unnamed as a bare
  declaration, dead descriptor declarations gating
  SampledBuffer and RuntimeDescriptorArray, and the
  ExtInst literal position defeating the texel
  conversion. CLOSED 2026-11-02 — see CL-43.**
  *(Medium — three pipeline creations in the
  shipped demo run (deploy of 2026-11-02, build
  `9e37e5640`); filed by the FD-89 lane, 2026-11-02,
  on the deploy record's evidence.)* The deploy
  proof run (FD-70 update, above) recorded the two
  modules byte-exact: 369,400 B, djb2
  `d4da162f9cbdb948` (harvested fixture
  `testdata/spirv/godot-fs-f2b738c259a79b4a.spv`),
  refused for `spirv-capability-unknown:29`,
  RuntimeDescriptorArray and FD-72's
  helper-invocation class; and 2,340 B, djb2
  `fe166085df72b93f` (`godot-fs-f1b4e0027d98e2e4.spv`,
  twice), refused for SampledBuffer and
  RuntimeDescriptorArray. Measured per class:
  (a) capability 29 is
  SampledImageArrayDynamicIndexing; the translator
  rejects the bare declaration outright, whatever
  the module's arrays look like. It is served
  exactly when no image array survives the fixup
  layer: spirvfix R19 strips the declaration once
  every image/sampled-image array is a used runtime
  array the specializer served (or a dead one R18
  removed) and no fixed-size image array exists;
  the bridge audit now names the capability and
  admits exactly that class, refusing it by name
  otherwise. (b) The 2,340-byte module's classes
  were entirely dead descriptors: its body reads
  only push constants and inputs, while its three
  descriptor variables (a texel-buffer runtime
  array, an SSBO-offsets block, a bindless-CBV
  array) are referenced by no instruction. spirvfix
  R18 strips declared-but-unreferenced descriptor
  variables and their orphaned types; the module
  now translates end to end, and the audit mirrors
  (a dead variable gates neither SampledBuffer nor
  RuntimeDescriptorArray). (c) The 369,400-byte
  module's SampledBuffer share was FD-78's ExtInst
  position proven live (see the FD-78 update): the
  orphan sweep counted GLSL.std.450 instruction
  numbers as id references, the texel image type
  never stopped looking array-owned, and the
  post-specialization texel conversion silently
  declined; with the position bounded in both
  sweeps the conversion runs (508 fetches widened)
  and SampledBuffer is genuinely served. Not closed
  here: the same module's remaining
  RuntimeDescriptorArray + capability-29 share is
  FD-88's data-dependent array, and its
  helper-invocation share is FD-72's. CL-43's owed
  clause — the two pipelines' creations succeeding
  in vivo on a shipped build — belongs to the
  deploy gate that carries the composing merge.
  The proof run also recorded one delivered guest
  memory fault (addr `0x2140bf26c`, rip
  `0x140301916`, at 2,116.8 s, ~42 s after the
  failures; the run continued to deadline): noted
  in the lane record as observed and unattributed;
  fixture work did not reproduce it and it is not
  filed as a defect. Record:
  `docs/reports/fd87-godot-sibling-classes-20261102.md`;
  evidence `test-results/fd87/`.

- **FD-88 — A descriptor array indexed by a value
  loaded from GPU memory cannot be specialized;
  the array, its RuntimeDescriptorArray
  declaration, and the SampledImageArrayDynamicIndexing
  declaration riding on it stay refused.**
  *(Medium — one array in the captured 369,400-byte
  Godot fragment module gates its pipeline; filed
  by the FD-89 lane, 2026-11-02.)* In
  `godot-fs-f2b738c259a79b4a.spv` the 3D image array
  at set 3 / binding 0 is reached by seven access
  chains: five index it with push-constant-derived
  values (the specializer's servable class, proven
  on the sibling arrays in the same module sharing
  that binding), and two add a term loaded through
  a root-descriptor (buffer-device-address) pointer
  — `& 0xFFFF` / `>> 16` of a GPU-memory load — so
  the element index is data-dependent and no
  per-signature clone set exists. Specialization is
  per-array fail-closed, so the two chains leave
  the whole array unserved, the five servable
  chains with it. Gating, named exactly: true
  binding arrays in the translation stack. The
  translator (naga) and WGSL have no runtime-sized
  texture arrays, so no fixup-layer rewrite can
  serve the class; a heap-sized fixed array with a
  whole-heap binding would be a page-tier feature
  with its own sizing and lifetime terms, not a
  fixup. (Boundary, verified on both mirrors:
  fixed-size image arrays are likewise outside the
  specializer's universe — runtime arrays only —
  and keep the same named refusal.) Closing terms:
  a translation-stack binding-array capability, or
  a page-tier heap-array feature filed and built as
  its own entry; until then both mirrors refuse the
  class by name
  (`spirv-capability:SampledImageArrayDynamicIndexing`
  / `spirv-capability:RuntimeDescriptorArray`) and
  spirvfix leaves the array untouched. Evidence:
  `test-results/fd87/` (index-provenance trace);
  record
  `docs/reports/fd87-godot-sibling-classes-20261102.md`.
  UPDATE (2026-11-02, lane-fd88) — the gating named
  above is corrected by measurement: the translation
  stack is NOT the blocker. The vendored translator
  (naga 30.0.1) serves this exact class: on minimal
  fixtures in the captured module's own form (runtime
  array of 3D sampled images, separate sampler array,
  OpSampledImage), a data-dependent index — a storage
  load reduced by `>> 16` / `& 0xFFFF`, the shape of
  the two unservable chains — translates to WGSL with
  the index expression preserved verbatim over an
  unsized `binding_array<texture_3d<f32>>`. The only
  translator refusal is the capability DECLARATION:
  every fixture declaring SampledImageArrayDynamicIndexing
  is refused by name, and the identical module without
  the declaration translates. On the captured module
  itself, with the declaration removed and one
  unrelated mis-ordering in the fixup's own output
  repaired (FD-92), the translator accepts the whole
  type/global section — all seven chains into the
  array included — and its next refusal is an
  OpIAddCarry in a root-address cone that no array
  chain's index cone contains (FD-70/R15 territory,
  rewritten in production with a pipeline signature;
  the FD-72 helper-invocation refusal stands behind
  it). Tint in the project's Chromium parses sized
  `binding_array<texture_3d<f32>, N>` shader modules.
  THE PROVEN BLOCKER IS ONE EXTERNAL COMPONENT: the
  WebGPU implementation of the project's Chromium
  (HeadlessChrome 145, SwiftShader adapter) cannot
  populate an arrayed texture binding. Measured:
  createBindGroupLayout accepts a texture entry with
  count 2/4/1024/65536, but pipeline validation
  reports the layout 'only provides 1 elements'
  against a count-2 entry — the count is not
  honoured; createBindGroup with an array of texture
  views as the entry resource throws at WebIDL
  conversion ('Failed to read the 'buffer' property
  from 'GPUBufferBinding'); repeated scalar entries
  for the binding are rejected ('binding index 0
  already used'). No spelling populates the array.
  Every other link is shaped and ready: the fixup's
  servability analysis already isolates exactly this
  class, and the tier's bind-group builder already
  assembles per-element view arrays in spec form (it
  would reach the same browser throw). Closing
  terms, revised: when the serving browser honours
  arrayed texture bindings, the in-project work is
  (i) a fixup rule stripping the cap-29 declaration
  for arrays the analysis proves binding-array-
  servable (loads and sampled-image formation only;
  escapes, stores and texel forms keep the refusal),
  with the audit mirror admitting cap 29 under the
  same verdict; (ii) the tier sizing the translator's
  unsized binding-array declarations from the guest
  layout's descriptor count in the pipeline
  signature, dropping the `enable wgpu_binding_array;`
  directive (Tint rejects the name), and a layout-
  entry arm carrying the count; (iii) per-element
  bind-group supply, which exists. Until then both
  mirrors KEEP refusing the class by name: that
  refusal is the correct behaviour, since an admitted
  module would fail later and worse — an uncaught
  browser throw at draw time instead of a named
  VK_ERROR_FEATURE_NOT_PRESENT at module creation.
  FD-88 stays OPEN, re-gated on the browser.
  Evidence: `test-results/fd88/` (fixture matrix,
  translator outputs, browser probes); record
  `docs/reports/fd88-binding-array-blocker-20261102.md`.
  NOTE for the composing merge: this entry also
  lives, in its original form, on lane-fd87's
  register; this branch's copy carries the narrowing
  and supersedes it.
- **FD-91 — The comment above the ADDR table in
  web/vk-protocol.mjs misstates the mapping it
  annotates.** *(Low, documentation. Filed by the
  FD-64 lane, 2026-11-02; FD-85..FD-90 are taken
  by parallel lanes' uncomposed branch records,
  so this number is provisional until the merge
  lane's audit.)* The comment says WebGPU has no
  border addressing or mirror-clamp mode and
  'both use clamp-to-edge'; the table it annotates
  maps MIRROR_CLAMP_TO_EDGE (4) to 'mirror-repeat'
  — only CLAMP_TO_BORDER (3) collapses to
  clamp-to-edge. The served behaviour is the
  table's (in use since the page tier; the
  border-emulation tier keys off the raw addrMode*
  fields precisely because the table collapses
  modes). The comment is the stale side. The
  filing lane left it unedited — its mandate was
  the legacy cases — so the fix (correct the
  comment; or, if a future audit finds the table
  wrong, the table, with the served-behaviour
  consequences stated) is queued here. Evidence:
  web/vk-protocol.mjs lines 89-90;
  `docs/reports/fd64-legacy-descset-20261102.md`.
- **FD-92 — spirvfix's fixed output for the captured
  369,400-byte Godot module is not spirv-val clean:
  synthesized pointer types are spliced ahead of a
  module type definition they reference. FIXED
  2026-11-02 on branch `lane-fd92` — see CL-47.**
  *(Medium — any strict consumer of the fixed module
  (spirv-val; the translator, once earlier refusals
  are lifted) rejects it; filed by the FD-88 lane,
  2026-11-02; the number is provisional until the
  merge lane's audit.)* Measured on lane-fd87's
  end-state output for
  `godot-fs-f2b738c259a79b4a.spv`
  (`test-results/fd87/godot-369400-d4da162f-fixed2.spv`):
  spirv-val reports "Operand '54[%bool]' requires a
  previous definition" — the synthesized Private and
  Function pointer-to-bool types (ids 17439/17440,
  allocated by the fixup) sit at instruction 299
  while OpTypeBool (id 54) is defined at instruction
  333. The raw module is spirv-val clean. The FD-87
  lane's verdicts never reached the defect: the
  translator refuses that module at the capability
  stage, before type parsing reaches the mis-ordered
  types, and its spirv-val check ran on the sibling
  fixture only. Closing terms: the fixup's splice
  machinery places synthesized type instructions
  without regard to dependencies on module types
  defined later in the type section; the fix keeps
  every synthesized type behind the definitions it
  references (or orders the type section
  topologically after splicing), proven by spirv-val
  on this module's fixed output with the corpus
  expectations unchanged. Evidence:
  `test-results/fd88/` (captured-naga5.log and the
  lane record's id-54 analysis).
  FIXED 2026-11-02 on branch `lane-fd92` (commit
  89ac92ff9): the splice machinery now computes a
  dependency-safe position — `defsSplicePos` returns
  the earliest position at or after a rule's anchor
  that follows every module definition the
  synthesized instructions reference (grammar-aware
  via idOperandUseIndex; a constant's resultType is
  its type word) — applied at the three
  intra-type-section splice sites:
  lower-mid-shader-discard (the measured site; its
  bool type, constants and pointer types now move as
  one dependency-ordered block),
  strip-point-builtins' constants, and the
  texel-pointer rewrite's addTypes. Reproduced on
  this base first: the raw module is spirv-val clean
  and the base fixup's output fails with the filed
  error verbatim. With the fix the captured module's
  output validates cleanly and is a pure reordering
  of the base output (instruction word-multiset
  identical over 18,352 instructions), so the
  module's meaning is unchanged; the translator's
  verdict is unchanged (capability-stage refusal,
  FD-88). Across the 72-module fixture corpus every
  fixed output is byte-identical to the base and
  every applied set is unchanged — the position only
  moves when a referenced definition would otherwise
  follow it — and the corpus gate passes 10/10 with
  its expectations untouched. New guard
  `web/tests/suite/unit/spirvfix-fd92.test.mjs`
  builds the shape in miniature: RED on the base
  (spirv-val on the base output: "ID '7[%bool]' has
  not been defined"), GREEN with the fix (raw and
  fixed minimal modules both spirv-val clean).
  Record:
  `docs/reports/fd92-spirvfix-splice-ordering-20261102.md`;
  evidence `test-results/fd92/`.

- **FD-104 — The 64-bit file-mmap path can return
  an error after the mapping has been established:
  in sys_mmap64_file's private branch, the content
  read that follows the map can fail (an errored
  positioned read of the file bytes) and the
  syscall returns that errno, but the pages stay
  mapped and the range stays reserved, where a
  failed host mmap leaves nothing mapped.** *(Low.
  Filed by the FD-82 lane, 2026-11-02; the number
  is provisional until the merge lane's audit --
  FD-87..FD-91 are held by parallel branches.)*
  A guest that trusts the failure believes the
  range is unmapped; an access there succeeds
  instead of faulting, and (since CL-48) the
  mapping counts against its description's
  close-event deferral like any other live
  mapping. Found by inspection while wiring
  FD-82's birth accounting (the birth is recorded
  before the read precisely because the mapping
  exists from that point, whatever the read does).
  Evidence:
  `docs/reports/fd82-materialized-mmap-close-deferral-20261102.md`.
  FIXED 2026-11-02 on branch `lane-fd92mmap` -- see
  CL-51 (provisional). The number is kept exactly
  as carried from lane-fd82; it collides with
  unrelated FD-104 entries on sibling branches
  (see CL-51), and the composing merge settles
  numbering by audit.

- **FD-103 — The 32-bit dispatcher's readv (145) has a
  null dispatch slot and answers -ENOSYS; only the
  64-bit readv exists. FIXED 2026-11-02 on branch
  `lane-fd92-93` — see CL-57.** *(Was Low. Filed by the
  FD-83/84 lane, 2026-11-02; the number was chosen
  past FD-85..FD-91, which live on unmerged lane
  branches off the same base, so the composition
  audit does not have to renumber it; provisional
  until that audit.)* Found while scoping FD-83,
  which covers the implemented 64-bit readv. The
  32-bit table carries the slot as null, so a 32-bit
  guest's readv returns -ENOSYS with only a debug
  line — an unimplemented-shape answer for a call
  Linux has always had, and one the one-event-per-
  call notification contract (FD-83) would then
  also apply to. writev (146) is dispatched on the
  same table, so the wiring shape exists. Evidence:
  the dispatch table in `source/kernel/syscall.cpp`
  on this base; the FD-83 contract in
  `test-results/fd83-84/`.

  *Update 2026-11-02 (lane-fd92-93): the entry was
  carried verbatim from lane-fd83-84's register and
  fixed here. Slot 145 dispatches to a new
  KProcess::readv over a new KObject::readv walk,
  validating in the host's order (bad descriptor
  EBADF, wrong mode EBADF, iovcnt range EINVAL,
  null iovec EFAULT) and holding FD-83's contract:
  exactly one IN_ACCESS per successful call — a
  byte-moving walk's per-segment events coalesce
  in the inotify queue, and a success that moved
  no bytes raises its event in KProcess::readv.
  Fixed in commits `48bb12b1c` (tests) and
  `8d2da6430` (fix) — see CL-57. Record:
  `docs/reports/fd92-fd93-readv-write-family-20261102.md`;
  evidence `test-results/fd92-93/`.*

- **FD-93 — The write family answers -EINVAL (or a
  silent 0) where the host answers -EBADF for a
  write on a descriptor opened read-only. FIXED
  2026-11-02 on branch `lane-fd92-93` — see CL-58.**
  *(Was Low. Filed by the FD-83/84 lane, 2026-11-02; the
  number was chosen past FD-85..FD-91 for the same
  reason as FD-103; provisional until the merge
  lane's audit.)* The mirror of FD-84, host-measured
  by this lane (host kernel 6.17.0-1020-oracle):
  write, pwrite and writev on an O_RDONLY descriptor
  answer EBADF, for count 0 and iovcnt 0 as well,
  and pwrite's negative offset answers EINVAL ahead
  of the mode answer. The emulator: `sys_pwrite64`
  gates on canWrite() with -EINVAL; `KProcess::write`
  answers -EINVAL for !canWrite; `KProcess::pwrite64`
  carries no mode check, so the call falls through
  to the backing write whose host EBADF the backing
  layer swallows into a silent 0; `sys_write64`
  carries no mode gate at all, so plain write and
  (through it) `sys_writev64` inherit the backing
  layer's answer rather than the kernel's. FD-84's
  filing covers the read family; this lane did not
  widen its fix to match. Evidence:
  `test-results/fd83-84/` (hostref-write.c and its
  output).

  *Update 2026-11-02 (lane-fd92-93): the entry was
  carried verbatim from lane-fd83-84's register and
  fixed here, at every site it names —
  KProcess::write and KProcess::writev (EINVAL ->
  EBADF; the vectored walk also gains its
  iovcnt-range and iovec-pointer validation, behind
  the mode check as measured), KProcess::pwrite64
  (the missing mode check, with the negative
  offset's EINVAL ahead of it), sys_write64 (a
  descriptor+mode gate ahead of the zero-count
  return; its later gate's answer corrected),
  sys_writev64 (the validation block its read
  counterpart carries) and sys_pwrite64 (the offset
  EINVAL reordered ahead of the descriptor and mode
  answers); the 32-bit dispatcher's pwrite64 checks
  the offset's high word ahead of forwarding, so
  that route keeps the same order. One correction
  to the text above: sys_write64 does carry a mode
  gate on the base — answering -EINVAL, behind the
  buffer copy, and bypassed entirely by the
  count==0 return; the entry's observable claim
  stands. Fixed in commits `48bb12b1c` (tests),
  `8d2da6430` (fix) and `0acf72a4b` (silent-failure
  baseline review) — see CL-58. Record:
  `docs/reports/fd92-fd93-readv-write-family-20261102.md`;
  evidence `test-results/fd92-93/`.*


- **FD-101 — The 64-bit sendmsg path gathers
  segment data from unreadable guest buffers
  as fabricated zeros instead of answering
  EFAULT. FIXED 2026-11-02 on branch
  `lane-fd101` — see CL-52.** *(Was Low.
  Filed by the FD-97 lane,
  2026-11-02; the number follows FD-100 on
  lane-fd98's register and is provisional
  until the merge lane's audit.)* Found
  auditing FD-97's trait beyond the file
  paths: `sys_sendmsg64`
  (source/kernel/syscall64.cpp) gathers each
  iovec segment into its per-thread scratch
  buffer through KMemory64::memcpyFromGuest
  with no readability check on the segment
  base, so a segment pointing at unmapped or
  unreadable guest memory is sent as that
  many zero bytes and the call reports
  success; sendmmsg loops sys_sendmsg64 and
  inherits it. The host answers -EFAULT and
  sends nothing for a fully unreadable
  segment (measured on the box's host kernel
  6.17.0-1020-oracle with a dedicated
  reference program,
  `test-results/fd97/hostref-sendmsg.c`:
  bad iov base -EFAULT; a good base is
  delivered intact). The file paths' form of
  the trait is FD-97 (fixed, CL-56); this is
  the vectored socket form, left for the
  owning lane — the fix shape mirrors
  FD-97's (readable prefix / EFAULT per
  segment), but the socket paths' partial
  and message-boundary semantics need their
  own host measurements first. Evidence:
  the gather loop in sys_sendmsg64 on this
  base.

  Fixed in commits `7eed978a9` (test),
  `f70b16116` (fix) and `4363174fa`
  (slice-test surface) — see CL-52. The
  closing contract was settled by measurement
  before fixing
  (`test-results/fd101/hostref-sendmsg-fd101.c`
  with sendto and recvmsg companions, host
  kernel 6.17.0-1020-oracle): the host's
  sendmsg is all-or-nothing — any unreadable
  byte in any nonzero segment answers
  -EFAULT with nothing delivered, including
  a segment that starts readable and crosses
  into an unmapped page (no short-count
  prefix, unlike FD-97's write contract);
  the descriptor answers precede the copy
  faults, and an iovlen over the host's
  1024 answers -EMSGSIZE. Record:
  `docs/reports/fd101-sendmsg-efault-20261102.md`;
  evidence `test-results/fd101/`.


- **FD-105 — The 64-bit sendmsg SCM_RIGHTS
  translation trusts the guest's cmsg_len:
  a length under the header size, or past
  the control area, makes the translation
  loop run unboundedly past its scratch
  area. FIXED 2026-11-02 on branch
  `lane-fd102-104` — see CL-53.** *(Was
  Low. Filed by the FD-101 lane,
  2026-11-02; provisional until the merge
  lane's audit.)* Found fixing FD-101: in
  sys_sendmsg64's ancillary translation
  (source/kernel/syscall64.cpp) the rights
  count is derived from the guest-supplied
  cmsg_len alone — nfds = (clen - 16) / 4 —
  with no check that the length covers the
  header or stays inside msg_controllen. A
  cmsg_len under 16 underflows the count to
  roughly 2^30 descriptors; the loop then
  reads guest words and writes 16-byte
  records into and far past the per-thread
  scratch cmsg area, and a merely oversized
  length overruns the same way. Code
  evidence on this base; not exercised
  against the running emulator (by
  construction the failure is a hang or
  scratch corruption). The FD-101 fix
  validates the rights array's readability
  only when clen >= 16 and deliberately
  does not clamp the count — that is this
  defect, on the wineserver fd-passing
  path, left for its owning lane. Fix
  shape: reject or clamp a cmsg_len under
  the header size or beyond the remaining
  controllen before the count is derived.

- **FD-106 — The 64-bit single-call socket
  send (sendto) copies its guest buffer
  through the same zero-filling accessor
  with no readability check: an unreadable
  buffer is sent as fabricated zeros and
  the call reports success. FIXED
  2026-11-02 on branch `lane-fd102-104` —
  see CL-54.** *(Was Low. Filed
  by the FD-101 lane, 2026-11-02;
  provisional until the merge lane's
  audit.)* sys_sendto64
  (source/kernel/syscall64.cpp) copies the
  buffer through KMemory64::memcpyFromGuest
  in chunks before the send, so it carries
  FD-101's trait in the single-call form;
  it does not share the msghdr gather, so
  it is filed separately rather than fixed
  with FD-101. The host contract is
  measured
  (`test-results/fd101/hostref-sendto-fd101.c`,
  host kernel 6.17.0-1020-oracle): unmapped
  and page-crossing sendto buffers answer
  -EFAULT with nothing delivered on stream
  and datagram sockets alike, and the
  descriptor answer (-ENOTSOCK on a file)
  precedes the fault. Fix shape: validate
  the buffer's full range before the copy,
  mirroring FD-101's checks and ordering.

- **FD-107 — The 64-bit recvmsg scatter
  drops received bytes silently when a
  guest segment is unwritable: the data is
  consumed from the socket and the call
  reports it delivered. FIXED 2026-11-02
  on branch `lane-fd102-104` — see
  CL-55.** *(Was Low. Filed by
  the FD-101 lane, 2026-11-02; provisional
  until the merge lane's audit.)* Found
  auditing FD-101's mirror direction:
  sys_recvmsg64
  (source/kernel/syscall64.cpp) receives
  into scratch through krecvmsg —
  consuming the message — then scatters to
  the guest iov through
  KMemory64::memcpyToGuest, which returns
  silently when its access check fails, so
  an unmapped or unwritable segment loses
  the bytes while the return value reports
  them received. The host answers -EFAULT
  for an unwritable receive buffer, and
  the consumption differs by socket type
  (measured,
  `test-results/fd101/hostref-recvmsg-fd101.c`,
  host kernel 6.17.0-1020-oracle): on a
  stream socket the data survives the
  failed call and a following recvmsg
  returns it; on a datagram socket the
  failed call consumes the datagram. The
  receive direction's fix needs its own
  design (validate the scatter ranges
  before krecvmsg consumes, and settle the
  stream/datagram difference), left for
  the owning lane.

- **FD-111 — The 64-bit recvfrom scatters
  through the same silent-drop accessor as
  FD-107's recvmsg: an unwritable receive
  buffer loses bytes the call reports
  delivered.** *(Low. Filed by the
  FD-105..104 lane, 2026-11-02; provisional
  until the merge lane's audit.)* Found
  closing FD-107: the dispatcher's recvfrom
  branch (source/kernel/syscall64.cpp,
  sharing its case block with sendto)
  receives through krecvfrom into scratch
  and copies back through
  KMemory64::memcpyToGuest, whose
  access-check failure returns silently —
  FD-107's trait in the single-call receive
  form. Measured on the host
  (`test-results/fd102-104/hostref-recvmsg2-fd104.c`,
  host kernel 6.17.0-1020-oracle): an
  unwritable buffer answers -EFAULT, a
  stream socket's data survives the failed
  call, and a datagram is consumed by it.
  FD-107's fix covers sys_recvmsg64 only;
  the fix shape mirrors it (validate the
  destination as the scatter proceeds,
  with the same settled stream-consumption
  divergence).

- **FD-112 — The 64-bit sendmsg control
  translation examines only the first cmsg
  header: nothing past it is validated or
  translated, where the host walks the
  whole chain.** *(Low. Filed by the
  FD-105..104 lane, 2026-11-02; provisional
  until the merge lane's audit.)* Measured
  on the host
  (`test-results/fd102-104/hostref-cmsglen-fd102.c`,
  host kernel 6.17.0-1020-oracle): a valid
  SCM_RIGHTS header followed by trailing
  slack inside msg_controllen answers
  -EINVAL — the zeroed trailing header is
  malformed — while the emulator sends the
  rights and the data without complaint. A
  second header of any kind, and a first
  header that is not SCM_RIGHTS, are
  likewise dropped silently where the host
  processes them (code evidence:
  sys_sendmsg64's single-header
  translation). Found measuring FD-105's
  host contract; FD-105's fix validates
  the first header's length only.

- **FD-113 — sys_recvmsg64's source-name
  copyback goes through the same
  silent-drop accessor: a peer address
  that cannot be written is lost while
  the call reports success.** *(Low. Filed
  by the FD-105..104 lane, 2026-11-02;
  provisional until the merge lane's
  audit.)* Code evidence, found auditing
  FD-107's function: after a successful
  receive, the name copyback
  (source/kernel/syscall64.cpp) copies the
  bounced sockaddr to the guest's msg_name
  through KMemory64::memcpyToGuest, which
  returns silently when its access check
  fails; the host's address copy faults
  the call (-EFAULT, with the received
  data already delivered). FD-107's fix
  covers the data scatter and the import
  side; the copyback needs its own settled
  answer, since by then the receive has
  already happened on every socket type.

- **FD-114 — A datagram sent with sendto
  on a connected unix socketpair is
  reported sent but never delivered: it
  reaches neither the peer's message
  queue nor its stream buffer in any form
  the peer can receive.** *(Low. Filed by
  the FD-105..104 lane, 2026-11-02;
  provisional until the merge lane's
  audit.)* Measured on this base while
  writing FD-106's test:
  KUnixSocketObject::sendto routes every
  send to the object's stream write path
  (source/kernel/kunixsocket.cpp); for a
  datagram pair, a sendto that returns its
  full length is afterwards invisible to
  the peer's poll (whose readiness covers
  both stores), to recvmsg, and to a
  blocking read, which parks on the empty
  stream buffer (gdb stack in
  `test-results/fd102-104/`). sendmsg on
  the same pair queues a real datagram and
  works. The host delivers a connected
  datagram sendto as one datagram. Not
  FD-106's claim — the dispatcher's buffer
  validation is independent of where the
  object's write path puts the bytes — so
  it is filed for its own lane.

- **FD-97 — The 64-bit single-call write paths
  fabricate zero bytes for an unreadable guest
  buffer instead of answering EFAULT. FIXED
  2026-11-02 on branch `lane-fd97` — see
  CL-56.** *(Was Low. Filed by the FD-94/96
  lane, 2026-11-02;
  provisional until the merge lane's audit.)*
  Found while wiring FD-94: KMemory64::
  memcpyFromGuest zero-fills its destination when
  the guest range fails its access check, and the
  single-call write paths copy the guest buffer
  through it before writing — sys_write64 and
  sys_pwrite64 (source/kernel/syscall64.cpp) hand
  the file whatever the copy produced, so a write
  from an unmapped or unreadable guest address
  lands that many zero bytes in the file and
  returns success. The host answers EFAULT and
  writes nothing (measured for the vectored form
  in FD-94's host reference programs: a bad first
  segment is EFAULT). FD-94's vectored paths work
  around the trait with per-segment range checks;
  the single-call paths keep it. The read side
  does not have the trait (its paths range-check
  the destination). Fix shape for the owning
  lane: range-check the guest buffer before the
  copy in the single-call write paths, or make
  the copy report failure and answer -EFAULT;
  either must keep the vectored paths' measured
  behaviour. Evidence: KMemory64::memcpyFromGuest
  in source/kernel/kmemory64.cpp and its call
  sites in sys_write64/sys_pwrite64 on this base.

  Fixed in commits `6d7c28d00` (test) and
  `c734fe89d` (fix) — see CL-56. The closing
  contract was refined by measurement before
  fixing (`test-results/fd97/hostref-fd97.c`,
  host kernel 6.17.0-1020-oracle): the host
  answers EFAULT only when no byte is
  readable; a readable prefix running into
  an unreadable page is written as a short
  count, so the fix writes the prefix rather
  than answering EFAULT for the whole call.
  Record:
  `docs/reports/fd97-single-call-write-efault-20261102.md`;
  evidence `test-results/fd97/`.



- **FD-94 — pwritev and pwritev2 are undispatched
  on the 64-bit dispatcher and absent from the
  32-bit syscall table. FIXED 2026-11-02 on
  branch `lane-fd94-96` — see CL-59.** *(Was Low.
  Filed by the
  FD-92/93 lane, 2026-11-02; the number follows
  FD-92/FD-93 as carried from lane-fd83-84 and is
  provisional until the merge lane's audit.)* Found
  while scoping FD-93's positioned-write family:
  the 64-bit dispatch carries pwritev (296) and
  pwritev2 (328) in its name table only — no
  X64_SYS define, no dispatch case — so both answer
  -ENOSYS, and the 32-bit table has no pwritev slot
  at all. The write-side twin of FD-92 (preadv was
  wired by FD-80/CL-39; the write side never was).
  For the wiring lane, measured on the host kernel
  (6.17.0-1020-oracle): pwritev on a bad descriptor
  with offset -1 answers EINVAL, the positioned
  family's offset-before-descriptor order
  (`test-results/fd92-93/hostref-off-order.c`).
  Evidence: the dispatch switch in
  `source/kernel/syscall64.cpp` and the table in
  `source/kernel/syscall.cpp` on this base.

  Fixed in commits `c060e0613` (tests; build
  corrections `f9a8fdeef` and `84e5feca7`) and
  `d528d792b` (fix) — see CL-59. Record:
  `docs/reports/fd94-fd96-pwritev-writev-20261102.md`;
  evidence `test-results/fd94-96/`.

  *Batch-3 merge note: this lane's test pinned
  pwritev's read-only answer as the house -EINVAL
  (section 9 of testPwritev64Syscalls), deferring the
  divergence to FD-93; with FD-93 composed in this
  batch, the merge decision recorded at FD-95 flipped
  that case to -EBADF, and sys_pwritev64 now answers it
  at the descriptor check.*

- **FD-95 — The read family's (bad descriptor,
  negative offset) combination answers EBADF where
  the host answers EINVAL.** *(Low. Filed by the
  FD-92/93 lane, 2026-11-02; provisional until the
  merge lane's audit.)* Measured on the host kernel
  (6.17.0-1020-oracle): pread, preadv and pwritev
  on a bad descriptor with offset -1 all answer
  EINVAL — the positioned family's offset check
  precedes even the descriptor check. FD-84's
  composed fix on lane-fd83-84 orders the
  descriptor check first in sys_pread64Impl and
  sys_preadv64, so that combination answers EBADF
  there; and the shared KProcess::pread64 carries
  no offset check in either shape (its dispatcher
  callers check the offset themselves, after the
  descriptor). The write side was ordered
  offset-first by FD-93; the read side's
  composition is the merge lane's call, since
  FD-84's fix is in flight. Evidence:
  `test-results/fd92-93/hostref-off-order.c` and
  its output; FD-84's entry and CL-50 on
  lane-fd83-84.

  **Settled and closed by the batch-3 merge audit
  (2026-11-02); no closure number consumed (the batch-2
  FD-102 precedent).** The order is settled for the whole
  positioned family from the host measurements on record
  -- FD-83/84's and FD-98's, taken on the box's 6.17
  kernel and agreeing: offset validity (EINVAL) first,
  except the v2 calls' offset -1, which is the
  file-position mode; then the descriptor answers (EBADF
  for a closed descriptor and for a wrong-mode one);
  then the iovcnt range (EINVAL); then the iovec array
  (EFAULT); and the v2 flags check last (EOPNOTSUPP,
  mask 0x3f). The shared positioned cores already held
  this order (sys_pread64Impl, sys_pwrite64,
  KProcess::pread64, KProcess::pwrite64), and the 32-bit
  vectored read already held it in KProcess::preadv;
  the merge aligned the three vectored wrappers that
  had deferred the mode answer to their per-segment
  walks -- sys_preadv64, sys_pwritev64 and
  KProcess::pwritev -- by placing the mode check with
  the descriptor check. The same decision aligns the
  positioned vectored write's wrong-mode answer with
  the write family's settled contract (FD-93): FD-94's
  pinned -EINVAL expectation for pwritev on a read-only
  descriptor (section 9 of testPwritev64Syscalls) is
  flipped to -EBADF. Proof: native suites serial, main
  675/675 and x64 83/83, 0 FAILED
  (test-results/merge-batch3-20261102/).


- **FD-96 — The 32-bit vectored write returns a
  later segment's error even after earlier segments
  wrote bytes; the host returns the bytes.
  FIXED 2026-11-02 on branch `lane-fd94-96` — see
  CL-60.** *(Was Low.
  Filed by the FD-92/93 lane, 2026-11-02;
  provisional until the merge lane's audit.)*
  Measured on the host kernel (6.17.0-1020-oracle):
  writev with a good first segment (3 bytes) and a
  bad second segment returns 3. The emulator's
  32-bit walk, KObject::writev, returns the failing
  segment's error result regardless of what was
  already written (it logs a kwarn for the partial
  case and still returns the error); the 64-bit
  walk (sys_writev64) returns the accumulated total
  in the same situation, as does the new 32-bit
  readv walk. FD-93's fix adds descriptor, mode,
  range and pointer validation ahead of this walk
  but does not change its return shape, and no test
  pins the partial-error case. Evidence:
  `test-results/fd92-93/hostref-write2.c` and its
  output; `KObject::writev` in
  `source/kernel/kobject.cpp` on this base.

  Fixed in commits `c060e0613` (tests) and
  `d528d792b` (fix) — see CL-60. Record:
  `docs/reports/fd94-fd96-pwritev-writev-20261102.md`;
  evidence `test-results/fd94-96/`.



- **FD-98 — The 32-bit dispatcher's preadv (333)
  and preadv2 (377) are null table slots and
  answer -ENOSYS; only the 64-bit preadv
  exists. FIXED 2026-11-02 on branch
  `lane-fd98` — see CL-61.** *(Was Low. Filed
  by the FD-98 lane,
  2026-11-02; the number follows FD-97 on
  lane-fd94-96's register and is provisional
  until the merge lane's audit.)* FD-94 wired
  the write-side twin (pwritev 334 / pwritev2
  378) and recorded these two slots as the read
  family's, deliberately untouched there. The
  host contract was measured fresh on the host
  kernel (6.17.0-1020-oracle) with dedicated
  reference programs
  (`test-results/fd98/hostref-preadv*.c`):
  segments fill in order at successive offsets
  and the file position does not move; a short
  read ends the walk; a later segment's failure
  returns the bytes already read; exactly one
  IN_ACCESS per successful call, 0-byte
  successes (at EOF, with iovcnt 0, with
  all-zero-length segments) included, and none
  on failure (FD-83's contract). Validation
  order, each step measured against its
  neighbours: a negative offset is EINVAL ahead
  of the descriptor check — except preadv2's
  offset -1, which is the file-position mode
  (read at the current position and advance
  it); a descriptor that cannot be read answers
  EBADF, ahead of the iovcnt range check, of
  the preadv2 flags check, and of an iovcnt-0
  success return; the iovcnt range check
  (EINVAL outside 0..1024) precedes the iovec
  array check (EFAULT for a null or unmapped
  array with iovcnt > 0), which precedes the
  flags check (EOPNOTSUPP for bits outside the
  defined RWF mask 0x3f — the read side accepts
  0x20/NOAPPEND on this kernel). Boundary
  inherited from the shared positioned core
  rather than created here: preadv on a pipe
  answers the core's -EINVAL where the host
  answers ESPIPE (measured in the same
  programs; 32-bit pread64 shares the core and
  the answer). The FD-95 interaction is
  recorded in the lane record: this wiring
  takes the host's offset-first side of
  FD-95's question. Evidence: the dispatch
  table in `source/kernel/syscall.cpp` on this
  base (slots 333/377 nullptr); the host
  reference programs and outputs in
  `test-results/fd98/`.

  Fixed in commits `8edaaaea2` (test) and
  `86dfb5830` (fix) — see CL-61. Record:
  `docs/reports/fd98-preadv32-20261102.md`;
  evidence `test-results/fd98/`.


- **FD-99 — The 64-bit dispatcher's preadv2
  refuses the file-position offset and one
  defined flag the host accepts. FIXED
  2026-11-02 on branch `lane-fd99-100` — see
  CL-62.** *(Low. Filed
  by the FD-98 lane, 2026-11-02; provisional
  until the merge lane's audit.)* Measured on
  the host kernel (6.17.0-1020-oracle) while
  pinning FD-98's contract
  (`test-results/fd98/hostref-preadv.c`):
  preadv2 with offset -1 reads at the current
  file position and advances it (the
  file-position mode, as pwritev2's -1 does
  since FD-94), and preadv2 accepts flag bit
  0x20 (RWF_NOAPPEND, defined on this kernel;
  the read side's defined mask is 0x3f). The
  composed sys_preadv64 (FD-80) answers EINVAL
  for any negative offset including -1 — its
  header comment records the -1 refusal as
  measured behaviour, which the fresh
  measurement contradicts for preadv2 (preadv
  itself does answer EINVAL at -1) — and
  answers EOPNOTSUPP for flags outside 0x1f.
  Both divergences are confined to the 64-bit
  dispatcher's preadv2 arm; FD-98's 32-bit
  wiring implements the measured behaviour.
  Fix shape for the owning lane: in
  sys_preadv64, treat preadv2's offset -1 as
  the file-position mode (walk at the current
  position and advance by the bytes read, as
  sys_pwritev64's -1 mode does) and widen the
  flags mask to 0x3f; extend
  testPreadv64Syscalls' matrix for both.

  Fixed on branch `lane-fd99-100`, commits
  `9e1b617c7` (tests) and `fad85c19c` (fix) —
  see CL-62. Proof and the FD-95 interaction
  are recorded there; the validation-order
  half of the restructure is FD-100's fix
  (CL-63).


- **FD-100 — The positioned vectored family's
  v2 flags check runs ahead of the iovcnt range
  and iovec array checks; the host runs it
  last. FIXED 2026-11-02 on branch
  `lane-fd99-100` — see CL-63.** *(Low. Filed by the FD-98 lane,
  2026-11-02; provisional until the merge
  lane's audit.)* Measured on the host kernel
  (6.17.0-1020-oracle) for both directions
  (`test-results/fd98/hostref-preadv-order2.c`,
  `hostref-pwritev-order.c`): with iovcnt -1
  and an undefined flag bit together, the host
  answers EINVAL (the range check first); with
  a null or unmapped iovec array and an
  undefined flag bit together, the host answers
  EFAULT (the array check first). The family's
  three wired implementations — sys_preadv64
  (FD-80), sys_pwritev64 and KProcess::pwritev
  (FD-94) — all test the flags before the
  iovcnt range and the array pointer, so those
  two combinations answer EOPNOTSUPP where the
  host answers EINVAL / EFAULT. Every other
  measured combination agrees. FD-98's 32-bit
  preadv wiring uses the measured order (flags
  last); the fix for these three is moving the
  flags test below the iovcnt and array checks
  in each.

  Fixed on branch `lane-fd99-100`, commits
  `9e1b617c7` (tests) and `fad85c19c` (fix) —
  see CL-63. The wrong-mode + range/array
  combinations characterized there belong to
  FD-84's / FD-93's composition.

- **FD-90 — The ALSA chain's mmap face delivers one
  buffer, then reports no space forever: the playback
  pointer the chain consumes never advances.** *(Medium —
  any ALSA client rendering through snd_pcm_mmap_begin /
  commit over the shipped plug → oss chain. Found
  2026-11-02 by the FD-85 continuation lane; numbered
  FD-90 provisionally — FD-85…FD-89 are taken on
  unmerged branches; the composing merge verifies by
  audit. FIXED on lane-fd90, awaiting merge —
  provisional CL-66.)* A native ALSA client using MMAP_INTERLEAVED
  access over the stock `default` PCM gets the full
  4,096-frame buffer from mmap_begin, and the first
  mmap_commit lands it at the emulator's audio input at
  full scale (`S16 input first non-zero peak=12000
  (n=8192)`; `first push 3250 frames peak=0.4550`); from
  then on `snd_pcm_avail_update` returns 0 permanently —
  eight fixed cycles, both S16_LE and Float32 — so no
  second buffer is ever offered. The position reporting
  underneath is incoherent in both access modes: the
  same client's writei run read `snd_pcm_delay` =
  **-190,464 frames** and avail **194,560** on a
  4,096-frame buffer. The emulator's OSS device reports
  GETOPTR as a write position modulo capacity and
  GETODELAY as a constant 0 under the bwPump bridge
  (`source/kernel/devs/devdsp.cpp`: ioctl64 GETOPTR,
  `getUsedBufferSize`), so the ALSA layer's hardware
  pointer has no playback motion to consume. Writei
  clients ignore position and play; mmap clients stall
  after one buffer. NOT the cause of FD-85 (proven
  there). A fix belongs to the OSS device's position
  contract — a played-frames counter sourced from the
  bwPump ring consumer is the shape the evidence
  supports — and must be proven against both access
  modes without reviving the waveOut poll stalls the
  GETODELAY fiction was originally introduced to avoid.
  Evidence: `test-results/fd85b/suite-fd85b-diag2/`
  (fresh-session mmap cycles + input lines),
  `test-results/fd85b/suite-fd85b-diag/` (writei
  position readings). Fixture:
  `tools/guest-fixtures/alsatone.c`.

  **Update 2026-11-02 (lane-fd90): FIXED — the OSS
  position face now reports the playback position.**
  Fix `dd00eee9b`: GETOPTR/GETODELAY derive from a
  played counter sourced from the bwPump ring
  consumer (bytesWritten minus the ring queue, the
  queue capped at capacity); SNDCTL_DSP_RESET no
  longer clobbers the stream geometry hw_params
  negotiated (the prepare-time RESET had shrunk the
  position modulus 16384 -> 8192 for the stream's
  whole life — the writei incoherence); and the
  reported position advances at most half a buffer
  per read, because the guest plugin's pointer
  callback consumes count_info.ptr alone (verified
  by disassembly of the shipped alsa-lib 1.2.8
  module), which aliases to no-motion when a
  non-paced sink drains a buffer or more between
  reads. GETOSPACE and the poll face are unchanged
  — the anti-stall surface is untouched — and the
  new delay reaches 0 exactly when the audible
  queue empties. Proof: suite test
  `alsa-mmap-stream` — RED on the base build with
  this entry's recorded signature (avail
  4096,0,0,0,0,0,0,0), GREEN on the fixed build
  (s16mmap 18,432 frames committed, floatmmap
  22,528; writei avail/delay coherent, summing to
  the 4,096-frame buffer). Provisional CL-66.
  Record: `docs/reports/fd90-oss-position-20261102.md`.

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

| `lane-fd58-60` | FD-58: chmod/fchmod/fchmodat apply the mode (stored on the node, reported by every stat writer, IN_ATTRIB on success; fchmodat NOFOLLOW on a symlink returns ENOTSUP as the host does); FD-59: IN_ACCESS generated for completed reads (mmap-write IN_MODIFY disproven against host references — the host generates none either); FD-60: link() creates a true hard link (host ::link between native paths, shared inode id, link group for counts and event fan-out); FD-79 and FD-80 filed | New test testChmodAccessHardLink in both suites: RED on the base (13 main / 18 x64 assertions failing), GREEN with the fix; host-kernel reference programs measured first (chmod/link/read/inotify matrices); full native suites serial: main 665/665 and x64 64/64, 0 FAILED; JS unit: the fs-dynamic-link-cache test that compiles the changed C++ passes standalone and in the runner (the lane tree is un-bootstrapped: 272 passed / 76 failed overall, every sampled failure a missing generated web/vendor file - the full JS run belongs to the merge gate). Record: `docs/reports/fd58-fd60-kernel-semantics-20261101.md`; evidence `test-results/fd58-60/` |
| `lane-fd77b` | FD-77 CONVICTED + FIXED: the silent freeze is an ABBA deadlock — msync held the mapped-files mutex across a writeback that demand-faults guest pages (memory mutex second), against the demand path's memory -> mapped-files order; the specified fault-stage instrument + mutex owner records were built and the first armed run parked into a capture that names both halves; fix snapshots the mapping and writes back after release | Fault-stage instrument `3ca9decf4` (citation audit 352 rows / 0 drift; instrumented build compiles, wasm 5ebee420); deterministic test testMsyncLockOrder RED on unfixed code -> GREEN with the fix `52ee264c7`; native serial main 666/666, x64 65/65; JS 348/348; fixed browser build compiles (wasm 646f85cc); consumer 6/6 mapped, 0 parks. Record: `docs/reports/fd77-msync-abba-20261101.md`; evidence `test-results/fd77b/` |
| `lane-fd72` | FD-75: the SPIR-V audit now refuses helper-invocation reads (BuiltIn HelperInvocation / OpIsHelperInvocationEXT) by name at vkCreateShaderModule (`spirv-builtin-helper-invocation`) instead of admitting modules whose draws the page tier would drop at pipeline translation; FD-72's boundary proven with primary evidence and its entry rewritten (stays OPEN on translator-stack terms); FD-74 filed (translator validator rejects fragment SampleMask builtins) | Native GUARD (both spellings refuse + counter, FragCoord neighbour admits, sabotage-verified); full native run green; JS unit at the integ baseline (347 passed, only the two dist-state audits failing on the unbuilt tree); legacy 110/4 standing set; citations zero drift; dist 'Browser export verified' with the support archive byte-identical; in vivo: the byte-identified Godot module refused at creation by name + hash (the run's only refusals), zero page-tier translation failures. Record: `docs/reports/fd72-helper-invocation-boundary-20261101.md` |
| `lane-fd81-linktmp` | FD-81: link() refused every temp-file-to-lock-name link in the browser build (the Emscripten backing stores have no hard-link operation; EMLINK on all of them - 57 per 32-bit boot on fontconfig cache locks, per the FD-58..60 deploy record) - now served at the emulator layer by sharing the source's backing path (one backing file, shared node id and link group; group-aware link counts, unlink and rename); native builds and symlink sources unchanged | New test testTempFileLinkPattern in both suites (the pattern end to end + a host-side backing census; sabotage-checked both ways); full native suites serial: main 666/666, x64 65/65, 0 FAILED; dist 'Browser export verified' (git_sha b0b3a70d3); in vivo boot32-vkboot PASS: 0 BW64-FAIL lines, 5 BW64-INFO hard-link lines on the deploy record's exact fontconfig files. Record: `docs/reports/fd81-temp-link-pattern-20261101.md`; evidence `test-results/fd81/` |
| `lane-fd63` | FD-63 CONVICTED + FIXED: the METRICS emission gate counted presents, Vulkan frame completions, GL and audio calls as activity but not syscall dispatch, so a session that stopped presenting had every later window discarded unreported by the no-activity branch — the panel froze at the last present-driven snapshot while the session executed on; the gate now counts syscall dispatch as activity | New section of the native metrics selftest drives six successive syscall-only intervals against the real registry: RED on unfixed code (24 PASS, exactly the two FD-63 assertions FAIL) -> GREEN with the fix `6d711e6a6`; writer fixture reproduced byte-identically modulo clock fields (committed fixture unchanged); reader contract + panel tests 21/21; browser build 'Browser export verified' / BUILD COMPLETE, wasm 1809b7d7, support c4dbe35986a5 unchanged. Record: `docs/reports/fd63-metrics-emission-20261101.md`; evidence `test-results/fd63/` |
| `lane-fd79-80` | FD-79: a file description's close event (IN_CLOSE_WRITE / IN_CLOSE_NOWRITE) is delivered at the host's moment — held while any mapping of the description is live, delivered at the last unmap (the emulator lost it in-session: destructor-driven close, pinned by the mapping entries and the system file cache); FD-80: the 64-bit dispatcher's preadv (295) and preadv2 (327) wired over pread64's factored core with the host's validation and one-IN_ACCESS-per-success shape; FD-82 (64-bit materialized mappings outside the deferral), FD-83 (readv silent on 0-byte success) and FD-84 (wrong-mode reads answer EINVAL, host EBADF) filed | New tests testCloseWriteDeferral (both suites; RED on the base — five arrival assertions failing; the first fix version missed write-fault CopyOnWrite pages and the test caught it) and testPreadv64Syscalls (x64 suite; RED on the base dispatch — 'unimplemented syscall #295/#327', -ENOSYS); host-kernel reference programs measured first (close-event matrix, preadv/preadv2 validation and notification matrices); full native suites serial: main 668/668 and x64 68/68, 0 FAILED; browser build compiles on the fixed tree (see the lane record). Record: `docs/reports/fd79-fd80-kernel-semantics-20261101.md`; evidence `test-results/fd79-80/` |
| `lane-fd66` | FD-66: `setupReadbacks` discarded still-pending readback plans without freeing their heap blocks (latent — every production submit pairs setup with completion, so the discard was reachable only from the harness's planning sections), and the harness's sanitizer flavour reported the fixture's abandoned blocks so it could never finish clean | RED on the base reproduced the filed measurement exactly (LeakSanitizer 1,132 bytes in 5 allocations from setupReadbacks, exit 1); GREEN fixed: sanitizer flavour 57 PASS, exit 0, zero leak reports, via the standard runner too (its sanitizer run budget raised to 180 s); sabotage (prologue alone reverted) reopens exactly 328 bytes in 2 allocations; native serial 667/667 + 66/66; JS 348/0; citations 352/0 after re-pins; browser build BUILD COMPLETE (wasm df27c5e0…). Record: `docs/reports/fd66-readback-disposal-20261101.md`; evidence `test-results/fd66/` |
| `lane-fd65` | FD-65: Gecko's "unable to find a usable font" — two layers: the 64-bit rootfs carried no font faces, and the shipped 64-bit win32u.so was built without freetype/fontconfig so GDI could enumerate and realize no font at all; faces staged (fonts64) and win32u rebuilt with its font engine (the build now fails loudly if either SONAME is absent); FD-85 + FD-86 filed | Guest step probe x64: EnumFontFamiliesEx 0 → 85 families, GetTextMetrics fail → tmHeight 17 (the 32-bit stack's numbers); the real common-dependencies Gecko stage green in two runs (MSHTML: RESULT 0, zero abort lines, test untouched — its remaining audio-capture failure is FD-85); completeness 0 failures; JS 348/0; dist 'Browser export verified'. Record: `docs/reports/fd65-gecko-fonts-20261102.md` |
| `lane-fd85` | FD-85: the audio-capture failure convicted as two defects — the test's capture watched SDL's bypassed ScriptProcessor node while guest audio flows through the bwPump AudioWorklet (FD-87: fixed, closed CL-42), and beneath it Wine's mmdevapi → winealsa → ALSA leg delivers silence to the emulator's audio input for every Windows audio client tried (FD-85 stays open, break point proven by measurement); wasapi-tone + winmm-tone fixtures added | Fixed capture hears the direct-DSP control tone at peak 0.3662 where the shipped capture counted 0 non-zero in 266,240 samples; shipped test post-fix still fails at its audio wait (Gecko green, `OPENAL: RESULT 0`, first push peak 0.0000, no S16 non-zero line); JS 348/0; dist 'Browser export verified' (wasm byte-identical to shipped). Record: `docs/reports/fd85-audio-leg-20261102.md` |
| `lane-fd77-stall-surface` | FD-77 remainder: a default session whose boot completes the desktop handshake but never maps a window and stops executing is now surfaced as a reported failure (the launcher takes its own thread dumps and convicts on the dump's measured zero-progress, two consecutive dumps, no fetch in flight); the held dump evidence is flushed to the preserved session log ahead of one BW64-FAIL line; slow-but-alive starts, armed diagnostic sessions, and every readiness rule are untouched | Unit tests over the shipped watchdog section: 8/8 green, 8/8 red on the unpatched launcher; shaped-session browser probe on the built dist: the park shape reports the failure with evidence in the log, the slow-but-alive and pre-handshake shapes raise no alarm; launcher UI regression green. Record: `docs/reports/fd77-stall-surfacing-20261101.md`; evidence `test-results/fd77-stall-surfacing/` |
| `lane-fd87` | FD-89 CLOSED (CL-43): the Godot second-wave sibling classes — capability 29 (SampledImageArrayDynamicIndexing) named and admitted exactly when its image arrays are specializer-servable (spirvfix R19 strips the stray declaration), dead descriptor declarations stripped (R18) so the captured 2,340 B module translates end to end, and FD-78's ExtInst literal position bounded in both orphan sweeps so the 369,400 B module's texel conversion runs and SampledBuffer is served; FD-88 filed (data-dependent descriptor-array indexing, translator-stack-gated); FD-78 updated (ExtInst position proven live, fixed at two sweeps) | Both modules are committed fixtures (`testdata/spirv/godot-fs-f2b738c259a79b4a.spv`, `godot-fs-f1b4e0027d98e2e4.spv`): new suite `spirvfix-fd87.test.mjs` 5/5 (small module translates; big module loses SampledBuffer, keeps FD-88's pair by name); corpus test green over all 74 modules with per-module rewrite sets re-measured (every R18 verdict independently verified dead by raw-word scan); native run-native 60 PASS incl. 3 new FD-87 guards (cap-29 admit/refuse-by-name both forms, dead descriptors, ExtInst sweep position); JS unit suite 351/0; vulkan citations 352 rows 0 drift after re-pinning through the change's line map; browser build 'Browser export verified', wasm `f66e592c`. The two pipelines' in-vivo creations remain CL-42's owed clause for the carrying deploy gate. Record: `docs/reports/fd87-godot-sibling-classes-20261102.md`; evidence `test-results/fd87/` |
| `lane-fd05` | FD-05: filling the file-descriptor table panicked and killed the emulator; exhaustion now answers EMFILE (EINVAL for an F_DUPFD minimum at/past the ceiling, EBADF for a dup2 target at/past it) and the guest survives — the allocation contract changed at its single choke point (`allocFileDescriptor`), all 22 production callers audited and made safe, SCM_RIGHTS partial delivery and accept requeue semantics preserved | New test testFdTableExhaustion in both suites: RED on the base (the fill hit the panic; the forked harness recorded the test as each suite's single failure, and un-forked the suite process died at the test), GREEN with the fix; native serial main 669/669 and x64 70/70, 0 FAILED; browser build BUILD COMPLETE on the fixed tree (wasm ea3bdaa0), JS unit 348/0, legacy 110/4 (FD-64's standing set on this base). Record: `docs/reports/fd05-fd-table-exhaustion-20261102.md`; evidence `test-results/fd05/` |
| `lane-fd92` | FD-92: spirvfix spliced its synthesized pointer-to-bool types ahead of the OpTypeBool they reference in the captured 369,400-byte Godot module (the module defines that type after its first global variable), so the fixed module failed spirv-val; synthesized types/constants are now spliced behind the definitions they reference (`defsSplicePos`, at the three intra-type-section splice sites) | spirv-val on the captured module's fixed output: clean (the base output fails with the filed error verbatim); the fixed output is a pure reordering of the base output (instruction word-multiset identical, 18,352 instructions); 72-module corpus: every output byte-identical to the base, applied sets unchanged, corpus gate 10/10; new guard spirvfix-fd92 RED on the base / GREEN with the fix. Record: `docs/reports/fd92-spirvfix-splice-ordering-20261102.md`; evidence `test-results/fd92/` |
| `lane-fd82` | FD-82: a 64-bit guest's file-backed mappings did not participate in FD-79's close-event deferral -- the 64-bit path materializes mappings as copied/shared pages in KMemory64 with no open-description accounting, so IN_CLOSE_WRITE / IN_CLOSE_NOWRITE fired at fd close while such a mapping was live; KMemory64 now tables its live file mappings per description and reports births and deaths (munmap, overwrite, mremap move/grow, fork, address-space destruction) through the same FsOpenNode accounting the demand-paged path uses | New test testCloseWriteDeferral64 (x64 suite; drives the real 64-bit syscall dispatch): RED on the base -- ten assertions failing (the event fired at fd close in every deferral case and never arrived at the mapping's death), GREEN with the fix; full native suites serial: main 668/668, x64 70/70, 0 FAILED; JS unit 348 passed / 0 failed; browser build 'Browser export verified' / BUILD COMPLETE (wasm 551ec728, support archive c4dbe35986a5 unchanged). Record: `docs/reports/fd82-materialized-mmap-close-deferral-20261102.md`; evidence `test-results/fd82/` |
| `lane-fd83-84` | FD-83: readv generates exactly one IN_ACCESS per successful call, 0-byte successes included (the walk's per-segment notifications suppressed under a KInotify scope, one event raised on success; the walk gains its own fd/mode/iovcnt/iovec validation in the host's order); FD-84: the positioned/plain read family answers EBADF on a descriptor that cannot be read, in the host's validation order (negative offset EINVAL first in the positioned core; preadv's mode answer ahead of its flag and iovcnt checks; KProcess::pread64 gains the mode check it lacked — it had fallen through to the backing read, whose host EBADF the backing layer swallowed into a silent 0); FD-103 (32-bit readv is a null dispatch slot) and FD-93 (the write family's mirror of FD-84, host-measured) filed | Extensions to testPreadv64Syscalls (x64 suite; RED on the base — 20 assertion failures across the new sections, spanning both defects) and testChmodAccessHardLink (both suites; RED on the base — the three FD-84 KProcess assertions); host-kernel reference programs measured first (readv event matrix incl. multi-segment, partial-success and zero-length shapes; wrong-mode matrices for the read and write families incl. zero counts and the validation orderings); full native suites serial: main 668/668 and x64 69/69, 0 FAILED; browser build on the fixed tree: BUILD COMPLETE, 'Browser export verified' (wasm `2601ef69`); JS unit 348/0 (silent-failure baseline for syscall64.cpp reviewed 115 -> 116), legacy 110/4 (FD-64's standing set on this base, unchanged). Record: `docs/reports/fd83-fd84-read-family-20261102.md`; evidence `test-results/fd83-84/` |

| `lane-fd92mmap` | FD-104: sys_mmap64_file establishes a 64-bit file mapping (or the addr==0 reservation) before copying the file content in; when that content read failed, the syscall returned the error but left the pages mapped and the range reserved, where a failed host mmap leaves nothing mapped -- both content-read error returns (the private branch's copy loop, and the shared branch's seed read after an addr==0 reservation) now tear the established range down through KMemory64::munmap before returning the error | New test testMmap64FileReadFailure in the x64 native suite, driving the real 64-bit syscall dispatch against an open description whose positioned reads fail on command (immediately, partway through the copy, and as the shared seed read; a never-failing control maps and serves its content): RED on the base (the four failure cases each left their range mapped), GREEN with the fix; full native suites serial: main 668/668, x64 70/70, 0 FAILED; JS unit 348 passed / 0 failed; browser build 'Browser export verified' / BUILD COMPLETE (build git_sha e37fa3c04, wasm 16abfc4c, support archive c4dbe35986a5 unchanged). Record: `docs/reports/fd92-failed-mmap64-teardown-20261102.md`; evidence `test-results/fd92mmap/` |
| `lane-fd101` | FD-101: the 64-bit sendmsg gather (sys_sendmsg64) fabricates zero bytes for unreadable guest segment memory — now the whole message import is validated in the host's order (descriptor answers first, then msghdr/name/iov array/control/segments) and answers -EFAULT with nothing sent, -EMSGSIZE for an iovlen over 1024, sendmmsg inheriting through the loop; FD-105 (the SCM_RIGHTS translation trusts the guest's cmsg_len), FD-106 (sendto's single-call form of the trait) and FD-107 (recvmsg's silent scatter drop) filed | New test testSendmsgBadBuffer64Syscalls (x64 suite; RED on the base — crossing sendmsg returned 200 with 200 bytes held by the peer, good+bad segments returned 24, unmapped iov array returned 0, sendmmsg good,bad returned 2; GREEN on the fix); host reference programs measured first (sendmsg/sendmmsg all-or-nothing matrix incl. crossing, message-boundary and descriptor-order cases; sendto and recvmsg companions); full native suites serial: main 668/668 and x64 70/70, 0 FAILED; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = the closing commit 4363174fa); JS unit 348/0 (the socket64-message slice mock extended for the validation's descriptor/page-flag surface). Record: `docs/reports/fd101-sendmsg-efault-20261102.md`; evidence `test-results/fd101/` |
| `lane-fd102-104` | FD-105: the SCM_RIGHTS translation trusted the guest's cmsg_len — now an under-header or past-controllen length and a count over the host's 253 cap answer -EINVAL before anything is sent, and accepted messages translate into a new extension area past the 64 KB scratch message so 253 records no longer overwrite the gathered payload; FD-106: sendto's dispatcher branch validates its buffer (descriptor answers first, then the clamped range readable) and answers -EFAULT with nothing sent; FD-107: sys_recvmsg64 validates its import in the host's order (descriptor answers, readable msghdr, -EMSGSIZE over 1024 segments, readable iov array) and its scatter validates each range page-piece by page-piece as it copies, answering -EFAULT at the first unwritable range with the prefix delivered; FD-111 (recvfrom's silent scatter), FD-112 (the control chain past the first header), FD-113 (the recvmsg name copyback) and FD-114 (datagram sendto reported sent but never delivered) filed | New x64 tests testSendmsgCmsgLen64Syscalls, testSendtoBadBuffer64Syscalls, testRecvmsgBadBuffer64Syscalls (RED on the base — past-controllen cmsg_len values sent their descriptors, the 253-record translation corrupted the payload in scratch, an under-header length killed its case, sendto delivered 16 fabricated bytes from an unmapped buffer and 200 from a crossing one, recvmsg reported 100 delivered into a half-unwritable scatter and recvmmsg reported 2 for a good+bad pair; GREEN on the fix); host reference programs measured first (cmsg_len shapes incl. the 253/254 cap and the chain-slack shape, recvmsg partial/crossing/peek/iovlen shapes); full native suites serial: main 668/668 and x64 73/73, 0 FAILED; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 9b6c993d9, wasm 4180f7cd); JS unit 348/0 (socket64-message slice mock extended; reviewed silent-failure allowance 115 -> 117). Record: `docs/reports/fd102-fd104-message-faults-20261102.md`; evidence `test-results/fd102-104/` |
| `lane-fd97` | FD-97: the 64-bit single-call write paths (sys_write64/sys_pwrite64) fabricate zero bytes for an unreadable guest buffer — now write only the guest-readable prefix and answer -EFAULT when no byte is readable, under the host's measured prefix contract; FD-101 (the sendmsg gather form of the trait) filed | New test testWriteBadBuffer64Syscalls (x64 suite; RED on the base — write from an unmapped buffer returned 16 with 16 zero bytes landed, crossing write returned 200, writev with a bad second segment returned 24; GREEN on the fix); host reference programs measured first (write/pwrite bad-buffer matrix incl. the short-count prefix cases; sendmsg matrix for FD-101); full native suites serial: main 668/668 and x64 70/70, 0 FAILED; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = the fixing commit); JS unit 348/0. Record: `docs/reports/fd97-single-call-write-efault-20261102.md`; evidence `test-results/fd97/` |

| `lane-fd92-93` | FD-103: the 32-bit dispatcher's readv (145) wired over a new KObject/KProcess readv — host-order validation (descriptor, mode, iovcnt range, iovec pointer) and FD-83's contract of exactly one IN_ACCESS per successful call, 0-byte successes included; FD-93: the write family answers EBADF on a descriptor that cannot be written, in the host's validation order, at every site the entry names (KProcess::write/writev/pwrite64 — the last gaining the mode check whose absence let the backing layer swallow the host's EBADF into a silent 0 — sys_write64's descriptor+mode gate ahead of its zero-count return, sys_writev64's validation block, sys_pwrite64 reordered offset-first, the 32-bit dispatcher's pwrite64 offset check); FD-94 (pwritev/pwritev2 undispatched), FD-95 (read family's bad-descriptor/negative-offset order) and FD-96 (32-bit writev's partial-error return shape) filed | New tests testReadv32Syscall (main suite; real int 0x80 dispatch — RED on the base, every call -ENOSYS), testWriteFamilyWrongMode (both suites) and testWriteFamily64Syscalls (x64 suite), all RED on the base for the recorded reasons and GREEN with the fix; host-kernel reference programs measured first (write-family matrix incl. zero counts and the validation orderings; the positioned family's offset-before-descriptor order across pread/pwrite/preadv/pwritev); full native suites serial: main 670/670 and x64 71/71, 0 FAILED; browser build on the fixed tree: BUILD COMPLETE, 'Browser export verified' (wasm `bcc8a852`); JS unit 348/0 (silent-failure baseline for syscall64.cpp reviewed 115 -> 116), legacy 110/4 (FD-64's standing set on this base, unchanged). Record: `docs/reports/fd92-fd93-readv-write-family-20261102.md`; evidence `test-results/fd92-93/` |
| `lane-fd98` | FD-98: the 32-bit dispatcher's preadv (333) and preadv2 (377) wired over a new KProcess::preadv walking pread64 — host-order validation measured fresh (offset EINVAL ahead of the descriptor check, except preadv2's -1 file-position mode; wrong-mode EBADF ahead of the iovcnt range check; the iovec array EFAULT ahead of the flags check, which runs last with the measured read mask 0x3f) and FD-83's contract of exactly one IN_ACCESS per successful call, 0-byte successes included; FD-99 (64-bit preadv2's -1 refusal and 0x1f mask) and FD-100 (the family's flags-check order) filed | New test testPreadv32Dispatch (main suite; real int 0x80 dispatch — RED on the base, every call -ENOSYS, GREEN with the fix); host-kernel reference programs measured first (preadv/preadv2 validation, position-mode, flags-mask and IN_ACCESS matrices, plus the pwritev2 order addendum); full native suites serial: main 671/671 and x64 70/70, 0 FAILED; browser build 'Browser export verified' (git_sha 86dfb5830, wasm 5c0d85fc, support archive c4dbe35986a5 unchanged); JS unit 348/0. Record: `docs/reports/fd98-preadv32-20261102.md`; evidence `test-results/fd98/` |
| `lane-fd99-100` | FD-99: sys_preadv64 takes preadv2's offset -1 as the file-position mode and the full defined read flag mask 0x3f, with the host's validation order throughout (offset validity ahead of the descriptor check; the iovec array check now a range check) -- FD-95 interaction recorded in CL-62. FD-100: the v2 flags check runs last, below the iovcnt range and iovec array checks, in sys_preadv64, sys_pwritev64 and KProcess::pwritev; the wrong-mode + range/array combinations characterized for FD-84's/FD-93's composition | Extended testPreadv64Syscalls sections 10-11 and testPwritev64Syscalls section 11 (x64 suite) and testPwritev32Dispatch section 2b (main suite) -- RED on the base with the filed signatures, GREEN with the fix; full native suites serial: main 671/671 and x64 70/70, 0 FAILED; browser build 'Browser export verified' (git_sha fad85c19c, wasm 52da72d5, support archive c4dbe35986a5 unchanged); JS unit 348/0. Record: `docs/reports/fd99-fd100-preadv2-family-order-20261102.md`; evidence `test-results/fd99-100/` |
| `lane-fd86` | FD-86: no Arial family on the 64-bit stack — prefix64 carried no Fonts\Replacements entry, the mechanism the 32-bit stack serves Arial through; canonical-set staging step `stage-font-alias64.py` (the 32-bit edit imported, so the stacks cannot drift) bakes `"Arial"="DejaVu Sans"` into prefix64, checker drift coverage added, win32-fonts suite test extended to x64, payload re-pinned as `runtime-assets-20261103` | win32-fonts PASS both architectures (x64 `ARIAL=0` — `ARIAL=1 METRICS=1 RESULT 0`; x86 unregressed); checker RED on this gap alone pre-fix, green after; bootstrap --check 0 failures; clean --local bootstrap from the new archive reproduces the replacement; Gecko stage `MSHTML: RESULT 0`, zero abort lines (long-window evidence run); JS 348/0; wasm unchanged. Record: `docs/reports/fd86-arial-prefix64-20261103.md` |

| `lane-fd23res` | FD-23 CLOSED (residue): `select` (23) implemented through the core factored out of `pselect6` (with the host's pre-checks, which also correct `pselect6`); `pidfd_getfd` (438) in the host's check order; `futex_waitv` (449) on both ABIs over the futex machinery (i386 table extended to 462 slots); per-number dispositions for every unimplemented number on both ABIs (404 entries, census-audited); FD-109 and FD-110 filed | Host-kernel reference programs measured first (select/pidfd/futex_waitv matrices; evidence `test-results/fd23res/hostref/`); new tests fail on the base for the recorded reasons and pass with the fix: x64 suite 72 tests, main suite 669 tests, 0 FAILED serial, RED runs show exactly the new tests failing (-ENOSYS); census: x86-64 implemented 199 -> 202, stubbed {334} only, i386 gap fully dispositioned (undispositioned 0, stale 0); browser build 'Browser export verified' (wasm c290559e). Record: `docs/reports/fd23-residue-20261102.md`; evidence `test-results/fd23res/` |
| `lane-fd85b` | FD-85 continuation: the remaining silence sub-split measured — the ALSA leg serves writei clients at full scale (new alsatone fixture, four format/access modes), winealsa hands libasound an unbroken stream of zeros on every device including libasound's null (file-plugin dump, validated against the native client in-session), the traced client side is healthy (single AudioClient, 26 GetBuffer / 24 ReleaseBuffer of the full buffer), and the identical fixture on the 32-bit Wine stack is heard through the same emulator and chain — the defect is convicted to the 64-bit Wine payload's mmdevapi → winealsa render path (FD-85 stays open on payload-fix terms); FD-90 filed (the chain's mmap face stalls after one buffer: the OSS playback pointer never advances) | Six preserved diag sessions (`test-results/fd85b/`): writei tone at the input (S16 peak 12000) and page (peak 0.4550); Wine dumps 72,192 / 100,224 / 33,792 Float32 frames, 0 non-zero, three runs; x86 OpenAL heard (26,763 non-zero, peak 0.2269); mmap cycles: first commit lands full-scale (n=8192), avail 0 thereafter; dist 'Browser export verified' on the base build (this lane changes no emulator code). Record: `docs/reports/fd85b-audio-bisection-20261102.md` |
| `lane-fd90` | FD-90: the OSS position face reported the write position and a constant 0 delay, so the ALSA mmap face stalled after one buffer and position readings were incoherent — GETOPTR/GETODELAY now derive from a played counter sourced from the bwPump ring consumer; SNDCTL_DSP_RESET no longer clobbers the hw_params-negotiated stream geometry at prepare; the reported position is slew-limited to half a buffer per read (the guest plugin's pointer callback consumes count_info.ptr alone, which aliases when a non-paced sink drains a buffer between reads) | Suite test alsa-mmap-stream: RED on the base build with the recorded signature (avail 4096,0,0,0,0,0,0,0), GREEN on the fixed build (s16mmap 18,432 frames / floatmmap 22,528 committed across the cycles; writei avail+delay coherent, summing to 4096); audio-tone PASS; common-dependencies identical to a base-tip control build under the same box load; native serial main 0 FAILED, x64 0 FAILED; legacy JS only the standing FD-64 four. Record: `docs/reports/fd90-oss-position-20261102.md`; evidence `test-results/fd90/` |
| `lane-fd88` | FD-88 NARROWED to its proven blocker (no code change): the class is re-gated from 'translation stack' to the serving browser — the vendored translator serves the class exactly, and the project's Chromium (145) cannot populate an arrayed texture binding at any API surface; both mirrors' by-name refusals stay as the correct fail-closed behaviour, with the in-project closing terms specified for when the browser link lands. FD-92 filed (the fixup's output for the captured module is not spirv-val clean — synthesized pointer types spliced ahead of OpTypeBool) | Minimal fixtures in the captured module's own form (committed as `testdata/spirv/fd88-*`): a translator matrix over eight array/index/capability variants proves the cap-29 DECLARATION is the only translator tripwire, and the data-dependent runtime-array form translates with its index expression preserved. Captured module: declaration stripped + the FD-92 ordering repaired → the translator accepts the whole type/global section; its next refusal is an OpIAddCarry that cone analysis proves sits outside all seven array chains. Browser probes (probe-slot, the project's Chromium): sized binding-array modules compile; layout counts are accepted but not honoured ('only provides 1 elements'); the createBindGroup array resource throws at WebIDL; duplicate entries are rejected. Record: `docs/reports/fd88-binding-array-blocker-20261102.md`; evidence `test-results/fd88/` |

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

- **CL-52 — The 64-bit message-sending
  gather answers for unreadable guest
  memory: sys_sendmsg64 validates the whole
  import in the host's order and answers
  EFAULT with nothing sent when any byte of
  the msghdr, name, iov array, control
  header, rights array or segment data is
  unreadable, EMSGSIZE when the iov count
  is over the host's limit, and the
  descriptor answers (EBADF, ENOTSOCK)
  before every copy fault (FD-101).**
  *(Provisional number, filed on lane-fd101,
  2026-11-02 — CL-40..CL-51 are held by
  unmerged lanes off this same base (fd66,
  fd65, fd85, fd87, fd64, fd05, fd82, fd78,
  fd83-84, fd92, fd92mmap, fd92-93, fd94-96,
  fd90, fd97); the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd101`, commits `7eed978a9`
  (test), `f70b16116` (fix) and `4363174fa`
  (slice-test surface). The validation runs
  ahead of the gather in the host's import
  order, using the readable-prefix helper
  FD-97 introduced (defined beside its
  caller on this branch; the two branches'
  copies are identical, and the merge
  keeps one): a segment must be readable
  over its whole contributed range, because
  the host's sendmsg is all-or-nothing —
  measured first
  (`test-results/fd101/hostref-sendmsg-fd101.c`,
  host kernel 6.17.0-1020-oracle): any
  unreadable byte in any nonzero segment
  answers -EFAULT with nothing delivered,
  including a segment crossing from a
  readable page into an unmapped one (no
  short-count prefix, unlike the write
  family); zero-length segments are skipped
  even with unreadable bases; sendmmsg
  sends the messages before a bad one and
  reports their count. Proof: new test
  testSendmsgBadBuffer64Syscalls (x64
  suite) RED on the base with the filed
  signatures and GREEN on the fix; full
  native suites serial — main 668/668, x64
  70/70; browser build verified (BUILD_INFO
  git_sha = `4363174fa`); JS unit 348/0.
  Filed from this lane: FD-102, FD-103,
  FD-104. Record:
  `docs/reports/fd101-sendmsg-efault-20261102.md`.

- **CL-53 — The 64-bit sendmsg SCM_RIGHTS
  translation validates the guest's
  cmsg_len before deriving the descriptor
  count: under the header size or past
  msg_controllen answers -EINVAL, as does
  a count over the host's 253 cap, and
  accepted messages translate into an
  extension area past the 64 KB scratch
  message instead of the 448-byte area
  whose overrun overwrote the gathered
  payload (FD-102).** *(Provisional
  number, filed on lane-fd102-104,
  2026-11-02 — CL-40..CL-52 are held by
  unmerged lanes off this same base; the
  merge lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd102-104`, commits `dcd1e3a8a`
  (test), `9fb96348d` (fix) and
  `9b6c993d9` (slice surface). Measured
  first
  (`test-results/fd102-104/hostref-cmsglen-fd102.c`,
  host kernel 6.17.0-1020-oracle): lengths
  0/8/12/15 answer -EINVAL, a length past
  controllen answers -EINVAL, unaligned
  lengths truncate the count (18 -> 0,
  22 -> 1), 253 descriptors land, 254
  answers -EINVAL, and the length
  validation precedes any data fault. The
  receive side's existing 28-record
  translation bound is unchanged — a
  253-descriptor message arrives as 28
  descriptors with MSG_CTRUNC, pinned by
  the new test. Proof: new test
  testSendmsgCmsgLen64Syscalls (x64
  suite) RED on the base (past-controllen
  lengths sent their descriptors, the
  253-record translation corrupted the
  payload it shared the scratch with, an
  under-header length escaped the scratch
  and killed its case) and GREEN on the
  fix; full native suites serial — main
  668/668, x64 73/73; browser build
  verified (BUILD_INFO git_sha =
  `9b6c993d9`, wasm 4180f7cd); JS unit
  348/0. Record:
  `docs/reports/fd102-fd104-message-faults-20261102.md`.

- **CL-54 — The 64-bit sendto answers
  for unreadable guest memory: the
  dispatcher branch runs the descriptor
  answers (-EBADF, -ENOTSOCK) first,
  then requires exactly the clamped
  range it is about to copy to be
  readable, else -EFAULT with nothing
  sent (FD-103).** *(Provisional number,
  filed on lane-fd102-104, 2026-11-02 —
  CL-40..CL-52 are held by unmerged
  lanes off this same base; the merge
  lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd102-104`, commits `dcd1e3a8a`
  (test), `9fb96348d` (fix) and
  `9b6c993d9` (slice surface). The host
  contract is the FD-101 lane's
  measurement
  (`test-results/fd101/hostref-sendto-fd101.c`):
  unmapped and crossing buffers answer
  -EFAULT with nothing delivered, stream
  and datagram alike, and the descriptor
  answer precedes the fault. The existing
  giant-message clamp applies before the
  validation, so the pre-existing
  truncation of oversized sends is
  preserved. (The filed entry names a
  sys_sendto64 function; the code is the
  dispatcher's sendto/recvfrom case
  block, and the fix lives there.)
  Proof: new test
  testSendtoBadBuffer64Syscalls RED on
  the base (an unmapped stream buffer
  delivered 16 fabricated bytes, a
  crossing buffer delivered 200, a
  datagram reported 16 sent) and GREEN
  on the fix; suites and build as CL-53.
  Record:
  `docs/reports/fd102-fd104-message-faults-20261102.md`.

- **CL-55 — The 64-bit recvmsg scatter
  never silently drops received bytes:
  the import is validated in the host's
  order (descriptor answers, readable
  msghdr, -EMSGSIZE for an iovlen over
  1024, readable iov array), and the
  scatter validates each segment
  page-piece by page-piece as it copies,
  answering -EFAULT at the first
  unwritable range with everything
  before it delivered (FD-104).**
  *(Provisional number, filed on
  lane-fd102-104, 2026-11-02 — CL-40..CL-52
  are held by unmerged lanes off this
  same base; the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd102-104`, commits
  `dcd1e3a8a` (test), `9fb96348d` (fix)
  and `9b6c993d9` (slice surface).
  Measured first
  (`test-results/fd102-104/hostref-recvmsg2-fd104.c`,
  host kernel 6.17.0-1020-oracle,
  extending the FD-101 lane's companion):
  an unwritable buffer answers -EFAULT;
  a bad range the received bytes never
  reach does not fault (a 40-byte
  datagram into [good 50][bad 50]
  succeeds); MSG_PEEK faults the same
  way and consumes nothing; iovlen 1025
  answers -EMSGSIZE; an unreadable
  msghdr or iov array answers -EFAULT.
  Validating the ranges before the
  receive (the filing's suggested shape)
  is wrong against that measurement —
  the host faults only on ranges the
  bytes actually reach — so the scatter
  validates as it copies, with the
  received count known. Two boundaries
  are settled, not papered over: on
  stream sockets the host leaves the
  data queued after a faulted call, but
  the socket object consumes into
  scratch with no pushback and no peek,
  so the -EFAULT answer and the
  delivered prefix match while the
  queue's survival diverges (datagram
  consumption matches exactly); and a
  receive buffer on a page the guest
  mapped read-only from birth is a legal
  write target under this memory model
  (commit-on-write — only unmapped
  pages and guest-mprotect restrictions
  are denied), the same predicate
  FD-97's write family settled on. The
  new test pins both boundaries. Proof:
  new test testRecvmsgBadBuffer64Syscalls
  RED on the base (100 reported
  delivered into a half-unwritable
  scatter, a crossing segment reported
  200, recvmmsg reported 2 for a
  good+bad pair, iovlen 1025 not
  -EMSGSIZE) and GREEN on the fix;
  suites and build as CL-53. Filed from
  this lane: FD-105, FD-106, FD-107,
  FD-108. Record:
  `docs/reports/fd102-fd104-message-faults-20261102.md`.
- **CL-56 — The 64-bit single-call write
  paths answer for unreadable guest buffers:
  only the guest-readable prefix is written,
  and a fully unreadable buffer answers
  EFAULT (FD-97).** *(Provisional number,
  filed on lane-fd97, 2026-11-02 — CL-40..
  CL-50 are held by unmerged lanes off this
  same base (fd66, fd65, fd85, fd87, fd64,
  fd05, fd82, fd78, fd83-84, fd92, fd92mmap,
  fd92-93, fd94-96, fd90); the merge lane's
  audit settles the final number.)* Fixed
  on branch `lane-fd97`, commits `6d7c28d00`
  (test) and `c734fe89d` (fix). A new helper
  (sys_guestReadablePrefix64) counts the
  leading readable bytes of the buffer page
  by page under the same predicate the
  copy's own access check applies;
  sys_write64 writes only the prefix and
  returns its EFAULT at the write point,
  after the descriptor and mode answers (the
  host's measured order: a bad fd with a bad
  buffer answers EBADF); sys_pwrite64 does
  the same after its own answers; writev
  inherits the shape through sys_write64.
  The host contract was measured first
  (`test-results/fd97/hostref-fd97.c`):
  fully unreadable -EFAULT with nothing
  written; a readable prefix written as a
  short count (96 of 200; 1 of 2 at a page's
  last byte); count 0 with a bad buffer
  returns 0; a PROT_WRITE-only buffer lands
  its real bytes. Proof: new test
  testWriteBadBuffer64Syscalls (x64 suite)
  RED on the base with the filed signatures
  and GREEN on the fix; full native suites
  serial — main 668/668, x64 70/70; browser
  build verified (BUILD_INFO git_sha = the
  fixing commit); JS unit 348/0. Record:
  `docs/reports/fd97-single-call-write-efault-20261102.md`.

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

- **CL-27 — The bridge's SPIR-V audit admitted
  helper-invocation modules the page tier could
  never translate, so their draws were dropped at
  pipeline time after the guest had been told
  module creation succeeded (was FD-75).** Fixed
  in `69bb72126` on `lane-fd72` (composed into
  integ as `c1b0c34b4`): `vk64AuditSpirv` had no
  rule for BuiltIn 23 or OpIsHelperInvocationEXT,
  so such a module was admitted at
  `vkCreateShaderModule` and failed later, per
  pipeline, in the page tier (`page tier cannot
  translate the fragment SPIR-V module (…) — frame
  skipped`) — the FD-49/FD-67 silent-drop shape.
  The audit now refuses any module that declares
  a BuiltIn HelperInvocation variable or contains
  an OpIsHelperInvocationEXT instruction, at
  creation, by name
  (`spirv-builtin-helper-invocation`, counted like
  every audit refusal; the creation refusal log
  names the module's size + djb2 hash), returning
  `VK_ERROR_FEATURE_NOT_PRESENT`. The boundary is
  presence-based because the translator's
  rejection is (FD-72 evidence item 1); the
  demote WRITE side is untouched (capability 5379
  stays admitted; only the read refuses).
  `docs/known-gaps/vulkan.md` gained the matching
  REJECTED-LOUDLY row, its vk64bridge.cpp
  citations re-pinned through the change's exact
  line map. Proof: a native guard in
  `tools/vkd3d/tests/vk64_native_test.cpp` (both
  spellings refuse + counter; a FragCoord
  neighbour module admits), sabotage-verified on
  the branch (with the audit triggers neutered
  the harness aborts at the guard's first
  assertion); on the composed tree
  `run-native.py` completes with the guard among
  its passes (55 PASS sections — the previous
  gate's 54 plus this guard) and the citation
  audit reads 352 rows, zero drift. In vivo on
  the branch (warmed archive, standard demo
  path, lane build `69bb72126` / wasm
  `04e73f59`): of 256 `vkCreateShaderModule`
  calls the only 2 refusals are the
  byte-identified Godot module (5,704 B, djb2
  `0b3b4d8cc939485a`), each logged by name + size
  + hash with `result=-8`; zero `page tier
  cannot translate` lines in the run — the
  silent-drop path this module took in the
  FD-62 lane's run no longer exists for it; the
  guest reached `vkQueueSubmit2` with 0
  pipeline-creation E_FAILs. FD-72 itself stays
  OPEN: this closure covers only the audit's
  mirror-discipline gap; serving the class waits
  on the translation stack, on the terms FD-72's
  rewritten entry states. Evidence:
  `test-results/fd72/` (branch),
  `test-results/merge-fd72-20261101/` (this
  gate); records
  `docs/reports/fd72-helper-invocation-boundary-20261101.md`,
  `docs/reports/merge-fd72-20261101.md`.
- **CL-28 — The 32-bit futex lost-wakeup flaw (FD-12's flaw
  half).** *(Filed provisionally as CL-26 on lane-fd12;
  numbered CL-28 at composition on 2026-11-01 — CL-26
  and CL-27 were taken on integ by the FD-73 and FD-75
  closures.)*
  Fixed on branch `lane-fd12` in `e8644f814`, applying the
  unmerged `lane-wake32` fix (`c66a35684`) verbatim — the
  32-bit regions had not drifted. FUTEX_WAKE now flags
  registered slots unconditionally and holds the
  pending-wake lock across its scan; a plain FUTEX_WAKE that
  wakes nobody records a pending wake; a fresh FUTEX_WAIT
  word-checks, then under the same lock either consumes a
  pending wake (a spurious EWOULDBLOCK, always legal) or
  registers its slot. Proof: the ported deterministic test
  (`72114f9ea`, harness entry 605) is red on the unfixed
  code (deterministic scenario -ETIMEDOUT at the full
  400 ms; race scenario 1 of 120 wakes lost) and green on
  the fixed code, 6/6 runs; full native suite 664/664
  (663 baseline + the new test), x64 suite 63/63, JS unit
  suite 346/0 against the fixed dist build. Evidence:
  `test-results/fd12/` (`native-test-RED-unfixed.log`,
  `native-test-GREEN-fixed.log`, `native-full-fixed.log`,
  `native-x64-fixed.log`, `js-unit-fixed.log`), record
  `docs/reports/fd12-park-lane-20261101.md`.
- **CL-29 — The guest-thread dump's silent 32-bit blindness
  (FD-76).** *(Filed provisionally as CL-27 on lane-fd12;
  numbered CL-29 at composition on 2026-11-01 — see
  CL-28.)*
  Fixed on branch `lane-fd12` in `80062923f`: the
  enumeration bound the dump walks is now raised by a
  shared helper (`bwNoteThreadDumpTid`) called from both
  the 32-bit and 64-bit syscall dispatchers under the same
  Emscripten guard as before; the dump's existing 32-bit
  reporting branch is finally reached. Proof: dumps in a
  live 32-bit Chromium session enumerate the real threads
  (maxTid=191 observed, per-thread state / syscall / args /
  wait condition), where every dump before reported
  maxTid=0 / dumped=0; the same instrument then captured
  the FD-77 parked state. Evidence: `test-results/fd12/
  consumer-diag-1/` (healthy dumps) and
  `test-results/fd12/consumer-hunt-4/` (parked dumps),
  record `docs/reports/fd12-park-lane-20261101.md`.

- **CL-30 — chmod applies the mode (FD-58).** *(Filed
  as CL-30 on lane-fd58-60, 2026-11-01; the number
  was verified free on the integration register and
  settled at composition.)*
  Fixed on branch `lane-fd58-60` in `81681e834`:
  `KProcess::chmod` resolved the node and returned 0
  without applying anything; 32-bit fchmod/fchmodat
  returned 0 marked IGNORED; 64-bit chmod/fchmod
  returned 0 as an inline no-op and fchmodat had no
  dispatch case (ENOSYS). The mode is now stored on
  the node (permission bits 0..07777 — setuid, setgid
  and sticky preserved verbatim, as the host stores
  them) and every stat writer reports it (stat64,
  lstat64, fstat64, statx, fstatat64 on both
  dispatchers, KFile::stat); a successful change
  fires IN_ATTRIB on the file watch and the parent
  watch. New `KProcess::fchmod` / `fchmodat` share
  the same core; fchmodat with AT_SYMLINK_NOFOLLOW on
  a symlink returns ENOTSUP and leaves the target
  untouched — the host's measured answer (kernel
  6.17.0-1020-oracle reference program). Proof:
  testChmodAccessHardLink (registered in both
  suites) — RED on the base with the reported mode
  stuck at the synthetic default, GREEN with the
  fix; evidence `test-results/fd58-60/`
  (`red-window-main.log`, `red-window-x64.log`,
  `green-window-main.log`, `green-window-x64.log`,
  `ref3.out`). Stated boundary: the stored mode is
  guest-visible state — the emulator's own access
  checks (canRead/canWrite) still use the synthetic
  mode, so a chmod does not change what the emulator
  itself allows; a zip-backed file accepts the mode
  as session state, as link counts already were.

- **CL-31 — link() creates a true hard link
  (FD-60).** *(Filed as CL-31 on lane-fd58-60, 2026-11-01;
  the number was verified free on the integration
  register and settled at composition.)*
  Fixed on branch `lane-fd58-60` in `81681e834`
  (event fan-out refined in `8c7c34ba8`):
  `KProcess::link` copied the content into a new
  node and incremented a counter on both nodes, so
  inode identity, shared content and counts were
  all observably wrong. The new name is now a
  second node carrying the source's node id, the
  two join a link group, and the content identity
  is a host `::link()` between the two native
  paths — writes through either name land in the
  same host file and unlinking one name leaves the
  other's data intact. Link counts come from the
  group's live membership, or from the host
  st_nlink for host-file nodes (exact by
  construction, since the guest's names are
  mirrored by host links); file-level inotify
  events fan out to every group member, and the
  source's IN_ATTRIB precedes the new name's
  IN_CREATE — the host's queue order, measured
  first with a reference program. A symlink source
  links its sidecar file, so the new name is a
  symlink to the same target. Sources that are not
  host files are refused with EPERM and a BW64-FAIL
  line instead of being silently copied. Proof:
  testChmodAccessHardLink — RED (distinct inodes,
  write through one name invisible through the
  other, counts wrong after unlink), GREEN;
  evidence `test-results/fd58-60/` (`ref3.out` for
  the host behaviour). Residuals, stated not
  hidden: the group-liveness count for a non-file
  node can lag while an unlinked name's node is
  still open (host-file nodes are exact);
  path-derived synthetic modes can differ between
  two names of one file in path-special cases; hard
  links already present on disk in a scanned tree
  still load as separate nodes, as before.

- **CL-32 — IN_ACCESS generated; mmap-write
  IN_MODIFY disproven (FD-59).** *(Filed
  as CL-32 on lane-fd58-60, 2026-11-01; the number
  was verified free on the integration register and
  settled at composition.)*
  Fixed/disproven on branch `lane-fd58-60` in
  `81681e834`. (1) IN_ACCESS was never generated:
  completed guest reads that deliver at least one
  byte now fire it — KObject-layer reads, the
  64-bit read/readv path, pread64, and directory
  enumeration via getdents — on the file watch and
  on the parent watch with the name used. A read
  returning 0 fires nothing and guest page-fault
  fills are not reads and fire nothing, both
  matching host measurements; a registration
  fast-out keeps the read hot path at one atomic
  load when no inotify instance exists. (2)
  IN_MODIFY for mmap writes is NOT-A-GAP: host
  reference programs (kernel 6.17.0-1020-oracle)
  wrote through MAP_SHARED and MAP_PRIVATE
  mappings and tried msync (MS_SYNC and MS_ASYNC),
  fsync, sync, munmap and close in every order —
  no IN_MODIFY at any point, only IN_OPEN and
  IN_CLOSE_WRITE. The emulator matches by
  generating none; no code change for this half.
  Proof: testChmodAccessHardLink pins the IN_ACCESS
  half (RED → GREEN in both suites); reference
  sources and outputs in `test-results/fd58-60/`
  (`ref_inotify.c/.out`, `ref2.c/.out`,
  `ref3.c/.out`). One adjacent difference the same
  references found is filed separately as FD-79.

- **CL-33 — The temp-file-to-lock-name link pattern
  is served with true shared content (FD-81).**
  *(Filed as FD-81 on lane-fd81-linktmp, 2026-11-01;
  the number was verified free on the integration
  register and settled at composition; this closure
  is written at composition, the merge gate having
  reproduced the branch's proof.)*
  Fixed on branch `lane-fd81-linktmp` in `b0b3a70d3`
  (composed as `95a6d68b9`): in the browser build the
  backing filesystems (MEMFS and IDBFS) provide no
  hard-link operation, and Emscripten's FS.link
  answers every attempt with EMLINK (errno 34 - the
  value on all 285 refusal lines in the FD-58..60
  deploy record, 57 per 32-bit boot), so FD-60's
  host-link path could never succeed there and every
  regular-file link was refused, although the same
  pattern succeeds on the host. `KProcess::link` now
  falls back (browser build, EMLINK, regular files
  only) to `KProcess::linkSharedBacking`: the new
  name is a second node with the source's id in the
  same link group, pointing at the SAME backing
  path, so content identity is real - every open of
  either name opens the one backing file. The
  group's new sharedBackingPath flag makes
  FsFileNode take link counts from the group's live
  membership (the backing's own count is 1), detach
  only on unlink while a sibling name remains (the
  last name's unlink deletes the backing), and move
  only the guest tree entry on rename. Each served
  link logs a capped BW64-INFO line; a served link
  is a success, not a BW64-FAIL. Native builds keep
  every refusal (there EMLINK is the host's real
  link-count limit); symlink and non-file sources
  keep FD-60's behaviour. Proof: new test
  testTempFileLinkPattern in both native suites -
  the fontconfig pattern end to end, including a
  host-side census showing exactly one backing file
  throughout (a copy would show two), writes visible
  through both names, content surviving unlink of
  the temp name, the backing deleted with the last
  name, a rename keeping the sharing; sabotage-
  checked both ways on the branch (a distinct
  backing path fails the shared-content assertions;
  disabling the unlink guard fails the survival
  assertions), restored and re-verified green. Full
  native suites serial: main 666/666, x64 65/65,
  0 FAILED - reproduced exactly at this
  composition's gate, the new test's line present in
  both suite logs. In vivo, also reproduced at the
  gate: the composed build's wasm is byte-identical
  to the branch build's (`8303eff4`), and the gate's
  boot32-vkboot console carries 0 BW64-FAIL lines of
  any kind and 5 BW64-INFO hard-link served lines on
  exactly the fontconfig files the deploy record
  lists (CACHEDIR.TAG and the four le32d4 caches,
  identical hashes); the boot64 console carries
  neither (no fontconfig link activity on 64-bit
  boots, as measured at the deploy). The deploy's
  57-per-boot full-session figure remains the next
  deploy's to re-measure on the shipped build.
  Residuals, as stated in FD-81: the group-liveness
  count can lag while an unlinked name's node is
  still open; after a cold rescan of the backing
  tree the two names load as separate nodes; a
  served link exists in the guest tree only, never
  as a second name in the backing store itself.
- **CL-34 — The silent freeze convicted: an msync /
  mapped-files ABBA deadlock, fixed at the lock
  order (FD-77).** *(Filed as CL-33 on lane-fd77b,
  2026-11-01; renumbered CL-34 at composition —
  CL-33 was taken on the integration register by
  FD-81's closure, written at its composition.)*
  Fixed on branch `lane-fd77b` in `52ee264c7`, on
  evidence from the fault-stage instrument built in
  `3ca9decf4` (per-thread demand-path stage markers
  plus owner records on the memory mutex and the RAM
  allocator, kept as permanent dump tooling). The
  parked capture (`test-results/fd77b/run-stage-1/`)
  shows the whole configuration in one reading: the
  memory mutex owned 635 s by a demand thread waiting
  inside the mapping lookup; the msync thread holding
  the mapped-files mutex, asleep inside its writeback
  on a not-yet-resident page of its own range; the
  allocator unheld and the fetch proxy empty in the
  same dumps. msync now snapshots the mapping under
  the mutex and writes back after release, and the
  pair's lock order (memory mutex before mapped-files
  mutex; never hold the mapped-files mutex across
  guest-memory access) is written at the declaration.
  Proof: `testMsyncLockOrder` (`5436ead2b`) forms
  the interleave deterministically — RED before the
  fix, GREEN after, both suites; full native suites
  serial main 666/666, x64 65/65, 0 FAILED; JS unit
  348/348; the browser build compiles on the fixed
  tree (wasm `646f85cc…`); six of six consumer runs
  on the fixed build mapped with no park. The
  launcher's "booted, no window, no output" failure
  surfacing remains specified-not-built, as the
  FD-77 entry records.

- **CL-35 — The 64-bit exe spawn signal is emitted
  regardless of BW64_DLLTRACE (FD-54).** *(Filed
  provisionally as CL-35 on `lane-fd54`, 2026-11-01 —
  CL-35 was the next free closure number on the
  integration register at filing; the composing merge
  verifies by audit and renumbers if a sibling lane
  took it first.)* Fixed on branch `lane-fd54` in
  `e3660ab57`: `sys_openat64` no longer conditions the
  process-spawn signal (`OPENED '<path>' -> fd <n>` on
  a successful .exe open) on `BW64_DLLTRACE` being
  unset. With the flag set, the open previously
  produced only the DLLTRACE dialect line, which the
  launcher's readiness observer never matches, so a
  traced 64-bit session could never complete the FD-47
  handshake; the signal is a contract line, and it is
  now emitted unconditionally, matching the 32-bit open
  paths. The DLLTRACE diagnostic line itself is
  unchanged. Proof: `testExeSpawnSignal64` (`bc2cfd046`)
  drives the real dispatcher with klog captured — RED
  before the fix, GREEN after, with the untraced
  control, the trace line's presence, and the
  no-signal-on-failed-open discipline all pinned;
  serial suites x64 67/67 and main 667/667, 0 FAILED;
  run-native 56 PASS; browser build "Browser export
  verified" (wasm `35302eba…`).
- **CL-36 — The METRICS stream froze mid-session:
  the emission gate did not count syscall dispatch
  as activity (FD-63).** *(Filed provisionally as
  CL-36 on lane-fd63, 2026-11-01 — CL-35 was filed
  provisionally by the FD-54 lane on its branch;
  the composing merge verifies by audit and
  renumbers if needed.)*
  Fixed on branch `lane-fd63` in `6d711e6a6`. The
  gate in `bw64_metrics_maybe_emit`
  (`source/util/bw64metrics.cpp`) decided whether a
  window was worth emitting from presents, Vulkan
  frame completions, GL calls and audio calls; the
  syscall stage — fed by every 64-bit syscall
  dispatch — was not counted, so once a session
  stopped presenting, each window fell into the
  no-activity branch and its accumulated counters
  were discarded unreported, freezing the panel at
  the last present-driven snapshot for the rest of
  the run (the filed 1,752 s run: three emissions,
  all SDL-present-driven, then silence for
  ~1,500 s). The gate now counts syscall dispatch
  as activity, with the reason written at the site.
  Proof: a new section of the native metrics
  selftest (`tools/bw64metrics-selftest.cpp`)
  drives six successive ~600 ms intervals of
  syscall-only accounting against the real
  registry — RED on the unfixed code (24 PASS,
  exactly the two new assertions FAIL), GREEN with
  the fix; the writer's contract fixture is
  reproduced byte-identically modulo clock fields,
  so the committed fixture stands; reader contract
  and panel tests 21/21; the browser build compiles
  on the fixed tree (wasm `1809b7d7…`, support
  archive `c4dbe35986a5…` unchanged).
- **CL-37 — Integer-offset root addresses served
  through a word view in both mirrors (FD-70).**
  *(Filed provisionally on branch `lane-fd70int`,
  2026-11-01; CL-35 and CL-36 are held provisionally
  by the FD-54 and FD-63 branches — the composing
  merge lane verifies and renumbers by audit.)*
  Fixed on branch `lane-fd70int` in `b837f16c8`
  (spirvfix serving), `7f409d71e` (bridge mirror +
  native guard) and `e47e03a6a` (citation re-pins).
  A root slot whose materializations compute their
  addresses in integer space (push base VA + affine
  data-dependent byte offset through OpIAddCarry) is
  re-rooted as a storage struct wrapping a runtime
  array of 32-bit words at the unchanged
  `bw_root_p<off>` name and binding, and each load
  through it is rewritten as word loads at its total
  affine byte offset / 4. Proof: the two
  specification fixtures take `root-descriptor-bda`,
  pass spirv-val and translate; corpus 146/148
  byte-identical with only the pair changed;
  `spirvfix-fd70.test.mjs` 11/11 with mutation
  boundaries and a sabotage pass; native guard
  admits the class end to end and keeps the
  mutations refused (run-native 57 PASS); browser
  build on the staged branch (wasm `1cdc208e`) with
  the full JS unit suite 348/348; citation audit
  352 rows, 0 DRIFT. Owed at the deploy gate that
  carries this branch: the pair's pipeline creations
  succeeding in vivo on the shipped build — the
  FD-70 closing terms' last clause, unmet by
  construction until a deploy carries the branch.
- **CL-38 — A file description's close event is
  delivered at the host's moment: held while any
  mapping of the description is live, delivered at
  the last unmap (FD-79).** *(Filed as CL-37 on
  lane-fd79-80, 2026-11-01 — CL-35 and CL-36 were
  taken provisionally by the FD-54 and FD-63
  closures on their own branches; renumbered CL-38
  at composition, CL-37 having been taken on the
  integration register by FD-70's closure in this
  batch; the text is preserved apart from the
  number.)* Fixed on branch
  `lane-fd79-80`. The host contract was re-measured
  first (6.17.0-1020-oracle): any live mapping of a
  description — shared or private, read or write —
  defers its close event (IN_CLOSE_WRITE, or
  IN_CLOSE_NOWRITE for a read-only description) to
  the last unmap; per-description, a second unmapped
  description of the same file fires at its own
  close. The emulator's event was lost in-session,
  not merely early: the open node's close is
  destructor-driven and a mapped file's KFile is
  pinned by its mapping entries and the system file
  cache. The fix tracks the contract explicitly at
  the open description: FsOpenNode carries a
  live-mapped-page count and pending/delivered
  state, both file and mem open nodes route their
  close notification through it (a destructor
  backstop delivers a still-pending event rather
  than losing it, and reopen() re-arms the state);
  KProcess::close signals the event when a close
  drops the process's last descriptor for the
  description; KMemory counts page births at the
  three mapping-creation sites and reports deaths
  before pages are invalidated or replaced (from
  unmap, from mmap overwrite, and for the whole
  address space at exec) — File pages by their
  mapping key, CopyOnWrite pages (a write fault
  converts a file page; its entry names the cache
  page, not the mapping) by range, to the newest
  table entry containing the page. Boundaries,
  stated exactly: a description shared with another
  process is signalled by the first process to drop
  its last descriptor and its pages are counted
  across the sharers, so in the fork corner the
  event can arrive early; it is never lost. The
  64-bit materialized-mapping path has no
  open-description accounting and is filed as
  FD-82. Proof: new test testCloseWriteDeferral in
  both native suites (six cases: shared-writable
  deferral, unmap-before-close, two mappings,
  private-read NOWRITE deferral, plain close, the
  two-description split) — RED on the base (five
  arrival assertions failing), GREEN with the fix;
  full native suites serial main 668/668 and x64
  68/68, 0 FAILED. Record:
  `docs/reports/fd79-fd80-kernel-semantics-20261101.md`;
  evidence `test-results/fd79-80/`.

- **CL-39 — The 64-bit dispatcher's preadv (295) and
  preadv2 (327) are wired: vectored positioned reads
  with the host's validation and notification shape
  (FD-80).** *(Filed as CL-38 on lane-fd79-80,
  2026-11-01; renumbered CL-39 at composition,
  following CL-38 above; the text is preserved
  apart from the number.)* Fixed on branch `lane-fd79-80`. The host
  contract was measured first (raw syscalls,
  6.17.0-1020-oracle): segments fill in order at
  successive offsets, the file position does not
  move, a short read at EOF ends the walk,
  zero-length segments are skipped, IN_ACCESS is
  generated exactly once per successful call
  including 0-byte successes, any negative offset is
  EINVAL (there is no readv fallback), iovcnt
  outside 0..IOV_MAX is EINVAL, an unknown preadv2
  flag bit is EOPNOTSUPP, and every defined RWF flag
  is accepted on a read. The implementation walks
  the iovec over pread64's factored core with
  per-segment notification suppressed and generates
  the single IN_ACCESS on success; pread64's own
  behaviour is unchanged. A descriptor that cannot
  be read keeps pread64's -EINVAL (the host's EBADF
  divergence predates this lane and is filed as
  FD-84). Proof: new test testPreadv64Syscalls in
  the x64 native suite — RED on the base (every
  call -ENOSYS), GREEN with the fix across the
  data/position/segment walk, the EOF cases, the
  validation matrix, the 64-bit offset, and the
  flag matrix; full native suites serial main
  668/668 and x64 68/68, 0 FAILED. Record:
  `docs/reports/fd79-fd80-kernel-semantics-20261101.md`;
  evidence `test-results/fd79-80/`.
- **CL-40 — A readback plan a setup discards is freed
  with it, and the sanitizer flavour finishes clean
  (FD-66).** *(Filed as CL-38 on `lane-fd66`,
  2026-11-01; renumbered CL-40 at composition —
  CL-38 and CL-39 were settled on the integration
  register by FD-79's and FD-80's closures in the
  FD-54 batch; the text is preserved apart from the
  number and the citation note below.)*
  Fixed on branch `lane-fd66` in `7d3f4ff31`
  (citations re-pinned at composition on the
  composed tree; the branch's own re-pin,
  `eaf03b285`, was made against the pre-batch
  bridge file and did not survive composition).
  `setupReadbacks` replans through
  `freePendingReadbacks()`, the only unpaired exit for
  a pending readback, so a plan that is never completed
  can no longer lose its heap blocks silently; the
  harness's planning sections dispose through the same
  helper, and the runner's sanitizer flavour has the
  run budget to report its verdict. Every free in the
  readback path goes through the new `heapRaw` field:
  the page-tier handles are 32-bit by ABI, and freeing
  through them under a 64-bit host allocator is a wild
  free — the first version of the fix proved it by
  crashing the sanitizer harness. Proof: the base
  tree's sanitizer run reproduced the filed leak
  exactly (1,132 bytes in 5 allocations from
  `setupReadbacks`); the fixed tree's flavour completes
  at 57 PASS with zero leak reports, and reverting the
  prologue alone reopens exactly 328 bytes in 2
  allocations. Full evidence:
  `docs/reports/fd66-readback-disposal-20261101.md`.
- **CL-41 — Gecko starved because the 64-bit GDI font
  engine was never built: the backport build configured
  win32u's unix half without freetype or fontconfig, and
  the 64-bit rootfs carried no font faces (FD-65).**
  *(Filed provisionally as CL-41 on lane-fd65,
  2026-11-02 — CL-35…CL-39 were settled by the
  fd54/fd63/fd70int/fd79-80 composition and CL-40 is
  expected for FD-66 in this batch; the composing merge
  verifies the number by audit.)*
  Fixed on branch `lane-fd65` in `51dd2019f`. The filed
  question — absent font set or an enumeration race —
  is answered in two layers. The 64-bit rootfs did
  carry no font faces at all (the 32-bit DejaVu staging
  was never mirrored); six DejaVu faces including the
  Serif pair are now staged into `glibc-rootfs64.zip`
  by `tools/rootfs64/stage-fonts64.sh`, wired into
  `apply-runtime-poststeps.sh` and pinned by the
  completeness checker's new fonts64 check. But faces
  alone changed nothing: a step probe in the guest
  (GetDC / EnumFontFamiliesEx / CreateFontIndirect +
  SelectObject + GetTextMetrics) showed the 64-bit
  stack enumerating ZERO families and failing metrics
  even for registry-registered Tahoma and for faces
  added with AddFontResourceEx, while the identical
  32-bit probe enumerated 83 families with metrics
  working — and DirectWrite's independent collection
  saw faces in the same processes, which is why
  Chromium-class text rendered while Gecko starved.
  The shipped `win32u.so` (FD-01 build `bffd9036…`)
  contains no freetype or fontconfig support: the
  backport build script configured its unix-half tree
  `--without-freetype --without-fontconfig`, and the
  font driver and fontconfig enumerator live in that
  half. The build now compiles that tree with both,
  and fails loudly if either SONAME is missing from
  the product; the rebuilt `win32u.so` (`d382cefb…`)
  is re-pinned (checker + backports README) and staged
  into `wine64.zip`, and the runtime-assets archive
  was regenerated (`runtime-assets-20261102`,
  `7df0cdc2…`; `bootstrap.py --check` green end to
  end). Proof: baseline common-dependencies RED with
  the filed signature (`no fonts … backend: gdi`, the
  ABORT, no `MSHTML: RESULT 0`); on the fixed build
  the step probe reads EnumFontFamiliesEx 0 → 85
  families and GetTextMetrics failing → tmHeight 17
  (the 32-bit numbers), and the real test's Gecko
  stage is green in two runs (`MSHTML: RESULT 0`,
  title and body-text checks pass, zero abort lines,
  the baseline's companion 0xC0000005 exit gone), its
  waits and assertions untouched; JS unit 348/0; the
  browser build compiles on the fixed tree ("Browser
  export verified", support archive unchanged). The
  test's final audio-capture stage fails on a
  separate, newly reachable defect — FD-85; the
  missing Arial family on this stack is FD-86.
  Record: `docs/reports/fd65-gecko-fonts-20261102.md`;
  evidence: `test-results/fd65/`.

- **CL-42 — The common-dependencies capture counted
  samples on SDL's bypassed ScriptProcessor node
  while guest audio flows through the bwPump
  AudioWorklet (FD-87).** *(Filed provisionally as
  CL-42 on lane-fd85, 2026-11-02 — CL-40 is expected
  for FD-66 and CL-41 stands filed for FD-65 in this
  batch; the composing merge verifies the number by
  audit.)* Fixed on branch `lane-fd85` in
  `65cabcd67`. The test (written 2026-10-18) wrapped
  `AudioContext.createScriptProcessor` and counted
  non-zero samples in those nodes' output buffers.
  Commit `13b33d3e1` (2026-10-20, "stream browser
  audio through AudioWorklet") moved the bwPump
  bridge in `platform/sdl/kdspaudio.cpp` onto an
  `AudioWorkletNode` connected straight to the
  destination and updated `audio-tone.test.mjs` in
  the same commit to tap that node; this test was
  not updated, so its only remaining ScriptProcessor
  source was SDL's own node, which the bwPump bypass
  leaves silent by construction — the test's audio
  stage became unpassable regardless of the guest.
  The capture now also taps the bridge worklet's
  output into the same counters (via the un-wrapped
  creator stored on the context, the audio-tone
  pattern); every wait and assertion is unchanged.
  Proof: in one session a Linux ELF's direct
  `/dev/dsp` tone was measured at the worklet tap at
  peak 0.3662 with 261,528 non-zero samples (and the
  worklet's own metrics reported inputPeak =
  outputPeak = 0.3662) while the shipped capture
  counted 0 non-zero in 266,240 samples — RED; a
  harness installing the fixed capture verbatim
  counted the same control tone at peak 0.3662 with
  143,360 non-zero samples — GREEN (the further
  non-zero counts in that run's OpenAL window were
  the control tone's device-drain tail, identified
  as such in the lane record, not OpenAL audio).
  JS unit suite 348/0; browser build "Browser
  export verified" with the wasm byte-identical to
  the shipped build (this closure changes no
  emulator code). The shipped test still fails its
  audio stage after this fix — on FD-85's
  remaining Wine-leg defect, which the fixed
  capture now honestly reports. Record:
  `docs/reports/fd85-audio-leg-20261102.md`;
  evidence: `test-results/fd85/`.

- **CL-43 — The second-wave sibling classes are
  served in the fixup layer, with the mirror
  admitting exactly the same classes (FD-89).**
  *(Numbered provisionally by the FD-87 lane,
  2026-11-02 — CL-40 and CL-41 stand provisionally
  on the fd66 and fd65 branches; the composing
  merge verifies the sequence by audit.)* Fixed on
  branch `lane-fd87`. spirvfix gains R18
  (`strip-dead-descriptors`: DescriptorSet/Binding
  variables in the descriptor storage classes with
  zero operand-aware uses are deleted with their
  decorations, entry-point interface entries and
  orphaned types, and RuntimeDescriptorArray is
  dropped when no runtime array survives) and R19
  (`strip-stray-sampled-image-array-dynamic-indexing`:
  capability 29 is deleted when no
  image/sampled-image array survives), and its
  descriptor-array orphan sweep now bounds the
  OpExtInst instruction-number word (the FD-78
  position this lane proved live). The bridge
  audit names capability 29
  (SampledImageArrayDynamicIndexing), admits it
  exactly when every used image/sampled-image
  runtime array is R15a-servable and no fixed-size
  image array exists, treats zero-use descriptor
  array variables as dead in both the array verdict
  and the skip-5302 simulation (whose scan bounds
  the same ExtInst word), and no longer lets a dead
  array variable gate the texel mirrors. Proof: the
  two captured modules are committed fixtures — the
  2,340-byte module's fixed form carries only the
  Shader capability and translates end to end; the
  369,400-byte module's fixed form drops
  SampledBuffer (508 texel fetches widened) and
  keeps exactly FD-88's pair; new JS suite
  `spirvfix-fd87.test.mjs` 5/5; corpus green over
  all 74 modules with every newly-firing rewrite
  independently verified against the raw modules;
  native run-native 60 PASS including three new
  guards; JS unit suite 351/0; citation audit 352
  rows, 0 drift; browser build 'Browser export
  verified', wasm `f66e592c`. Owed, in FD-70's
  clause form: the two pipelines' creations
  succeeding in vivo on a shipped build, which
  belongs to the deploy gate that carries the
  composing merge — and which now gates only on
  FD-88 and FD-72 for these pipelines. Record:
  `docs/reports/fd87-godot-sibling-classes-20261102.md`.

- **CL-44 — Four legacy-suite subtests threw
  'DESC_SET has 132 trailing bytes' against the
  current V2 protocol fixtures (was FD-64).**
  Closed on branch `lane-fd64` in `9d132afc7`.
  This number is PROVISIONAL: CL-40/41/42 are
  provisionally taken by in-flight lanes'
  uncomposed records; the composing merge
  verifies and renumbers by audit (the FD-63
  precedent). FD-64's fifth subtest (the spirvfix
  corpus conversion) had already been resolved by
  the FD-62 lane's measured corpus re-baseline
  (`ea7283041`) and kept green by the FD-70
  composition's fixture enrollment (`bf2ce6bbf`).
  For the four: the fixture in
  web/tests/vkwebgpu-v2.test.mjs was the stale
  side — it encoded the superseded DESC_SET
  header (u8 count) and a descriptor array as one
  entry with count 3, while docs/vulkan-protocol.md
  §0x08, the bridge writer (emitDescSet: u8
  setIndex, u8 pad, u16 nBind; applyDescWrite
  stores one Binding per element, count 1), the
  vk-protocol.mjs reader and the suite's
  vk-desc-set-wire.test.mjs all carry the current
  contract. The correction is test-file only: the
  fixture writes the current header and
  per-element array entries, and the decode
  assertions assert that contract (five bindings
  in set 0; binding 1 as elements [0,1,2], each
  count 1, kind 2). A second stale expectation
  the throw had masked — the decoded sampler
  object's shape — was updated to the current
  decoder contract (the ADDR mapping for the
  fixture's CLAMP_TO_BORDER / MIRROR_CLAMP_TO_EDGE
  modes, plus the raw addrModeU/V/W fields the
  border-emulation feature added in `26ffedefa`,
  pinned by vk-border-emulation.test.mjs); no
  assertion was dropped and no served behaviour
  changed. Proof: web/tests/vkwebgpu-v2.test.mjs
  20/20; full legacy group 115 passed / 0 failed
  (baseline 110/4 — the standing failing set is
  empty); suite-unit group 348/0 and
  tools/run-tests.mjs ALL TESTS PASSED on the
  built tree (browser build in-lane, wasm
  d631d254, byte-identical to the shipped
  build's). Sabotage:
  restoring the stale u8-count header in a
  scratch copy returns the same four failures
  (220 trailing bytes over the five current
  entries). Record:
  `docs/reports/fd64-legacy-descset-20261102.md`;
  evidence `test-results/fd64/`.

- **CL-45 — A guest that fills its file-descriptor
  table now gets EMFILE instead of killing the
  emulator (FD-05).** *(Filed provisionally as
  CL-44 on lane-fd05, 2026-11-02 — CL-40..CL-43
  are held provisionally by parallel branches
  (the FD-66, FD-65, FD-85, FD-87 and FD-64
  filings); the composing merge verifies by
  audit and renumbers if needed.)*
  Fixed on branch `lane-fd05`. The panic had
  exactly one caller: `allocFileDescriptor` is
  the only path into `getNextFileDescriptorHandle`,
  so the allocation contract changed at that
  single point — the scan returns
  MAX_NUMBER_OF_FILES as its "none available"
  sentinel and the allocator returns no
  descriptor — instead of the ~50-site rewrite
  the entry estimated. Two call sites
  (pidfd_open, createPidFdForProcess) already
  implemented exactly this null contract and
  were unreachable only because the panic fired
  first; the remaining 20 production callers
  were audited one by one and now propagate the
  kernel's answer: open, dup, memfd, epoll,
  inotify, timerfd, eventfd, signalfd and
  socket creation return -EMFILE; F_DUPFD with
  a minimum at/past the ceiling returns
  -EINVAL (the host's distinction); dup2/dup3
  targets at/past the ceiling return -EBADF,
  so explicit targets can no longer grow the
  table past the advertised RLIMIT_NOFILE;
  socketpair stays atomic (the first
  descriptor is closed when the second cannot
  be allocated); SCM_RIGHTS installs what fits
  and marks the rest MSG_CTRUNC; the unix
  accept requeues its pending connection, the
  loopback accept pre-checks and requeues, and
  the host accept pre-checks and, in the
  lost-race case only, closes the consumed
  host descriptor. Exhaustion logs one
  `BW64-FAIL fd-table-exhausted ... outcome=returned-EMFILE`
  line per process. Characterized residuals,
  none of them a crash: the native accepts
  answer EMFILE on entry when the table is
  already full rather than after a connection
  becomes pending (the unix accept keeps its
  blocking shape); internal explicit-handle
  allocations (fork's table copy, stdio setup)
  bypass the ceiling by construction and can
  only carry handles the process already
  legally holds. Proof: new test
  testFdTableExhaustion in both native suites
  drives a process to the ceiling through dup,
  checks every creator family, the exact
  ceiling boundary, and recovery after one
  close — RED on the base (the fill hit the
  panic: in the forked harness the test was
  each suite's single failure, its child dead
  at the panic line; un-forked, the suite
  process died at the test), GREEN with the
  fix: native serial main 669/669 and x64
  70/70, 0 FAILED, exactly one exhaustion log
  line per run. Browser build on the fixed
  tree: BUILD COMPLETE with "Browser export
  verified" (wasm `ea3bdaa0…`, support archive
  `c4dbe35986a5…` unchanged); JS unit 348/0
  (the three source-slice tests whose mock
  errno tables predated the new returns —
  fs-file-create, browser-network,
  unix-connect-handoff — gained `K_EMFILE=24`
  in their mocks and pass with the edited
  functions exercised in their harnesses); JS
  legacy 110/4, FD-64's standing set on this
  base. Record:
  `docs/reports/fd05-fd-table-exhaustion-20261102.md`;
  evidence `test-results/fd05/`.

- **CL-46 — No liveness scan in the SPIR-V serving
  layer counts a literal word as an id reference:
  every sweep routes through operand-aware
  positions (FD-78).** *(Provisional number,
  assigned on lane-fd78, 2026-11-02; CL-40…CL-44
  are held by the fd66 / fd65 / fd85 / fd87 / fd64 /
  fd05 lane branches — the merge lane renumbers
  by audit if the composition order differs.)*
  Fixed on branch `lane-fd78` off integ
  `cc1d89ab3`. The operand-aware walker gained an
  enumerating sibling (`operandRefIds` in
  `tools/spirvfix/spirvfix.mjs`,
  `r15::operandRefIds` in
  `source/vulkan/vk64bridge.cpp`) — a word counts
  as a reference exactly where the per-op walker
  finds one — plus its missing OpExecutionModeId
  case; the descriptor-array orphan sweep, the
  storage-image type prune, the R15b root-cone
  sweep, the texel-orphan scan, the
  specialized-alias scan and the bridge's
  skip-5302 simulation now route through it, each
  keeping its existing op exclusions. Positions
  covered: VectorShuffle components,
  CompositeExtract / CompositeInsert indices,
  OpSwitch case literals, OpLine words, the
  OpArrayLength structure index, the leading
  literals of OpFunction / OpVariable, constant
  values and type widths in the prune, and the
  ExtInst instruction number — the last subsumes
  the FD-87 lane's targeted bound at those two
  scans; the merge lane resolves the overlap by
  audit. Audited and deliberately unchanged:
  `stripDeadUndefs` (the raw scan is its
  documented fail-closed contract), the
  fail-closed veto scans, and the OpGroupDecorate
  family at the walker's default. Proof: new JS
  suite file
  `web/tests/suite/unit/spirvfix-fd78.test.mjs`
  (31 checks — per-position walker tests, mutation
  guards on the FD-70 fixture, two new assembled
  prune fixtures
  `testdata/spirv/fd78-prune-constant.spv` /
  `fd78-prune-function.spv`, and cone / texel /
  alias mutation guards on captured modules),
  every mutation guard RED on the base scans and
  GREEN with the fix by direct experiment; native
  guard in `tools/vkd3d/tests/vk64_native_test.cpp`
  (nine literal carriers, a function-control
  carrier, a genuine-reference control) RED on the
  base bridge, GREEN with the fix, full native
  suite green; spirvfix corpus tests 10/10; full
  JS unit suite 349 passed, 0 failed; the lane build
  completed with Browser export verified (wasm
  `c9a30661`). The register's drifted line
  citations were re-pinned; the citation audit
  reports 0 drift. Record:
  `docs/reports/fd78-literal-positions-20261102.md`.

- **CL-47 — spirvfix's fixed output for the captured
  Godot module validates cleanly: synthesized
  definitions are spliced behind the definitions
  they reference (FD-92).** *(Filed provisionally
  as CL-45 on lane-fd92, 2026-11-02 — CL-40..CL-44
  are held provisionally by parallel branches (the
  FD-66, FD-65, FD-85, FD-87, FD-64 and FD-05
  filings); the composing merge verifies by audit
  and renumbers if needed.)*
  Fixed on branch `lane-fd92` (commit 89ac92ff9).
  The defect: rules spliced synthesized types and
  constants at the first-variable anchor without
  regard to dependencies on module types defined
  later in the type section; in the captured
  369,400-byte Godot module, lower-mid-shader-
  discard's pointer-to-bool types landed at
  instruction 299, ahead of the OpTypeBool at 333
  they reference, and spirv-val rejected the fixed
  module. The fix adds `defsSplicePos`, which
  computes the earliest splice position at or after
  a rule's anchor that follows every module
  definition the new instructions reference, and
  applies it at the three intra-type-section splice
  sites (lower-mid-shader-discard,
  strip-point-builtins, the texel-pointer rewrite).
  Proof: spirv-val clean on the captured module's
  fixed output (the base output fails with the
  filed error verbatim); the output is a pure
  reordering of the base output (instruction
  word-multiset identical over 18,352 instructions —
  the module's meaning is unchanged) and the
  translator's verdict is unchanged; the 72-module
  fixture corpus is byte-identical to the base with
  applied sets unchanged and the corpus gate green
  at 10/10; new guard
  `web/tests/suite/unit/spirvfix-fd92.test.mjs` is
  RED on the base and GREEN with the fix. Record:
  `docs/reports/fd92-spirvfix-splice-ordering-20261102.md`;
  evidence `test-results/fd92/`.

- **CL-48 -- A 64-bit guest's file-backed mappings
  now participate in FD-79's close-event deferral:
  the event is held while any materialized mapping
  of the description is live and delivered at its
  last death (FD-82).** *(Filed provisionally as
  CL-45 on lane-fd82, 2026-11-02 -- CL-40..CL-44
  are held provisionally by parallel branches
  (the FD-66, FD-65, FD-85, FD-87, FD-64 and FD-05
  filings); the composing merge verifies by audit
  and renumbers if needed.)* Fixed on branch
  `lane-fd82`. The 64-bit file-mmap path
  (sys_mmap64_file) materializes a file mapping as
  copied pages (MAP_PRIVATE) or registry-shared
  pages (MAP_SHARED) inside KMemory64, which kept
  no record of which open description a mapping
  belonged to, so the description's close signal
  always saw a zero live count and the event was
  delivered at fd close. KMemory64 now keeps a
  per-address-space table of its live file mappings
  (start page -> page count + the KFile, pinned for
  the mapping's lifetime exactly as the
  demand-paged path's MappedFile pins it):
  sys_mmap64_file counts the birth at the
  description for both materializations, and
  deaths are reported before the pages are
  invalidated or replaced -- from munmap, from
  overwrite by a later mapping (anonymous or
  shared-file), from mremap (a shrink trims the
  entry through the munmap path; a grow or move
  re-notes the resulting range, counting the new
  birth before the source is released so the live
  total never dips through zero), from fork (the
  child inherits the table without re-counting
  births -- deaths are counted across the twins
  and saturate at zero, the FD-79 fork model --
  with DONTFORK pages detached in the child
  without releasing), and from destruction of the
  address space (exec replacement and teardown).
  Boundaries, stated exactly: the fork corner
  inherits FD-79's stated boundary (the event can
  arrive when the first sharer's pages are gone
  rather than the last; early, never lost); a
  mapping duplicated by mremap's zero-length
  shared-page path counts as a second live mapping
  of the description; mappings the emulator's own
  loader makes (not through a guest descriptor)
  are outside the contract, as on the demand-paged
  path. One correction to FD-82's filed text: the
  demand-paged path's deferral closure is CL-38,
  not CL-36. Proof: new test
  testCloseWriteDeferral64 in the x64 native suite,
  driving the real 64-bit syscall dispatch (mmap /
  munmap / close) -- seven cases: shared-writable
  deferral, private-read NOWRITE deferral, two
  mappings of one description, unmap-before-close,
  plain close, overwrite by an anonymous MAP_FIXED
  mapping, and a partial unmap of a two-page
  mapping -- RED on the base (ten assertions
  failing: the event fired at fd close in every
  deferral case and never arrived at the mapping's
  death; the unmap-before-close and plain-close
  guards passed), GREEN with the fix; full native
  suites serial main 668/668 and x64 70/70,
  0 FAILED (x64 baseline 69 + this test; the test
  lives in the x64 suite because the materialized
  path exists only in the 64-bit build -- the
  shared contract's main-suite guard remains
  testCloseWriteDeferral); JS unit 348 passed /
  0 failed; browser build on the fixed tree
  (clean-bootstrap staging from this base's pinned
  runtime-assets-20261101 archive, 13 assets
  verified): 'Browser export verified' / BUILD
  COMPLETE, wasm 551ec728, support archive
  c4dbe35986a5 unchanged. Record:
  `docs/reports/fd82-materialized-mmap-close-deferral-20261102.md`;
  evidence `test-results/fd82/`.

- **CL-49 — readv generates exactly one IN_ACCESS
  per successful call, 0-byte successes included
  (FD-83).** *(Provisional number, filed on
  lane-fd83-84, 2026-11-02 — CL-40..CL-44 are held
  by the unmerged fd66, fd65, fd85/fd87, fd64 and
  fd05 lanes off this same base; the merge lane's
  audit settles the final number.)* Fixed on branch
  `lane-fd83-84`, commits `0332ee82f` (tests),
  `3459a7d20` (fix) and `bdbac75a5` (silent-failure
  baseline review). The host contract was
  re-measured first (host kernel
  6.17.0-1020-oracle): exactly one IN_ACCESS per
  successful readv — one segment or three, data or
  0 bytes (at EOF, with iovcnt 0, with
  all-zero-length segments) — none on failure, and
  a call that reads bytes before a later segment
  fails returns the bytes as a success and
  generates the event. `sys_readv64` previously
  walked its segments through `sys_read64`, whose
  per-segment notification fires only when bytes
  move: 0-byte successes generated nothing, and the
  one-event shape for multi-segment reads held only
  by accident of the inotify queue coalescing
  consecutive identical events; the walk also
  carried no validation of its own (an out-of-range
  iovcnt was never EINVAL; iovcnt 0 on a
  non-readable descriptor returned a silent 0). The
  fix validates in the host's order (bad descriptor
  EBADF, wrong mode EBADF — CL-46, iovcnt range
  EINVAL, null iovec with iovcnt > 0 EFAULT), walks
  the segments under a new
  `KInotify::SuppressAccessNotifications` scope
  (thread-local RAII depth, checked in
  notifyAccessed), and raises one IN_ACCESS on
  success; plain read/pread keep FD-59's
  silence-at-0, and the FD-59 disposition comments
  in kinotify/kfile now state the vectored
  exception. Proof: testPreadv64Syscalls (x64
  native suite) section 10 — RED on the base (the
  0-byte shapes generated no event; the iovcnt
  range and iovcnt-0 mode cases answered wrong),
  GREEN with the fix across the multi-segment
  walk, the short final segment, all three 0-byte
  shapes, the failure set, and a plain-read
  control that stays silent; full native suites
  serial main 668/668 and x64 69/69, 0 FAILED;
  browser build on the fixed tree verified (wasm
  `2601ef69`, support archive unchanged); JS unit
  348/0, legacy 110/4 (FD-64's standing set on
  this base). Record:
  `docs/reports/fd83-fd84-read-family-20261102.md`;
  evidence `test-results/fd83-84/`.

- **CL-50 — The positioned/plain read family
  answers EBADF on a descriptor that cannot be
  read (FD-84).** *(Provisional number, filed on
  lane-fd83-84, 2026-11-02 — see CL-45's note.)*
  Fixed on branch `lane-fd83-84`, commits
  `0332ee82f` (tests), `3459a7d20` (fix) and
  `bdbac75a5` (silent-failure baseline review).
  The host contract was re-measured first: read,
  pread, readv, preadv and preadv2 on an O_WRONLY
  descriptor answer EBADF — for count 0 and
  iovcnt 0 as well, and ahead of the buffer check;
  a negative offset in the positioned family
  answers EINVAL ahead of the mode answer; in
  preadv/preadv2 the mode answer precedes the
  iovcnt range check and the unknown-flag
  EOPNOTSUPP. The answer is now -EBADF at
  `sys_read64`, at `sys_pread64Impl` (whose
  negative-offset check the fix places ahead of
  the mode, also giving plain pread64 the host's
  order), in `sys_preadv64` (fd, offset, mode,
  flags, iovcnt), in `KProcess::read`, and in
  `KProcess::pread64`, which previously carried
  no mode check at all and fell through to the
  backing read — the backing layer swallows the
  host's EBADF into a silent 0, visible in the
  RED logs as a `BW64-FAIL host-read errno=9`
  line beside a 0 return to the guest. Proof:
  testPreadv64Syscalls (x64 native suite)
  section 9 — the write-only case the FD-80 lane
  pinned at -EINVAL, rewritten to the eleven-
  assertion host matrix (both orderings, the
  zero-count cases, no event on failure) — and
  testChmodAccessHardLink's FD-84 section through
  the KProcess API (plain read, zero-length read,
  pread64) in both native suites; RED on the base
  (the x64 run failed 20 assertions across the
  new sections 9-10, the FD-84 KProcess
  assertions failing in both suites), GREEN with
  the fix; full native suites serial main 668/668
  and x64 69/69, 0 FAILED; browser build
  verified, JS as CL-45. The write family's
  mirror divergence was measured alongside and
  filed as FD-93 rather than widened into this
  fix. Record:
  `docs/reports/fd83-fd84-read-family-20261102.md`;
  evidence `test-results/fd83-84/`.

- **CL-51 -- A failed 64-bit file mapping now
  leaves nothing mapped and nothing reserved:
  when the content read that follows the map in
  sys_mmap64_file fails, the established range is
  torn down before the error is returned
  (FD-92).** *(Filed provisionally as CL-46 on
  lane-fd92mmap, 2026-11-02 -- CL-40..CL-45 are
  held provisionally by parallel branches (the
  FD-66, FD-65, FD-85, FD-87, FD-64, FD-05 and
  FD-82 filings); the composing merge verifies by
  audit and renumbers if needed. The defect
  number FD-92 is kept exactly as carried from
  lane-fd82's register, where the FD-82 lane
  filed it: the same number is used by unrelated
  entries on two other sibling branches --
  lane-fd92 carries an FD-92 for a spirvfix
  output-ordering defect, and branch lane-fd92-93
  is chartered on the FD-92 / FD-93 numbers -- so
  the merge audit must settle which entry keeps
  FD-92.)* Fixed on branch `lane-fd92mmap`.
  sys_mmap64_file materializes a file mapping in
  two phases: it establishes the mapping first
  (mmapReserveAndMap for addr==0,
  mmapAnonymousFixed otherwise; for MAP_SHARED it
  reserves the range and then reads a seed buffer
  before installing the shared pages), and only
  then copies the file content in with positioned
  reads. Both content-read error returns -- the
  private branch's copy loop and the shared
  branch's seed loop (the latter only with
  addr==0, where the reservation already exists)
  -- returned the read's errno with the
  established range left mapped and reserved. A
  guest that trusted the failure believed the
  range unmapped; an access there succeeded
  instead of faulting, and the range was
  unavailable for reuse. Both sites now release
  the established range through KMemory64::munmap
  before returning the error: the private branch
  unmaps [aligned, aligned+mapLen)
  unconditionally (that span covers both of its
  establishment forms), the shared branch unmaps
  only the range it reserved. Composition with
  FD-82's accounting (lane-fd82, CL-48) is by
  construction: lane-fd82 counts the private
  mapping's birth before the content read and
  reports mapping deaths from KMemory64::munmap,
  so this teardown releases the birth as well;
  in the shared branch lane-fd82 counts the
  birth only after the shared pages are
  installed, so the seed-failure teardown has no
  birth to release. Boundaries, stated exactly:
  for an explicit-address map over a live
  mapping, the prior contents were already
  destroyed by the map phase's zeroing (the
  pre-existing two-phase materialization
  contract, which snapshots only the partial
  head/tail pages), so teardown leaves the range
  unmapped rather than resurrecting the prior
  mapping -- 'nothing mapped' is the failed-host-
  mmap contract the entry specifies, and that is
  what the guest now observes; mmapSharedFile's
  own failure return after a successful seed
  read is a separate contract (its callers
  include loader paths) and is not changed here.
  The trigger is exercised at the interface the
  defect lives at: the in-tree file backings
  currently fold host read errors into a 0 (EOF)
  answer, so no real file in the tree makes the
  positioned read return an errno today;
  KFile::preadNative's contract nonetheless
  forwards the backing's result verbatim and its
  callers (this function, sys_pread64's core)
  are written against (S32)result < 0 as the
  error answer, so the test drives that contract
  with an open description whose readNative
  serves a pattern up to a byte budget and then
  returns -EIO. Proof: new test
  testMmap64FileReadFailure in the x64 native
  suite -- five cases (explicit-address private
  failure on the first byte, explicit-address
  private failure after one served page, a
  never-failing control that maps and serves its
  content, addr==0 private failure, addr==0
  shared seed failure; the addr==0 cases locate
  the reservation through a control allocation
  at the mmap cursor) driving the real 64-bit
  syscall dispatch -- RED on the base (the four
  failure cases each reported their range still
  mapped: 0x100000000, 0x100100000, 0x700003000,
  0x700008000; the control passed), GREEN with
  the fix; full native suites serial main
  668/668 and x64 70/70, 0 FAILED (x64 baseline
  69 + this test); JS unit 348 passed / 0
  failed; browser build on the fixed tree
  (clean-bootstrap staging from this base's
  pinned runtime-assets-20261101 archive, 13
  assets restored and verified, --check PASS
  including the FD-73 retained-archive line):
  'Browser export verified' / BUILD COMPLETE,
  build git_sha e37fa3c04, wasm 16abfc4c,
  support archive c4dbe35986a5 unchanged.
  Record:
  `docs/reports/fd92-failed-mmap64-teardown-20261102.md`;
  evidence `test-results/fd92mmap/`.

- **CL-57 — The 32-bit dispatcher's readv is wired
  with the host's validation and FD-83's
  one-IN_ACCESS-per-success contract (FD-92).**
  *(Provisional number, filed on lane-fd92-93,
  2026-11-02 — CL-40..CL-44 are held by the
  unmerged fd66, fd65, fd85/fd87, fd64 and fd05
  lanes and CL-45/CL-46 by lane-fd83-84, all off
  this same base; the merge lane's audit settles
  the final number. The FD-92 number is the filing
  lane's, carried exactly; an unrelated
  shader-fixup entry on branch `lane-fd92` uses the
  same number, flagged in this lane's record.)*
  Fixed on branch `lane-fd92-93`, commits
  `48bb12b1c` (tests) and `8d2da6430` (fix). Slot
  145 was null and answered -ENOSYS. The fix
  dispatches it to KProcess::readv over a new
  KObject::readv walk: validation in the host's
  order (bad descriptor EBADF, wrong mode EBADF,
  iovcnt outside 0..IOV_MAX EINVAL, null iovec
  with iovcnt > 0 EFAULT); the walk skips
  zero-length segments, returns the bytes already
  read when a later segment fails, and ends at a
  short segment; exactly one IN_ACCESS is
  generated per successful call — the byte-moving
  case through the file layer's per-segment
  events coalesced by the inotify queue, the
  0-byte case (at EOF, with iovcnt 0, with
  all-zero-length segments) raised by
  KProcess::readv. Proof: new test
  testReadv32Syscall in the main native suite
  drives the real int 0x80 dispatch — RED on the
  base (all 20 assertions failing, every call
  -ENOSYS), GREEN with the fix; full native suites
  serial main 670/670 and x64 71/71, 0 FAILED;
  browser build 'Browser export verified' (wasm
  `bcc8a852`). Record:
  `docs/reports/fd92-fd93-readv-write-family-20261102.md`;
  evidence `test-results/fd92-93/`.

- **CL-58 — The write family answers EBADF on a
  descriptor that cannot be written, in the host's
  validation order (FD-93).** *(Provisional number,
  filed on lane-fd92-93, 2026-11-02, on the same
  terms as CL-47.)* Fixed on branch `lane-fd92-93`,
  commits `48bb12b1c` (tests), `8d2da6430` (fix)
  and `0acf72a4b` (silent-failure baseline
  review). The host contract was measured first
  (host kernel 6.17.0-1020-oracle, extending the
  filing lane's matrix): write, pwrite and writev
  on an O_RDONLY descriptor answer EBADF, for
  count 0 and iovcnt 0 as well; writev's mode
  answer precedes its iovcnt-range and
  iovec-pointer answers; a negative offset in the
  positioned family answers EINVAL ahead of both
  the descriptor and the mode answers. The base
  answered -EINVAL at KProcess::write,
  KProcess::writev, sys_write64's gate and
  sys_pwrite64; returned a silent 0 from
  KProcess::pwrite64 (no mode check — the backing
  write's host EBADF swallowed by the backing
  layer, visible in the RED log), from sys_write64
  at count 0 (its gate sat behind the zero-count
  return) and from sys_writev64 at iovcnt 0 (no
  validation of its own); and took a guest fault
  at address 0 for writev with a null iovec at the
  KProcess layer. The fix answers EBADF at every
  named site and adds the missing validation in
  the measured order. Proof: new tests
  testWriteFamilyWrongMode (both suites) and
  testWriteFamily64Syscalls (x64 suite) — RED on
  the base, GREEN with the fix; full native suites
  serial main 670/670 and x64 71/71, 0 FAILED;
  browser build 'Browser export verified' (wasm
  `bcc8a852`). Record:
  `docs/reports/fd92-fd93-readv-write-family-20261102.md`;
  evidence `test-results/fd92-93/`.

- **CL-59 — The positioned vectored writes are
  wired on both dispatchers with the host's
  validation order and return shape (FD-94).**
  *(Provisional number, filed on lane-fd94-96,
  2026-11-02 — CL-40..CL-48 are held by the
  unmerged fd66, fd65, fd85/fd87, fd64, fd05,
  fd83-84 and fd92-93 lanes off this same base;
  the merge lane's audit settles the final
  number. The FD-94/FD-96 numbers are the filing
  lane's, carried exactly.)*
  Fixed on branch `lane-fd94-96`, commits
  `c060e0613` (tests; build corrections
  `f9a8fdeef`, `84e5feca7`) and `d528d792b`
  (fix). 64-bit: new sys_pwritev64 dispatches
  296/328 over the pwrite64 core; 32-bit: new
  KProcess::pwritev dispatches table slots
  334/378 over KProcess::pwrite64. Validation
  in the measured order: a negative offset is
  EINVAL ahead of the descriptor check, except
  pwritev2's offset -1 — the file-position mode
  (write at the current position and advance
  it); the descriptor check (EBADF) precedes
  the pwritev2 flags check (EOPNOTSUPP for bits
  outside the defined RWF mask 0x3f — measured:
  0x20 NOAPPEND is defined and accepted on the
  host, unlike the read side's 0x1f; NOWAIT is
  accepted by the emulator per the preadv2
  precedent, though this host's /tmp filesystem
  answers EOPNOTSUPP for it as a per-filesystem
  capability) and the iovcnt range check
  (EINVAL outside 0..1024); a bad iovec array
  with iovcnt > 0 is EFAULT. RWF_APPEND writes
  at end of file, ignoring the offset, and
  leaves the position untouched outside the -1
  mode. Segment buffers are range-checked per
  segment because the core's guest copy
  zero-fills an unreadable buffer instead of
  failing (FD-97); a later segment's failure
  returns the bytes already written; the walk
  raises exactly one watcher-visible IN_MODIFY
  (per-segment events coalesce in the inotify
  queue, as on the host). The read-only
  descriptor answer stays the write family's
  house -EINVAL (the host's EBADF divergence
  is FD-93's registered territory, exactly as
  FD-92's test pins the read side). Proof:
  testPwritev64Syscalls (x64 suite) and
  testPwritev32Dispatch (main suite), RED on
  the base with -ENOSYS on every call and GREEN
  on the fix; full suites serial — main
  670/670, x64 70/70; browser build
  'Browser export verified'; JS unit 348/0
  (the silent-failure baseline for syscall64.cpp
  is bumped 115 -> 118 with this lane — the
  counted returns are the new function's
  guest-validation errno answers; the merge
  audit should expect 119 once lane-fd92-93's
  116 bump composes).
  Record:
  `docs/reports/fd94-fd96-pwritev-writev-20261102.md`;
  evidence `test-results/fd94-96/`.

- **CL-60 — The 32-bit vectored write returns
  the bytes already written when a later
  segment fails (FD-96).** *(Provisional
  number, filed on lane-fd94-96, 2026-11-02 —
  numbering as in CL-49; the merge lane's audit
  settles the final number.)*
  Fixed on branch `lane-fd94-96`, commits
  `c060e0613` (tests) and `d528d792b` (fix).
  KObject::writev (source/kernel/kobject.cpp)
  returned the failing segment's error result
  regardless of bytes already written; it now
  returns the accumulated count when any
  segment wrote, and the error only when
  nothing was written — the host's shape
  (measured: a good 3-byte first segment and a
  bad second returns 3) and the shape the
  64-bit walk already had. The old kwarn's
  'TODO file pointer' wording goes with it;
  the walk's short-write accumulate-and-
  continue behaviour is unchanged. KObject::
  writev's only caller is KProcess::writev, so
  the change is contained to the 32-bit
  dispatcher's writev (146); the new 32-bit
  pwritev walk has the same return shape by
  construction. Proof: testWritevPartialError
  (main suite) drives the walk over a KEvent
  whose counter admits one 8-byte segment and
  refuses the next — RED on the base (returned
  -EAGAIN where 8 was owed) and GREEN on the
  fix, with the first-segment-fails and
  all-good boundaries pinned; full suites
  serial — main 670/670, x64 70/70.
  Record:
  `docs/reports/fd94-fd96-pwritev-writev-20261102.md`;
  evidence `test-results/fd94-96/`.

- **CL-61 — The 32-bit dispatcher's positioned
  vectored reads are wired with the host's
  validation order and FD-83's notification
  contract (FD-98).** *(Provisional number,
  filed on lane-fd98, 2026-11-02 — CL-40..CL-50
  are held by the unmerged fd66, fd65,
  fd85/fd87, fd64, fd05, fd83-84, fd92-93 and
  fd94-96 lanes off this same base; the merge
  lane's audit settles the final number.)*
  Fixed on branch `lane-fd98`, commits
  `8edaaaea2` (test) and `86dfb5830` (fix).
  New KProcess::preadv dispatches table slots
  333/377 over KProcess::pread64. Validation
  in the order measured fresh on the host
  kernel for this lane: a negative offset is
  EINVAL ahead of the descriptor check, except
  preadv2's offset -1 — the file-position mode
  (read at the current position and advance
  it); a descriptor that cannot be read is
  EBADF, ahead of the iovcnt range check
  (EINVAL outside 0..1024); the iovec array
  check (EFAULT for a null/unmapped array with
  iovcnt > 0) precedes the preadv2 flags check
  (EOPNOTSUPP outside the defined RWF mask
  0x3f), which runs last. A later segment's
  failure returns the bytes already read; a
  short read ends the walk; the file position
  is untouched outside the -1 mode. Exactly
  one IN_ACCESS per successful call, 0-byte
  successes included (FD-83's contract): a
  byte-moving walk notifies per segment
  through the file layer and the queue
  coalesces; a success that moved no bytes
  raises it in KProcess::preadv, FD-92's readv
  shape. The mode answer is EBADF from this
  wiring's own gate, ahead of FD-84's
  in-flight core fix; the two agree once
  FD-84 composes, since the walk's per-segment
  core is one of FD-84's named sites.
  FD-95 interaction: for the (bad descriptor,
  negative offset) pair this wiring answers
  EINVAL — the host's side of FD-95's open
  question, the same side FD-93/FD-94 took for
  the write family; if the merge lane settles
  FD-95 descriptor-first instead, the offset
  check in KProcess::preadv moves behind the
  descriptor check (one reorder) and this
  closure's pinned case flips with it.
  Proof: testPreadv32Dispatch (main suite,
  real int 0x80 dispatch) — RED on the base
  (every call -ENOSYS; the full assertion
  battery failed) and GREEN on the fix; full
  suites serial — main 671/671, x64 70/70;
  browser build 'Browser export verified'
  (BUILD_INFO git_sha `86dfb5830`, wasm
  `5c0d85fc`, support archive `c4dbe35986a5`
  unchanged); JS unit 348/0, no silent-failure
  baseline change. Record:
  `docs/reports/fd98-preadv32-20261102.md`;
  evidence `test-results/fd98/`.

- **CL-62 — The 64-bit dispatcher's preadv2
  takes its file-position offset and the full
  defined read flag mask (FD-99).** *(Provisional
  number, filed on lane-fd99-100, 2026-11-02 —
  CL-52 is left unused: a sibling lane's
  report claimed it for FD-101, but that
  claim could not be verified on disk (no
  lane register carries a CL-52 closure, and
  the report's cited commits do not exist in
  the repository — checked 2026-11-02);
  CL-53/CL-54 were verified free on every
  lane register. The audit settles the final
  number.)*
  Fixed on branch `lane-fd99-100`, commits
  `9e1b617c7` (tests) and `fad85c19c` (fix).
  sys_preadv64 refused preadv2's offset -1
  outright (EINVAL) and rejected the defined
  flag RWF_NOAPPEND (0x20) as EOPNOTSUPP (mask
  0x1f); the host reads at the current position
  and advances it for -1 and accepts every flag
  in the read mask 0x3f. sys_preadv64 now
  implements the -1 file-position mode (the
  walk starts at the descriptor's position and
  seeks it forward by the bytes read; exactly
  one IN_ACCESS, as the rest of the family),
  accepts 0x3f, and takes the host's validation
  order throughout: offset validity ahead of
  the descriptor check (plain preadv on a bad
  descriptor with offset -1 answers EINVAL;
  preadv2 answers EBADF there, -1 being valid
  for it), the descriptor check ahead of the
  iovcnt range and iovec array checks, the
  flags check last (FD-100's order, applied to
  this arm here). Its iovec array check is now
  a range check like the write side's, so an
  unmapped array answers EFAULT instead of
  reading as zero-length segments. FD-95
  interaction: the offset-first order takes
  the host's side of FD-95's open question for
  the preadv arm, as CL-61 did for the 32-bit
  arm; if the merge lane settles FD-95
  descriptor-first, this arm's offset check
  moves behind the descriptor check and the
  pinned bad-descriptor cases flip with it.
  Proof: testPreadv64Syscalls sections 10-11
  (x64 suite) — RED on the base (offset -1
  -EINVAL, no bytes, position unmoved; 0x20
  -EOPNOTSUPP; the bad-descriptor order cases
  answered from the wrong side) and GREEN on
  the fix; full suites serial — main 671/671,
  x64 70/70; browser build 'Browser export
  verified' (BUILD_INFO git_sha `fad85c19c`,
  wasm `52da72d5`, support archive
  `c4dbe35986a5` unchanged); JS unit 348/0, no
  silent-failure baseline change. Record:
  `docs/reports/fd99-fd100-preadv2-family-order-20261102.md`;
  evidence `test-results/fd99-100/`.

- **CL-63 — The positioned vectored family's
  v2 flags check runs last, below the iovcnt
  range and iovec array checks (FD-100).**
  *(Provisional number, as in CL-53.)*
  Fixed on branch `lane-fd99-100`, commits
  `9e1b617c7` (tests) and `fad85c19c` (fix).
  sys_preadv64, sys_pwritev64 and
  KProcess::pwritev all tested the v2 flags
  ahead of the iovcnt range check and the
  iovec array check, answering EOPNOTSUPP for
  (out-of-range iovcnt + undefined flag) and
  (null/unmapped array + undefined flag); the
  host answers EINVAL and EFAULT there. The
  flags test now runs below both checks in
  each of the three (in sys_preadv64 as part
  of FD-99's restructure). KProcess::preadv
  already used the measured order and is
  untouched. Two neighbouring combinations
  were measured during this lane and
  deliberately left: in the two 64-bit arms
  the mode answer comes from the walk's
  per-segment core, behind the range/array
  checks, so a wrong-mode descriptor with an
  out-of-range iovcnt or a bad array answers
  EINVAL/EFAULT where the host answers
  EBADF — on the read side a facet of FD-84
  (whose fix owns the core's mode gate; an
  addendum on FD-84's entry records the
  combination), on the write side part of
  FD-93's composition; both are named here so
  the merge lane sees them. Proof: the
  flags-last pins in testPreadv64Syscalls
  section 11, testPwritev64Syscalls section
  11 and testPwritev32Dispatch section 2b —
  RED on the base (every combination
  -EOPNOTSUPP) and GREEN on the fix; shared
  suite, browser and JS figures as in CL-53.
  Record and evidence as in CL-53.
- **CL-64 — The 64-bit stack served no Arial family:
  prefix64 carried no font replacement entry, the
  mechanism the 32-bit stack serves Arial through
  (FD-86).**
  *(Filed provisionally as CL-55 on lane-fd86,
  2026-11-03 — a sweep of the lane registers that
  day showed CL-50–CL-54 claimed, CL-54 highest;
  the composing merge verifies the number by
  audit.)*
  Fixed on branch `lane-fd86` in `a3349c4cb`. The
  new canonical-set staging step
  `tools/rootfs64/stage-font-alias64.py`, wired into
  `apply-runtime-poststeps.sh` after
  `stage-fonts64.sh`, writes `"Arial"="DejaVu Sans"`
  into prefix64's `user.reg` under
  `[Software\Wine\Fonts\Replacements]`, importing the
  edit (`configure_replacements`) from
  `tools/browser/stage-wine32-fonts.py` so the two
  stacks cannot drift in shape; it refuses to stage
  an alias whose target face is absent from
  `glibc-rootfs64.zip`. The completeness checker
  gains the matching drift check, and the shipped
  win32-fonts suite test now runs both architectures
  (it ran x86 only, which is why the gap was
  invisible), its assertion unchanged. The payload
  was re-pinned as release `runtime-assets-20261103`
  (only `prefix64.zip` changed); a clean bootstrap
  from the new retained archive reproduces the
  replacement, and `bootstrap.py --check` passes
  (0 failures, 1 standing skip). Proof: the checker
  failed on this gap alone before the fix; the
  extended win32-fonts test reproduced the filed
  x64 signature (`ARIAL=0 METRICS=0 RESULT 4`, x86
  leg green) and passes on the fixed build on both
  architectures; the Gecko stage prints `MSHTML:
  RESULT 0` with zero abort lines — the shipped
  test green at the stage on a load-gated run
  after two contention-bound attempts at its
  180 s stage window (see the record); JS unit
  348/0; the wasm is
  byte-identical to the base build. Record:
  `docs/reports/fd86-arial-prefix64-20261103.md`;
  evidence: `test-results/fd86/`.

- **CL-65 — FD-23's residue is closed: `select`,
  `pidfd_getfd` and `futex_waitv` are implemented to the
  host-measured contracts, and every unimplemented
  system call on both ABIs carries a recorded
  disposition (FD-23).** *(Provisional number, filed on
  `lane-fd23res` 2026-11-02; settles at the merge audit —
  integ's settled series ends at CL-39 and sibling
  branches hold provisional CL-40..CL-51.)* Fixed on
  branch `lane-fd23res`, commit `096d5b3e3`. The host
  contracts were measured first (raw-syscall reference
  programs, 6.17.0-1020-oracle; sources and outputs in
  the lane evidence): select writes back the remaining
  time and answers EBADF for a bad descriptor in any set;
  pidfd_getfd checks flags before the pidfd, answers
  ESRCH for a terminated target and EPERM across owners,
  and installs FD_CLOEXEC over the shared description;
  futex_waitv answers with the woken entry's index,
  validates the whole array before pinning, and treats
  its timeout as absolute in the named clock. The
  implementation: select runs on a core factored out of
  pselect6 (which gains the same host pre-checks and is
  corrected by them; pselect6 never writes its timespec
  back, as on the host; select's nfds is capped at the
  emulator's 1024-descriptor sets, a stated boundary);
  pidfd_getfd dispatches beside pidfd_open in the host's
  check order; futex_waitv parks one futex slot per entry
  on a shared condition with registration under the
  pending-wake lock so a racing wake is consumed from
  the pending table with its index, and the i386 table
  is extended from 440 to 462 slots so 449 dispatches at
  all. The long tail is dispositioned in
  `tools/conformance/syscall_dispositions.json` (404
  entries in seven categories, each with a reason and a
  route), audited by the census tool: undispositioned 0,
  stale 0; census on the fixed tree reads x86-64
  implemented 202, stubbed {334 `rseq`} only, missing
  160; i386 dispatched 206, gap 244. `rseq`'s deliberate
  loud `-ENOSYS` is unchanged — its prerequisite is
  filed as FD-102 — and the 64-bit pselect6's ignored
  signal mask is filed as FD-103. Proof: new native
  tests testSelectSyscall, testPidfdGetfdSyscall and
  testFutexWaitvSyscall (x64 suite) and
  testFutexWaitv32Syscall (main suite) — RED on the base
  (the base answers -ENOSYS: the three x64 tests are the
  x64 suite's only failures, the 32-bit test the main
  suite's only failure) and GREEN with the fix; full
  native suites serial x64 72 tests and main 669 tests,
  0 FAILED; browser build 'Browser export verified'
  (wasm c290559e). Record:
  `docs/reports/fd23-residue-20261102.md`; evidence
  `test-results/fd23res/`.
- **CL-66 — The ALSA chain's mmap face delivered one
  buffer then stalled: the OSS position face reported
  the write position and a constant 0 delay.**
  GETOPTR / GETODELAY now derive from a played counter
  sourced from the bwPump ring consumer; SNDCTL_DSP_RESET
  no longer clobbers the hw_params-negotiated stream
  geometry at prepare; the reported position is
  slew-limited to half a buffer per read. Fixed on
  branch `lane-fd90` (proof test `223cb8fbf`, fix
  `dd00eee9b`, proof adjustments `adb032921`): the
  alsa-mmap-stream suite test is RED on the base with
  the recorded signature (avail 4096,0,0,0,0,0,0,0) and
  GREEN on the fix (s16mmap 18,432 frames / floatmmap
  22,528 committed across the cycles; writei avail +
  delay coherent, summing to 4096); audio-tone PASS;
  native suites serial, main and x64, 0 FAILED. Record:
  `docs/reports/fd90-oss-position-20261102.md`;
  evidence `test-results/fd90/`. (The lane recorded its
  closure in FD-90's entry and Section B only; this
  record was written by the batch-3 merge audit, which
  also settled its number — the lane's provisional
  CL-45.)
