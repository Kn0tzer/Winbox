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
  entry).** *(Critical)*
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
  texel buffers reached through a descriptor-heap array.**
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
- **FD-46 — `accept4`'s close-on-exec flag was silently
  dropped on the 32-bit side (and would have been on the
  64-bit side).** *(Medium)* Found by the FD-23 lane while
  implementing the missing 64-bit `accept4` (see section B).
  Every socket accept implementation consumed the flags word
  in an internal encoding — close-on-exec as the descriptor-flag
  bit, value 1 — while the syscall layer passed the guest's
  flags untranslated, and in the guest ABI `SOCK_CLOEXEC` is
  value 0x80000. The test never matched, so `accept4` with
  `SOCK_CLOEXEC` returned a descriptor that leaked across exec,
  with no error anywhere — a confident wrong answer, the FD-15
  shape, in the kernel. (Non-blocking worked only because the
  two encodings coincide for that flag.) Fixed on
  `lane-fd23-syscalls`: `kaccept` validates the guest flags and
  translates them at the single entry point both ABIs share;
  plain accept (flags 0) is unaffected. Proof: the new
  dispatch-level `accept4` test fails before (the number was
  unimplemented on 64-bit, and the flag assertion is the one
  the 32-bit path violated) and passes after, including
  unknown-flags refusal before the pending connection is
  consumed; full record
  `docs/reports/fd23-syscall-census-20261031.md`. Closes when
  the branch lands and is re-proven at the merge gate.
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

- **FD-38 — Directory-notification (inotify) real delivery is
  disabled because one semantics defect was never isolated.**
  *(Medium)* With the two CL-02 defects fixed, a real consumer
  still parks at the same startup point; the remaining
  incompatibility sits somewhere in event-generation/coverage
  semantics and is not reproduced by the feature's synthetic
  tests (which all passed while the product was broken). The
  entry point currently refuses loudly and guests fall back to
  polling — honest, but the feature does not exist until the
  remainder is found and delivery is proven against a real
  consumer. Evidence: integration tree
  `test-results/site-chromium-20261030/REPORT.md`
  ("unisolated remainder"; disposition commit `c9259ec42`).

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
  thread state they execute on.** *(Medium, emulator
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
---

## B. Fixed on a lane branch — awaiting merge

These are findings whose fixes are written and proven on their
branches but are **not** in the integration tree. The entries
they answer stay open until the branch lands and, where the fix
is behavioural, is re-proven there.

| Branch | What it fixes | Proof on the branch |
| --- | --- | --- |
| `lane-firstvisit` | FD-01: the desktop-initialization impatience defects (Wine win32u backport — wait raised, announcement check retries) | Fresh-profile before 0/6 → after 6/6; JS 333/0; native 648/0; completeness 0 failures |
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

---

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
