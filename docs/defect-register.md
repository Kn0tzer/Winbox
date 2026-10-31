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
  program on the first attempt. CLOSED 2026-10-31 — see CL-06.**
  *(Was Critical)*
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
  32-bit programs face the same race the 64-bit side just had
  fixed. Evidence:
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
- **FD-40 — The 64-bit boot smoke test stalls before the
  window station exists, on unmodified code too.** *(Low —
  observed, cause unproven)* During the FD-15 lane's
  regression checks, the project's `boot64-smoke` browser
  probe failed three times with the identical signature:
  the guest never reaches `create_winstation` (240 s watch
  expires), an earlier `wineboot --init` process shows as
  terminated, and guest threads sit waiting in
  `KUnixSocketObject::lockCond`. An A/B run settles that this
  is not an FD-15 effect: a baseline build of the unmodified
  integ tip `c4bab5e07` fails the same way, while the
  graphics-heavy Celeste boot probe passes on the fixed
  build. Whether this is box load, a unix-socket wait defect,
  or a relative of the known start stalls is unproven — it is
  registered so the observation is not lost. Evidence:
  `docs/reports/fd15-honest-gl-answers-20261031.md`
  (regression section); failing-run artifacts in the lane
  tree at `test-results/suite-boot64-smoke/` and
  `test-results/baseline-c4bab5e07-boot64-smoke/` (the
  baseline tree itself was a scratch tree, since removed).
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
- **FD-39 — The native suite aborts mid-run in the clone
  test group when the syscall-ABI tests run first.**
  *(Medium, suite reliability)* Running tests 610-621 in one
  process aborts with an uncaught `int` throw
  (`terminate called`, rc 134) in the clone tests after the
  syscall-ABI block; the identical window aborts on the
  pristine base tip, and every test in the window passes
  standalone. A preceding-test interaction, uncharacterized.
  Evidence: `docs/reports/fd06-portio-20261031.md`.
- **FD-40 — A suite run can flood gigabytes of crash
  diagnostics in the tail tests.** *(Medium, suite
  reliability)* After ~650 passes a full serial run can
  enter an unbounded diagnostic loop ("probably about to
  crash, could not find emulation instruction that caused
  exception") and grow its log by gigabytes until killed;
  the same flood aborted a deploy-gate attempt on the
  shipped tree, and all tail tests pass standalone. The
  diagnostic path has no rate limit. Evidence:
  `docs/reports/fd06-portio-20261031.md`;
  `docs/reports/deploy-20261031.md`.

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
  program on the first attempt (was FD-01).** Two impatience
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

---

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
