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
  UPDATE (2026-11-06, branch `lane-fd01-proof`): the closure is
  re-proven on the live batch-9 build (git_sha `d50bcc02f`, wasm
  `92b36180...`, built 2026-11-06T22:32:28Z) under the same hardened
  protocol — fresh profile per boot, restart budget pre-exhausted
  (bwretry=2), 900 s window, offline FD-47 classification. The
  batch-9 deploy lane certified 3/3 x86-64 (median ready 88.1 s)
  and 5/5 x86 (median 18.8 s) on the live build; this lane
  independently certified a further 3/3 x86-64 (95.7/105.4/126.8 s),
  for a batch-9 total of 6/6 x86-64 (median 100.5 s) and 5/5 x86 —
  every boot with the real handle (0x10048 / 0x10052), explorer
  opened, zero restarts, zero rescues, zero false-ready, zero
  failure signatures. The closure stands on batch 9; the restart
  stopgap was needed zero times and its retirement remains a
  separate decision. Record:
  `docs/reports/fd01-proof-batch9-20261106.md`; evidence:
  `test-results/fd01-proof-batch9-20261106/` (this lane) and
  `test-results/deploy-batch9-20261106/cert-x64`, `cert-x86`
  (deploy lane, re-verified from disk).
  Batch-10 composition (2026-11-06): the closure proof above
  settles as CL-136 — see section C.
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
  programs expect, and no GPU-compute API exists. CLOSED
  2026-11-06 — see CL-134.** *(High, class
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
  **Update 2026-11-05 — floors (c) and (d) raised on
  `lane-machine-definition`; FD-03 stays OPEN for (a) and (b).**
  (c) The machine definition is now 4 GiB of RAM and 2 vCPUs, each
  from its single existing source (`KSystem::guestRamInfo`,
  `KSystem::guestCpuCount`); every guest-visible face follows —
  sysinfo on both ABIs, /proc/meminfo, /proc/cpuinfo, /proc/stat,
  sysfs, both sched-affinity ABIs, the 64-bit CPUID
  logical-processor field, and getcpu (which now reports the first
  vCPU in the thread's affinity mask instead of a hardcoded 0);
  faces that hardcoded one CPU were fixed individually (see the
  lane record). (d) The Vulkan bridge's
  64-bit device-local heap is 4 GiB, matching guest RAM and the
  32-bit bridge's existing advertisement; the single-allocation
  cap stays 2 GiB, because one allocation is backed by one WebGPU
  buffer. The raise is exercised, not just advertised: the new
  guest fixture `tools/testdata/machine_definition_check.c` checks
  each answer and then uses it — a 2 GiB mmap (twice the old
  advertised total) committed and verified page by page, affinity
  pinned to vCPU 1 alone and read back, CPUID cross-checked
  against sysconf. (a) and (b) are unchanged, and the record is
  plain about why: no OpenCL ICD or CUDA runtime exists in a
  browser to exercise, and AVX/AVX2 cannot be delivered — the
  emulator has no YMM register state, no XSAVE machinery, and no
  VEX decoding in either engine, so advertising it would be a
  claim the first AVX instruction would disprove; the fixture
  pins the CPUID AVX bit clear so the negative cannot silently
  flip. Proof: native suites 686/686 and 111/111 serial,
  run-native 70 PASS, fixture ALL PASS, x64 smoke 27/27, browser
  build 'Browser export verified', JS 353/0 + legacy 115/0,
  citation audit 352 rows 0 drift. Record:
  `docs/reports/fd03-machine-definition-20261105.md`; evidence
  `test-results/machine/`.
  **Update 2026-11-07 — the floor-(c) raise measured
  end-to-end in a real program's own runtime (D22,
  `lane-d22-mindustry`).** The JVM bundled in the Mindustry
  game archive (OpenJDK 25.0.1, extracted byte-for-byte)
  was run inside the guest under the game's own launch
  arguments on the current integration code (build
  `4913603a9`, verified export): its ergonomics follow the
  raised definition exactly — `MaxHeapSize = 1073741824`
  (the 25% default of the 4 GiB the JVM sees),
  `InitialHeapSize = 67108864`, the human-readable settings
  print `Max. Heap Size (Estimated): 1.00G`, and at the Java
  API level `Runtime.maxMemory()` returns 1073741824 with
  `Runtime.availableProcessors()` returning 2; a live
  allocation then held 268435456 bytes (maxMemory/4) and
  the heap grew to 757071872 bytes without error. On the
  pre-raise definition the same defaults arithmetic gives
  a 256 MiB heap and 1 processor. Record:
  `docs/reports/d22-mindustry-ergonomics-20261107.md`;
  evidence `test-results/d22-mindustry-20261106/`.
  **Update 2026-11-06 — remaining terms (a) and (b) closed as
  proven not deliverable on `lane-fd03`; FD-03 is CLOSED — see
  CL-134.** (a) The shipped set stages Wine's `opencl.dll` and
  its unix forwarder plus the Khronos ICD loader, but no
  vendor ICD exists anywhere in the set — no `.icd` file, no
  registry vendor key, no implementation in the emulator
  source — and no CUDA binary is staged at all; the guest's
  only GPU path (the Vulkan→WebGPU bridge) exposes no OpenCL
  or CUDA surface an ICD could be written against. (b) The
  CPU has no 256-bit register state (its SIMD union is
  128-bit), no XSAVE machinery beyond the legacy mask
  (XGETBV returns x87+SSE only; XSETBV is unimplemented),
  and no VEX decoding in either engine, so no AVX/AVX2
  instruction could execute; the honest negatives stay
  pinned by the guest fixture (CPUID AVX bit clear) and the
  native self-test (XGETBV(0) = 0x3). Record:
  `docs/reports/fd03-remaining-terms-20261106.md`.
- **FD-04 — Networking gaps (campaign parked by directive; gaps
  inventoried and verified).** *(Medium)* What works: opt-in IPv4
  TCP through the relay, connect-by-name. What does not exist:
  UDP at all; guest-side explicit DNS APIs are unverified; IPv6;
  inbound connections; networking is off by default. Evidence:
  `docs/reports/chromium-networking-20261017.md`; the deployed
  relay state recorded 2026-10-29.
  Update 2026-11-07 (branch `lane-dnsresolve`): the clause
  "guest-side explicit DNS APIs are unverified" is now verified
  on the shipped browser path. A Winsock guest program resolves
  a host name with getaddrinfo, connects to the returned
  address and reads an HTTP 200 response; an unresolvable
  `.invalid` name fails promptly with WSAHOST_NOT_FOUND.
  Proven end to end on both architectures by the deterministic
  suite test `web/tests/suite/browser/winsock-dns.test.mjs`
  (fixture `tools/guest-fixtures/win32-dns.c`), commit
  `b3bc87931`; evidence `test-results/suite-winsock-dns/`;
  record `docs/reports/fd04-guest-dns-20261107.md`. FD-04
  stays open for the remaining gaps above; the shipped
  networking default is unchanged.

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
- **FD-09 — `KThread::modify_ldt` panics on invalid indexes.
  FIXED 2026-11-07 on branch `lane-d278` (commit
  `f998e2e01`) — see CL-134 (lane-local).** *(Was Medium)*
  Guest input reaching an internal-invariant panic in
  the thread-object layer; flagged by the fault-delivery
  lane as outside its area. Evidence:
  `docs/reports/cleanup-fault32-20261030.md`
  (branch `lane-cleanlog32`, "deliberately left alone").
  Update (2026-11-07): the host's answers were measured
  with raw `modify_ldt` syscalls and are now the guest's
  answers — a byte count other than one descriptor is
  -EINVAL (checked first), an unreadable descriptor
  pointer is -EFAULT, and an out-of-range index is
  -EINVAL instead of the panic; each rejection logs one
  counted `BW64-FAIL syscall.modify_ldt` line. Pinned by
  the new native tests `testModifyLdtInvalid32` and
  `testModifyLdtIndex32` (main suite 616 and 617), which
  fail on the base — 617 with this entry's exact panic
  text — and pass with the fix. Record:
  `docs/reports/d278-futex-modifyldt-fpe-20261107.md`;
  evidence `test-results/d278/`.

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
  forever. CLOSED 2026-11-06 — see CL-137.** *(Was Medium)* `BString::split("")` never advances its
  cursor, so any caller that reaches it with an uninitialized
  separator hangs the process. Found via a debugger stack of a
  hung test process; the filesystem layer now guards its own
  entry point, but the utility itself is unfixed. Owner: the
  util layer. Evidence: `docs/reports/cleanup-fs-20261031.md`
  (branch `lane-cleanfs`, "found outside this layer").
  Closed on `lane-ditems` in `14783353f`: `BString::split(const char*)`
  now returns the whole string as the single result for an empty
  (or null) delimiter, inside `split` itself, so every caller is
  covered, not only the filesystem layer's guarded entry point.
  Proof: native test `testBStringSplit` (commit `5dc0b8513`) —
  on the unpatched code the test does not fail, it hangs (killed
  by timeout, the recorded RED); on the fixed code it passes,
  with `split('/')` and `split("::")` controls unchanged.
  Evidence: `test-results/ditems/`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.
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
- **FD-14 — Fault reporting has two blind spots left.
  CLOSED 2026-11-06 — see CL-138.** *(Was Low)* `CPU64::raiseSyncFault` logs nothing itself, so
  direct kernel-side callers remain unlogged at the delivery
  point (deliveries routed through the interpreter's funnel do
  log); and the 32-bit core's two gate sites that mirror the
  64-bit ones were not confirmed in their own context.
  Evidence: `docs/reports/cleanup-cpu-core-20261030.md`
  (branch `lane-clean-cpu`, "handed off").
  Closed on `lane-ditems` in `14783353f`, both halves. (a)
  `CPU64::raiseSyncFault` now logs its own deliveries at the
  delivery point — a rate-limited `BW64-FAIL sync-fault64 ...
  delivered=1` line (first 64, then every 1024th; the flood
  discipline of the interpreter funnel) — so the direct
  kernel-side caller (rt_sigreturn's bad-frame path) is no
  longer silent; refused deliveries log nothing. Proof:
  native x64 test `testRaiseSyncFaultDeliveryLogged64`
  (commit `5dc0b8513`) — RED on the unpatched code ("delivery
  produced no delivery-point log line"), GREEN after, with
  the emitted line in the run log. (b) The 32-bit core's two
  gate sites were confirmed in their own context:
  `normal_invalid` and `normal_ud2`
  (`source/emulation/cpu/normal/normal_other.h`) recover-gate
  exactly as their 64-bit mirrors do — the skip path logs once
  per RIP through `bw64RecoverClaimRip`, the fall-through
  delivers through `KThread::signalIllegalInstruction`, which
  logs via `bw64LogGuestFault32` — no code change needed.
  Evidence: `test-results/ditems/`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

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

- **FD-193 — A futex on an unmapped word panics the
  emulator, parks on the null page, or works on a
  phantom page (frontier review D2). Filed and FIXED
  2026-11-07 on branch `lane-d278` (commit `f998e2e01`)
  — see CL-135 (lane-local).** *(Was High. The number
  is lane-local: `lane-fd176` claims FD-193 for an
  unrelated filing, and CL-135 is likewise claimed on
  sibling branches; the composition audit settles
  both.)* On the 32-bit side, a shared futex operation
  on an unmapped word reached a kernel panic ("Could
  not find futex address"), and a private WAIT proved
  neither the word nor the timeout pointer before
  parking — it read the null page and parked on
  whatever it found there. On the 64-bit side the page
  lookup materialized a fresh zero page for an unmapped
  word, so every operation silently worked on a phantom
  word. The host's answers, measured with raw syscalls:
  -EFAULT for every operation on an unmapped word,
  except a 64-bit private WAKE / WAKE_BITSET, which
  wakes nothing and returns 0; and a bitset operation
  with a zero mask is -EINVAL — the rule the 64-bit
  side already had, now enforced on the 32-bit side
  too. The guest answers the same, and each 32-bit
  rejection logs one counted `BW64-FAIL syscall.futex`
  line. Pinned by the new native tests
  `testFutexUnmapped32` and `testFutexSharedUnmapped32`
  (main suite 614 and 615) and `testFutexUnmapped64`
  (x64 suite 6), which fail on the base — the 32-bit
  cases with the panic behaviour above — and pass with
  the fix. Record:
  `docs/reports/d278-futex-modifyldt-fpe-20261107.md`;
  evidence `test-results/d278/` (the raw host-reference
  programs and their outputs are in its `hostref/`
  directory).

- **FD-194 — Floating-point faults are delivered in
  the wrong signal slot with the wrong code (frontier
  review D7). Filed and FIXED 2026-11-07 on branch
  `lane-d278` (commit `f998e2e01`) — see CL-136
  (lane-local).** *(Was Medium. The number is
  lane-local: the FD-189 lane's PlugPlay filing takes
  FD-194 in the batch-10 settlement, and CL-136 is
  likewise claimed on sibling branches; the
  composition audit settles both.)* A divide quotient
  overflow delivered SIGFPE with FPE_INTOVF and trap 4,
  where the host delivers FPE_INTDIV and trap 0 exactly
  as for divide-by-zero — the INTOVF / trap-4 pair
  belongs to the INTO instruction, whose host delivery
  is SIGSEGV and is unchanged. And
  `CPU::prepareFpuException` delivered x87 exceptions
  through the SIGSEGV slot, where the host delivers
  SIGFPE with trap 16; the guest's own handler
  classifies traps 16/19 only inside its SIGFPE
  handler, so the old slot could never produce the
  right Windows exception in the guest. Both behaviours
  measured with raw signals on a real kernel. Pinned by
  the new native test `testFpeDelivery32` (main suite
  618), which fails on the base showing both wrong
  deliveries and passes with the fix. Record:
  `docs/reports/d278-futex-modifyldt-fpe-20261107.md`;
  evidence `test-results/d278/`.
- **FD-195 — Caller audit of the no-throw segment-fault
  entry points (frontier review D5): one silent write-drop
  found and fixed; the rest proven sound. CLOSED
  2026-11-06 — see CL-140.** *(Filed and closed by
  `lane-ditems`; the number is lane-local and settles at
  the next composition.)* `KThread::seg_mapper` /
  `seg_access` with `throwException=false` promise the
  caller a delivered fault instead of a throw. The audit
  read every call site: the four explicit-false sites
  (`CPU::startException`, `CPU::runNextSingleOp`,
  `NormalCPU::run`, and the multi-threaded platform's
  containment site) each re-fetch or retire loudly after
  delivery and are sound; the default-true callers
  (softmmu read/write pages, `KMemory::preflightWrite`)
  are sound because both functions throw on every path
  that does not return from the on-demand mapping. One
  real defect: the invalid page's write accessors called
  `seg_mapper` and returned — when the on-demand mapping
  succeeded, the guest's write was silently dropped
  (reads returned a fabricated 0, right only because a
  fresh anonymous page is zero-filled). Fixed in
  `14783353f`: all six accessors re-dispatch through the
  page's current type after `seg_mapper` returns, and a
  write that still cannot land logs a `BW64-FAIL
  invalid-page-write ... outcome=dropped` line instead of
  vanishing. Proof: native test `testFaultDelivery32`
  additions (commit `5dc0b8513`) write and read back
  through the Wine syscall-frame window that triggers
  the on-demand mapping — RED on the unpatched code
  (readback mismatch), GREEN after. Audit observation,
  no action: the soft read/write page accessors would
  dereference a null page pointer if `seg_access` ever
  gained a return path; today it cannot return to them.
  Evidence: `test-results/ditems/`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

- **FD-196 — Opening a file collapsed almost every host
  error into "invalid argument" (frontier review D17).
  CLOSED 2026-11-06 — see CL-141.** *(Filed and closed
  by `lane-ditems`; the number is lane-local and settles
  at the next composition.)* `translateOpenError()` in
  `KProcess` mapped only EACCES/EEXIST/ENOENT/EISDIR and
  turned every other host open failure — a full disk,
  too many open files, a read-only filesystem — into
  -EINVAL, so a guest could not tell why an open failed
  even though the filesystem layer preserves the host
  errno exactly (`FsFileNode::open` restores it before
  returning). Fixed in `14783353f`: the map now covers
  25 errnos (adding EAGAIN, EBUSY, EFAULT, EFBIG, EINTR,
  EIO, ELOOP, EMFILE, EMLINK, ENAMETOOLONG, ENFILE,
  ENODEV, ENOMEM, ENOSPC, ENOTDIR, ENXIO, EOVERFLOW,
  EPERM, EROFS, ETXTBSY, EXDEV; `K_EMLINK` and
  `K_EOVERFLOW` added to `include/kerror.h`); the EINVAL
  default stands only for node types that return no
  host errno. Proof: native test `testTranslateOpenError`
  (commit `5dc0b8513`), a 28-row table pinning every
  mapped errno plus the default rows — RED on the
  unpatched code, GREEN after. Evidence:
  `test-results/ditems/`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

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
  have no honest answer (the FD-15 residue). CLOSED
  2026-11-07 — see CL-137.** *(Was Medium)*
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
  RESOLVED AS PROVEN UNFIXABLE 2026-11-07 on branch
  `lane-fd39-residue` (see CL-137): the four sampling
  effects cannot be expressed as texture/sampler state
  in either shipped backend. The WebGPU backend's
  sampler descriptor has no borderColor, LOD-bias or
  swizzle field (and its one swizzle escape, a
  texture-view swizzle, sits behind an optional device
  feature this backend never requests); the legacy
  route's host is WebGL2, whose ES 3.0 parameter set
  defines none of LOD bias, border color, depth-texture
  mode or the border wrap (texture swizzle alone is
  defined there -- recorded in the lane record as the
  one expressible cell, not taken: it cannot close this
  entry, whose default route is the WebGPU backend, and
  it would split rendering behaviour by route). The
  only remaining route is rewriting sampling
  expressions in translated shaders with per-texture
  state plumbed through generated pipelines -- the
  sampler/shader backend feature this entry named, not
  a fix-sized unit. The values stay honest state on
  both routes, exactly as the 2026-10-31 fix left them.
  CORRECTION to the RESOLVED IN PART paragraph above:
  the CLAMP_TO_BORDER wrap never reached a real sampler
  as "WebGPU clamp-to-border" -- WebGPU has no such
  address mode, and a real GPUDevice rejects the value
  the backend wrote (only the recording device in the
  unit tests accepted it). Fixed in `8258111c6`: the
  sampler now decodes CLAMP_TO_BORDER to clamp-to-edge
  (the Vulkan tier's disposition for the same mode),
  the raw mode stays in the params map, and the
  approximation is logged once. Evidence:
  `docs/reports/fd39-residue-20261107.md`.
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
  **Closed 2026-11-05 — see CL-119.**
  The focused test the 2026-11-05
  audit (its item L2) asked for
  settles it: the KHR spelling IS
  served on the 64-bit guest path.
  The 64-bit guest shim resolves
  `vkGetPhysicalDeviceProperties2KHR`
  — and the Features2 and
  MemoryProperties2 KHR spellings —
  to real alias wrappers that
  forward to the core wrappers, so
  a 64-bit guest's KHR call reaches
  the bridge under the core fn id;
  the 64-bit switch needs no KHR
  case because no 64-bit producer
  emits the KHR id. (The 32-bit
  shim traps the KHR id itself,
  which is why the 32-bit
  dispatcher carries the paired
  cases.) Proof: a GUARD section in
  the native bridge test calls
  through the pointer the shim's
  proc-address lookup returns for
  each KHR name and pins the core
  trap id plus byte-identical
  answers (root struct and pNext
  chain) against the core name,
  for all three query pairs;
  run-native 70 PASS, exit 0, on
  branch `lane-audit-l1l2` (test
  commit `64e7e3c0e`). The one real
  residue found on the way — the
  lookup returns different
  function addresses for a KHR
  name and its core spelling — is
  filed separately as FD-166.

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

  **CLOSED 2026-11-03 at the batch-4
  composition, CL-68.** FD-108's fix
  (CL-67) is composed, and the closing
  term is proven on the composed tree
  itself: the batch-4 gate's
  common-dependencies run passes
  unloosened with OPENAL: RESULT 0
  (evidence
  `test-results/merge-batch4-20261103/`
  and `test-results/suite-common-dependencies/`).
  The audio stack was innocent
  throughout; the samples were computed
  wrong by the x87 register-cache defect
  FD-108 records, and with the values
  right the capture hears the tone.
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
  **CLOSED 2026-11-03 by lane-fd87loss, CL-67
  (settled at the batch-4 composition).**
  The filed mechanism was wrong, and the correction
  is part of this closure: the 64-bit memory
  serving is innocent -- a page-level store/read
  trace (BW64_BSSDIAG) shows every store landing
  in the one committed buffer and every later read
  returning exactly what was stored; in the
  failing loops the STORED VALUES THEMSELVES were
  already wrong. The fixtures' `sin` is mingw
  libm's x87 wrapper (fld/fsin/fstp through
  `__sinl_internal`), and its results degraded
  across repeated calls: a bare `fldl; fsin;
  fstpl` loop printed the iterated-sine sequence
  (sin of the previous result: 0.4794255,
  0.4612696, 0.4450853, ...), while an FNSAVE dump
  after a fresh load showed the correct 80-bit
  state. Root cause: with `KSystem::useF64 ==
  false` (the 64-bit guest), `FPU::FLD_F32`,
  `FLD_F64`, `FLD_I16` and `FLD_I32` wrote the
  loaded value into the SoftFloat `regs[]` slot
  but left `isRegCached[slot]` untouched, while
  `FPU::getF64()` (reached from the FSTP handlers
  and other double readers) sets that flag and
  leaves its cached double behind -- so the next
  load into the same physical slot kept the stale
  cached value authoritative, and FSIN and every
  other cache-honoring op computed on the previous
  occupant of the slot. Fixing commit `dd6a6855d`
  (lane-fd87loss): the four load entry points now
  clear `isRegCached[store_to]` on the 80-bit
  path, matching FLD_I64, FLD_F80, LD80 and the
  constant loaders, which already did. Proof
  (lane record
  `docs/reports/fd87-x87-stale-cache-20261103.md`;
  evidence `test-results/fd87loss/`): fpprobe6's
  accumulator reads exactly 184.1776 and
  fpprobe7's post-loop arrays hold the true
  values; fpprobe16's direct-call and
  pointer-call sections agree (0.909297 /
  0.479426); fpprobe21's loop returns
  0.47942553860420301 on every iteration;
  fpprobe8's sections A/B/D (previously lost) are
  exact, fpprobe9 counts 1 zero, fpprobe19/20 are
  exact including the bare-asm section; native
  suites run serially -- main Test 0 FAILED,
  X64Test 0 FAILED, both EXIT=0; browser build
  "Browser export verified". The closing term on
  FD-85's audio stage was verified on the
  composed state (batch-3 integration tip
  caadab5ad + this fix, in a scratch tree, runtime
  assets 20261103): the shipped
  common-dependencies suite test PASSES
  (1 passed, 0 failed; OPENAL RESULT 0) with its
  checks unloosened. This also discharges FD-85's
  remaining blocker: the audio stack was innocent
  exactly as lane-fd85pay measured -- the tone
  buffer had been filled with the corrupted
  values.

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
  The kernel half landed in round 3B: a guest evdev gamepad at
  `/dev/input/event5` (15 buttons, 6 axes, Xbox-360 identity
  045e:028e) with host-feed entry points, proven natively by
  `testDevInputJoystick`. The gamepad lane (branch
  `lane-gamepad`, 2026-11-05/06) delivered the rest of the
  emulator's own chain: the SDL layer opens the first host
  controller (SDL_GameController when classifiable, raw
  joystick in the standard layout otherwise) and feeds the
  kernel entry points, with a full-state sync on open and a
  neutral state on removal — the feed is live in the browser
  build. Guest discovery is carried too: a synthetic `/sys`
  input class (`source/kernel/sys/sysinput.cpp`), statfs
  naming the `/sys` and `/proc` trees with their real
  filesystem magics (FD-172, closed as CL-125), and the udev
  data a real system would hold for the device, synthesised
  because no udev daemon runs in the guest. The purpose-built
  witness (`tools/guest-fixtures/gamepad-witness.c`,
  `web/tests/suite/browser/input-gamepad.test.mjs`) polls
  XInput and DirectInput in the guest.
  What still keeps a program from seeing the pad sits inside
  the guest's Wine layer, in two separately filed defects: on
  the 32-bit side every registration exists and the PlugPlay
  service runs, but the bus driver fails to initialise like
  every other kernel driver in this emulator (FD-173); on
  the 64-bit side the shipped prefix carries no PlugPlay or
  bus registration at all (FD-174). This entry stays open on
  those two closings; when they land, the witness test is
  the proof.
  Carried limits, by design: one controller only (the kernel
  models a single pad; XInput slots 1–3 truthfully report
  not connected); the legacy WinMM joystick interface is not
  carried (no `/dev/input/js0` exists — the kernel lane's
  recorded decision, and no current consumer uses the legacy
  driver); force feedback is not carried (the device
  advertises none).
  Evidence: `docs/reports/fd24-gamepad-20261106.md`;
  `docs/known-gaps/coverage.md` (gamepad / XInput rows).
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
  *Progress 2026-11-07 (lane `lane-cpudiff`): the CPU
  differential harness was repaired and expanded. As found on
  the integration tip it did not link (11 undefined symbols),
  its fault stub misclassified the INT3 snippet terminator so
  7,539 of 7,567 cases failed spuriously, the string-op
  builder was disabled, and three of its cases were defective
  (one, `lodsb rep`, could never run: without checkMem the
  native runner never patched the scratch pointer, so the
  native side always crashed). Repaired baseline, measured:
  4,409 forms / 7,567 cases. The lane then added control-flow
  coverage (all 16 jcc in rel8+rel32 under eight flag
  scenarios, jmp/call/ret/loop/jrcxz/jecxz, pushfq/popfq/
  lahf/sahf) and string widths and repeat modes (all widths
  of MOVS/STOS/LODS/SCAS/CMPS under REP, REPE/REPNE early
  exits, DF=1 forms, single-step forms), and re-enabled the
  original string cases: 4,494 forms / 7,920 cases, all
  passing (`test-results/cpu-differential/`). The new
  coverage immediately caught a real emulator defect — REP
  LODS executed one iteration — filed as FD-193 (lane-local)
  and fixed on this lane. Generated fuzzing, the rest of
  this entry, remains open. Evidence:
  `docs/reports/cpudiff-coverage-expansion-20261107.md`.*

- **FD-26 — Interpreter throughput is the frame-rate ceiling,
  and the instrument to target the work does not exist.**
  *(Medium)* Measured: about 90% of a Portal-class frame is
  guest execution in the interpreter; the fast engine measured
  *worse* for the 32-bit case and stays off. Material gain
  needs interpreter-throughput work, and the op-mix histogram
  that would aim it has not been built. Evidence:
  `docs/reports/frame-floor-investigation-20261027.md`;
  `docs/reports/portal-perf-profile-baseline-20261026.md`.

  *Progress 2026-11-05 (lane `lane-perf2`): one reject family
  closed. The fast engine now compiles FF /0 INC, FF /1 DEC,
  FE /0 INC and FE /1 DEC (all sizes, register and memory forms)
  instead of refusing every block containing one — the FF family
  was the largest single reject family in the October census and
  the coverage table's own note named `ff 05` as most of its
  remaining weight. On a controlled guest bench (100k passes of an
  8-wide INC/DEC timing loop per sample), the fast engine went
  from 1.6-1.8x SLOWER than the interpreter (warm medians 491 /
  958 ms against interpreter 310 / 546 ms) to 2.9-3.2x FASTER
  (89 / 102 ms against 287 / 298 ms), checksum identical in every
  mode; Doom presented 132 frames in the demo-matrix window
  against 13 before (different machine load; corroboration, not
  the controlled figure). FD-26 stays OPEN: guest execution is
  still the ceiling and most reject families remain. Record:
  `docs/reports/perf-incdec-fastpath-20261105.md`.*

  *Progress 2026-11-07 (lane `lane-fd26`): two more reject
  families closed. The fast engine now compiles the scalar
  SSE moves MOVSD (F2 0F 10/11), MOVSS (F3 0F 10/11) and the
  MOVQ load (F3 0F 7E), and the packed FP bitwise-logic ops
  ANDPS/ANDPD, ANDNPS/ANDNPD, ORPS/ORPD, XORPS/XORPD
  (0F 54/55/56/57), instead of refusing every block
  containing one. Both families are bit-exact by
  construction (moves and bitwise ops only; scalar FP
  arithmetic stays out of scope - its NaN / min-max
  semantics are not bit-identical between the engines). On
  the controlled guest bench (sseprobe64: 100k passes of an
  8-wide scalar-move loop and an 8-wide packed-logic loop
  per sample, warm medians, fresh session per engine mode,
  modes 0,1,1,0), the fast engine went from no advantage on
  the integration tip (scalar 967 / 416 ms against
  interpreter 408 / 489 ms; logic 357 / 330 ms against
  246 / 219 ms - the loops' blocks fell back whole) to
  8.6-10.8x faster than the interpreter in the same runs
  for the scalar loop (34 / 40 ms against 293 / 433 ms) and
  2.7-6.0x faster for the logic loop (82 / 34 ms against
  221 / 205 ms); battery and loop checksums identical
  across every mode and build, interpreter medians
  unchanged across builds (box load differed between the
  two bench runs; the within-run ratios are the controlled
  figures). FD-26 stays OPEN: guest execution is still the
  ceiling; scalar FP arithmetic and most other reject
  families remain. Record:
  `docs/reports/fd26-scalar-logic-fastpath-20261107.md`.*

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
  UPDATE (2026-11-06, branch `lane-vkhost-gen`): the
  next-pointer-writer panic emissions named above are
  discharged — the FD-28 fix (commit `26a90c5b6`) taught
  the generator the shipped log-once-and-skip defaults
  for `vulkanGetNextPtr`, `vulkanDeleteNextPtr` and
  `vulkanWriteNextPtr`, and regeneration is
  byte-identical. The two union types above remain open
  here. Record:
  `docs/reports/fd28-vkhost-generator-20261106.md`.
- **FD-28 — One generated graphics file has silently diverged
  from its generator. CLOSED 2026-11-06 — see CL-137
  (provisional; settles at the next composition).**
  *(Medium)* `vk_host.cpp` carries
  hand-written fixes (error propagation, map-failure handling,
  log-once unsupported operations) that a wholesale
  regeneration would destroy. It needs an owner: either the
  generator learns those implementations, or the file stops
  presenting itself as generated. Evidence:
  `docs/reports/tooling-honesty-20261030.md`.
  Closure (2026-11-06, branch `lane-vkhost-gen`): the
  generator learned the hand-written implementations — fix
  commit `26a90c5b6`. Regenerating now reproduces
  `vk_host.cpp` and every other generator output
  byte-identically (sha256-verified against the committed
  blobs), so the file's autogenerated header is true again
  and regeneration is safe. Record:
  `docs/reports/fd28-vkhost-generator-20261106.md`;
  evidence: `test-results/fd28/`.
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

  Fixed on branch `lane-buildrel`:
  the single-process mode that died
  is the harness's multi-worker
  thread mode, replaced by process
  sharding (see FD-32's closing
  account); at the default worker
  count the binary now completes
  111 tests, 0 FAILED, and the old
  thread path still dies at 79 of
  111 on the same tree -- see
  CL-122.
- **FD-32 — Multi-worker native suite runs die on the loaded
  box.** *(Low)* Runs at the harness's default worker count
  die with exit 139 inside the first few tests, in a
  thread-dump path no recent lane modified; single-worker runs
  are the trustworthy mode and every lane now uses them. Not
  A/B-tested against a pristine-tip binary by the lane that
  documented it most recently. Evidence:
  `docs/reports/cleanup-cpu-core-20261030.md`
  (branch `lane-clean-cpu`, verification).

  *Addendum 2026-11-03 (batch-4 composition):
  lane-fd87loss filed this same shape from
  its own runs and supplied the A/B test
  this entry lacked: the X64Test binary at
  the default worker count exits 139 after
  ~47-65 test entries (the death point
  varying by run — clone-sharing,
  sigaltstack and condvar-deadline areas),
  and the identical crash reproduces on
  the unmodified base build in the same
  tree, base and fixed binaries dying at
  the same test under the same conditions
  — pre-existing, and unrelated to that
  lane's change. With one worker the same
  binaries complete with 0 FAILED. The
  merge lane folds the filing into this
  entry rather than giving it its own
  number: same failure, same remedy
  (serial runs), and the open gap in this
  entry was exactly the A/B proof the
  filing supplies. The batch-3 merge
  record likewise attributed its own
  early parallel-run failure to this
  entry. Evidence: `test-results/fd87loss/`
  (suite logs).*

  *Closing account 2026-11-05
  (lane-buildrel): multi-worker runs
  are now process shards. Each shard
  process runs a strided slice of
  the entries through the serial
  mode's own fork-per-test isolated
  path (extracted unchanged), so a
  test keeps the standalone
  environment FD-41 proved it needs;
  shard output is replayed in shard
  order, failure counts return over
  a pipe, and a shard that dies
  before reporting fails all its
  tests loudly. Proof on the fixed
  tree: the preserved thread path
  (BW64_TEST_NO_SHARD=1) dies exit
  139 after 2 tests with a runaway
  guest-memfault loop, while 4
  shards complete 686 tests,
  0 FAILED, matching the serial
  total; the default invocation
  completes the same. Fixed on
  branch `lane-buildrel` -- see
  CL-123.*
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

  Fixed on branch `lane-buildrel`:
  (a) the message scratch and its
  invalidation move out of the
  64-bit gate in syscall64.cpp (the
  32-bit dispatcher's slow paths use
  the same scratch; the test build's
  stub for it is deleted, so tests
  link the real one), the execve
  invalidation is ungated in
  kprocess.cpp, and the
  multi-threaded mainloop guards its
  wire-present calls like the
  single-threaded one. The release
  build now links and starts, and
  every other native configuration
  builds. (b) not reproduced on the
  current tree: the threshold
  helper's uses outside the WASM
  JIT configuration already carry
  the guard, and the release
  configuration's full compile is
  error-free -- see CL-124.
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
  stash has rotted. CLOSED 2026-11-06 — see CL-139.** *(Was Low, operations)* The 2026-10-25 proof
  that a particular stash could be dropped relied on a backup
  directory that no longer exists, and the stash list has
  since shifted — what the proof described is no longer what
  the label points at. No stash may be dropped on the old
  proof; any future drop needs a fresh export first.
  Evidence: `docs/reports/docs-cleanup-20261030.md`
  (branch `lane-docsclean`).
  Closed on `lane-ditems` (operations; no code change): a
  fresh export of the repository's only stash
  (`stash@{0}`, commit
  `22998f7b8e551db6fa33794742c72a6b89f521e9`, "item 8
  triangle fans in progress") now sits at
  `/home/ubuntu/winbox-project/evidence/stash-export-20261106/`
  — working-tree patch, index patch, and the untracked-files
  commit's tree, with hashes and restore instructions in its
  README. The stash itself was NOT dropped. The alkhaser
  report the same review item named is index-linked from
  `docs/INDEX.md`, and the 54 pre-existing broken links stand
  accepted as recorded in
  `docs/reports/docs-cleanup-20261030.md`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.
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

  Batch-5 update, 2026-11-03: the demo's front
  blockers on current builds are closed —
  FD-127 (the device-setup park, CL-100),
  FD-128 (the worker trap, CL-101) and FD-129
  (the slot writer, CL-102) — and on the fixed
  branches the demo runs through device setup,
  creates its shader modules and pipelines in
  vivo (see FD-89's entry), opens its window,
  and stays alive and working. No present has
  been observed yet: the first frame still
  waits on FD-72/FD-74 (the helper-invocation
  class) and FD-88 (browser bind-group arrays),
  and on the downstream submit behaviour this
  entry holds open. This entry stays open.

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

  **GENERAL COLD-START SEGMENT MEASURED AND SHRUNK —
  this entry stays open** (lane-fd71, 2026-11-05;
  record:
  `docs/reports/fd71-startup-decl-scans-20261105.md`).
  The Godot portion above is untouched by this lane
  and still waits on FD-55. The general cold start
  every program pays on a first visit was attributed
  on the standard start measurement (fresh profile
  per boot): of a 47.6 s x86 boot at load 4.4, the
  file-system setup window was ~12.6 s, and inside
  it the startup code scanned every archive's
  central directory twice before initializing —
  looking for `fullfills.txt`, then `depends.txt`
  dependency declarations — at a measured 4,970 ms,
  returning nothing: none of the seven shipped
  archives contains either file (enumerated
  directly). Fix `c0397ec11`: the declarations are
  now captured during the directory walk
  `FsZip::init` already performs (first exact
  root-level match, same full-read + CRC rules; a
  corrupt entry counts as absent) and resolved
  after the listed zips initialize, with the old
  predicates and ordering unchanged, pinned by the
  new native test `testFsZipDeclCapture`
  (`7ac6802ab`; subdirectory namesake ignored,
  first duplicate wins, corrupt counts as absent).
  Proof: interleaved boot-rate A/B (base wasm
  `9b51d601` vs fixed `c70cc5b7`, x86) medians
  28,054 ms → 24,863 ms ready (−11.4%), all three
  pairs faster, 6/6 starts succeeded; JS unit
  353/0; native main 690/0; x64 start on the fixed
  build succeeded (419.1 s at load ~17–22). Also
  measured and rejected in the same record:
  switching the default engine build to JIT for
  startup (a JIT boot exhausted the 420 s harness
  budget, still progressing, against a 53.5 s base
  start the same round). The walk that remains in
  `FsZip::init` is filed as FD-166.

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
  demand-paged path is exact after CL-38; closing
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

  Update (deploy 2026-11-03, build `caadab5ad`): the
  owed in-vivo run was attempted twice on the
  shipped build through the standard demo path
  (FD-70 capture configuration, 3,300 s deadline).
  In both attempts the session booted (Godot banner
  at 114.5 s and 181 s) and the guest stayed alive
  and executing to the end of the window, but no
  window mapped inside the capture driver's 1,200 s
  launch wait, so the run obtained no shader-module
  or pipeline evidence in either direction (0
  vkCreateShaderModule lines, 0 BW64-FAIL lines in
  both preserved consoles). CL-43's pipeline clause
  therefore stays owed, still gated on FD-88 +
  FD-72; the attempts are recorded in full in
  `docs/reports/deploy-batch3-20261103.md`,
  evidence `test-results/deploy-batch3-20261103/`
  (godot-proof-attempt1/ and godot-proof/).

  Update (deploy 2026-11-03, build `600da7651`): the
  owed in-vivo run was retried once on the shipped
  build through the standard demo path (FD-70 capture
  configuration, 5,400 s deadline), this time with the
  capture driver's launch wait extended from 1,200 s
  to 2,700 s and the run sequenced into the quietest
  window of the deploy (the sibling lane's build and
  suite both finished first; 1-minute load 2.24 at
  launch, against ~3–12.7 across batch 3's attempts).
  The session booted (Godot banner at 173.2 s) and
  completed Vulkan instance creation and vkd3d-proton
  device initialisation (device caps at 210.4 s, the
  console's last Vulkan milestone), but no window
  mapped inside the extended 2,700 s launch wait, so
  the run again obtained no shader-module or pipeline
  evidence in either direction (console 16,369 lines:
  0 vkCreateShaderModule lines, 0
  vkCreateGraphicsPipelines lines, 0 BW64-FAIL lines,
  present count 0 throughout). The guest stayed alive
  and executing to the expiry (final metrics windows
  90–146 syscalls per 500 ms window) but parked:
  after 1,000 s the console carries only metrics and
  thread dumps, and the thread dump at 2,675.3 s
  shows the guest process running with
  min_noprogress_ms 2,460,001 and its threads parked
  in ntdll. CL-43's pipeline clause therefore stays
  owed, still gated on FD-88 + FD-72; on this
  evidence the batch-3 ambiguities are excluded —
  neither the 1,200 s wait nor box load explains the
  missing window. One attempt was made and no second:
  batch 3's two attempts at the shorter wait already
  established the shape's reproducibility, and this
  attempt removed the two confounds they left open.
  The attempt is recorded in full in
  `docs/reports/deploy-batch4-20261103.md`, evidence
  `test-results/deploy-batch4-20261103/` (godot-proof/).

  Update (batch-5 composition, 2026-11-03):
  the FD-128 lane's branch capture (diag-fix1,
  on the FD-127-fixed tree) supplies the
  in-vivo account this clause has been waiting
  on: 93 shader modules created with zero
  refusals; 6 graphics + 68 compute pipelines
  created; 6 vkQueueSubmit + 6 vkQueueSubmit2;
  the FD-72 helper-invocation module met in
  vivo and refused by name under FD-75; no
  swapchain or present observed — the guest
  settles into the downstream shader-retry
  cycle. The clause itself — the two
  pipelines' creations succeeding in vivo on a
  SHIPPED build — remains owed to the batch-5
  deploy, which will carry the FD-127, FD-128
  and FD-129 fixes (CL-100, CL-101, CL-102).

  Update (batch-5 deploy, 2026-11-03, build
  `19babd34a`): the owed in-vivo run was made
  once on the shipped build, with the
  lineage-committed capture driver
  (`web/tests/fd128-godot.mjs`) in the FD-128
  lane's capture configuration (warmed archive,
  vktrace on, vkd3d shader dumps to the guest
  home, guest thread dumps on, 1,200 s deadline)
  pointed at the public live site, so the bytes
  under test are the shipped ones (the driver's
  recorded stamp is the shipped stamp:
  `19babd34a` / wasm `05298a62`, dirty 0).
  Evidence: `test-results/deploy-batch5-20261103/`
  (godot-proof/, godot-driver-out.log); the full
  account is in
  `docs/reports/deploy-batch5-20261103.md`. The
  demo booted (banner at 97.6 s) and - for the
  first time in a deploy-side run - its window
  mapped, at 148.8 s; the FD-127 park does not
  occur on the shipped build. 155
  vkCreateShaderModule creations succeeded (143
  distinct module hashes; the first, at 130.6 s,
  is the 1,216 B module, hash `f598618b6233f21a`,
  the same first module as the branch captures).
  Exactly 2 creations were refused, both the
  FD-72 helper-invocation module (5,704 B, hash
  `0b3b4d8cc939485a`), logged REFUSED with result
  -8 (VK_ERROR_FEATURE_NOT_PRESENT) at 177.5 s
  and 178.2 s - refused by name under FD-75, as
  designed. The bridge trapped 6
  vkCreateGraphicsPipelines calls and 130
  vkCreateComputePipelines calls; the only
  failed pipeline attempts in the run are the
  guest's own two Create(Graphics)PipelineState
  errors (0x80004005, at 177.5 s and 178.2 s),
  each the direct consequence of one FD-72
  refusal. 6 vkQueueSubmit2 calls (0 plain
  vkQueueSubmit); 5 vkAllocateMemory, 32
  vkCreateImage, 10 vkCreateBuffer, 355
  vkCreatePipelineLayout. No vkCreateSwapchainKHR
  and no vkQueuePresentKHR occurred; the metrics
  present count reached 2 and the Vulkan present
  count stayed 0, and the final display was the
  mapped window without rendered frames (5,701
  lit pixels, 0.44% non-dark) - the guest settles
  into the downstream shader-retry cycle (module
  and compute-pipeline creations recurred
  intermittently to at least 1,170.7 s without
  changing any outcome). The run's 8,674-line
  console carries 0 BW64-FAIL lines; the driver's
  CDP exception watch captured no wasm trap (no
  exceptions file) - the FD-128 death does not
  occur on the shipped build; the guest was alive
  and executing at the 1,200 s deadline (driver
  exit 0). 5 bridge DROPPED lines, all
  destroy-of-unknown-object guards (1
  vkDestroySemaphore, 4 vkDestroyPipelineCache).
  Verdict on CL-43's clause, stated plainly: NOT
  discharged on its exact terms. Neither of the
  clause's two modules was created in this run -
  no 369,400-byte and no 2,340-byte creation
  appears (0 occurrences of either size) - so
  their in-vivo creation on a shipped build
  remains unmeasured: this capture's guest reaches
  the FD-72 module first, is refused there, and
  cycles without reaching the phase that produced
  those modules in the FD-70-era run. What the run
  settles: on the shipped build the demo passes
  every blocker this lineage fixed (FD-127's park,
  FD-128's trap, FD-129's writer - CL-100, CL-101,
  CL-102) with its window mapped and the
  module/pipeline account above, and the only
  in-vivo pipeline failures remaining in the
  capture are FD-72's. One attempt was made and no
  second, per this clause's one-run-per-deploy
  pattern; the clause stays owed, its remaining
  gate now FD-72 (with FD-74 and FD-88 behind it,
  per FD-55's entry).

  Update (batch-6 deploy, 2026-11-04, build
  `49c67137d`): the owed in-vivo run was made
  once more on the shipped build, with the same
  capture driver and configuration (warmed
  archive, vktrace on, vkd3d shader dumps to the
  guest home, 1,200 s deadline) pointed at the
  public live site, so the bytes under test are
  the shipped ones (the driver's recorded stamp
  is the shipped stamp: `49c67137d` / wasm
  `9abd2f24`, dirty 0). Evidence:
  `test-results/deploy-batch6-20261104/`
  (godot-proof/, godot-driver-out.log); the full
  account is in
  `docs/reports/deploy-batch6-20261104.md`. The
  demo booted (banner at 99.3 s) and its window
  mapped at 144.6 s; the FD-127 park does not
  occur on the shipped build. 144
  vkCreateShaderModule creations succeeded (133
  distinct module hashes; the first, at 126.7 s,
  is the 1,216 B module, hash `f598618b6233f21a`,
  the same first module as the branch captures).
  Exactly 2 creations were refused, both the
  FD-72 helper-invocation module (5,704 B, hash
  `0b3b4d8cc939485a`), logged REFUSED with result
  -8 (VK_ERROR_FEATURE_NOT_PRESENT) at 242.7 s
  and 243.2 s - refused by name under FD-75, as
  designed. The bridge trapped 6
  vkCreateGraphicsPipelines calls and 121
  vkCreateComputePipelines calls; the only failed
  pipeline attempts are the guest's two
  Create(Graphics)PipelineState 0x80004005 lines,
  one per refusal. Also trapped: 6
  vkQueueSubmit2 (0 plain vkQueueSubmit), 5
  vkAllocateMemory, 32 vkCreateImage, 10
  vkCreateBuffer, 349 vkCreatePipelineLayout. No
  vkCreateSwapchainKHR and no vkQueuePresentKHR
  occurred; the metrics present count reached 2.
  The 8,589-line console carries 0 BW64-FAIL
  lines; no wasm trap was captured; the guest
  was alive and executing at the 1,200 s
  deadline (last module creation at 1,176.3 s).
  5 bridge DROPPED lines, all
  destroy-of-unknown-object guards (1x
  vkDestroySemaphore, 4x vkDestroyPipelineCache).

  Verdict: the clause is again NOT discharged on
  its exact terms - neither named module
  (369,400 B / 2,340 B) was created in this run
  (0 occurrences of either size among the 144
  creations), so their in-vivo creation on a
  shipped build remains unmeasured; this
  capture's guest reaches the FD-72 module
  first, is refused there, and cycles without
  reaching the phase that produced those modules
  in the FD-70-era run. One attempt was made and
  no second, per this clause's one-run-per-deploy
  pattern; the clause stays owed, its remaining
  gate FD-72 (with FD-74 and FD-88 behind it, per
  FD-55's entry).

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
  delivered. FIXED 2026-11-02 on branch
  `lane-fd105-108` — see CL-69.** *(Was Low.
  Filed by the FD-102..104 lane, 2026-11-02;
  settled as FD-111 at the batch-3
  composition.)* Found
  closing FD-104: the dispatcher's recvfrom
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
  whole chain. FIXED 2026-11-02 on branch
  `lane-fd105-108` — see CL-70.** *(Was Low.
  Filed by the FD-102..104 lane, 2026-11-02;
  settled as FD-112 at the batch-3
  composition.)* Measured
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
  the call reports success. FIXED
  2026-11-02 on branch `lane-fd105-108` —
  see CL-71.** *(Was Low. Filed
  by the FD-102..104 lane, 2026-11-02;
  settled as FD-113 at the batch-3
  composition.)* Code evidence, found auditing
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
  the peer can receive. FIXED 2026-11-02
  on branch `lane-fd105-108` — see
  CL-72.** *(Was Low. Filed by
  the FD-102..104 lane, 2026-11-02;
  settled as FD-114 at the batch-3
  composition.)* Measured on this base while
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

- **FD-115 — A first-visit 32-bit start of the
  shipped build can wedge before the desktop
  handshake: a winedevice-host guest thread loops
  on a delivered memory fault, the desktop never
  opens, and the start fails silently.
  FIXED 2026-11-05 on branch
  `lane-fd115` — see CL-127.** *(Medium.
  Filed by the wedge-incidence lane, 2026-11-02,
  as FD-103 — the next free number on the
  register that lane based on. That number was
  taken on the composed register by the batch-3
  audit's readv settlement (first-composed wins),
  so the batch-4 audit settles this entry as
  FD-115.)* Observed once, during
  the deploy checks of the shipped build
  `8ac504abf` / wasm `9a7d101c` (the FD-66 + FD-65
  + FD-85 + FD-77-stall-surface batch), measured
  through the public route: fresh browser profile
  per boot, the deskpin-anchored harness session
  (session=1), the launcher's restart budget
  pre-exhausted (bwretry=2), a 900 s window,
  classified offline under the hardened FD-47
  rule. Deploy record:
  `docs/reports/deploy-fd66-65-85-fd77surface-20261102.md`;
  evidence
  `test-results/deploy-fd66-65-85-fd77surface-20261102/`.
  The instance is boot 1 of that deploy's x86
  n=5: result class "other", pageReady false, note
  "budget exhausted", wall 901,393 ms. Its
  preserved console (684 lines,
  `live-x86/boot-1/console.log`) starts normally —
  wineserver32 and the deskpin spawned, the
  fontconfig set served (5 BW64-INFO served-link
  lines) — and then a guest thread of the
  winedevice host process (pid=71, tid=76; the
  session had opened `winedevice.exe` -> fd 63)
  enters a delivered-memfault loop it never
  leaves: the first fault reads addr=`0x10584384`
  eip=`0x10584384` err=0 (count=1), then
  addr=`0xfffffd08` at the same eip, err=2,
  repeated. The console preserves 65 BW64-FAIL
  guest-memfault32 lines — counts 1–64
  consecutively and a final preserved line at
  count=1024, so the thread took at least 1,024
  delivered faults inside the window. The closing
  thread dumps show that thread still running at
  rip=`0x10584384` with noprogress_ms=780018. The
  desktop handshake never completed (no explorer
  OPENED signal, no deskpin READY line), zero
  session restarts occurred, and the FD-77 stall
  surfacing did not fire — by its stated design it
  reports only stalls after the handshake — so
  within the window the start presented as an
  endless wait, not a reported failure. No
  mechanism is claimed for the fault; the deploy
  record claims none and the incidence work below
  adds none. The deploy's other starts were
  clean: the four following x86 starts certified
  (handle `0x10052`, certified-ready 33.6–47.1 s,
  median 40.8 s per the preserved summary), all
  three x64 starts certified, and six consumer
  Chromium sessions mapped with clean consoles;
  the 65 BW64-FAIL lines are the only failure
  lines in the deploy's 8-boot census (5,141
  lines). MEASURED INCIDENCE (wedge-incidence
  lane, 2026-11-02): the identical protocol and
  driver (a byte-identical copy of the deploy's
  `live-driver.mjs`, md5
  `fc1a4cadc8ceb764ac7c9b8aa592f6f5`), n=10
  further fresh first-visit x86 starts against
  the same live build (BUILD_INFO re-read during
  the series: git_sha `8ac504abf`, wasm
  `9a7d101c`). Result: 10/10 certified, handle
  `0x10052` on every boot, certified-ready
  22.6–24.9 s (median 24.2 s), zero restarts,
  zero false-ready. Census of the ten preserved
  consoles (3,170 lines): 0 BW64-FAIL lines of
  any kind, 0 guest-memfault32 lines, exactly 5
  BW64-INFO served-link lines and 3 explorer
  OPENED lines per boot. The wedge did not
  reproduce, so no cleaner signature was
  obtained; the deploy instance remains the only
  one. Combined count on the shipped build: 1
  wedged start in 15 measured first-visit x86
  starts (the deploy's 5 + this series' 10), 14
  certified. That bounds the observed rate at
  one-in-fifteen so far; it does not prove the
  rate, and one silent wedged first visit is a
  defect at any rate — first-visit session-start
  reliability is the project's stated top
  usability priority — so the entry stays open.
  Noted without any claim of identity: the
  FD-54-batch deploy's Godot proof run recorded
  one delivered guest memory fault (64-bit, addr
  `0x2140bf26c`, rip `0x140301916`; see FD-89's
  entry) that was observed, unattributed, and not
  filed; the shapes differ (a single delivered
  fault in a run that continued, against a
  pre-handshake fault loop in a 32-bit start) and
  no shared mechanism is asserted — the
  cross-reference exists so the two observations
  are findable from each other. Closing terms:
  the fault's mechanism is identified and fixed,
  and a first-visit series at least this size on
  the fixed build shows no wedge and no memfault
  lines; OR a pre-handshake stall of this shape
  is surfaced to the user as a reported failure
  instead of an endless wait (FD-77's surfacing
  covers only post-handshake stalls). Lane
  record:
  `docs/reports/fd103-wedge-incidence-20261102.md`;
  series evidence
  `test-results/wedge-incidence-20261102/` (on
  the lane tree: the driver copy, driver.log,
  `series/results.jsonl`, `series/summary.json`,
  `series/boot-1..10/console.log`,
  `series-census.txt`, `live-build-info.json`).
  **Update (batch-3 deploy, 2026-11-03):** that
  deploy's checks added 5 further clean first-visit
  x86 starts on the shipped batch-3 build (5/5
  certified, 0 BW64-FAIL); the running tally on
  shipped builds is now 1 wedged in 20 measured
  first-visit x86 starts. The entry stays open on
  its closing terms.
  **Update (batch-4 and batch-5 deploys,
  2026-11-03):** the batch-4 deploy's checks
  (build `600da7651`) added 5 further clean
  first-visit x86 starts (5/5 certified, census
  0 BW64-FAIL), bringing the tally to 1 wedged
  in 25; the batch-5 deploy's checks (build
  `19babd34a`, deploy record
  `docs/reports/deploy-batch5-20261103.md`) added
  5 more (5/5 certified, handle `0x10052` every
  time, certified-ready median 21.8 s, 0
  BW64-FAIL in the boots' consoles). The running
  tally on shipped builds is now 1 wedged in 30
  measured first-visit x86 starts. The entry
  stays open on its closing terms.

  **Update (batch-6 deploy, 2026-11-04):** the
  batch-6 deploy's checks (build `49c67137d`,
  deploy record
  `docs/reports/deploy-batch6-20261104.md`) added
  5 further clean first-visit x86 starts (5/5
  certified, handle `0x10052` every time,
  certified-ready median 20.8 s, 0 BW64-FAIL in
  the boots' consoles). The running tally on
  shipped builds is now 1 wedged in 35 measured
  first-visit x86 starts. The entry stays open
  on its closing terms.

  **Update (FD-115 lane, 2026-11-05):**
  the fault's mechanism was not
  identified — the 2026-11-02 instance
  remains the only capture of the
  fault-loop shape, on a build several
  batches old, and the shape did not
  recur in this lane's measured starts.
  The entry closes on its second
  closing term: a pre-handshake stall
  of this shape is now surfaced as a
  reported failure. Fix `de0144c72` on
  branch `lane-fd115`: the launcher's
  stall watchdog counts printed
  delivered-fault lines (BW64-FAIL
  guest-memfault32, delivered=1) per
  guest thread before the desktop
  handshake completes; at 32 lines
  from one thread it reports the start
  as failed (BW64-FAIL
  component=launcher.session
  reason=prehandshake-fault-loop, with
  the user-facing failure page) instead
  of an endless wait. The count clears
  at the handshake; faults spread
  across threads never convict. Proof:
  unit tests RED on the base, GREEN
  with the fix (stall-watchdog.test.mjs);
  the shaped-session probe's fault-loop
  scenario passes against the built
  dist; JS unit suite 353/0 on the
  fixed tree. Series (identical
  protocol: fresh profile per boot,
  bwretry=2, 900 s window, FD-47
  classification): against the live
  shipped batch-7 build, 19 measured
  first-visit x86 starts — 18 certified
  and one silent stall of the different
  FD-165 shape (no fault lines; filed
  and closed separately, CL-120); the
  fault-loop shape did not recur, so
  the shipped-build tally for this
  shape is now 1 wedged in 54 measured
  starts. On fixed builds: 34/34
  measured certified on the build
  carrying only this fix (`4ce0dc424`;
  one further boot never started — a
  harness probe-slot timeout on the
  shared box), and 36/36 certified on
  the final lane build (`b7f5d4003`,
  this fix plus FD-165's), with 0
  BW64-FAIL and 0 guest-memfault32
  lines in every preserved console and
  0 restarts; any recurrence on such a
  build now ends as a reported failure.
  Record:
  `docs/reports/fd115-fd165-prehandshake-surfacing-20261105.md`;
  evidence `test-results/fd115/` — see
  CL-127.

- **FD-123 — A zero-length plain write on
  a 64-bit datagram socket queues nothing,
  where the host queues an empty
  datagram. FIXED 2026-11-03 on branch
  `lane-fd116-117` — see CL-80.** *(Was
  Low. Filed by the
  FD-113..115 lane, 2026-11-03; settled as FD-123 at the batch-4
  composition.)* Measured
  on the host
  (`test-results/fd113-115/hostref-fd115.c`):
  write(fd, buf, 0) on a connected datagram
  socket returns 0 and the peer's recv
  returns 0 for a real empty datagram.
  Every other emulator write path queues
  it (the 32-bit write, and sendmsg per
  FD-119), but the 64-bit dispatcher's
  sys_write64 returns 0 on a zero count
  before the descriptor is even resolved,
  so the kobject never sees the call. The
  writev gather is NOT this defect: a
  gather totalling zero queues nothing on
  the host too (measured in the same
  program, and covered by CL-79). Filed,
  not fixed, by the FD-113..115 lane; the
  fix belongs with the 64-bit write
  family's own lane.

- **FD-124 — The JS slice test
  unix-connect-handoff no longer compiles
  against the current kunixsocket.cpp: the
  destructor and connect/accept it
  extracts outgrew its stub class.
  FIXED 2026-11-03 on branch
  `lane-fd116-117` — see CL-81.**
  *(Was Low. Filed by the FD-113..115 lane,
  2026-11-03; settled as FD-124 at the batch-4
  composition.)* Found running the full
  JS unit suite on a built tree: the
  test's stub KUnixSocketObject has no
  abstractRegistered / abstractName /
  oneWayPeerLink members and no
  recvBuffer, and the helpers the current
  destructor and connect path use
  (abstractRegistry, resolveNamedPeer and
  friends) are not stubbed, so its
  extracted check.cpp fails to compile.
  Verified failing identically on the
  unmodified base `963272707` — the
  breakage came with FD-118's abstract
  registry and one-way datagram links,
  and the FD-109..112 lane's JS accounting
  (272 passed / 76 failed on an
  un-bootstrapped tree, failures sampled)
  did not isolate it. The product code is
  not implicated: the native suites cover
  the same paths and pass. The harness
  completion is its own piece of work.

- **FD-125 — SOCK_SEQPACKET unix sockets
  are not modelled: a seqpacket socketpair
  is refused and a singly created seqpacket
  socket behaves as a stream socket.
  FIXED 2026-11-03 on branch
  `lane-fd118-119` — see CL-82.** *(Was
  Low. Filed by the FD-116..117 lane,
  2026-11-03; settled as FD-125 at the
  batch-4 composition.)* Measured on the host
  (`test-results/fd116-117/hostref-fd116.c`):
  a connected seqpacket socket's
  zero-length write queues an empty record
  (the peer's recv returns 0, the next
  recv -EAGAIN), and nonzero writes keep
  record boundaries. The emulator defines
  the type constant but nothing implements
  it: ksocketpairFds answers -EINVAL for
  the type, and ksocket(AF_UNIX,
  SOCK_SEQPACKET) creates an object every
  path treats as stream (the datagram
  branch is the only non-stream branch),
  so records merge into a byte stream and
  an empty write queues nothing. Found by
  the FD-116..117 lane's host reference
  pass; not part of FD-123, whose filed
  shape is the datagram dispatcher's
  zero-count return. Filed, not fixed.

- **FD-126 — A datagram send to a peer
  whose descriptor was just closed answers
  -ENOTCONN, where the host answers
  -ECONNREFUSED for the first send after
  the close. FIXED 2026-11-03 on branch
  `lane-fd118-119` — see CL-83.** *(Was
  Low. Filed by the
  FD-116..117 lane, 2026-11-03; settled as FD-126 at the batch-4
  composition.)* Measured
  on the host
  (`test-results/fd116-117/hostref-fd116.c`):
  on a datagram pair, closing the peer and
  sending answers -ECONNREFUSED once;
  later sends answer -ENOTCONN. The
  emulator destroys the peer object with
  its descriptor, so the surviving
  socket's connection no longer resolves
  and every send — any path, any count —
  answers -ENOTCONN immediately. The
  one-shot refusal needs the socket layer
  to remember a closed peer distinctly
  from a never-connected one. Found by the
  FD-116..117 lane's host reference pass;
  the FD-123 guest test pins the
  emulator's standing -ENOTCONN for the
  zero-length case with this entry as the
  reference. Filed, not fixed.


- **FD-116 — sys_recvmsg64 reports a
  fabricated source name for queued
  messages: the reported length is the
  guest's offered capacity and the copied
  bytes are bounce-buffer content,
  whatever the sender was.
  FIXED 2026-11-03 on branch
  `lane-fd109-112` — see CL-73.** *(Was Low. Filed
  by the FD-105..108 lane, 2026-11-02;
  settled as FD-116 at the batch-4
  composition.)* Found staging FD-113: the
  object layer's recvmsg writes
  msg_namelen only on its stream-buffer
  fallback (0, for connected sockets); on
  the message-queue path it never writes
  it, so the bridge's initialised value
  — min(nameLength, 256) — survives as
  the reported length, and the copyback
  copies that many bytes from the bounce
  sockaddr. Measured on the host
  (`test-results/fd105-108/hostref-name-fd107.c`):
  a named datagram sender's address
  arrives with its real length (11 for
  the measured abstract name) and an
  unnamed sender reports 0 and copies
  nothing. Fixing it needs the object
  layer to carry the sender's address
  with the queued message — a design
  step, not a copyback change.

- **FD-117 — Data queued as a message is
  invisible to read and recvfrom: the
  object's read path drains only the
  stream buffer, so a datagram sent with
  sendmsg (or, after FD-114, sendto)
  cannot be received by the single-call
  receive at all.
  FIXED 2026-11-03 on branch
  `lane-fd109-112` — see CL-74.** *(Was Low. Filed
  by the FD-105..108 lane, 2026-11-02;
  settled as FD-117 at the batch-4
  composition.)* Code evidence, found staging
  FD-111: KUnixSocketObject::read waits
  on and drains recvBuffer only, and
  recvfrom routes through it; the
  message queue is consulted by recvmsg
  and by poll/readiness but never by
  read. On the host a datagram is
  received by whichever receive call the
  program uses. The stream side has the
  same split (sendmsg on a stream pair
  queues a message read cannot see). A
  fix has to settle message-boundary and
  partial-consumption semantics for the
  byte-stream read path.

- **FD-118 — Datagram delivery to a named
  unix socket has no route: sendto with a
  destination address answers
  -EDESTADDRREQ for every unix socket,
  and connect() on a datagram socket sets
  the connected flag without linking a
  peer object.
  FIXED 2026-11-03 on branch
  `lane-fd109-112` — see CL-75.** *(Was Low. Filed
  by the FD-105..108 lane, 2026-11-02;
  settled as FD-118 at the batch-4
  composition.)* Code evidence
  (source/kernel/kunixsocket.cpp):
  sendto's dest_addr branch returns
  -K_EDESTADDRREQ unconditionally
  (logged BW64-FAIL in the code itself),
  and connect()'s datagram path sets
  connected = true and returns -K_EISCONN
  without setting the connection the send
  paths deliver through, so neither
  addressed nor connected sends can reach
  a bound named peer; /dev/log is the one
  working named destination, and only
  because the stream write path prints
  it. FD-114 fixed the connected-pair
  case only.

- **FD-119 — A zero-length datagram sent
  with sendmsg is silently dropped: the
  queueing tail reads the socket filter's
  0 as 'filtered' and queues nothing,
  where the host delivers an empty
  datagram. FIXED 2026-11-03 on branch
  `lane-fd109-112` — see CL-76.** *(Was Low. Filed
  by the FD-105..108 lane, 2026-11-02;
  settled as FD-119 at the batch-4
  composition.)* Code evidence:
  KSocketObject::applySocketFilter
  returns the packet length when no
  filter is installed — 0 for an empty
  packet — and KUnixSocketObject::sendmsg's
  tail treats !kept as filtered and
  returns the submitted length without
  queueing. FD-114's sendto branch
  distinguishes the two cases and queues
  the empty message; sendmsg's tail was
  left as found.

- **FD-120 — sendto/connect to a pathname
  whose socket has closed answers -ENOENT
  in the emulator, where the host answers
  -ECONNREFUSED for as long as the file
  remains. FIXED 2026-11-03 on branch
  `lane-fd113-115` — see CL-77.** *(Was
  Low. Filed by the
  FD-109..112 lane, 2026-11-03; settled
  as FD-120 at the batch-4 composition.)* Measured
  on the host
  (`test-results/fd109-112/hostref-connect-fd111.c`):
  after the bound socket closes, its path
  node remains and addressed sends answer
  -111 (-ECONNREFUSED); only after unlink
  do they answer -2 (-ENOENT). In the
  emulator the binding socket's destructor
  unlinks its node, so a stale path
  resolves as ENOENT instead. The FD-118
  resolver already distinguishes ENOENT
  (no node) from ECONNREFUSED (node that
  is not a live unix socket); the gap is
  the unlink timing.

- **FD-121 — Stream bytes queued by
  sendmsg and bytes in the stream buffer
  do not keep call order at the read
  paths: the host serves write() and
  sendmsg() bytes in call order, the
  emulator's two stores are always read
  queue first. FIXED 2026-11-03 on branch
  `lane-fd113-115` — see CL-78.** *(Was
  Low. Filed by the
  FD-109..112 lane, 2026-11-03; settled
  as FD-121 at the batch-4 composition.)* Measured
  on the host
  (`test-results/fd109-112/hostref-read-fd110.c`):
  a stream write of "Z" followed by a
  sendmsg of "W12" reads back "ZW12". The
  emulator keeps stream sendmsg payloads
  in the message queue and plain writes
  in recvBuffer, and FD-117's read drains
  the queue first, so interleaved calls
  can read back out of order. Serving the
  host order needs one ordered store (or
  an order record across the two).

- **FD-122 — writev on a connected
  datagram socket queues one datagram per
  iovec segment, where the host delivers
  a single datagram for the whole
  gather. FIXED 2026-11-03 on branch
  `lane-fd113-115` — see CL-79.** *(Was
  Low. Filed by the
  FD-109..112 lane, 2026-11-03; settled
  as FD-122 at the batch-4 composition.)* Measured
  on the host
  (`test-results/fd109-112/hostref-writev-fd115.c`):
  writev of two segments ("AB", "CD") on
  a datagram pair arrives as ONE datagram
  "ABCD". The emulator's writev walks its
  segments through the per-call write,
  and FD-117's writeNative datagram branch
  queues one datagram per call, so the
  receiver reads two datagrams ("AB",
  "CD").

- **FD-130 — A datagram socket whose pair
  peer closed answers 0 (end of stream)
  for a read once its queue is drained,
  where the host answers -EAGAIN:
  datagram has no end of stream. FIXED
  2026-11-03 on branch `lane-fd120-123`
  — see CL-84.** *(Was Low.
  Filed by the FD-125..126 lane,
  2026-11-03; provisional until the merge
  lane's audit.)* Measured on the host
  (`test-results/fd118-119/hostref-fd118-119b.c`):
  on a datagram pair, after the peer
  writes one byte and closes, the survivor
  receives the byte and a further
  nonblocking recv answers -EAGAIN. The
  emulator returns the queued byte and
  then 0, because the pair destructor's
  inClosed marking reaches the read wait
  loop's end-of-stream return for every
  socket type. FD-110 shaped the datagram
  read gates around inClosed for queued
  data; the drained case was not in its
  matrix. Found by the FD-125..126 lane's
  host reference pass while measuring
  FD-126 — the receive side of the same
  peer accounting, outside FD-126's filed
  send shape. The FD-126 guest test pins
  the standing 0 with this entry as its
  reference. Filed, not fixed.

- **FD-141 — A receive on a
  never-connected socket answers -EPIPE
  for stream and seqpacket alike, where
  the host answers -EINVAL for stream,
  -ENOTCONN for seqpacket and -EAGAIN
  for datagram. FIXED 2026-11-03 on
  branch `lane-fd120-123` — see
  CL-85.** *(Was Low. Filed by the
  FD-125..126 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* Measured on the host
  (`test-results/fd118-119/hostref-fd118-119b.c`):
  a nonblocking recv on a fresh socket
  answers -EINVAL (stream), -ENOTCONN
  (seqpacket), -EAGAIN (datagram). The
  emulator's read and readNative gates
  answer -EPIPE whenever there is no
  connection, no inClosed and an empty
  queue, exempting only datagram — so
  stream and seqpacket both answer
  -EPIPE where the host gives two other
  answers. Pre-existing stream-family
  behaviour, found by the FD-125..126
  lane's host reference pass while
  measuring FD-125's unconnected shapes;
  outside FD-125's filed record shape.
  Filed, not fixed.

- **FD-142 — A connection-oriented
  connect to a bound but not listening
  socket is queued as a pending
  connection — a blocking connect waits,
  a nonblocking one answers
  -EINPROGRESS — where the host answers
  -ECONNREFUSED at once. FIXED
  2026-11-03 on branch `lane-fd120-123`
  — see CL-86.** *(Was Low. Filed by
  the FD-125..126 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* Measured on the host
  (`test-results/fd118-119/hostref-fd118-119c.c`):
  a seqpacket connect to a seqpacket
  socket that is bound but not listening
  answers -ECONNREFUSED. The emulator's
  mutual connect branch never consults
  the destination's listening state: it
  queues the client on the destination's
  pending list, where only an accept --
  which a non-listener never issues --
  or the destination's destruction
  resolves it. The stream branch has the
  same shape and the same divergence.
  Found by the FD-125..126 lane's connect
  matrix pass; FD-125's guest test
  deliberately does not pin the case.
  Filed, not fixed.

- **FD-143 — A connected stream
  socket's sendto with a destination
  answers -EOPNOTSUPP, where the host
  answers -EISCONN. FIXED 2026-11-03 on
  branch `lane-fd120-123` — see
  CL-87.** *(Was Low. Filed by the
  FD-125..126 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* Measured on the host
  (`test-results/fd118-119/hostref-fd118-119b.c`):
  sendto with an address on a connected
  stream pair end answers -EISCONN
  (106). The emulator's addressed branch
  answers -EOPNOTSUPP for every
  non-datagram type. An earlier lane
  measured -EOPNOTSUPP for a stream
  socket's addressed sendto (recorded
  with FD-111) without recording whether
  the socket was connected; the two
  measurements are filed together here
  for the audit to reconcile — the
  connected shape is -EISCONN on the
  current kernel. Found by the
  FD-125..126 lane's addressed-send
  matrix. Filed, not fixed.

- **FD-144 — A stream socket whose pair
  peer closed answers -ENOTCONN for a
  sendmsg without a header destination,
  where the host answers -EPIPE. FIXED
  2026-11-03 on branch `lane-fd124` —
  see CL-88.** *(Was Low. Filed by the
  `lane-fd120-123` lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured on the host
  (`test-results/fd120-123/hostref-fd120-123c.c`):
  on a stream pair, after the peer
  closes, the survivor's sendmsg with no
  name answers -EPIPE (measured with
  MSG_NOSIGNAL), as do its sendto
  without an address and its plain send.
  The emulator's sendmsg answers
  -ENOTCONN whenever the connection no
  longer resolves and the type is
  stream: the not-connected branch does
  not distinguish never-connected (host:
  -ENOTCONN, measured in the same pass)
  from peer-died. FD-125 gave
  seqpacket's sendmsg exactly that
  distinction (its outClosed check
  answers -EPIPE); the stream branch
  kept the plain -ENOTCONN. Found by the
  `lane-fd120-123` lane's host reference pass
  while measuring FD-143's sendmsg
  shapes; outside FD-143's filed
  addressed shape -- an addressed
  sendmsg on the survivor answers
  -EISCONN and is fixed there. Filed,
  not fixed.

- **FD-145 — The browser build's guest
  CPU self-test reports 4 failures of
  955: two cases require a JIT engine
  the standard build does not select,
  and the chmod/fchmod cases expect a
  no-op success the syscall layer does
  not give.** *(Low. Filed by the
  FD-144 lane, 2026-11-03; provisional
  until the merge lane's audit.)*
  Measured on this lineage's browser
  build (the FD-144 lane's full-suite
  run and a standalone re-run,
  identical both times; the guest
  console is preserved in that lane's
  evidence,
  `test-results/fd124/cpu64-selftest-console.log`):
  the guest summary is 951 passed,
  4 failed — "chmod → 0 (no-op
  success)" answered -ENOENT,
  "fchmod → 0 (no-op success)"
  answered -EBADF, "required JIT path
  did not execute any blocks" and
  "no JIT engine selected
  (engine=off)". The JIT pair prints
  only when the fixture sets
  BW64_JIT_VERIFY=1 and then requires
  an engine the standard
  (interpreter) browser build does
  not select, so it fails by
  construction on every standard
  build. The chmod/fchmod pair
  encodes a disposition — those calls
  succeed as no-ops — that the 64-bit
  syscall layer does not implement
  for a null path or an unopened
  descriptor; whether the test's
  disposition or the layer's answer
  is wrong is undecided.
  SETTLED by the FD-145 settlement
  lane (lane-fd125-ab, 2026-11-03):
  the base A/B its closing terms
  asked for is now measured. The
  same self-test, run standalone
  against a build of the unmodified
  base (daf5ec34e, wasm 10d65ac0),
  reports the identical summary --
  951 passed, 4 failed -- with the
  identical four failure lines
  (chmod R15=-ENOENT, fchmod
  R15=-EBADF, the engine=off pair);
  the lineage build (wasm 07d5b927,
  byte-identical to the FD-144
  lane's own build) reports the
  same. The four failures are
  PRE-EXISTING on the base, not a
  regression on this lineage.
  Dispositions, one per case:
  (1) The JIT pair: a fixture
  expectation defect for the
  standard build. The assertions
  exist only under BW64_JIT_VERIFY=1
  and demand an engine the standard
  interpreter build does not select
  by design (engine=off with zero
  blocks is the correct record
  there; the JIT build target
  exists for the engine-selected
  proof). The self-test should not
  count them as failures when no
  engine is selected. No product
  change owed. (2) fchmod
  (descriptor 0 staged unopened):
  the layer's -EBADF is the host's
  answer for the staged call --
  host-measured on the box kernel:
  fchmod on a closed or never-opened
  descriptor answers -EBADF, and
  fchmod(0) answers 0 only when
  descriptor 0 is open. The case's
  no-op-success expectation is a
  stale disposition from before
  FD-58 implemented chmod for real;
  the case should be re-staged on
  an open descriptor (expect 0) or
  expect -EBADF as staged. The
  layer stands. (3) chmod (null
  path): the same stale expectation;
  the case should expect the
  implemented layer's real answer.
  One real divergence sits
  underneath: the layer answers
  -ENOENT for a null path, where
  the host answers -EFAULT
  (host-measured: chmod("") answers
  -ENOENT, chmod(NULL) -EFAULT) --
  filed separately as FD-146. What
  remains open in this entry is
  only the self-test cases'
  re-staging, a test-side change;
  the product behaviour they probed
  is either correct (fchmod) or
  tracked under FD-146 (chmod).
  Evidence: the settlement lane's
  record,
  `docs/reports/fd125-settlement-20261103.md`,
  and `test-results/fd125ab/` in
  its worktree (both builds'
  consoles). DISCHARGED
  test-side by the FD-145 restage
  lane (lane-fd125-restage,
  2026-11-03; closure claim CL-95,
  lineage-provisional), commit
  027688ad0, cpu64SelfTest.cpp
  only -- no product code changed.
  (1) JIT pair: the end block now
  switches on the engine surface
  the run itself selected. With
  no engine selected (engine=off)
  it asserts the off flavour's
  correct record -- every JIT
  counter zero -- and a nonzero
  counter is the failure; the
  pre-existing wasm / interp /
  engine checks stand unchanged
  for engine-selected runs. Proven
  both ways: the standard build
  prints "JIT64: engine=off
  blocks=0 insns=0" with no
  failure line; the wasm64-mt-jit
  build (run with
  BW_TEST_EXPECT_JIT=1) prints
  "JIT64: engine=wasm blocks=319
  insns=816" and "WASM64:
  blocks=319 insns=816
  entered=319", suite PASS both.
  (2) fchmod: re-settled as staged
  to the settlement's alternative
  disposition -- expects -EBADF,
  named "fchmod → -EBADF (unopened
  descriptor)". (3) chmod:
  re-settled to disposition (3) --
  expects the layer's real answer
  -ENOENT, named "chmod → -ENOENT
  (null path)"; the host -EFAULT
  divergence stays FD-146. The
  settlement's first-choice
  stagings were tried first and
  are measured unavailable inside
  this fixture: the fixture CPU
  runs with no KThread/KProcess
  (its header: the suite
  "validates CPU64 + KMemory64 in
  isolation"), openat answers
  -ENOSYS from sys_openat64's
  no-thread guard, so no
  descriptor can be opened and no
  path can resolve in it. The
  attempt's console is preserved
  (cpu64-selftest-console-openstaging-attempt.log
  in the lane evidence): chmod
  staged with a stack-built "/"
  path answered -ENOENT; the
  openat-staged fchmod answered
  -EBADF on the openat's -ENOSYS.
  A fixture-environment fact, not
  a product failure. Result on
  the standard build: summary 953
  passed, 0 failed -- the 953
  staged cases, all passing; the
  two engine=off pseudo-failures
  no longer counted -- with
  "PASS: chmod → -ENOENT (null
  path)" and "PASS: fchmod →
  -EBADF (unopened descriptor)"
  in the guest console; suite
  PASS. Fixed on this branch;
  closes at merge with the suite
  re-run there. Evidence: the
  lane record,
  `docs/reports/fd125-fd130-test-discharge-20261103.md`,
  and `test-results/fd125-restage/`
  in its worktree.

- **FD-146 — A path system call
  given a null path pointer answers
  the empty-path error where the
  host answers -EFAULT: the 64-bit
  dispatcher's shared guest-string
  reader maps a null pointer to the
  empty string. FIXED 2026-11-03 on
  branch `lane-fd126` — see CL-89.**
  *(Was Low. Filed by the FD-145
  settlement lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured
  instance: chmod(NULL, 0) -- the
  emulator answers -ENOENT; the
  host (box kernel, reference
  program in the settlement lane's
  evidence) answers -EFAULT for a
  null path and -ENOENT for an
  empty string, so the emulator's
  answer is exactly its empty-path
  answer. Mechanism, read from the
  source: the dispatcher's
  readGuestString64 returns an
  empty string for address 0, so a
  null path is indistinguishable
  from "" for every caller. The
  reader is shared across the
  dispatcher's path system calls;
  that generality is by inspection
  -- chmod is the measured instance.
  Found by the FD-145 settlement
  lane's disposition work. Closing
  terms: null path pointers answer
  -EFAULT at the path calls (or at
  the reader, with its other
  consumers audited), proven by a
  guest test staging a null path
  against the host's answer.
  Fixed on branch `lane-fd126` —
  see CL-89.

- **FD-147 — The D3D10 and D3D11
  comprehensive browser tests fail:
  device creation and shader
  compilation succeed, the draw
  completes without error, and the
  readback shows nothing drawn
  (LIT=0). FIXED 2026-11-03 on
  branch `lane-d3d11lit` — see
  CL-103.** *(Filed by the FD-145
  settlement lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured
  standalone on both the settlement
  lane's lineage build (wasm
  07d5b927) and its unmodified base
  (daf5ec34e, wasm 10d65ac0), with
  the tests' full staging (pinned
  DXVK payloads, staged libvulkan):
  d3d10-comprehensive fails on
  "tri10: RESULT 1" (its console
  shows D3D10CreateDevice OK,
  shaders compiled, then LIT=0),
  d3d11-triangle on "tri1193:
  RESULT 1", d3d11-comprehensive on
  "D3D11 must render pixels
  (LIT=0)" -- identical on both
  builds, so the failure is
  pre-existing on the base, not a
  regression on this lineage. The
  same builds pass
  d3d12-comprehensive, so the
  browser graphics path itself
  renders; what differs is the
  DXVK D3D10/11 route. Whether a
  different DXVK payload vintage
  changes the verdict is unmeasured
  (both builds ran the pinned
  vintage the lane trees stage).
  Evidence: `test-results/fd125ab/`
  in the settlement lane's worktree
  (both builds' run logs and guest
  consoles). Closing terms: the
  D3D10/11 draws land in the render
  target the tests read back, and
  the three tests pass standalone.
  Closing account (lane-d3d11lit,
  2026-11-03): the failing tests' one
  hard signal was a single
  bridge-recover warning — the page
  tier rejecting the draw's frame
  because its FRAME_BEGIN header
  carried the frame-overflow flag.
  Instrumented measurement convicted
  the source: `vkCmdClearColorImage`
  (DXVK's init-image clear of the new
  render target; its only call site
  passes rangeCount 1, and the range
  array read back as exactly one
  range) reached the handler with
  the count slot holding
  0x200000000001 — the true uint32
  count in the low half, unrelated
  bits above — and the guard
  `args.a[4] > 64` compared the full
  slot, fired the overflow, and
  poisoned the frame; the pending
  readback then never completed and
  the guest read back zeroes. A
  uint32 parameter's high slot half
  is not data: the bridge truncates
  at 245 other call sites, including
  the readArray on the same handler's
  next line. The same raw-slot
  comparison sat in all six
  frame-overflow count guards (clear
  colour, clear depth/stencil, clear
  attachments; both ABIs). Fix
  `738d882ca` (composed as
  `129e739cf`) compares the
  (U32)-truncated count at all six;
  caps and the loud refusal of
  genuinely over-cap counts are
  unchanged. Fixed on branch
  `lane-d3d11lit` — see CL-103.

- **FD-148 — The D3D9 comprehensive
  browser test fails: a DXVK clear
  never reaches the browser
  compositor with its final red
  colour. FIXED 2026-11-04 on
  branch `lane-fd148-149` — see
  CL-109.** *(Filed by the FD-145
  settlement lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured
  standalone on both the lineage
  build (wasm 07d5b927) and the
  unmodified base (daf5ec34e, wasm
  10d65ac0): the identical
  assertion fails on both -- "x64
  DXVK clear must reach the browser
  compositor with its final red
  color" -- so the failure is
  pre-existing on the base. The
  shape neighbours FD-147's (work
  completes, nothing visible
  lands) at a different stage:
  there the guest's own readback
  is empty; here the guest-side
  clear is asserted against what
  the compositor shows. Whether
  the two share a root is
  unmeasured. Evidence:
  `test-results/fd125ab/` in the
  settlement lane's worktree.
  Closing terms: the clear reaches
  the compositor as the test
  stages it, and d3d9-comprehensive
  passes standalone. Closing
  account (lane-fd148-149,
  2026-11-04): the failure is in
  the emulator's Vulkan serving,
  not in the guest or the test.
  The per-pass readback diagnostic
  showed the clear submission's
  target reading back constant
  [232,40,100,255] — the clear
  landed exactly — while the
  present blit sampled that same
  texture yet the swapchain target
  read ~black (1 non-black pixel).
  DXVK's swapchain blitter (the
  D3D9 presenter's path) creates
  its sampler with usePixelCoord =
  VK_TRUE — Vulkan's
  unnormalizedCoordinates — with
  CLAMP_TO_BORDER addressing: it
  samples in texel space
  (0..640 x 0..480). The bridge
  marshalled the flag in the guest
  struct but its Sampler record
  dropped it, and neither the wire
  protocol nor the page tier had
  any unnormalised handling, so
  the page tier sampled with
  normalised WebGPU coordinates
  far outside [0,1] and its
  border-colour emulation masked
  everything to the border colour
  except a sub-texel corner — the
  observed signature. Fix
  `5ac7f7431` carries the flag end
  to end: the bridge stores it on
  the Sampler record from both
  vkCreateSampler ABIs and appends
  it to the SAMPLER record, the
  wire protocol decodes it, and
  the page tier divides the
  affected sample calls'
  coordinates (and
  textureSampleGrad gradients) by
  the texture dimensions in a
  per-draw WGSL pipeline variant,
  ahead of the border rewrite; the
  record-layout test pin is
  updated in `82e3ebb51`. Base
  FAIL (219,863 ms; redDominant 4)
  → fixed PASS (all three stages
  RESULT 0, redDominant 722,880),
  controls d3d11-triangle,
  d3d11-comprehensive,
  d3d10-comprehensive and
  d3d12-comprehensive PASS on the
  same build. Fixed on branch
  `lane-fd148-149` — see CL-109.

- **FD-149 — The 32-bit D3D9 browser
  test (d3d9-32) never reaches its
  capture: the guest session runs,
  its output carries a Vulkan
  surface error, and the staged
  clear program's CAPTURE_READY
  never appears inside the test's
  300 s wait. FIXED 2026-11-04 on
  branch `lane-fd148-149` — see
  CL-110.** *(Filed by the FD-145
  settlement lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured
  standalone on both the lineage
  build (wasm 07d5b927) and the
  unmodified base (daf5ec34e, wasm
  10d65ac0): identical failure on
  both -- "Guest output /clear9:
  CAPTURE_READY/ not seen within
  300000ms" at ~326 s, with the
  harness's stall dump showing the
  same guest thread state on both
  builds -- so the failure is
  pre-existing on the base. The
  guest console on both builds
  carries Wine's "Failed to load
  Wine graphics driver supporting
  Vulkan" and "Required Vulkan
  extension VK_KHR_surface not
  supported" lines ahead of the
  stall. Evidence:
  `test-results/fd125ab/` in the
  settlement lane's worktree (both
  builds' consoles and dumps).
  Closing terms: the 32-bit D3D9
  clear program reaches its
  capture and d3d9-32 passes
  standalone. Closing account
  (lane-fd148-149, 2026-11-04):
  the cause is the project's own
  32-bit payload build. The
  shipped 32-bit win32u.so is the
  FD-02 backport (sha256
  b2d9d135...), and the stub
  string "built without Vulkan
  support" exists only in that
  module; the 64-bit module and
  the rest of the 32-bit tree are
  Vulkan-capable. The backport's
  build script configured its
  i686 tree --without-vulkan: its
  private sysroot carried only
  the freetype/fontconfig deb
  closure and its fail-closed
  markers checked only the font
  defines, so the FD-02 rebuild
  silently lost the stock module's
  Vulkan driver entry (win32u is
  where that entry lives). Fix
  `6e3dba081`: the i686 sysroot
  gains the sha256-pinned bookworm
  libvulkan dev+runtime debs,
  SONAME_LIBVULKAN joins the
  fail-closed configure markers,
  and the build refuses a product
  carrying the stub string; the
  rebuilt module (sha256
  9d538e79..., 1,502,028 bytes)
  restaged through the sanctioned
  regen path into
  runtime-assets-20261104. Base
  FAIL (CAPTURE_READY never seen
  in 300 s) → fixed d3d9-32 PASS
  (62,685 ms; the presented clear
  reads redDominant 722,880; zero
  stub lines in the console), and
  the 32-bit session still boots
  through desktop initialisation
  and runs all three staged
  programs, so the FD-02 fix the
  backport exists for is intact.
  Fixed on branch `lane-fd148-149`
  — see CL-110.

- **FD-150 — The cpu32-jit-growth
  browser test fails on the
  standard build although its
  guest program passes: the test's
  JIT-statistics assertions read
  fields the standard build's
  statistics JSON does not carry.**
  *(Low. Filed by the FD-145
  settlement lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured
  standalone on both the lineage
  build (wasm 07d5b927) and the
  unmodified base (daf5ec34e, wasm
  10d65ac0): the guest reports
  "cpu32-jit-growth: RESULT 0" on
  both, then the harness assertion
  stats.cpu32.retiredCounting ===
  true reads undefined, on both --
  pre-existing on the base. The
  mechanism is in the source: the
  statistics implementation is
  compiled only under the JIT
  build flag, which the standard
  (interpreter) build target does
  not define, so the standard
  build's bw64_jit_stats_json_text
  carries no cpu32 section. Like
  FD-145's JIT pair, the test as
  staged demands a JIT-flavoured
  build while running on the
  standard one. Disposition owed:
  run this test against the JIT
  build target, or gate its
  statistics assertions on the
  build's engine surface. Evidence:
  `test-results/fd125ab/` in the
  settlement lane's worktree.
  DISCHARGED test-side by the
  FD-145 restage lane
  (lane-fd125-restage, 2026-11-03;
  closure claim CL-96,
  lineage-provisional), commit
  0d4463b53, cpu32-jit-growth
  .test.mjs only. Two staging
  defects were in the test.
  (a) The statistics assertions
  ran unconditionally; they now
  run only when the stats JSON's
  cpu32 section declares itself
  available -- the surface the
  JIT build flavour emits. (This
  entry's "carries no cpu32
  section" above reads precisely
  as "carries no counting
  fields": outside the JIT flag
  the JSON writer appends the
  stub section {"available":0,
  "enabled":0, "engine":
  "unavailable"}.) The gate shape
  is the one jit64.test.mjs uses
  for the cpu64 section; the
  guest RESULT 0 asserts on every
  build. (b) The boot staged the
  wrong engine parameter: jit=1
  selects CPU64 only (launcher:
  "?jit=1 enables CPU64 only;
  ?jit32=1 also enables CPU32"),
  so even on the JIT build the
  engine this test measures never
  started -- measured there in
  the test's original form:
  available:true, enabled:false,
  blocksEntered 0,
  instructionsRetired 0. The boot
  now stages jit32=1, and the
  gated block also asserts the
  section reports itself enabled.
  Proven: standard build PASS
  (gate closed over the stub);
  wasm64-mt-jit build PASS with
  the gate open -- the section
  measured in the same run shape
  reports enabled:true,
  retiredCounting:true,
  blocksEntered 105339927,
  instructionsRetired 452013423,
  and the original ungated
  assertions pass verbatim there
  (scratch probe, never
  committed; output preserved in
  the lane evidence). Fixed on
  this branch; closes at merge.
  Evidence: the lane record,
  `docs/reports/fd125-fd130-test-discharge-20261103.md`,
  and `test-results/fd125-restage/`
  in its worktree.

- **FD-151 — The Node self-test
  build target (make
  wasm64-selftest) does not
  compile: the staged-fault path
  reads mutex members through a
  handle type that is void* in
  that build.** *(Low. Filed by
  the FD-145 restage lane,
  2026-11-03; provisional until
  the merge lane's audit.)*
  Building the target fails in
  faultstage.h:124/132 — "member
  reference base type
  'BOXEDWINE_MUTEX' (aka 'void *')
  is not a structure or union" at
  the staged mutex's lock/unlock
  calls — instantiated from
  soft_rw_page.cpp:153 and
  soft_copy_on_write_page.cpp:166.
  A/B: the identical failure
  reproduces on the unmodified
  base (b298ed6eb) in a throwaway
  worktree, so it is pre-existing
  on this lineage and unrelated
  to the FD-144/FD-145 lanes'
  diffs (neither touches those
  units). The browser dist builds
  compile the same units clean;
  the self-test target (the
  milestone-1 standalone harness,
  not a campaign gate vehicle)
  builds with a different flag
  set, and which define or
  include order routes
  BOXEDWINE_MUTEX to its platform
  definition there was not
  characterised. Closing terms:
  the target compiles and its
  run is reported, or the target
  is retired in favour of the
  suite's browser self-test with
  the Makefile saying so.
  Evidence:
  `test-results/fd125-restage/build-node-selftest.log`
  and `basecheck-node-target-build.log`
  in the FD-145 restage lane's
  worktree. Filed, not fixed.
  Fixed on branch
  `lane-fd151` — see CL-105.

- **FD-131 — An empty path given
  to a path system call resolves to
  the directory node itself in the
  shared path resolver, so the call
  answers as if the directory had
  been named where the host answers
  -ENOENT. FIXED 2026-11-03 on
  branch `lane-fd131` — see CL-90.**
  *(Was Low. Filed by the
  FD-146 lane, 2026-11-03;
  provisional until the merge
  lane's audit.)* Measured at the
  64-bit chmod/fchmodat calls before
  that lane's dispatcher guards
  (the guest test's RED run in
  `test-results/fd126/`): with the
  working directory "/", chmod("")
  and fchmodat(dirfd, "") answered
  0 and chmodded the directory,
  where the host answers -ENOENT
  (`test-results/fd126/hostref-fd126.c`);
  the answer flips to -ENOENT only
  when the working directory's own
  path does not resolve, which is
  why the FD-145 settlement lane
  measured -ENOENT in the browser
  self-test's environment.
  Mechanism, read from the source:
  Fs::getNodeFromLocalPath returns
  the root node for a full path of
  "" or "/", and a relative empty
  path joins to the working
  directory, whose walk returns the
  directory node -- an empty final
  path is never refused. Every path
  call resolving through it shares
  the behaviour (open, stat, unlink
  and the rest, by inspection; the
  chmod family is the measured
  instance, and its two 64-bit
  dispatcher sites now guard the
  empty path themselves under
  FD-146). Closing terms: an empty
  path answers -ENOENT at the path
  calls generally -- at the
  resolver with its callers
  audited, or call by call --
  proven by guest tests staging an
  empty path at representative
  calls against the host's answer.
  Fixed on branch `lane-fd131` —
  see CL-90.

- **FD-132 — A null path pointer is
  erased to the empty string before
  the empty-path route at the 32-bit
  string reader and at the 64-bit
  dispatcher's open-coded path cases,
  so those shapes now answer the
  empty-path error where the host
  answers -EFAULT.** *(Low. Filed by
  the FD-131 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* KMemory::readString
  returns the empty string for a null
  address by construction, so on the
  32-bit side a null path is
  indistinguishable from an empty one
  by the time it reaches the process
  layer; FD-131's guards therefore
  answer -ENOENT for a null path at
  every 32-bit path call, where the
  host answers -EFAULT. On the 64-bit
  side the helper-based calls
  (open/openat, stat family, readlink)
  and FD-146's two guarded sites
  already answer -EFAULT for null,
  but the open-coded dispatcher cases
  (the access family, mkdir, symlink,
  link, rename and unlink cases) copy
  the path with memcpyFromGuest and
  carry no null guard, so a null path
  takes the empty route there too and
  answers -ENOENT after FD-131.
  FD-146's reader audit set the
  precedent that reader-level pointer
  fidelity is its own lane; this
  entry carries the remaining shapes.
  Closing terms: a null path answers
  -EFAULT at the path calls
  generally, proven by guest tests
  staging a null path at
  representative calls on both
  dispatchers against the host's
  answer. Fixed on branch
  `lane-fd132` — see CL-91.

- **FD-133 — The 64-bit dispatcher's
  extended-attribute cases answer
  without ever resolving the path.**
  *(Low. Filed by the FD-131 lane,
  2026-11-03; provisional until the
  merge lane's audit.)* The xattr
  cases (syscalls 188-199) are flat
  stubs: get forms answer -ENODATA,
  set forms 0, list/remove forms
  -ENOTSUP or 0, for existent and
  nonexistent paths alike. The host
  resolves the path first and answers
  -ENOENT for a path that does not
  resolve, including an empty one
  (measured by the FD-131 lane for
  getxattr/lgetxattr on the 32-bit
  side, whose pair now guards the
  empty path; the 64-bit cases were
  left unchanged by that lane as
  outside FD-131's mechanism).
  Closing terms: the 64-bit xattr
  cases resolve the path before
  answering, so an unresolvable path
  answers -ENOENT and a resolvable
  one keeps the stub answers, proven
  by guest tests. Fixed on branch
  `lane-fd133-135` — see CL-97.

- **FD-134 — KProcess::statx
  substitutes the descriptor's path
  for a non-empty path whenever
  AT_EMPTY_PATH is set.** *(Low.
  Filed by the FD-131 lane,
  2026-11-03; provisional until the
  merge lane's audit.)* Read from the
  source during the FD-131 audit:
  when the flag is set the method
  replaces the path argument with
  the descriptor's directory
  wholesale, so statx(dirfd, "name",
  AT_EMPTY_PATH) stats the descriptor
  instead of dirfd/name. The host
  stats the named path; the flag
  matters only for an empty path.
  The 64-bit dispatcher's own statx
  helper does not share the
  behaviour (it uses the flag only
  to detect the empty form), so the
  divergence is reachable through
  the process API's other callers.
  Closing terms: statx with
  AT_EMPTY_PATH and a non-empty path
  stats the named path, proven by a
  process-API test. Fixed on branch
  `lane-fd133-135` — see CL-98.

- **FD-135 — The 64-bit statfs case
  ignores the path entirely, beyond
  the empty shape FD-131 fixed.**
  *(Low. Filed by the FD-131 lane,
  2026-11-03; provisional until the
  merge lane's audit.)* The case
  answers the same fabricated
  filesystem statistics for every
  path, resolvable or not; the host
  answers -ENOENT for a path that
  does not resolve. FD-131 guarded
  only the empty shape there (a
  readable empty first byte), as the
  filed defect's mechanism was the
  empty path; the general
  nonexistent-path shape stands.
  The process layer's own statfs
  methods do resolve the path and
  answer -ENOENT for a missing one,
  so a dispatcher-side resolution
  check has a model to follow.
  Closing terms: statfs at the
  64-bit dispatcher answers -ENOENT
  for a path that does not resolve
  and keeps the fabricated answer
  for resolvable paths, proven by a
  guest test. Fixed on branch
  `lane-fd133-135` — see CL-99.

- **FD-136 — The 64-bit dispatcher's
  utimensat stub answers success for
  a null path pointer; the host
  answers -EINVAL in every shape.**
  *(Low. Filed by the FD-132 lane,
  2026-11-03; provisional until the
  merge lane's audit.)* The case is
  a stub that reports success for
  almost every input (it does not
  model per-file times); FD-131
  taught it to answer -ENOENT for a
  readable empty first byte without
  AT_EMPTY_PATH, and its comment
  records that a null path is "never
  dereferenced" and keeps the success
  answer. The FD-132 lane's host
  measurement (test-results/fd132/
  hostref-fd132.c) shows the kernel
  special-cases the null filename at
  utimensat before any name fetch or
  descriptor resolution: -EINVAL at
  AT_FDCWD, at a directory or file
  descriptor, at an unopened
  descriptor, with null or valid
  times, and with or without
  AT_EMPTY_PATH. The 32-bit side is
  a real call and was fixed by
  FD-132 to the same answer; the
  64-bit stub stands. Closing terms:
  the 64-bit utimensat case answers
  -EINVAL for a null path pointer,
  proven by a guest test, with its
  FD-131 empty-path answers
  unchanged. Filed, not fixed.
  **Amendment (lane-fd136-137,
  2026-11-03): the host claim above
  is contradicted at the raw-kernel
  level; the closing terms are
  withdrawn pending the merge
  audit.** The lane re-measured the
  host with its own reference
  programs (test-results/fd136-137/
  hostref-fd136.c and
  hostref-fd136-rawgrid.c on branch
  `lane-fd136-137`, headline
  shapes strace-verified). The
  -EINVAL in every shape is the
  host libc wrapper's contract:
  glibc's utimensat() rejects a
  null path in userspace and never
  issues the syscall. The raw
  kernel's answers for
  utimensat(dfd, NULL, ...) are
  per-shape: at AT_FDCWD, -EFAULT
  with flags 0, AT_EMPTY_PATH or
  AT_SYMLINK_NOFOLLOW (the name
  fetch faults; there is no null
  special-case ahead of it); at a
  valid directory or file
  descriptor with flags 0, success
  — the null path is the working
  descriptor form, and it is the
  exact shape glibc's own
  futimens() issues (strace:
  futimens(fd, ts) enters the
  kernel as utimensat(fd, NULL, ts,
  0) and returns 0); at a valid
  descriptor with AT_EMPTY_PATH or
  AT_SYMLINK_NOFOLLOW, -EINVAL; at
  an unopened descriptor, -EBADF
  with flags 0 and -EINVAL with
  AT_EMPTY_PATH. Implementing this
  entry as filed (unconditional
  -EINVAL at the 64-bit stub) would
  break the guest futimens shape at
  the 64-bit dispatcher and diverge
  from the kernel at AT_FDCWD, so
  lane-fd136-137 changes no product
  code for FD-136. Corrected
  closing terms for the audit: the
  64-bit utimensat case answers the
  raw kernel's per-shape matrix
  above for a null path, proven by
  a guest test; and the audit
  should re-examine FD-132's
  32-bit utimensat exception
  alongside (CL-91 rests on the
  same wrapper measurement: the
  32-bit side currently answers
  -EINVAL for the (fd, NULL, 0)
  futimens shape where the kernel
  answers 0, and -EINVAL at
  AT_FDCWD where the kernel answers
  -EFAULT). Still filed, not fixed.
  Fixed on branch `lane-fd136-138`
  — see CL-93.

- **FD-137 — A null memfd_create name
  is erased to the empty string on
  both dispatchers, so the call
  creates the file where the host
  faults the name copy.** *(Low.
  Filed by the FD-132 lane,
  2026-11-03; provisional until the
  merge lane's audit.)* The name is
  a label, not a path, so FD-132's
  guards did not cover it; the
  erasure is the same mechanism: the
  32-bit helper reads the name with
  the string reader (a null pointer
  becomes ""), and the 64-bit case
  copies the name only when the
  pointer is nonzero, passing "" for
  null. memfd_create("") is legal on
  the host (measured,
  test-results/fd132/
  hostref-fd132-memfd.c: a null name
  answers -EFAULT, an empty one
  returns a descriptor), so both
  emulator sides create the file and
  return a descriptor where the host
  faults. Closing terms:
  memfd_create with a null name
  answers -EFAULT on both
  dispatchers, proven by guest
  tests. Fixed on branch
  `lane-fd136-137` — see CL-92.

- **FD-138 — memfd_create accepts
  names longer than the host's
  limit on both dispatchers.**
  *(Low. Filed by the FD-136-137
  lane, 2026-11-03; provisional
  until the merge lane's audit.)*
  Found while proving FD-137. The
  host rejects a memfd_create name
  longer than 249 chars with
  -EINVAL (measured,
  test-results/fd136-137/
  hostref-fd137.c on branch
  `lane-fd136-137`: 249 chars
  returns a descriptor, 250 and
  319 answer -EINVAL). The emulator
  validates no name length anywhere
  on the path: the 64-bit case
  copies at most 255 chars into its
  256-byte buffer and creates the
  file, and the 32-bit helper
  passes the whole guest string to
  KProcess::memfd_create, which
  creates unconditionally. Guest
  observation on the FD-137 tests:
  a 300-char name returns a
  descriptor on both dispatchers
  (test-results/fd136-137/
  test-main-fd137-green.log and
  test-x64-fd137-green.log,
  observation lines). Closing
  terms: memfd_create with a name
  longer than 249 chars answers
  -EINVAL on both dispatchers,
  proven by guest tests; names of
  249 chars or fewer still create.
  Filed, not fixed. Fixed on branch
  `lane-fd136-138` — see CL-94.

- **FD-139 — utimensat accepts flag
  bits outside AT_SYMLINK_NOFOLLOW |
  AT_EMPTY_PATH on non-null paths, on
  both dispatchers; the host answers
  -EINVAL.** *(Low. Filed by the
  FD-136-138 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* Found while measuring
  FD-136's null-path matrix at the
  raw syscall level
  (test-results/fd136-138/
  hostref-fd136b.c on branch
  `lane-fd136-138`): the kernel
  validates the flags word ahead of
  the name fetch for every shape, so
  utimensat(AT_FDCWD, "f.txt", times,
  0x2000) answers -EINVAL. The
  emulator validates nothing on the
  non-null shapes: the 64-bit stub
  consults only AT_EMPTY_PATH (for
  its FD-131 empty-path guard) and
  reports success, and the 32-bit
  real call's KProcess::utimesat
  consults only AT_EMPTY_PATH and
  AT_SYMLINK_NOFOLLOW and proceeds to
  set the times. So a guest passing
  an undefined flag bit is told the
  call worked (and, on the 32-bit
  side, the file's times change)
  where the host refuses. FD-136's
  fix covers the null path only —
  its matrix includes the flags
  verdict — so the non-null shapes
  are filed here rather than folded
  in. Closing terms: utimensat with
  a flag bit outside
  AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH
  answers -EINVAL on both dispatchers
  for non-null paths too, proven by
  guest tests. Filed, not fixed.
  Fixed on branch `lane-fd139-152`
  — see CL-107.

- **FD-152 — The 64-bit dispatcher's
  fstatfs case answers its fabricated
  statistics for any descriptor,
  valid or not.** *(Low. Filed by
  the FD-133-135 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* Found while measuring
  FD-135: the case (syscall 138)
  shares the statfs helper and
  ignores its descriptor argument
  entirely, answering 0 where the
  host answers -EBADF for an
  unopened descriptor (measured,
  test-results/fd133-135/
  hostref-fd133-135.c). The process
  layer's own fstatfs64 checks the
  descriptor and answers -EBADF, so
  a model exists. FD-135's filing
  named only the path form (137);
  this is the descriptor sibling.
  Closing terms: fstatfs at the
  64-bit dispatcher answers -EBADF
  for a descriptor that is not open
  and keeps the fabricated answer
  for open ones, proven by a guest
  test. Filed, not fixed. Fixed
  on branch `lane-fd139-152` — see
  CL-106.

- **FD-140 — A path through a
  non-directory answers -ENOENT
  where the host answers
  -ENOTDIR.** *(Low. Filed by the
  FD-133-135 lane, 2026-11-03;
  provisional until the merge lane's
  audit.)* Measured at statfs (the
  host answers -ENOTDIR for a path
  descending through a regular file,
  test-results/fd133-135/
  hostref-fd133-135.c) and observed
  on the fixed FD-135 tree: the
  guest test's printed observation
  shows statfs("a.txt/child")
  answering -ENOENT. The shared
  resolver reports no node for such
  a path, and every call routed
  through it gives the resolver's
  bare -ENOENT, so the shape is
  resolver-level rather than
  statfs-specific; FD-131 and
  FD-135 deliberately left the
  resolver unchanged. Closing
  terms: a path whose prefix
  component is not a directory
  answers -ENOTDIR at the path
  calls, with the resolver's other
  consumers' answers unchanged,
  proven by guest tests. Filed,
  not fixed. Fixed on branch
  `lane-fd139-152` — see CL-108.

- **FD-127 — The Godot demo's main thread parks
  inside D3D12 device setup: a thread creation's
  pipe2 never returns in the emulator.** *(High —
  the demo's whole rendering path on current
  builds, deterministic; the same shape can in
  principle bite any guest whose Wine thread
  creation takes the same path in the same state.
  Filed by the godotpark lane, 2026-11-03, on the
  preserved batch-3/batch-4 deploy captures —
  no new capture run; see the lane record for why.)*
  Measured account (full evidence chain in
  `docs/reports/fd127-godot-device-setup-park-20261103.md`):
  in all three long Godot captures on builds
  `caadab5ad` and `600da7651`, the demo's main
  thread (emulator pid 10, tid 11) logs
  `vkd3d_init_device_caps` (batch-4: 210.4 s) and
  then freezes at a thread creation inside device
  setup. Its saved PE syscall frame is
  `NtCreateThreadEx` (shipped `ntdll.dll+0xd5c4`,
  stub immediate verified = syscall 0x2d), reached
  from d3d12core through kernelbase's CreateThread
  path. On the unix side, the shipped `ntdll.so`'s
  `NtCreateThreadEx` calls
  `wine_server_handle_to_fd`, whose pipe2 call
  site passes `O_CLOEXEC` — matching the thread's
  frozen registers exactly (rip at the return
  point of glibc's pipe2 wrapper, `SYS_pipe2`
  = 293; rdi = fd-array pointer, rsi = 0x80000).
  The thread then executes zero guest instructions
  for the rest of the capture (2,460,001 ms of
  no-progress at the final batch-4 dump), holds no
  guest wait condition, and is in none of the
  host-side states the dump can attribute
  (proxyfetch inflight 0, guest-memory mutex
  unowned, fault stage none) — by the FD-77
  instrument's own definition, a wait inside a
  syscall implementation. The emulator's 64-bit
  pipe2 handler is synchronous (validate, then
  `ksocketpairFds` under the process's host
  `fdsMutex`, write the fds, return), so the block
  sits in host code inside that path or in the
  dispatch around it; which host primitive blocks
  (or spins) is NOT established — `fdsMutex`
  contention is the only named candidate and no
  evidence shows a holder (the process's only
  other threads are the two workers below, and
  the mutex is recursive). The freeze state is
  byte-identical across the three runs (same rip,
  same registers, same frame), so it is not a
  race, load, or wait-length artefact. The two
  worker threads created in the same setup window
  (tids 44/45) sit in ordinary
  SleepConditionVariableSRW → RtlWaitOnAddress →
  NtWaitForAlertByThreadId futex waits — idle
  workers waiting on the main thread, not its
  blockers. Downstream, nothing else happens: 0
  vkCreateShaderModule, 0 pipelines, no window,
  present count 0. Contrast: on build `9e37e5640`
  (2026-11-02) the same demo and capture
  configuration proceeded through device setup
  (modules created in vivo, pipelines attempted),
  so the behaviour appeared between that build
  and batch 3's; the introducing change is not
  identified here. This supersedes FD-55's
  2026-11-01 wait characterisation for current
  builds and sits in front of FD-89/CL-43's owed
  in-vivo pipeline proof and FD-55's downstream
  items. Closing terms: instrument the pipe2
  handler and `ksocketpairFds` (entry/exit +
  host-lock acquisition with holder, plus a host
  CPU sample during the freeze to separate
  blocked from spinning), re-run the same capture
  (the freeze reproduces at ~215 s of guest time;
  stop at the first post-freeze dump), and close
  when the named block is removed and the capture
  shows the main thread proceeding past device
  setup — window mapped and first
  vkCreateShaderModule observed — with the native
  and JS gates green.
  Fixed on branch `lane-fd127fix` — see CL-100.

- **FD-128 — With FD-127 fixed, the Godot demo
  runs through device setup and into its first
  frames' work, then a WASM worker dies on an
  unaligned access and the guest stops.** *(High —
  it is now the demo's front blocker, and any
  guest reaching the same emulator path can hit
  it. Filed by the fd127fix lane, 2026-11-03, from
  its diag5/diag6 verification captures on the
  FD-127-fixed build.)* Measured account: on the
  fixed build (lane-fd127fix tip `f6f010eb4`), the
  same capture that froze at device setup now
  proceeds — `vkd3d_init_device_caps` (diag6:
  257.6 s), first `vkCreateShaderModule` (261.6 s;
  1,216 bytes, hash f598618b6233f21a), pipeline
  creations (32 traps), queue submits (6), first
  window mapped (299.3 s). At 301.5 s — within a
  second of the window mapping, during the first
  frames' image work (`vkd3d_create_image`
  active) — the page reports `worker sent an
  error! ... Uncaught RuntimeError: operation
  does not support unaligned accesses` in
  boxedwine64.js, the launcher's window.onerror
  fires with the same trap, the guest's thread
  dumps stop, and no present is ever observed
  (present count 0). The same shape reproduced in
  both fixed-build captures (diag5: window mapped
  226.0 s, trap 227.4 s; diag6: window 299.3 s,
  trap 301.5 s). The trap is a WebAssembly
  unaligned-access RuntimeError — an unaligned
  atomic or memory operation in the compiled
  module executing on a worker; which emulator
  path executes it is NOT established (the page
  reports only the bundle file/line). This sits
  in front of FD-89/CL-43's owed in-vivo pipeline
  proof (pipelines now create in vivo; the proof's
  terms need the run to survive to its measurement
  point) and FD-55's remaining downstream items.
  Closing terms: name the trapping operation and
  the code path that executes it (a source-level
  bisect, or an instrumented build that logs the
  last bridge/emulator action before the trap),
  fix the unaligned access at its site (or serve
  that path in an alignment-safe form), and close
  when the same capture shows the demo proceeding
  past the trap point to its first present, with
  the native and JS gates green.
  Fixed on branch `lane-fd128` — see CL-101.

- **FD-129 — A guest worker's current-thread TLS
  slot is overwritten with small integers during
  Vulkan bridge activity; the writer is
  unidentified.** *(High — it silently destroys
  the thread identity every kernel path trusts;
  FD-128 made the known fatal consumers immune,
  but the write continues and any wider or
  differently-aimed instance is unaccounted.
  Filed by the fd128 lane, 2026-11-03, from its
  diag-diag2 … diag-diag6 instrumented captures;
  number is this branch base's next after
  FD-128 and may be renumbered at composition —
  the defect is the runningThread-slot writer.)*
  Measured account: the slot holding
  `KThread::runningThread` for the Godot demo's
  servicing guest thread (emulator tid 0xB) read
  its genuine KThread pointer from thread start
  (diag-diag5: slot 0x23c78d8 on pthread
  0x22c0f38, KThread 0x2231008 at 9.1 s); at
  270.8 s the same slot read 0x25 and at 272.3 s
  0x9, in the middle of the first
  vkAllocateMemory/vkCreateImage burst after the
  window mapped. A tripwire on setCurrentThread
  — the slot's only legitimate writer — never
  received a bogus value in any run. A 1 kHz
  monitor polling registered slots saw values
  change at Vulkan-call frequency during the
  burst (0x40, 0x8, 0x48 …) and saw the same
  class of small-integer writes on other
  workers' slots from early boot (0x6702 at
  62.8 s, 0x3e at 69.6 s, vkSeq still 0). The
  slot address is constant per worker; the
  worker's remaining state is unaffected (it
  executed correct guest code for 200+ s while
  its slot was being written during compute
  stretches, and only identity consumers
  observed the damage). Whether the writer is
  emulator code writing through a computed
  pointer, or a second TLS image in the page's
  worker setup aliasing the slot, is NOT
  established. Closing terms: identify the
  writer (the poll correlation plus targeted
  guards on candidate per-thread state are the
  known starting points), fix the write at its
  source, and show a Godot-class capture in
  which no registered slot ever holds a
  non-KThread value — at which point FD-128's
  registry fallback becomes defence in depth
  rather than a load-bearing correction.
  Record: `docs/reports/fd128-current-thread-identity-20261103.md`;
  evidence `test-results/fd128/` (diag-diag5 and
  diag-diag6 carry the slot/monitor evidence).
  Fixed on branch `lane-fd129` — see CL-102.

- **FD-153 — The Vulkan bridge's drop logging
  reports two legal no-op shapes as dropped
  guest commands.** *(Low — behaviour is
  faithful in both shapes; the harm is
  diagnostic: drop lines are the bridge's
  honesty signal for refused guest work, and
  these false positives masked this lane's
  real signal. Filed by the lane-d3d11lit
  lane, 2026-11-03, from its instrumented
  captures; number is that branch's next
  after its FD-130 (the D3D10/11 entry,
  composed FD-147) and is settled here as
  FD-153 — the defect is the drop log's two
  no-op shapes.)* Measured on the DXVK
  D3D11 route:
  (1) `vkCmdBeginRendering`'s attachment
  helper runs unconditionally for the depth
  and stencil slots, and its `!address` guard
  logs a DROPPED line for each null pointer —
  on a colour-only pass (measured colors=1,
  pDepth=0x0, pStencil=0x0) that is two drop
  lines for a pass that loses nothing.
  (2) `captureStageSpec` log-drops the whole
  vkCreateGraphicsPipelines call when a stage's
  VkSpecializationInfo is empty (mapEntryCount
  0, null pointers — DXVK's per-stage default):
  there is nothing to capture, and the pipeline
  is created correctly with the shader's
  defaults. Consoles for the D3D10/11 defect
  (FD-147) read as render-pass and pipeline
  loss while the single operative signal was
  one bridge-recover warning. Closing terms:
  neither shape counts or logs as a drop (an
  informational line that cannot be mistaken
  for a drop is acceptable); genuine refusals
  — null structures the API requires, over-cap
  counts, unreadable guest memory — stay loud;
  and a survey of the VK64_DROP_IF sites for
  the same no-op-shape pattern (a helper
  invoked with a legal null/empty input)
  is part of the close. Filed, not fixed.
  Closing account (lane-fd153,
  2026-11-04): both filed shapes are
  silent now — the attachment helpers
  (both ABIs) return ahead of the
  guard on a null pointer, and
  captureStageSpec returns on
  mapEntryCount 0 ahead of its (still
  loud) over-cap and claimed-but-absent
  guards. The survey the filing
  required found three more instances
  of the same pattern, all fixed in
  the same change: resolveFrame
  dropped when descriptor set 0 was
  never bound (a recorded-but-stale
  set id stays loud), the 32-bit
  buffer/image copy guards dropped a
  zero-region copy with a null array
  (regions claimed with no array stay
  loud), and the VKPROF trap scans
  counted idle slots as drops with
  profiling enabled (now a plain skip;
  the census's only removals,
  257 -> 255 sites). Every other
  guarded site in the census was
  classified genuine and is unchanged.
  The survey's one opposite-sign find
  — the 32-bit specialization
  capture's silent header guard — is
  filed as FD-155. Fixed on branch
  `lane-fd153` — see CL-104.
- **FD-154 — Five browser-suite tests fail
  identically on the batch-4 base and the
  batch-5 composed tree in the current suite
  environment.** *(Medium — none of the five
  is a batch-5 regression (each reproduced
  on an unmodified build of the base
  `0c7e2f3fa`, same box and conditions,
  2026-11-04), but each is a real red test
  whose cause is unmeasured; filed by the
  batch-5 merge lane from its gate so the
  failures live on this queue and not only
  in the merge record.)* Measured at the
  batch-5 gate (composed wasm `05298a62`;
  control build of the batch-4 tip,
  BUILD_EXIT 0, 'Browser export verified'):
  (1) winsock-http: the guest's TCP connect
  never reaches the harness's loopback relay
  (relay stats accepted 0, zero bytes both
  ways) and `BW_TCP connected_event=1` never
  prints — identical on base; three composed
  attempts, one base attempt.
  (2) input-mouse-rel: the guest's RAW
  sequence print never arrives within 30 s —
  identical on base.
  (3) input-fullscreen: the harness's click
  on the canvas times out on Playwright's
  element-stability wait — identical on
  base.
  (4) jit64: the wineboot exit_group pattern
  is not seen within its 120 s window (test
  durations 128–129 s on both trees) —
  identical on base; the fd133-135 lane
  measured this test passing on its own tree
  in its own gate, so the failure is
  environment- or condition-shaped, not a
  code regression in either direction.
  (5) jitbench: its 'requested JIT mode
  latched' assertion fails — identical on
  base; both trees' attempts ran above the
  test's own one-minute load guard of 4, so
  its numbers are invalid under its own
  rules on both. Closing terms: each test
  passes in the suite environment on a
  current build, or is shown to be gated on
  a named external condition and re-scoped
  accordingly; the winsock-http item closes
  only with relay stats showing the connect
  crossing (accepted 1 or more, bytes both
  ways). Evidence:
  `test-results/merge-batch5-20261103/`
  (composed runs) and its `control-base/`
  subdir (base runs and the base build log).
  Adjudicated 2026-11-04 by the FD-154 lane (worktree `wt-fd154`,
  branch `lane-fd154`, base `3ea2fd97d`; lane record
  `docs/reports/fd154-adjudication-20261104.md`; evidence
  `test-results/fd154/` on that tree — stock base runs, focused
  probes, fixed-build runs). Per item:
  (1) winsock-http — PRODUCT defect, FIXED (`d26f606a1`): every
  AF_INET stream socket in the browser build is marked
  `loopbackStream`, and `KNativeSocketObject::connect` refused any
  loopback destination with no guest listener internally, never
  using the socket's native transport — which, with the relay
  enabled, is the only path to the harness relay (the fixture
  connects to 127.0.0.1:80 by design). The guest printed
  `connect=-1 error=10061` and the relay saw no connection at all
  (accepted 0). All other data-path predicates already treated
  such a socket as native; connect() now falls through to the
  native connect when no guest listener exists and a native
  transport is present. Proof on the fixed build: test passes,
  relay stats accepted 1, 76 bytes guest→TCP, 324 bytes back —
  this item's closing term is met.
  (2) input-mouse-rel — PRODUCT defect, FIXED (`3cce91bcb`): both
  relative-motion producers advanced the sink's tracked pointer
  by the delta at queue time, and the delivering connection
  seeded its virtual position from that pointer and added the
  delta again — the first motion of every relative stream
  arrived doubled (injected 100,50 → guest RAW 200,100, measured
  with the product's `BW64_XWIRE` log). Producers no longer
  advance the pointer for relative events; the connection owns
  the virtual position and reports deliveries back through a new
  bookkeeping-only `noteDeliveredPointer()`. Proof: test passes
  on the fixed build, both architectures.
  (3) input-fullscreen — HARNESS defect over a PRODUCT defect,
  both FIXED. Filed failure (locator click timeout): the guest's
  ClipCursor makes the page take pointer lock, which a real
  browser grants only on a user gesture but the headless harness
  grants immediately; under lock, Playwright's hit test reports
  the body intercepting the canvas while `elementFromPoint`
  returns the canvas, and dispatched clicks arrive at (0,0) — a
  neutral control page in the same browser clicks correctly.
  Harness fix (`e482eb6a6`): the test keeps the browser unlocked
  and delivers the press through the injection channel at the
  same point. Underneath it, a product defect in three layers,
  all in the XWire grab path, all fixed: pointer input for a
  cursor-clipping guest blacked out entirely (keys flowed; no
  pointer event was ever delivered). (i) A GrabModeSync grab
  froze input at grab time, so no first event could ever reach
  the grabbing client (`f610e90b2`). (ii) Events routed by the
  connection's own grab were dropped as foreign-target: Wine
  grabs the pointer on the root window (grab log: win=0x600001,
  mask=0x4c, ownerEvents=0, pointerMode=sync), and the root is
  owned by the session's first connection — X reports grab
  events to the grabbing client, so own-grab targets are now
  exempt from the drop (`dbe7705f3`, which also logs grab
  parameters and every pointer-path drop reason under
  `BW64_XWIRE`; the drops were silent). (iii) The delivery loop
  re-froze after the first delivered event under a sync grab,
  but this client never sends XAllowEvents (zero opcode-35
  requests in full captured sessions), so every later core
  pointer event stalled in the frozen queue for the life of
  the grab; the native X11 layer's settled policy treats
  GrabModeSync as GrabModeAsync for exactly this reason, and
  the XWire loop now matches it (`25471296c`). Proof: the test
  passes on the fixed build, both architectures; the guest
  prints the moved pointer, BUTTON UP, and the clipped
  1500-count move exactly once.
  (4) jit64 — HARNESS defect, FIXED (`189f6b302`): the
  wineboot exit wait demanded `wineboot.exe--init` adjacency,
  but `commandLineForLog()` renders argv space-joined by
  construction (`wineboot.exe --init`); the demanded substring
  occurs zero times in the full gate console while the boot
  itself completes correctly (pins spawned, wineboot exits 0).
  Pattern now matches the rendered form; with it, runs advance
  past the wineboot wait on the unmodified product. The same
  availability scoping as item (5) applies to this test's own
  generated-code assertion (`41de3c327`): the suite build is
  the interpreter target and the stats report the JIT
  unavailable in it. With both harness fixes the test passes
  end to end on the current build (both modes, all samples,
  checksum `8b527b4986bdaccf`). Its benchmark stage's waits
  remain load-paced on the shared box, as this filing noted.
  (5) jitbench — HARNESS defects, FIXED (`b99c92154`,
  `260545f5b`, `1d2c059d8`, `87d9e021b`): the launcher maps
  `?jit=1` to CPU64 only and latches CPU32 off; CPU32 follows
  `?jit32`, which the benchmark boot never passed, so the x86
  mode-1 latch assertion failed by construction. The boot now
  drives `jit32` from the mode. Second, the engines are not
  there to latch in the suite's build: the suite runs the
  default interpreter browser build (the JIT is a separate
  build variant) and the 32-bit JIT is not compiled into the
  x64 target at all — the product's own stats say so
  (`available` 0; cpu32: `"reason":"32-bit JIT not compiled in
  this target"`; cpu64 stays `engine:"interp"` with zero
  blocks entered). The test now gates its JIT assertions on
  the stats reporting the engine available, and otherwise
  asserts the consistent story (no generated code reported
  entered; checksum stability still covers the interpreter);
  JIT-variant builds keep the full assertions. Whether the
  shipped build should switch to the JIT variant is a build
  decision outside this filing. Benchmark numbers remain
  load-gated by the test's own guard
  (`BW_TEST_JITBENCH_MAX_LOAD`, default 4) on the shared box.
  Two extracted-handler unit contracts in
  `xwire-pointer-grab.test.mjs` encoded the pre-fix grab and
  sink semantics (a sync grab freezes pointer state; the sink
  advances its tracked position at queue time) and were updated
  to the measured contracts (`58f759879`). Suites on the lane
  tip: JS unit 353 passed / 0 failed; native x64 108 tests,
  0 FAILED in the gate-standard serial mode (the threaded mode
  shows the known pre-existing topology failure and teardown
  crash other lanes have recorded; serial is the standard).
  Native main suite on the lane tip: 682 tests, 0 FAILED, gate-standard serial mode.
  Closing account (batch-7 merge
  audit, 2026-11-04): the lane's
  adjudication meets every closing
  term of this entry -- each of the
  five tests passes on the fixed
  build in the suite environment,
  jitbench's generated-code
  assertions are re-scoped to the
  named external condition the
  terms allow (the engines are not
  compiled into the suite's build),
  and winsock-http closes with the
  relay stats showing the connect
  crossing (accepted 1, 76 bytes
  guest-to-TCP, 324 bytes back).
  Closed at this composition --
  see CL-118.

- **FD-155 — The 32-bit specialization
  capture discards guest input
  silently.** *(Low — the served
  behaviour is correct (nothing is
  captured; the pipeline keeps the
  shader's defaults); the harm is the
  silence itself: refused guest work in
  the bridge is reported, never dropped
  quietly, and this is the one capture
  path that can refuse without a line.
  Filed by the FD-153 lane, 2026-11-04,
  from its VK64_DROP_IF census
  survey.)* `captureStageSpec32`'s
  header guard is a plain
  `if (...) return;` — no log line, no
  drop counter: on the 32-bit path an
  unreadable VkSpecializationInfo, a
  mapEntryCount over the capture cap
  of 16, and claimed entries whose
  map-entry array or data is absent
  all discard the guest's
  specialization info silently. The
  64-bit twin (`captureStageSpec`)
  reports all three through
  VK64_DROP_IF, and the 32-bit
  function's own per-entry guards
  report too; only its header is mute.
  (The empty shape — mapEntryCount 0 —
  is silent on both paths and correct:
  a legal no-op, per FD-153.) Closing
  terms: the three header refusals
  report through the same drop channel
  as the 64-bit path, and a native
  test pins the loud cases on the
  32-bit path. Closing account
  (lane-fd155, 2026-11-04): the
  header is restructured to mirror
  the 64-bit twin -- a null spec
  pointer and an empty
  VkSpecializationInfo return
  silently (the legal no-spec and
  no-op shapes), while the three
  refusals report through
  VK64_DROP_IF: an unreadable
  struct, a mapEntryCount over the
  cap of 16, and claimed entries
  whose map-entry array or data is
  absent (null array, null data,
  zero or over-256 data size). The
  drop census grows 255 -> 258 (the
  three new return-action sites;
  pin updated in
  vk-drop-macro-guard.test.mjs).
  Fixed on branch `lane-fd155` --
  see CL-111.

- **FD-156 — The MEMORY64 variant of
  the Node self-test target (make
  wasm64-selftest-mem64) does not
  compile: the SDL audio bridge's
  EM_ASM block casts ring-buffer
  pointers to int, which a 64-bit
  pointer build rejects. FIXED
  2026-11-04 on branch `lane-fd156`
  -- see CL-112.** *(Low.
  Filed by the FD-151 lane,
  2026-11-03; provisional until
  the merge lane's audit.)*
  Building the target fails at
  platform/sdl/kdspaudio.cpp:371 —
  "cast from pointer to smaller
  type 'int' loses information" at
  both pointer arguments of the
  EM_ASM call that arms the audio
  worklet bridge ((int)&bwRingWrite,
  (int)&bwRingRead). Under
  -sMEMORY64=1 pointers are 64-bit,
  so the casts cannot compile; the
  wasm32-host targets (wasm64-
  selftest, the browser build)
  accept them. It is the build's
  only error site. Pre-existing on
  the integration tip by
  construction: the file is
  byte-identical there, and the
  FD-151 lane's diff does not touch
  it or its inputs. Closing terms:
  the mem64 target compiles and
  its run is reported. Evidence:
  `test-results/fd151/build-mem64.log`
  in the FD-151 lane's worktree.
  Fixed on branch `lane-fd156` in
  `3ee906b3a` (run instructions
  corrected in `b4d3f2e8f`): the two
  casts are dropped and the ring
  index addresses go to EM_ASM as
  pointers -- the treatment the
  ring base at $0 already gets.
  Emscripten gives every pointer
  type the sig `p`, which the
  MEMORY64 runtime's argument
  reader delivers to JS as a plain
  Number holding the byte offset
  the bridge's JS consumes; a
  uintptr_t cast would have taken
  sig `j` and arrived as a raw
  BigInt. The target compiles
  (MAKE_EXIT=0) and its run is
  reported: the box's Node 22
  rejects the module at
  instantiation (its V8 refuses
  the 64-bit table limits), so the
  run used an official Node
  v24.11.0 whose tarball sha256
  verified against the release
  sums -- 953 passed, 0 failed,
  exit 0, the same standard as the
  other variants. The standard
  variant still builds and runs
  953/0; native suites serial main
  682 and x64 108, 0 FAILED; JS
  unit 353/0, legacy 115/0;
  browser build verified
  (BUILD_INFO git_sha `b4d3f2e8f`,
  dirty 0, wasm `aba69204...`).
  Record:
  `docs/reports/fd156-mem64-selftest-target-20261104.md`;
  evidence `test-results/fd156/`.
  Fixed on branch `lane-fd156`
  -- see CL-112.

- **FD-157 — The 32-bit extended-
  attribute calls do not resolve
  their target path for attribute
  names outside the DOSATTRIB
  special case, answering -ENOTSUP
  where the host answers -ENOENT
  for a missing path and -ENODATA
  for an existing file.**
  *(Low. Filed by the FD-139-152
  lane, 2026-11-04; provisional
  until the merge lane's audit.)*
  Found while grounding FD-140's
  host matrix (raw syscalls,
  test-results/fd139-152/
  hostref-fd139-152c.c): the host
  answers getxattr("missing",
  "user.x") -ENOENT and
  getxattr("a.txt", "user.x")
  -ENODATA; the 32-bit dispatcher's
  getxattr/lgetxattr/setxattr/
  lsetxattr/listxattr/llistxattr/
  removexattr/lremovexattr cases
  resolve only for the user.
  DOSATTRIB name (the FD-133 fix)
  and answer -ENOTSUP (-38) for
  every other name, resolved or
  not. The 64-bit dispatcher
  resolves first and answers
  -ENODATA for a missing attribute
  on an existing file; its
  missing-path answer is -ENOENT
  (now -ENOTDIR through a file, per
  CL-108). Closing terms: the
  32-bit xattr cases answer
  -ENOENT for a path that does not
  resolve (and -ENOTDIR for one
  descending through a file) and
  -ENODATA for an unresolved
  attribute name on a file that
  does resolve, proven by guest
  tests; the -ENOTSUP answer for
  names the emulator does not
  model on files it does resolve
  may stay where the host's
  -ENODATA is not the measured
  answer for that name class.
  Filed, not fixed. Closing
  account (lane-fd157-161,
  2026-11-04): closed on this
  branch -- see CL-113. The
  eight path forms now resolve
  before answering: getxattr and
  lgetxattr resolve every name
  (a miss answers -ENOENT, a
  path through a file -ENOTDIR,
  an unresolved attribute on a
  resolved file -ENODATA), and
  setxattr, lsetxattr, listxattr,
  llistxattr, removexattr and
  lremovexattr -- previously
  unhandled slots answering
  -ENOSYS -- share one helper
  that validates the set forms'
  flags word first, then
  resolves, answering -ENOTSUP
  (set/remove) or an empty list
  (list) for a resolved target,
  matching the 64-bit
  dispatcher's standing
  contract. The host matrix also
  corrected this lane's entry
  order: a null attribute name
  faults ahead of the path
  verdicts (measured: getxattr
  of an empty path with a null
  name answers -EFAULT, not
  -ENOENT), and getxattr and
  lgetxattr now check the name
  before the empty-path verdict.
  The descriptor forms' own
  verdicts are a separate
  filing (FD-162).

- **FD-158 — readlink and
  readlinkat answer -EINVAL for a
  path that does not exist, on
  both dispatchers; the host
  answers -ENOENT.** *(Low. Filed
  by the FD-139-152 lane,
  2026-11-04; provisional until
  the merge lane's audit.)*
  KProcess::readlinkInDirectory
  returns -EINVAL for any
  resolution miss (the same answer
  it gives a resolved non-link),
  and the 64-bit dispatcher's
  readlink helper does the same;
  host-measured at the raw syscall
  level (test-results/fd139-152/
  hostref-fd139-152c.c):
  readlinkat of a missing path
  answers -ENOENT, of an existing
  non-link -EINVAL. CL-108 taught
  both sites the -ENOTDIR case
  only; the plain-missing case
  stays -EINVAL. Closing terms:
  readlink/readlinkat of a
  missing path answers -ENOENT on
  both dispatchers, of an existing
  non-link stays -EINVAL, proven
  by guest tests. Filed, not
  fixed. Closing account
  (lane-fd157-161, 2026-11-04):
  closed on this branch -- see
  CL-114. Both resolver sites
  split the missing case from
  the resolved-non-link case:
  a missing path answers
  -ENOENT, a resolved non-link
  keeps -EINVAL, and the
  through-a-file case keeps
  -ENOTDIR.

- **FD-159 — A path whose final
  component is erased by
  normalization (a trailing "/",
  "/." or "/.." through a regular
  file) resolves to the file
  itself where the host answers
  -ENOTDIR.** *(Low. Filed by the
  FD-139-152 lane, 2026-11-04;
  provisional until the merge
  lane's audit.)* Host-measured
  (raw syscalls, test-results/
  fd139-152/hostref-fd139-152.c):
  statfs("a.txt/"), statfs
  ("a.txt/."), statfs("a.txt/..")
  all answer -ENOTDIR. The guest's
  printed observations on the
  FD-140 lane's base (testNotDir64
  run, test-results/fd139-152/
  red-x64.log) answer 0 for all
  three: the resolver's path
  normalization drops the trailing
  component before the walk, so
  no walk step ever looks inside
  the file and CL-108's verdict
  cannot form. Distinct mechanism
  from FD-140 (there the walk
  itself stops inside the file);
  left open deliberately so the
  normalization change is scoped
  on its own. Closing terms:
  "a.txt/", "a.txt/." and
  "a.txt/.." answer -ENOTDIR at
  the path calls while ".", "..",
  and trailing-slash paths through
  real directories keep their
  current answers, proven by
  guest tests. Filed, not fixed.
  Closing account
  (lane-fd157-161, 2026-11-04):
  closed on this branch -- see
  CL-115. The resolver itself
  now judges the anchor: in
  Fs::getNodeFromLocalPath, when
  the caller asks for the
  notDir verdict and the raw
  path ends in "/", "/." or
  "/..", the component before
  the marker is resolved first;
  a non-directory anchor sets
  the notDir verdict, a missing
  anchor under "/.." answers
  the plain miss the erased
  pair used to skip, and a
  directory anchor falls
  through to the ordinary walk.
  The same marker at readlink
  steps through the link and
  judges its target (a link to
  a directory answers -EINVAL,
  to a file -ENOTDIR), measured
  on the host in this lane and
  handled at both readlink
  sites. The audit the filing's
  mechanism implied found the
  mid-path form of the same
  erasure (a ".." after a
  missing or non-directory
  component in the middle of a
  path), which is a different
  mechanism and is filed
  separately as FD-164.

- **FD-160 — The 64-bit
  dispatcher's rmdir (84) has no
  case: it answers -ENOSYS
  ("unimplemented syscall #84")
  for every path.** *(Low. Filed
  by the FD-139-152 lane,
  2026-11-04; provisional until
  the merge lane's audit.)* Found
  by this lane's 64-bit -ENOTDIR
  guest test: rmdir of any path
  logs the unimplemented line and
  answers -ENOSYS, where the
  host answers by shape (0 for
  an empty directory, -ENOTDIR
  through a file, -ENOENT for a
  missing path — raw-measured in
  this lane's host matrix) and
  the 32-bit dispatcher routes
  the call to KProcess::rmdir,
  which implements it, as does
  the 64-bit mkdir case beside
  it. Closing terms: rmdir at
  the 64-bit dispatcher routes
  to KProcess::rmdir like the
  neighbouring cases and answers
  the host's verdicts, proven by
  a guest test. Filed, not fixed.
  Closing account
  (lane-fd157-161, 2026-11-04):
  closed on this branch -- see
  CL-116. The 64-bit dispatcher
  gained the missing rmdir case
  (84), routed to
  KProcess::rmdir like the
  32-bit dispatcher, with the
  null-path (-EFAULT) and
  empty-path (-ENOENT) verdicts
  the neighbouring cases carry.

- **FD-161 — A hard link whose
  destination is a bare filename
  in the working directory
  answers -ENOENT even when the
  source exists.** *(Low. Filed
  by the FD-139-152 lane,
  2026-11-04; provisional until
  the merge lane's audit.)*
  KProcess::link resolves the
  destination's parent through
  Fs::getParentPath of the raw
  destination; for a slash-less
  name that computation yields
  the name itself
  (lastIndexOf finds no slash
  and substr(0, -1) returns the
  whole string), so the parent
  lookup resolves the
  not-yet-existing destination
  and misses. Measured on the
  FD-140 lane's base and fixed
  trees alike (its process-API
  link control): link("a.txt",
  "hl.txt") answers -ENOENT;
  the host creates the link.
  rename and symlink compute the
  full path first and do not
  share the shape. Unrelated to
  FD-140 (no non-directory is
  involved); found by that
  lane's control. Closing terms:
  link to a bare filename in the
  working directory succeeds,
  and the other getParentPath
  consumers are audited for the
  same slash-less shape, proven
  by guest tests. Filed, not
  fixed. Closing account
  (lane-fd157-161, 2026-11-04):
  closed on this branch -- see
  CL-117. Fs::getParentPath of
  a slash-less path now returns
  the empty relative path (the
  working directory) instead of
  the string itself. The audit
  the closing terms required
  covered every getParentPath
  consumer: the absolute-path
  callers (executable lookup,
  open-create, mkdir, rename's
  destination, symlink, the
  unix-socket bind path, the
  startup-argument scan, the
  container UI) always hold a
  slash and are unchanged; the
  rename/renameat inotify
  parent lookups and the
  zip-mount caller take the
  same slash-less shape and
  now name the working-
  directory node, matching the
  host; the local-directory
  builder treats the empty
  parent as the no-op it
  already was for the root.

- **FD-162 — The 32-bit
  dispatcher's xattr descriptor
  forms answer the emulator-
  private -EBADFD (77) for an
  unopened descriptor, where the
  host answers -EBADF (9); and
  flistxattr answers -ENOTSUP
  without consulting its
  descriptor at all.** *(Low.
  Filed by the FD-157-161 lane,
  2026-11-04; provisional until
  the merge lane's audit.)*
  Host-measured at the raw
  syscall level in this lane's
  matrix (test-results/
  fd157-161/hostref-fd157-161
  .out): fgetxattr, fsetxattr,
  flistxattr and fremovexattr of
  descriptor 9999 all answer
  -EBADF on the host. The 32-bit
  handlers answer -K_EBADFD for
  the three forms that look the
  descriptor up, and flistxattr
  returns -K_ENOTSUP
  unconditionally. The 64-bit
  dispatcher answers -EBADF for
  the same shapes (its FD-133
  block checks the descriptor),
  so this is a 32-bit-only
  divergence. The descriptor
  forms sit outside FD-157's
  eight path forms; found while
  measuring that filing's host
  matrix. Closing terms: the
  four 32-bit descriptor forms
  answer -EBADF for an unopened
  descriptor (and flistxattr of
  a valid descriptor answers
  the empty list), proven by
  guest tests. Filed, not fixed. Closing
  account (lane-fd162-164,
  2026-11-05): closed on this
  branch -- see CL-119. The four
  descriptor forms now share the
  host's entry order: the set
  form's flags word first
  (-EINVAL for a word outside
  0x3), then the name fetch, then
  the descriptor verdict, which
  is -EBADF (-EBADFD is gone);
  and flistxattr consults its
  descriptor, answering the empty
  list for an open one. Fix
  `9b4ce67b1`; guest test
  testXattrDescriptor32Syscalls
  red on the base, green on the
  fix. Record: `docs/reports/
  fd162-164-xattr-namespaces-
  dotdot-20261105.md`.

- **FD-163 — An xattr name in a
  namespace the emulator does
  not model answers -ENODATA on
  a resolved file, where the
  host answers -ENOTSUP (the
  namespace itself is not
  supported).** *(Low. Filed by
  the FD-157-161 lane,
  2026-11-04; provisional until
  the merge lane's audit.)*
  Host-measured at the raw
  syscall level in this lane's
  matrix: getxattr of an
  existing file with the name
  "bogus.x" answers -ENOTSUP on
  the host (an unknown namespace
  is rejected before the
  attribute lookup), while a
  well-formed name in a known
  namespace ("user.x") answers
  -ENODATA. Both dispatchers
  answer -ENODATA for every
  unresolved name on a resolved
  file, so a guest cannot tell
  "namespace unsupported" from
  "attribute absent". FD-157's
  closing terms allowed -ENOTSUP
  to stay only where the host's
  -ENODATA is not the measured
  answer; for unmodelled-
  namespace names the host's
  measured answer is -ENOTSUP,
  so the case belongs to a
  namespace-validity rule, not
  to FD-157's resolution fix.
  Closing terms: getxattr-class
  calls with a name outside the
  modelled namespaces answer
  -ENOTSUP on a resolved file on
  both dispatchers, proven by
  guest tests. Filed, not fixed. Closing
  account (lane-fd162-164,
  2026-11-05): closed on this
  branch -- see CL-120. The get
  forms on both dispatchers now
  answer the namespace verdict on
  a resolved target: a prefix
  with no handler -ENOTSUP, a
  recognised namespace with an
  absent attribute -ENODATA, a
  bare recognised prefix -EINVAL;
  and an empty name answers
  -ERANGE at fetch time, ahead of
  the path and descriptor
  verdicts. Fix `9b4ce67b1`;
  guest tests
  testXattrNamespace32Syscalls
  and testXattrNamespace64Syscalls
  red on the base, green on the
  fix. Record as in CL-119.

- **FD-164 — A ".." component
  away from the end of a path is
  disposed of textually, without
  the component before it ever
  being resolved: "missing/../
  a.txt" resolves as "a.txt" and
  "a.txt/../d" resolves as "d",
  where the host answers -ENOENT
  and -ENOTDIR; and a bare ".."
  at the root answers -ENOENT in
  the guest where the host
  answers 0 (the root's parent
  is itself).** *(Low. Filed by
  the FD-157-161 lane,
  2026-11-04; provisional until
  the merge lane's audit.)*
  Host-measured at the raw
  syscall level in this lane's
  matrix (statfs): a ".." after
  a missing component answers
  -ENOENT and after a regular
  file -ENOTDIR, because the
  host resolves each component
  as it walks. The guest's
  cleanPath erases each ".."
  together with its predecessor
  before any lookup (the same
  erasure FD-159 covers at the
  END of a path; FD-159's
  resolver fix judges only the
  trailing marker, so the rest
  of the family survives it --
  observed in this lane's guest
  tests, which print the three
  shapes answering with the
  erased resolution). Closing
  terms: a non-trailing ".."
  whose predecessor does not
  resolve answers -ENOENT, one
  whose predecessor resolves to
  a non-directory answers
  -ENOTDIR, and ".." at the root
  resolves to the root, at the
  path calls, proven by guest
  tests. Filed, not fixed. Closing
  account (lane-fd162-164,
  2026-11-05): closed on this
  branch -- see CL-121. When a
  caller asks for the notDir
  verdict, the resolver now
  judges each non-trailing ".."
  by resolving the path ahead of
  it -- a miss propagates as a
  miss, a predecessor resolving
  to a non-directory sets the
  verdict -- and the path
  normalisation clamps a ".." at
  the root instead of failing the
  walk. Fixes `9b4ce67b1` and
  `872d1b474` (the slice-safe form
  of the scan, behaviour
  identical); guest tests
  testMidDotDot32Syscalls and
  testMidDotDot64Syscalls red on
  the base, green on the fix.
  Record as in CL-119.

- **FD-165 — The set/remove
  xattr forms' namespace
  classification is unmeasured
  on the host and unmodelled in
  the guest: beyond the one
  modelled name, both dispatchers
  answer their standing -ENOTSUP
  for every name on a resolved
  target, whether or not the host
  would classify the name first.**
  *(Low. Filed by the FD-162-164
  lane, 2026-11-05; provisional
  until the merge lane's audit.)*
  FD-163's raw-syscall matrix
  covered the get forms, whose
  namespace rule is now
  implemented; the same matrix
  did not extend to the set/remove
  forms, and the one remove-side
  shape it did touch (the modelled
  name through the descriptor
  form, on a file) is not pinned
  for a file that does not carry
  the attribute. Whether the host
  classifies names for
  setxattr/removexattr and their
  l/f variants — and so whether
  the standing -ENOTSUP is right
  for each name class there — is
  unknown. Closing terms: the
  set/remove forms' name handling
  measured at the raw syscall
  level (including the descriptor
  remove form for the modelled
  name on a file that does not
  carry it) and aligned with the
  measurement, proven by guest
  tests. Filed, not fixed.
 Closing
  account (lane-fd165,
  2026-11-05): closed on this
  branch -- see CL-122. The
  set/remove forms' name handling
  was measured at the raw syscall
  level (four rounds, including
  the descriptor remove form for
  the modelled name on a file
  that does not carry it, which
  answers -ENODATA) and aligned
  with the measurement: the set
  forms copy their value ahead of
  the target verdicts (a null
  value with a nonzero size
  answers -EFAULT); on a resolved
  target an unknown family and
  system.* outside the posix_acl
  names answer -ENOTSUP, a bare
  recognised prefix answers
  -EINVAL, a posix_acl set
  answers -EINVAL and its remove
  on a file or directory is a
  no-op success, and on the link
  itself every user.* name
  answers -EPERM. Recognised
  namespaces are stored (one
  native sidecar per attribute),
  so the host's 0 reads back
  through the get/list forms,
  with CREATE answering -EEXIST
  and REPLACE -ENODATA. Fixes
  `a2899c5b5` and `37dba7b69`;
  guest tests
  testXattrWriteClassify32Syscalls
  and
  testXattrWriteClassify64Syscalls
  red on the base, green on the
  fix. Record: `docs/reports/
  fd165-xattr-write-
  classification-20261105.md`.

- **FD-166 — A first-visit 32-bit
  start of a shipped build can stall
  before the desktop handshake with
  no fault lines at all: the earliest
  guest processes stop making any
  guest progress while every other
  thread blocks, and the start fails
  silently. FIXED 2026-11-05 on branch
  `lane-fd115` — see CL-128.**
  *(Medium. Filed and fixed by the
  FD-115 lane, 2026-11-05; the number
  is the next free on the register
  that lane based on and settles at
  the next composition.)* Observed
  once, during that lane's
  reproduction series against the
  live shipped batch-7 build
  `4006ddb58` / wasm `c0af957a`,
  measured through the public route
  with FD-115's filing protocol
  (fresh profile per boot, bwretry=2,
  900 s window, FD-47 classification):
  boot 10 of 20, result class "other",
  pageReady false, note "budget
  exhausted", wall 901,559 ms. Its
  preserved console (2,373 lines,
  `test-results/fd115/repro-live-batch7/boot-10/console.log`
  in the lane tree) carries 0
  BW64-FAIL lines and 0
  guest-memfault32 lines — not
  FD-115's shape. Explorer was opened
  (twice) but no deskpin READY line
  ever printed. Across the console's
  14 thread dumps, the session's
  three earliest processes (pids 10,
  12, 14) each show one running
  thread with min_noprogress_ms
  growing to 780078 — no guest
  progress for the final 780 s —
  their step-rings cycling the same
  emulator steps, while all 25 other
  dumped threads sat blocked in
  socket reads
  (wait=KUnixSocketObject::lockCond),
  no fetch in flight, no
  memory-mutex owner. No failure was
  surfaced: FD-77's surfacing
  convicts only after the handshake,
  and FD-115's only on delivered
  faults. No mechanism is claimed
  for the underlying guest stall;
  none is visible in the console.
  The series' other 18 measured
  starts certified (median 58.9 s);
  measured incidence for this shape
  on shipped builds: 1 in 19 in this
  series. Closing terms: a
  pre-handshake stall of this shape
  is surfaced to the user as a
  reported failure instead of an
  endless wait — met by the fix in
  CL-128. Lane record:
  `docs/reports/fd115-fd165-prehandshake-surfacing-20261105.md`.

- **FD-167 — Forty function pairs that shipped demo programs import
  resolve to unimplemented stubs: the program loads, and the named
  feature path fails when exercised.** *(Medium. Filed by the
  DLL-export-census lane, 2026-11-05; provisional until the merge
  lane's audit.)* Measured by the export-implementation census
  (`tools/conformance/dll_export_census.py`; evidence
  `test-results/dll-export-census/dll-export-census.json`). No demo
  imports a stub on its static load path (in-closure regular imports:
  0 across all 15 demos); every hit is a delay import or an import of
  a dynamically loaded library. Chromium's chrome.dll delay-imports
  35 stub functions: Bluetooth GATT (bluetoothapis ×8, bthprops.cpl
  ×3), the ESENT Jet engine (×12), Media Foundation (mf
  MFCreateDeviceSource, mfplat MFCreateMediaBufferWrapper), wevtapi
  EvtRender, WinUSB (×8), propsys VariantCompare. Gang Beasts' Xbox/
  Party plugin DLLs regularly import four msvcp140 error-path helpers
  (std::_Throw_C_error, std::_Throw_Cpp_error, std::_XGetLastError,
  std::_Xbad_function_call — family overlap with FD-169). Mindustry's
  steam_api64.dll regularly imports setupapi
  SetupDiGetDeviceInterfaceAlias. Closing terms: each pair either
  implemented in the shipped libraries (the census re-run shows it
  resolved and the baseline is regenerated in the same change) or
  proven unreachable from the importing program, with the reason
  recorded here. Filed, not fixed.
  **Disposition (2026-11-05, `lane-fd167`):** 39 of the 40 pairs meet
  a closing term on this branch. Twenty-seven are implemented in the
  shipped libraries (census re-run resolves them in the
  architectures the demos use; baseline regenerated in the same
  change): bluetoothapis GATT \u00d78 + bthprops.cpl \u00d73 (documented
  no-radio contract), winusb \u00d78 (documented no-device contract),
  mf MFCreateDeviceSource (upstream's implementation), mfplat
  MFCreateMediaBufferWrapper (a genuine offset/length window over an
  existing buffer), propsys VariantCompare, wevtapi EvtRender
  (upstream's not-implemented answer instead of a stub death),
  msvcp140 \u00d73 (the throw helpers, upstream bodies backported), and
  setupapi SetupDiGetDeviceInterfaceAlias (a genuine lookup). The
  twelve ESENT Jet pairs are proven unreachable: their only caller
  in any shipped demo is Chromium's legacy-Edge profile importer
  (the call sites sit among that importer's own strings), which runs
  only against a legacy EdgeHTML profile's ESENT WebCache database
  on an explicit browser-data import; the shipped demo carries no
  such profile and no such action, and upstream Wine has never
  implemented esent (336/336 named stubs), so there is no reachable
  path and no upstream behaviour to mirror. The remaining pair,
  msvcp140 `?_Throw_Cpp_error@std@@YAXH@Z`, is neither implemented
  nor proven unreachable and is refiled as FD-172. Proof and full
  account: `docs/reports/fd167-demo-reachable-stub-contracts-20261105.md`.
- **FD-172 — msvcp140 `?_Throw_Cpp_error@std@@YAXH@Z` remains an
  unimplemented stub: no authoritative semantics for it exist in any
  open source consulted.** *(Low. Filed by the FD-167 lane,
  2026-11-05 as that entry's remainder; provisional until the merge
  lane's audit.)* The function is one of the four MSVC helpers a
  Gang Beasts plugin DLL imports (FD-167); the other three are
  implemented there. Wine master and ReactOS both ship this export
  as a stub, it does not appear in Microsoft's open STL sources, and
  its argument's enumeration appears in no public header, so there
  is nothing to mirror: guessing which C++ exception it throws would
  hand the guest a silent wrong answer instead of the stub's loud
  failure. Closing terms: implement it once an authoritative
  source for its semantics exists (a public header, a documented
  enumeration, or a behavioural reference from the real DLL), proven
  by a guest test like FD-167's; until then it stays a stub and the
  census keeps it counted. Filed, not fixed.
- **FD-168 — win32u.dll carries 2,037 validated unimplemented stubs
  (926 x86-64 + 1,111 i386): 1,572 NtUser*/NtGdi* names plus 465
  composition/display/Dxgk-family names a program can call and be
  answered "unimplemented".** *(Medium. Filed by the DLL-export-
  census lane, 2026-11-05; provisional.)* Measured by the
  export-implementation census; the exact list is the win32u rows of
  `tools/conformance/dll_export_baseline.json`. The working NtUser/
  NtGdi surface uses the syscall-entry shape and is not in this
  count (the census distinguishes the two generated shapes and
  validates every counted stub against the name string embedded in
  its own body: 34,990 hits, 0 unvalidated, across the shipped set).
  Closing terms: drawn down family by family — implementation in
  the Wine build (a rootfs restage) or a recorded per-family
  disposition that no guest can reach the family in this
  environment; each draw-down regenerates the census baseline in
  the same change. Filed, not fixed.

  *Addendum — first draw-down (lane-fd168, 2026-11-05; partial, the
  entry stays open).* Fourteen x86-64 names are now real syscall
  implementations in the Wine 8 backports build (patch
  `tools/buildWine/patches/wine8-win32u-fd168-backports.patch`,
  restaged runtime, re-pinned hashes, census baseline regenerated in
  the same change): NtUserAlterWindowStyle, NtUserBuildHimcList,
  NtUserBuildNameList, NtUserBuildPropList, NtUserGetClipCursor,
  NtUserGetCurrentInputMessageSource, NtUserGetPointerType,
  NtUserGetThreadState, NtUserQueryDisplayConfig, NtUserQueryWindow,
  NtUserRegisterWindowMessage, NtGdiGetMiterLimit,
  NtGdiSetMiterLimit, NtGdiIcmBrushInfo. Census effect: x86-64
  win32u stubs 926 -> 912; i386 unchanged at 1,111; FD-168 total
  2,037 -> 2,023; shipped-set named stubs 33,756 -> 33,742; only
  x64/win32u.dll changed across all 1,171 tables. Proof: guest test
  `win32u-fd168` PASS — 17 checks calling the previously stubbed
  exports directly and cross-checking each answer against the
  user32/gdi32/imm32 counterpart or documented contract, all ok=1,
  fixture RESULT 0 (the run also caught and fixed a real bug: the
  miter-limit pair must use Wine 10's DWORD bit-pattern parameter
  form, not the FLOAT form Wine 8's stale header declares); JS unit
  suite 354 passed / 0 failed on the final tree; legacy 115/0;
  completeness checker and bootstrap 0 failures. Key reachability
  fact: across every shipped DLL on both architectures, zero of the
  2,037 names is imported by any other shipped DLL — all reachability
  is a guest calling win32u directly. Dispositions for the remaining
  2,023, each recorded rather than left silent: (a) 31 NtGdiDdDDI*
  names are implemented upstream only inside Wine 10's
  Vulkan-backed D3DKMT layer (`dlls/win32u/d3dkmt.c`), which has no
  Wine 8 counterpart and which the emulator's DXVK/vkd3d path does
  not use; gdi32 exposes no D3DKMT surface here. (b) Six names are
  blocked on machinery this vintage does not have: NtUserSwitchDesktop
  (no `set_input_desktop` wineserver request; wineserver ships from
  the vendor build, shared with the 32-bit side), NtUserNotifyIMEStatus
  (no `pNotifyIMEStatus` in Wine 8's user-driver interface),
  NtUserSetWindowFNID (no `set_window_fnid` request, no `fnid` field),
  NtUserModifyUserStartupInfoFlags (the flags word it mutates is a
  win32u-side cache introduced upstream later; nothing here reads it),
  NtUserGetPointerDeviceRects (no pointer-device model; upstream's own
  body returns an error for any real device handle), NtGdiMakeFontDir
  (depends on upstream's restructured font engine; Win16-era API).
  (c) Eight names have only FIXME semi-stub bodies upstream that
  return success-shaped constants without doing the work
  (NtUserEnableMouseInPointer, NtUserIsMouseInPointerEnabled,
  NtUserGetPointerInfoList, NtUserRegisterTouchPadCapable,
  NtUserInitializeTouchInjection, NtUserGetWindowDisplayAffinity,
  NtUserIsChildWindowDpiMessageEnabled, NtGdiCancelDC) — porting them
  would fake the draw-down, so they stay stubs until a real
  implementation is possible. (d) One name, NtUserSendInteracsetive-
  ControlHapticsReport, is a misspelling in Wine 8's own spec that
  upstream removed; no caller can exist for it. (e) The remaining 866
  x86-64 names are still unimplemented in current upstream Wine —
  the largest cluster (215) is the compositor/input-compositor
  internal surface whose counterparty processes (dwm.exe and the
  Windows composition stack) do not exist in Wine's architecture,
  followed by the NtGdiDdDDI* kernel-graphics surface (153, same
  disposition family as (a)) and long tails of NtUser*/NtGdi*
  getters, setters and registration calls with no upstream body to
  port and no importer in the shipped set. (f) The i386 side's 1,111
  stubs ship from the vendor Wine 10.0 package build; this project
  has no i386 PE-substitution pipeline (only win32u.so is rebuilt),
  and the 35 names Wine 10 implements are already real on i386
  because the shipped i386 DLL is that build. FD-168 stays open at
  2,023 with these dispositions. Record:
  `docs/reports/fd168-win32u-drawdown-20261105.md`.
- **FD-169 — The shipped Microsoft C/C++ runtime DLLs carry 10,698
  validated unimplemented stubs: msvcp60–140, msvcr70–120 (+_app),
  ucrtbase, concrt140, msvcm80/90 and the api-ms-crt set.** *(Medium.
  Filed by the DLL-export-census lane, 2026-11-05; provisional.)*
  Measured by the export-implementation census; the exact list is
  in `tools/conformance/dll_export_baseline.json`. Any MSVC-built
  program whose code paths reach these functions — C++ streams,
  complex arithmetic, concurrency runtime, locale and conversion
  helpers — is answered "unimplemented" at call time; the four
  Gang Beasts imports of FD-167 are this family's demo-visible tip.
  Closing terms: family-by-family implementation or substitution
  of the shipped CRT DLLs with functional builds, proven per family
  by a guest test that calls the previously stubbed functions and
  by the census re-run with the baseline regenerated in the same
  change. Filed, not fixed.
  Progress (2026-11-06, lane-fd169): the C-runtime family is
  discharged. 1,634 of the 10,698 stub slots are genuinely
  implemented (916 x86-64 + 718 x86), proven per DLL by the
  regenerated census (total stub slots 34,990 -> 33,356, named
  stubs 32,122; baseline drops by exactly the resolved count and
  the census unit gate passes) and at guest level by
  fd169-crt64 / fd169-crt32 (54 behavioural checks each: RESULT 0
  on the new runtime; the x86-64 fixture dies on the pre-change
  runtime with no RESULT line -- negative control run against the
  retained pre-stage wine64.zip). Per-DLL stub counts are in
  docs/reports/fd169-crt-family-20261105.md. What remains in this
  entry, each with its reason in that record: the concurrency
  runtime (concrt140 + the same names inside msvcr100/110/120),
  the mixed-mode msvcm80/90 assemblies, the legacy msvcp60/70/71
  STL ABIs, the msvcp90-source family remainders (codecvt, iostream
  and time facets), msvcp140_2's special-function set,
  msvcp140_atomic_wait (incl. the tzdb functions, which have no
  data source in the browser build), vcruntime140's exception
  machinery, the msvcrtd debug heap, the __crt* UCRT/compiler
  plumbing, and a short tail of functions whose contracts are not
  publicly pinned (_getsystime/_setsystime, the _d/_f/_ld libm
  helpers, the _mbcasemap data export). FD-172 (filed by this
  lane) covers a neighbouring gap the census cannot see: complex
  helpers missing outright from the versioned DLLs' export lists.
  This entry stays open for the remainder families above.
- **FD-170 — Ninety shipped DLL tables are ≥90% unimplemented stubs
  (6,734 named stubs, excluding the win32u and CRT families filed
  separately): entire subsystems a program can load but never use —
  esent.dll is 336/336 named stubs in both architectures (the Jet
  database engine does not exist), msasn1 259/266, adsldpc 175/175,
  wdscore 159/159, bluetoothapis 95/96, winusb 21/22, plus dx8vb,
  mprapi, fwpuclnt, clusapi, regapi, samlib and 78 more.**
  *(Medium. Filed by the DLL-export-census lane, 2026-11-05;
  provisional.)* Measured by the export-implementation census; the
  exact table list and stub lists are in
  `tools/conformance/dll_export_baseline.json`. Chromium's ESENT
  and Bluetooth imports (FD-167) land here. Closing terms: a
  per-DLL decision recorded here — implemented, substituted, or
  accepted as a documented absence (for hardware- and
  service-bound subsystems a browser build may never be able to
  provide); the census keeps the list exact meanwhile, and any
  draw-down regenerates the baseline in the same change.
  **DISCHARGED on `lane-fd170` per those terms — see CL-131**
  (Batch-9 settled number: CL-131. FD-167 is not discharged — it stays
  open: 39 of 40 pairs meet a closing term and the 40th is refiled as
  FD-172.)
  Record: `docs/reports/fd170-whole-dll-decisions-20261105.md`.
  Method: the stub state of all 50 DLLs was measured in all three
  Wine vintages the project can draw on (shipped 8.0, shipped 10.0,
  upstream master); for 49 of the 50 it is essentially identical in
  all three — upstream has never implemented them anywhere, so
  there is nothing authoritative to mirror. Implemented on the x64
  side, where an authoritative source exists
  (`wine8-fd170-contracts.patch` in the Wine 8.0 backport pipeline,
  hash-pinned fail-closed; census-verified against the built
  binaries): bluetoothapis 95 → 71 named stubs (Wine 10.0's own
  radio/SDP code backported + the FD-167 no-radio/no-device
  contract bodies; the x86 side was drawn down by FD-167), winusb
  21 → 13 (the FD-167 no-device contract bodies; x86 likewise
  FD-167's), netutils 22 → 17 (Wine 10.0's `NetApiBuffer*`
  heap-buffer functions). The remainders of those three stay stub:
  bluetoothapis's device/SDP-object layer is stub even on master
  (72 of 97) pending upstream's winebth driver work; winusb's
  descriptor/policy layer is libusb-backed upstream and the
  sandbox has no USB device backend; netutils's other 17 are
  server-service utilities stub in every vintage. Guest proof:
  `fd170-contracts64.test.mjs` is committed on the branch; its
  verdict is owed to the batch-9 gate — every attempt on
  2026-11-05/06 died in the shared probe queue or in a guest boot
  that outlasted the hung-probe cap at load 13–19 (preserved logs
  in the record), the same handling FD-167's x64 side received.
  Documented absences (47 DLLs; the missing substrate in each
  case; per-DLL counts and evidence in the record): esent — a full
  embedded database engine, never implemented upstream in any
  vintage (336/336 on master); msasn1 — a whole ASN.1 encoding
  runtime: upstream implements 7 of 266 in every vintage and the
  rest has no authoritative open implementation to mirror (the
  FD-172 standard: unmirrorable semantics stay stub, not
  guessed); adsldpc — Active Directory directory service; apphelp
  — the system shim database (a Windows data artifact this
  runtime does not ship) plus in-process patching machinery;
  fwpuclnt — the kernel filtering platform / BFE service; clusapi
  and resutils — the failover-cluster service; mprapi and rtutils
  — the RRAS service; samlib — the SAM/LSA service; sppc — the
  software-protection/activation service and its infrastructure;
  winsta — the session-manager service (the runtime has one
  session by construction); localspl, ntprint, printui — the
  print spooler (no spooler, no printers); vssapi — the
  volume-shadow-copy service and snapshot drivers (no
  block-device layer); feclient — the EFS service; wiaservc —
  the WIA service and imaging hardware; dhcpcsvc and dhcpcsvc6 —
  the DHCP client service (guest networking is the emulator's
  relay; upstream stubs the whole DLL in every vintage including
  the trivial initialize pair, and implementing the pair alone
  would fake a service that cannot answer); ktmw32 — the kernel
  transaction manager; tbs — TPM hardware; hal — the hardware
  layer itself; d3d8thk — display-driver kernel thunks (graphics
  here is the WebGPU/WebGL bridges); slbcsp and sccbase —
  smart-card hardware; traffic — the kernel packet scheduler;
  msdrm and drmclien — DRM/RMS license infrastructure and
  protected-content paths; twinapi.appcore, appxdeploymentclient
  and coremessaging — the UWP/WinRT app model and its deployment
  service; msls31 — a full text-layout engine never implemented
  upstream; dx8vb — the retired DirectX 8 VB automation layer;
  atmlib — retired ATM networking hardware; regapi — the NT-era
  registry API superseded by advapi32 (provided); utildll and
  svrapi — retired LAN Manager surfaces; npptools — NetWare-era
  provider tools; w32skrnl — the 16-bit WOW layer (the CPU core
  is long-mode only; no 16-bit subsystem can exist beneath it);
  rasdlg — RAS dial-up UI and service; wmasf — the ASF media
  runtime superseded by Media Foundation (provided); tdh — ETW
  provider metadata (no provider registry is kept); odbcbcp —
  SQL Server bulk copy (needs the SQL Server driver and a
  server; odbc32 itself is provided); bcp47langs — WinRT
  globalization data (upstream 2 of 71 on master); magnification
  — the desktop magnifier/compositor stack; wdscore —
  undocumented setup-scoring internals. Census after this
  change: 33,719 named stubs total (was 33,756; exactly the 37
  resolved), the FD-170 remainder 6,697, baseline regenerated in
  the same change, runtime assets re-cut as
  `runtime-assets-20261105-fd170`.

- **FD-171 — The remaining stub long tail: 14,287 validated
  unimplemented stubs spread over the mostly-implemented shipped
  DLLs (setupapi 263+265 across arches, rpcrt4 214+214, combase
  175+166, sechost 120+120, ntdll 113+111, kernel32 89+97, shell32
  69+69, gdi32 67+68, and several hundred further tables), under a
  standing guard: the export-implementation census fails its check
  on any new stub in the shipped libraries.** *(Low. Filed by the
  DLL-export-census lane, 2026-11-05; provisional.)* Totals for the
  shipped batch-7 vintage: 161,676 named exports across 1,171 DLL
  tables; 127,920 (79.1%) are real implementations or forwarders to
  one; 33,756 (20.9%) are validated stubs — partitioned disjointly
  as FD-168 (2,037) + FD-169 (10,698) + FD-170 (6,734) + this entry
  (14,287). The exact per-function list is
  `tools/conformance/dll_export_baseline.json`, guarded by
  `tools/conformance/dll_export_census.py --check` and by
  `web/tests/suite/unit/dll-export-census.test.mjs`. Closing terms:
  this entry closes when the long tail is dispositioned DLL by DLL
  (implemented, or accepted with a recorded reason) — until then it
  is the register's standing measure of the unimplemented surface,
  and it may only shrink: a resolved stub is recorded by
  regenerating the baseline in the fixing change, and a new stub
  fails the check until it is filed here or fixed. Filed as the
  standing census entry, not fixed.
  **FD-171 lane update (2026-11-05, lane-fd171):** the long tail is
  now dispositioned function by function in
  `tools/conformance/dll_export_dispositions_fd171.json` (all
  14,287 entries: 203 implemented by this lane, 19 implemented by
  the sibling FD-167 lane on the same census base, the rest left
  with a recorded reason — 12,303 still stubs in upstream Wine
  master c42c1532, 1,381 absent from upstream's spec entirely, and
  the remainder in named blocked/rejected categories). The 203
  implementations (config-manager family in setupapi/cfgmgr32,
  ntdll splay generic tables + fast activation-context calls,
  propsys conversions, pdh formatted counter arrays, combase
  HSTRING marshaling + fail-fast entry points, kernelbase/kernel32
  HeapSummary + ReadConsoleInputEx + RegisterWaitForInputIdle, and
  the single-function DLLs) ship as
  `tools/buildWine/patches/wine8-fd171-contracts.patch` (x86-64,
  Wine 8.0 backports) and `wine10-fd171-contracts.patch` (i386,
  Wine 10.0), staged hash-pinned into both wine zips; the census
  baseline is regenerated in this change and drops by exactly the
  203 resolved (33,756 -> 33,553 named stubs), with zero new stubs.
  Proven by the guest contract fixtures
  `web/tests/suite/browser/fd171-contracts64.test.mjs` /
  `fd171-contracts32.test.mjs`. Record:
  `docs/reports/fd171-long-tail-20261105.md`. The standing guard
  (census `--check` + the JS census unit test) is unchanged and
  remains this entry's enforcement. The lane claims the entry's
  closing terms are met (dispositioned DLL by DLL with recorded
  reasons); final closure settles at the merge audit, which must
  also reconcile the propsys/mf overlap with FD-167's pins (the two
  lanes' backports touch disjoint functions in those DLLs).


- **FD-175 — The JIT statistics text export truncates at 3072
  bytes, so on any full Wine boot the statistics JSON is
  unparseable and consumers silently lose the engine counters.**
  *(Low. Filed by the performance lane, 2026-11-05; provisional
  until the merge lane's audit.)* `bw64_jit_stats_json_text()`
  renders into a `static thread_local char text[3072]` in both
  build flavors (`source/emulation/cpu/wasm/wasmJitStats.cpp`,
  `source/emulation/cpu/jit64wasm_runtime.cpp`). After a real
  boot the JSON (driven by the reject census) exceeds 3072 bytes;
  `JSON.parse` of the result throws "Unterminated string in JSON
  at position 3071" (observed in vivo, suite run
  `perf2-bench-before`, which completed its samples and then
  failed on the stats read). `web/tests/suite/browser/
  jitbench.test.mjs` reads stats through this export, so its
  recorded cpu64 stats are the ones at risk. The buffer export
  `bw64_jit_stats_json(char*, int)` is unaffected; consumers can
  also read the same counters from the runtime's METRICS console
  lines. Filed, not fixed.

- **FD-176 — The block-invalidation differential gate no longer
  links: `scripts/build-jit-wasm.sh` fails on the integration
  tip, so the invalidation tests currently have no working build
  path.** *(Medium. Filed by the performance lane, 2026-11-05;
  provisional until the merge lane's audit.)* The script's
  standalone recipe predates the FD-82 / FD-129 cross-references:
  linking `jit64_invalidate_tests.cpp` now reports undefined
  references to `FsOpenNode::noteMappedPagesReleased` (from
  `kmemory64.cpp`) and `bwCurrentThreadRegistryLookup()` (gate
  log `inv_build_interp.log`, 2026-11-05). Adding the defining
  translation units only moves the gap (`KInotify::notifyClosed`,
  `BoxedWineCondition` ctor/dtor, then `FsNode::linkGroupMembers`,
  `removeTimer`, `scheduleThread`): the recipe needs the kernel
  core set, or the tests need to move into a harness that already
  links it. The tests themselves were not run by this lane for
  exactly this reason. Filed, not fixed.
  CLOSED — see CL-134. Fixed on `lane-fd176` (commit `5bec2fa3e` on
  base `faf95d26c`): the recipe links the kernel core set
  (fsopennode, fsnode, kthread, ksystem, kinotify, kscheduler,
  synchronization, bfile, platform/linux, with sdl2-config flags),
  the three harness stubs that stood in for the unlinked kernel are
  removed, and — the second defect in the same gate — the blocks
  binary now runs with `BW64_JIT=1 BW64_JIT_ENGINE=off
  BW64_JIT_FAST_DISPATCH=0`, without which the BBlock cache is never
  entered and the differential is vacuous (fast dispatch defaults on;
  the engine defaults to interp). Proof: full
  `./scripts/build-jit-wasm.sh --selftest-only` exit 0 (jit64 phase-1
  873/0; gen emitters byte-identical; invalidate interp+blocks
  pass=54 fail=0, logs byte-identical; wasm fixture tests 314/0);
  sensitivity re-verified (bw64NoteWriteSlow neutered: T1/T3/T4 fail
  with stale values, pass=17 fail=37). Record:
  `docs/reports/fd176-invalidate-gate-link-20261106.md`.

- **FD-177 — `FsZip::init` still walks each
  archive's central directory entry by entry
  through the lazy fetch layer, at ~0.3 ms per
  entry: 1,606 ms of the standard x86 cold start
  across 5,677 entries in four archives.**
  *(Low — first-visit startup speed. Filed by
  lane-fd71, 2026-11-05, from the FD-71 phase
  attribution; record:
  `docs/reports/fd71-startup-decl-scans-20261105.md`;
  evidence: `test-results/fd71/`.)* The walk is
  the one the node index is built from, so it
  cannot be deleted like the declaration scans
  FD-71's lane removed; but the central directory
  is contiguous at the end of each archive, so
  fetching it in bulk and parsing it in memory —
  instead of one small proxied read per entry
  through the browser lazy layer — should remove
  most of the cost. The per-entry figures come
  from an instrumented build's segment timers
  (support archive: 1,219 ms of entry-info reads
  for 3,542 entries; wine32: 322 ms for 1,926).
  Closing terms: an init that obtains the
  directory in bulk, with before/after segment
  timings and the standard start measurement
  taken the same way, and identical node trees
  (the native suite plus a boot on each shipped
  archive set). Filed, not fixed.

- **FD-178 — Fresh-worktree setup
  duplicated ~2 GB of generated
  payload inputs into every tree,
  and the shipped prep helper no
  longer performs the ritual it
  documents.** *(Low. Filed and
  fixed by the shared-build-store
  lane, 2026-11-05; numbering
  provisional until the merge
  audit.)* Every new working copy
  needed the same generated
  inputs — the pinned runtime
  zips and Slang assets, the DXVK
  and vkd3d build outputs, the
  `tools/rootfs64` payload
  extractions (gecko, mono32,
  gstreamer32/64, openal,
  cjk-font) and the
  `tools/browser` tool payloads
  (zig, disasm) — copied in per
  tree: 1,981,059,339 real bytes
  measured for one freshly
  prepared tree, and 14 trees
  held 12,255,216,359 bytes of
  real copies between them. The
  helper script exits 1 partway
  on a fresh tree (its donor
  checkout no longer holds
  several payload dirs), never
  listed the gstreamer payloads
  or vkd3d build.w32, sourced
  DXVK from a stale checkout (a
  tree assembled from it fails
  the build's manifest gate: the
  support pack it produces is
  not the pinned c4dbe35986a5),
  and overwrote the tree's
  runtime-assets manifest with
  stale 20261013 pins.
  Closing terms: the payloads
  live once in a shared store,
  verified against the project's
  committed pins before use; a
  fresh tree consumes them by
  link in one documented step
  and builds and passes the
  ordinary suites exactly as a
  tree set up the old way,
  proven side by side; the
  setup documentation no longer
  describes the per-copy ritual.
  FIXED on branch
  `lane-sharedbuild` — see
  CL-125.

- **FD-179 — Proc-address lookup
  returns different function
  addresses for a KHR spelling and
  its core spelling.** *(Low.
  Filed by the audit-follow-up
  lane, 2026-11-05, from the FD-19
  test's first run.)* In the
  64-bit guest shim, the lookup
  table's entry for a promoted
  query's KHR name carries that
  name's own alias wrapper, so
  `vkGetInstanceProcAddr` returns
  one address for
  `vkGetPhysicalDeviceProperties2KHR`
  and a different one for
  `vkGetPhysicalDeviceProperties2`
  (likewise the Features2 and
  MemoryProperties2 pairs). Both
  addresses are served and answer
  identically (proven by the FD-19
  guard test, CL-119), so no wrong
  answer has been observed; the
  gap is the pointer identity
  itself — the shim's own comment
  states an ICD must return the
  same pointer for both names, and
  a guest that compares pointers
  would see two functions where a
  real driver shows one.
  Measured: the native bridge
  test's first FD-19 run failed
  exactly on the pointer-equality
  assertion (evidence
  `test-results/audit-l1l2/runnative-3.log`
  in the lane's worktree). Closing
  terms: the lookup returns
  identical pointers for promoted
  alias pairs, proven by the
  native bridge test. Filed, not
  fixed. *(Numbered FD-179
  provisionally on this branch —
  FD-165 was claimed by
  lane-fd162-164; settles at the
  next composition.)*

- **FD-180 — A 32-bit benchmark
  guest boot can die in a
  delivered-fault loop, and the
  host's fault path recurses until
  the page's stack is
  exhausted.** *(Medium — a
  running session dies outright;
  the fault path is general, not
  benchmark-specific. Filed by the
  audit-follow-up lane, 2026-11-05,
  discharging the batch-7 merge
  record's note that the one-off
  trap should be filed on its own
  evidence if it recurs — the
  2026-11-05 audit's item L1.)*
  Measured in a bounded repetition
  series on the batch-7 build
  (wasm `c0af957a`, identical to
  the batch-7 gate build): seven
  jitbenchmix-x86 boots completed
  cleanly (four samples each,
  checksum `b8476f000fa05ee2`),
  and the eighth (JIT mode 1) died
  during the benchmark's run. The
  guest thread (pid 81, tid 86)
  faulted first at
  addr=eip=`0x10584384` (err=0),
  then wrote repeatedly at
  `0xfffffd08` with eip unchanged
  (err=2), every fault delivered
  (delivered=1) and none
  recovering: 65 BW64-FAIL
  guest-memfault32 lines, the
  instrument's printed count
  reaching 1024, all in this one
  boot (the other seven had
  none). On the host side the
  worker then died with "Uncaught
  RangeError: Maximum call stack
  size exceeded", its stack a
  repeating three-frame wasm
  cycle (functions 716, 1366,
  516) — fault delivery recursing
  per fault until the JavaScript
  stack ran out. The batch-7
  gate's standalone re-run saw one
  "memory access out of bounds"
  wasm trap on this same fixture's
  first boot; that exact signature
  did not recur in this series,
  and whether the two are one
  defect is unproven — they are
  recorded together here because
  the fixture, the class (a boot
  dying inside the emulator) and
  the fault machinery are shared.
  FD-115's wedged shipped-build
  run likewise looped on delivered
  faults; identity unproven.
  Evidence:
  `test-results/audit-l1/jitbench.json`
  and
  `test-results/suite-audit-l1/console.log`
  in the lane's worktree. Closing
  terms: the fault path stops
  delivering a non-recovering
  fault indefinitely (the guest
  thread ends as a reported
  failure and the page survives),
  and a repeated mix-fixture
  series of at least this size
  completes with no fault-loop
  boot. Filed, not fixed.
  *(Numbered FD-180 provisionally
  on this branch; settles at the
  next composition.)*

- **FD-181 — A 64-bit benchmark
  guest, spawned into a healthy
  session, can produce no output
  at all: its only thread parks
  in a socket-condition wait and
  never runs.** *(Medium-Low as
  measured — the shape is a guest
  process start stalling, which
  is product-path machinery, but
  every observation so far was
  taken under heavy shared-box
  load, which is recorded with
  each attempt and not excluded.
  Filed by the audit-follow-up
  lane, 2026-11-05, from the same
  L1 repetition series as
  FD-167.)* Three attempts at a
  jitbenchmix-x64 boot series on
  the batch-7 build (wasm
  `c0af957a`) each stopped at the
  first boot: the session booted,
  the benchmark was spawned, and
  no benchmark output appeared
  within the harness's 180 s wait
  (zero BW_JITBENCHMIX lines; no
  fault, trap or page-error lines
  either). The hang diagnostic
  shows the benchmark process
  (pid 10) with its single thread
  WAITING on
  `KUnixSocketObject::lockCond`
  in two of the three attempts
  (RUNNING, unresolved, in the
  first). Invocation 1-minute
  loads: 15.1, 19.6, 12.2. The
  same fixture passed both modes
  in the batch-7 gate's focused
  re-run, so this is intermittent,
  not deterministic. Evidence:
  `test-results/audit-l1x64*/jitbench.json`
  and
  `test-results/suite-audit-l1x64*/console.log`
  in the lane's worktree. Closing
  terms: a repeated x64 mix
  series completes on a quiet
  box, or a stall is captured
  together with the server-side
  state the thread is waiting on
  and the cause is fixed or the
  wait bounded and reported.
  Filed, not fixed. *(Numbered
  FD-181 provisionally on this
  branch; settles at the next
  composition.)*

- **FD-182 — The MSVC complex-multiply helper family is missing
  outright from the versioned C runtime DLLs: `_Cmulcc`, `_Cmulcr`,
  `_FCmulcc`, `_FCmulcr`, `_LCmulcc`, `_LCmulcr` are not exported at
  all by the shipped msvcr120 (either architecture), and the same
  family plus `_Cbuild`, `_FCbuild`, `_LCbuild` is absent from
  msvcr70/71/80/90/100/110 and from msvcrt.** *(Low. Filed by the
  FD-169 lane, 2026-11-05, from guest-proof fallout.)* These are not
  stubs — the export names do not exist in the shipped DLLs, because
  Wine's spec files for those DLLs never listed them, although the
  real Microsoft DLLs export them and the implementations already
  exist in Wine's shared msvcrt source (ucrtbase exports the whole
  family and the FD-169 guest fixtures prove it there). A program
  importing them from a versioned CRT fails to load. Found when the
  FD-169 fixture called `_Cmulcc` through a NULL pointer on
  msvcr120. Closing terms: add the spec lines (the code is already
  in the tree; mind the `_MSVCR_VER>=120` guard on the complex
  chunk for the older DLLs), rebuild through the FD-169 pipelines,
  re-pin, regenerate the census baseline (named-export counts grow;
  stub counts unchanged), and prove with the FD-169 fixtures'
  complex group resolving the exports in the versioned DLLs too.
  Filed, not fixed.

- **FD-183 — setupapi's pre-existing config-manager layer is hollow:
  roughly twenty CM_* functions that the census counts as
  implemented are semi-stubs returning success with empty results.**
  *(Medium. Filed by the FD-171 lane, 2026-11-05.)* In the shipped
  Wine 8.0 `dlls/setupapi/stubs.c` (and the Wine 10.0 equivalent),
  the non-`_Ex` config-manager functions that predate FD-171's work
  answer CR_SUCCESS without touching the device tree:
  `CM_Get_Device_ID_List{A,W}` returns an empty list,
  `CM_Get_Device_ID_List_Size{A,W}` reports the empty list's size,
  `CM_Get_Child` / `CM_Get_Child_Ex`, `CM_Locate_DevNode{A,W,ExA,ExW}`,
  `CM_Get_Device_Interface_List_Size{A,W,ExA,ExW}`,
  `CM_Create_DevNode{A,W}`, `CM_Connect_Machine{A,W}`,
  `CM_Get_DevNode_Registry_Property*`, `CM_Get_DevNode_Status*` and
  `CM_Request_Device_Eject*` have the same shape (FIXME + success or
  empty success). Any guest that enumerates devices through the
  classic (non-Ex) entry points is silently told there are no
  devices. Invisible to the export census, which only detects
  winebuild stub bodies. Found when the FD-171 lane implemented the
  `_Ex` family against the registry device model and read the
  neighbouring layer. Fix route: reimplement the classic entry
  points over the same registry model FD-171 used for the `_Ex`
  family (the `_Ex` implementations in
  `tools/buildWine/patches/wine8-fd171-contracts.patch` are the
  template: machine handle NULL == local machine), or forward them
  to the `_Ex` forms where the signatures allow.

- **FD-173 — A fresh working
  copy's first build compiles
  every file from scratch, even
  though the project's builds are
  byte-for-byte reproducible and
  an already-built copy's builds
  are incremental.** *(Medium.
  Filed by the incremental-build
  lane, 2026-11-05; provisional
  until the merge lane's audit.)*
  Measured on the base: in an
  already-built copy the standard
  build recompiles 0 files with
  no change and exactly 1 file
  after a one-file change, so the
  compiler layer was never the
  problem. The full compile is
  paid at the copy lifecycle:
  `project/emscripten/Build/` is
  ignored, every lane starts in a
  fresh worktree with no build
  directory, finished trees are
  stripped of theirs, and the one
  preparation step linked the
  generated payload inputs from
  the shared store but did
  nothing for build outputs.
  Closing terms: a seeded fresh
  copy's first build after a
  one-file change recompiles only
  that change, its program file
  is byte-identical to a
  from-scratch build of the same
  tree at the same source state,
  and the standard suites pass on
  the result. Fixed on the lane
  as CL-126.

- **FD-174 — The preparation
  step's payload links show up as
  untracked files, inflating the
  changed-file count the build
  records as provenance.**
  *(Low. Filed and fixed by the
  incremental-build lane,
  2026-11-05; provisional until
  the merge lane's audit.)*
  `.gitignore` names the
  generated payload directories
  in trailing-slash form, which
  matches real directories only;
  since the shared-store
  preparation step links those
  directories in as symlinks,
  every prepared copy showed 13
  untracked entries, and
  BUILD_INFO.json's
  git_dirty_files counted them
  (14 recorded for a tree with
  exactly one real change).
  Fixed on the lane in commit
  `1dae2e9d5`: the same paths are
  also ignored without the
  trailing slash, matching both
  forms; a prepared tree's status
  then shows only real changes,
  and the first build after the
  fix recorded git_dirty_files 2
  for a tree with exactly two
  genuine modifications.

- **FD-184 — A core CPU header defines two-letter
  macros that poison later header inclusions.**
  *(Low, tooling)* `source/emulation/cpu/common/cpu.h`
  defines `u16`/`h16`/`u8`/`h8` as macros for the
  register union's member spellings (`#define h8 byte[1]`
  and family, cpu.h:147-150). Any translation unit that
  has seen the boxedwine.h chain and afterwards includes
  headers that use those spellings as ordinary
  identifiers fails to compile: asmjit's `a64operand.h`
  declares a member function `Vec h8()`, which the macro
  rewrites into an array declaration. Demonstrated
  2026-11-05 by the lane-pch build: an early form of
  that lane's change forced the chain into asmjit's own
  `unicompiler_a64.cpp` / `unicompiler_x86.cpp` and both
  died with `error: declaration of 'byte' as array of
  functions` at cpu.h:148 (evidence:
  `test-results/pch/pch-native.log`, the first failed
  run). No translation unit in the tree fails today -
  verified: no project source includes asmjit headers
  directly, and the two library units above never see
  the chain - but the landmine is armed for any future
  include-order change, unity build, or header refactor.
  Closing terms: the four macros gone or made
  collision-proof (scoped spellings at the use sites, or
  push/pop_macro discipline around third-party includes
  that need the names), with the full native and
  browser builds and suites green, plus a compile
  fixture that includes the asmjit headers after the
  boxedwine.h chain. Filed, not fixed. Number
  provisional: sibling branches carry provisional
  filings through FD-172, and the composition audit
  settles final numbers by substance.
- **FD-185 — The Emscripten `test` target does not
  compile: six test units need the multi-threaded
  configuration the target never defines.** *(Medium,
  tooling)* `project/emscripten`'s `test` target
  compiles TEST_SOURCES - the same test units the
  native test build compiles with
  BOXEDWINE_MULTI_THREADED defined - without that
  define (its flags are `-D__TEST -DASMJIT_STATIC`).
  Six units in `source/test/process/` use members that
  exist only in the multi-threaded configuration and
  fail: testCondWait.cpp (BoxedWineCondition::lock and
  unlock, KThread::startSignal), testInotify.cpp,
  testNotDir32.cpp and testPathVerdicts32.cpp
  (KProcess::stat), testMsyncLockOrder.cpp (a lock
  constructor over the configuration's mutex type),
  testThreadLifetime.cpp (a configuration-dependent
  return statement). Measured 2026-11-05 by the
  lane-pch census: `make test -k` built everything else
  (432 objects, including every precompiled-header
  consumer) and failed on exactly these six; each of
  the six fails identically when compiled plainly with
  the target's own flags, so the breakage is
  pre-existing and unrelated to that lane's change
  (evidence: `test-results/pch/emtest-k.log`; record:
  `docs/reports/pch-build-20261105.md`). The
  `wasm32-jit-selftest` target shares the source list
  and the same missing define and is expected to fail
  identically; not separately measured. The target is
  in no standing gate, which is how it rotted
  unnoticed. Closing terms: the six units compile in
  the target - by configuration guards in the units,
  by the target defining the configuration it actually
  runs, or by the target's source list excluding units
  that cannot apply - and the target links; or the
  target is removed as dead with its references cleaned
  up. Filed, not fixed. Number provisional (see
  FD-173's note).

- **FD-186 — Compiling the
  project recompiled every
  source file from scratch, in
  every working copy, every
  time.** *(Low. Filed and fixed
  by the build-cache lane,
  2026-11-05; numbering
  provisional until the merge
  audit.)* A full browser build
  compiles 270 translation
  units and a native
  configuration ~377, and none
  of it was remembered: every
  lane's fresh working copy —
  and every repeat build — paid
  the full compile again on the
  shared 4-core box (one browser
  build measured at 1,434 s at
  the loads in the lane record).
  Closing terms: a compile whose
  inputs and flags are unchanged
  returns the remembered result,
  machine-wide across working
  copies; proven by before/after
  timings under recorded loads,
  a byte-identical product file,
  and the standard suites
  passing on the result; a first
  build with an empty cache
  still works; the setup rides
  the existing worktree
  preparation step so future
  copies get it automatically.
  FIXED on branch
  `lane-buildcache` — see
  CL-132.

- **FD-187 — statfs named one flat filesystem for every path, which
  hid every synthetic device from guest udev. The 32-bit process
  layer answered f_type 0xEF53 (EXT3) everywhere and the 64-bit
  layer the tmpfs magic everywhere. The guest's libudev verifies a
  device's sysfs path by requiring fstatfs to report SYSFS_MAGIC
  there (measured by disassembling the staged libudev: its
  set-syspath verification refuses the device otherwise), so every
  synthetic device — the gamepad included — was rejected before
  enumeration. All four entry points (32-bit
  statfs/statfs64/fstatfs, 64-bit statfs64/fstatfs64) now report
  SYSFS_MAGIC (0x62656572) for nodes under `/sys` and
  PROC_SUPER_MAGIC (0x9fa0) under `/proc`, keeping the previous
  answers elsewhere. CLOSED — see CL-133. Filed and fixed by the
  gamepad lane, 2026-11-06. Record:
  `docs/reports/fd24-gamepad-20261106.md`.

- **FD-188 — Kernel-mode Wine drivers fail to initialise in
  the emulator.** *(High)*
  Every attempt to load a kernel-mode driver in a guest
  session fails the same way: ZwLoadDriver answers c0000142
  (STATUS_DLL_INIT_FAILED) for MountMgr, nsiproxy, NDIS and
  wineusb, and a guest-side StartService for the WineBus
  driver returns error 1114 (ERROR_DLL_INIT_FAILED). The
  failures appear in preserved session logs from unmodified
  builds (wt-fd169's crt32/crt64 suite consoles; the
  integration tree's dxvk test-results), so the condition
  predates and is independent of any one lane's changes.
  Consequence: no PlugPlay device class can appear in a
  guest — HID devices (game controllers, FD-24) included —
  because the bus driver that would create them cannot
  start. The PlugPlay service itself runs (32-bit prefix:
  state 4) and the bus device and service are registered
  there; the driver simply never initialises. Closing
  terms: a guest session in which the standard drivers
  initialise (or the failing component is identified and
  its init path repaired), proven by a guest probe that
  starts the WineBus service successfully and by the FD-24
  witness then seeing the controller. Filed by the gamepad
  lane, 2026-11-06, from the measurements in
  `docs/reports/fd24-gamepad-20261106.md`.
  Seen on the 64-bit side too, once FD-189 registers the services:
  the FD-189 lane's green64 guest console shows `ZwLoadDriver
  failed to create driver …\Services\winebus: c0000142` /
  `Auto-start service L"winebus" failed to start: 1114` (same for
  wineusb), while the bus devices do enumerate (`ROOT\WINE\WINEBUS`
  + `ROOT\WINE\WINEUSB`). Evidence
  `test-results/fd189/green64-fd189-lines.log` on `lane-fd189`.
  Batch-10 composition (2026-11-06): the driver-load layer of
  this entry is FIXED — see CL-137 (fix commit `7b0c2a50b` on
  `lane-fd188`, composed in this batch). The entry remains OPEN
  on the second half of its closing terms: the FD-24 witness
  does not yet see the controller, because the guest enumerates
  no HID device at all — that remainder is filed as FD-195.
- **FD-189 — The shipped 64-bit guest prefix has no
  PlugPlay or bus-driver registration. FIXED 2026-11-06 — see CL-135
  (provisional; settles at the next composition).** *(Was Medium)*
  In a 64-bit guest session the PlugPlay, WineBus and
  WineUsb services do not exist (a guest probe's OpenService
  answers error 1060 for all three) and the root device
  enumeration is empty. The shipped `prefix64.zip`'s
  system.reg contains no PlugPlay or WineBus entries at
  all; the 32-bit `prefix32.zip` carries them (its guest
  probe shows PlugPlay running and both bus services
  registered, stopped). So on the 64-bit side the
  plug-and-play stack is absent before any driver question
  arises (FD-173 is the separate driver-initialisation
  defect that also blocks the 32-bit side). Closing terms:
  the 64-bit prefix (or its in-session initialisation)
  registers the PlugPlay service and the ROOT\\WINE bus
  devices the way the 32-bit one does, proven by the same
  guest probe reporting the services present. Filed by the
  gamepad lane, 2026-11-06; evidence
  `docs/reports/fd24-gamepad-20261106.md`.

- **FD-190 — The 32-bit `setupapi.dll` still ships
  `SetupDiGetDeviceInterfaceAlias` as an unimplemented stub, and the
  32-bit guest fixture never tests it.** *(Medium)*
  The 64-bit side is real: FD-167 implemented the alias function
  there and the 64-bit fixture's alias checks pass. On the 32-bit
  side the function is still a validated stub (census baseline:
  `x86/setupapi.dll` carries 242 stubs and
  `SetupDiGetDeviceInterfaceAlias` is among them, while the 64-bit
  table no longer lists it), and the 32-bit guest fixture
  `fd167-contracts32` contains no alias checks at all, so the
  32-bit fixture passed without ever exercising this path — the
  32-bit side was never validated. Closing terms: implement the
  32-bit alias function with the same genuine behaviour as the
  64-bit one and add alias checks to the 32-bit fixture, proven by
  the fixture passing with the checks. Found by the fd167-64fix
  lane during the batch-9 gate and verified by the coordinator
  against the census baseline, 2026-11-06; evidence
  `docs/reports/fd167-64fix-20261106.md`.

- **FD-191 — `CM_Locate_DevNodeW` is an unimplemented stub that
  returns failure unconditionally, and the stub census never
  covered it.** *(Medium)* The function is exported by the shipped
  `setupapi.dll` (its name is present in the shipped binary), but
  Wine 8.0 implements it in `dlls/setupapi/stubs.c` as a FIXME stub
  returning `CR_FAILURE` unconditionally, so any guest program that
  locates a device node through it always fails. The stub census
  never listed it (it is not a spec stub, so it appears in neither
  the 64-bit nor the 32-bit `setupapi.dll` stub lists), which is why
  FD-171 missed it. The FD-171 guest fixture documents the gap in
  its own source and works around it by obtaining the device handle
  through the working SetupDi path, so the fixture passes without
  ever exercising the stub. Closing terms: implement the function
  (upstream's cfgmgr32 device-node tree backported) or prove the
  stub claim wrong, proven by a guest call returning a real device
  node. Found by the fd171-contracts-fix lane during the batch-9
  gate and verified by the coordinator (export present in the
  shipped DLL; absent from both census stub lists; fixture
  workaround documented in `tools/guest-fixtures/fd171-contracts.c`),
  2026-11-06.

- **FD-192 — The 32-bit `d2d1.dll` still crashes on
  `D2D1CreateDeviceContext` with a null surface instead of
  returning "invalid argument".** *(Medium)* The 64-bit side was
  fixed in the batch-9 gate (the Wine patch now returns
  `E_INVALIDARG` when the surface is null, proven by the 64-bit
  fixture's `D2D1CreateDeviceContext(NULL)` check returning
  `hr=80070057`). The 32-bit Wine 10 `d2d1.dll` never received the
  check: the 32-bit guest fixture crashes deterministically at
  that call (`BW64-FAIL guest-memfault32 pid=111 tid=112 addr=0x0
  eip=0x78692441`, Wine unhandled page fault on read access to
  `00000000`), reproduced byte-identically on re-run, after passing
  every earlier group — so the 32-bit side is one missing fix, not
  a broken fixture or boot. Closing terms: port the null-surface
  check to the 32-bit `d2d1.dll`, proven by `fd171-contracts32`
  passing with the `D2D1CreateDeviceContext(NULL)` check green.
  Found by the batch-9 final gate, 2026-11-06; evidence the gate
  logs `test-results/merge-batch9-20261106/browser-fixtures-contracts-final.log`
  and `fd171-contracts32-final-rerun1.log`.
- **FD-194 — Concurrent checking-queue submissions
  corrupt each other's runs.** *(Medium)* The checking
  queue's executor mounts one shared source tree
  (`winbox-project/checks/src`), and every `submit-check`
  run re-prepares that mount to its own lane's commit
  before creating its pipeline. When submissions overlap,
  whichever submission prepared the mount last is what
  every still-queued pipeline actually mounts — so their
  verify-source step fails (expected one commit, mounted
  another) and the pipelines die with no check results at
  all. Measured 2026-11-06/07 during the parallel-lane
  wave: pipelines 18, 19, 20 and 21 were submitted for
  four different commits and all four mounted a fifth
  lane's commit (`81bd25c49`) and failed at verify-source;
  only pipeline 17, whose submission did not overlap,
  verified. The failure is fail-closed — no lane received
  a wrong pass; every affected pipeline failed loudly —
  but the queue is unusable whenever lanes submit
  concurrently, and each new submission can void runs
  already queued. It needs an owner: serialize
  submissions (a lock in `submit-check`), or give each
  pipeline its own mount. Filed by `lane-vkhost-gen`,
  whose FD-28 run was pipeline 18. Evidence:
  `checks/evidence/pipeline-18/` (and 19–21) on the box;
  record: `docs/reports/fd28-vkhost-generator-20261106.md`.
  *(Lane-local number: FD-193 is claimed by three
  sibling branches for unrelated findings; the settled
  numbering belongs to the next composition audit.)*

- **FD-193 — `BW64_BLOCKSTATS` prints garbage.** *(Low —
  diagnostic only. Filed by lane-fd176, 2026-11-06.)* The
  `tryBlockStep` atexit reporter keeps a `static CPU64*` to the
  first CPU that allocates a block table, but every test context
  owns a new CPU that is destroyed at the end of its test — by
  process exit the pointer dangles, and the printed counters are
  nonsense (measured: `built=281474198778416 ...
  coverage=5120879205109700.0%`). It also registers once per CPU,
  so the line repeats. No effect on any gate (the flag is off by
  default and was only set during diagnosis); the BLOCKSTATS
  numbers must not be used as proof of block execution. Closing
  terms: the reporter reads live counters (or is removed), proven
  by a run with the flag set printing sane per-process totals.
  Found while proving the FD-176 environment fix; record:
  `docs/reports/fd176-invalidate-gate-link-20261106.md`.

- **FD-194 — The 64-bit PlugPlay service is registered but fails
  to start: services.exe reports error 1053.** *(Medium)* The
  fd189-services64 guest probe on the FD-189 build proves the
  service is present (state=1) and services.exe attempts to
  auto-start it, but the console records `service L"PlugPlay"
  failed to start` / `Auto-start service L"PlugPlay" failed to
  start: 1053`, and plugplay.exe ran and exited with status 0
  without reporting SERVICE_RUNNING. On the 32-bit prefix the same
  service reaches SERVICE_RUNNING (state 4). This is distinct from
  FD-188 (a user-mode service process failing its start handshake,
  not a kernel driver failing to load). Closing terms: a 64-bit
  guest session in which the PlugPlay service reaches
  SERVICE_RUNNING, proven by the fd189-services64 probe reporting
  `PlugPlay present state=4`. Found by the FD-189 lane, 2026-11-07;
  evidence `test-results/fd189/green64-fd189-lines.log` and the
  green64 guest console.

- **FD-195 — The guest enumerates no HID device at all with a simulated gamepad installed.** *(High)*
  With a simulated gamepad installed and the FD-188 driver-load fix in place, the guest's SetupDi HID enumeration finds 0 HID devices. The WineBus service runs (state 4), the root WINEBUS device exists, the five driver .inf files are present in `C:\windows\inf\`, and setupapi reads `winehid.inf` and enumerates compatible drivers for the one PnP-enumerated WINEBUS device (VID_845E&PID_0001). The gamepad device (VID_045E&PID_028E) is never enumerated as a device at all, and only one of the two kernel-created WINEBUS child devices is PnP-enumerated. So winebus's Unix-side device discovery does not surface the gamepad, and the function-driver install for the one enumerated HID PDO does not complete to a started HID device. Lead: a single `BW64-FAIL netlink-recvfrom flags=0x22 outcome=returned-EINVAL` appears in the witness session — libudev's monitor uses netlink for hotplug events, and if recvfrom fails winebus never learns about devices that appear after its initial scan. Closing terms: the FD-24 witness sees the controller (XInput slot=0 CONNECTED + DINPUT devices=1). Found by the FD-188 lane, 2026-11-07; evidence `test-results/fd188/hid-enum-run2.log` and `witness-run2.log`; record `docs/reports/fd188-drivers-initialise-20261107.md`.

- **FD-196 — FD-188's tightened prefix/runtime validation left
  the packaging test fixtures behind: `test_prepare_prefixes.py`
  fails 3 of 11 on the lane's own tip.** *(Low — test fixtures
  only. Filed by the batch-10 merge lane, 2026-11-06.)* FD-188's
  fix added two requirements to
  `tools/rootfs64/prepare_prefixes.py` (the 32-bit prefix must
  carry the five driver .inf files in `C:\windows\inf\`; every
  .sys in a wine runtime needs its `drivers/` mirror), but the
  synthetic fixtures in `tools/rootfs64/test_prepare_prefixes.py`
  were not updated, so that file's test suite errors 3 of 11 on
  the FD-188 branch tip while passing on the base tip and on
  `lane-fd189`. No project check runs this file automatically
  (only the tools/rootfs64 README names it), so no lane gate saw
  it. FIXED in the batch-10 composition — see CL-138: the
  fixtures now carry the required members; the validators are
  unchanged.

- **FD-197 — The checking queue's shared source mount races
  under concurrent submissions: queued pipelines mount another
  lane's commit and fail verify-source, producing no check
  verdicts.** *(Medium — checks infrastructure. Filed by the
  batch-10 merge lane, 2026-11-07.)* Every submission through
  `checks/submit-check` re-prepares the one shared `checks/src`
  mount, so two submissions in flight at once overwrite each
  other's queued jobs: the later preparation wins and the
  earlier pipeline's verify-source step fails against the wrong
  commit — fail-closed, no wrong verdict is ever produced, but
  no verdict at all. Measured casualties in the 2026-11-06/07
  submission flood: pipelines 18, 19, 20 and 21 all failed
  verify-source this way (pipeline 21 was this batch's own
  composition run: it expected the composed tip `c8bd65a4f`
  and found `81bd25c49` mounted); the race struck again
  mid-run at least once (a pipeline whose verify-source had
  passed on its exact tip then had the shared tree replaced
  under its workspace step, leaving every check log empty);
  further submissions the same night (pipelines 25, 29 and
  31 among them) failed the same way, and every lane that
  re-submitted once submissions were serialised (pipelines
  22, 26 and 30) verified and ran fully. Fix direction,
  recorded not prescribed: serialise submissions, or give
  each pipeline its own source mount. Closing terms: two
  submissions issued concurrently both verify-source against
  their own commits and run to verdicts. Evidence: the
  pipelines' own verify-source logs under
  `checks/evidence/pipeline-*/queue-logs/`.

- **FD-198 — The known-gaps audit's allowlist covered only
  one of the gate script's two documentary mentions of the
  word "stubs", so the audit failed on the composed batch-10
  tree.** *(Low — checker bookkeeping. Filed by the batch-10
  merge lane, 2026-11-07.)* `scripts/build-jit-wasm.sh`
  carries two documentary comments: the older one noting the
  block-invalidation link uses ripSampler's designed inert
  stubs, and the one FD-176's fix added recording that the
  harness stubs which once stood in for the unlinked kernel
  units are removed. The allowlist entry for that file
  counted one mention, so the known-gaps audit counted two
  against an allowance of one and failed the batch-10 gate's
  unit run — a bookkeeping defect in the checker, not a gap
  in the product. FIXED in the batch-10 composition — see
  CL-139: the count is 2 and the reason covers both mentions.

- **FD-199 — FD-188's re-cut `wine64.zip` flattened
  `usr/lib/wine/wineserver` from a symlink into a regular
  file holding the link target's text.** *(Medium — shipped
  artifact. Filed by the batch-10 merge lane, 2026-11-07.)*
  The archive FD-188's restage produced (pin `1b725073…`)
  carried the wineserver entry as a regular file (mode
  0o600) whose content was the link target path, where the
  base archive carries a symlink (mode 0o120755). The
  packaging-prefix unit test caught it in the batch-10 gate:
  shipped Wine archive entries must be executable ELF files,
  and a 26-byte text file is not one. FIXED in the batch-10
  composition — see CL-140: the entry's type bits were
  restored at the byte level and the retained archive
  re-cut around the repaired zip.

- **FD-200 — The archive producer path that re-cut
  `wine64.zip` for FD-188 drops symlink type bits, so any
  future re-cut through the same path flattens links the
  same way.** *(Medium — tooling. Filed by the batch-10 merge
  lane, 2026-11-07.)* FD-199's flattened entry was repaired
  at the byte level for this batch, but the path that
  produced the defective archive is unchanged: whatever
  staging/re-zip step FD-188's restage used wrote the
  wineserver entry back as a regular file instead of
  preserving its symlink type. Until the producer preserves
  entry types (or a post-cut check rejects a shipped archive
  whose entries lost them), the next re-cut can silently
  reintroduce FD-199 — the packaging test would catch it at
  gate time, but the artifact pipeline should not be able to
  produce it. Closing terms: a re-cut of the 64-bit runtime
  archive through the producer path yields an archive whose
  symlink entries survive as symlinks, proven by a
  central-directory comparison against the pre-cut archive
  showing entry types preserved. Found by the batch-10
  merge lane, 2026-11-07, from FD-199's byte-level repair
  evidence.

- **FD-196 — Browser probes starve behind a build queue:
  no probe can start while any build is queued, so probe
  slot counts do not bound browser-test throughput on a
  busy box.** *(Medium)* Two serial build gates stand
  before any browser starts: `acquireProbeSlot` refuses
  acquisition while a `build-pending` file names a live
  pid, and `tools/bw64-build.sh` claims that file when a
  build starts *waiting* — before it holds the build
  lock — removing it only at its own exit, so a relay of
  queued builds holds the claim continuously; a probe
  that does acquire then waits again in setup for the
  build lock itself (`waitForBuildIdle`, 600 s default).
  Measured 2026-11-07 on the 20-lane wave (project work
  on cores 0–2, a Minecraft server on core 3): the claim
  was held without a usable gap from 04:24Z to past
  06:11Z across at least six builds (the DNS lane's, the
  batch-10 merge build, an FD-26 JIT build, two FD-194
  builds, and others), and three suite boots
  (boot64-smoke twice, boot32-vkboot once) each failed
  at the 900 s slot wait — 901729 ms, 900490 ms,
  901735 ms — *without testing anything*, while other
  lanes' probes queued behind the same relay. One probe
  did slip through a handoff gap, acquired, passed the
  build-idle wait, and booted the guest, proving the
  gates pass when a gap aligns. Net effect: on a
  build-saturated box, browser testing is serial-to-zero
  no matter how many probe slots exist; raising slot
  defaults cannot parallelize anything until probe
  starts are possible during a build queue (for
  example, claiming `build-pending` only while holding
  the build lock, or reserving a bounded probe window).
  Slot defaults stay heavy 2 / light 2 — the 2×
  verification in `web/tests/parallel-probe-README.md`
  remains the highest proven probe concurrency, and the
  3× measurement under the current core split is still
  owed (apparatus preserved with the record). Found by
  the probe-parallel lane, 2026-11-07; evidence
  `docs/reports/probe-parallel-20261107.md` and
  `test-results/probepar/` on branch
  `lane-probe-parallel`. (Number lane-local; the next
  composition audit settles it.)
- **FD-201 — The D22 JVM-ergonomics probe's settings run
  waits for a heap-size label the bundled JDK never prints,
  so a run that captured the value reports a timeout.**
  *(Low — test tooling. Filed by the D22 lane, 2026-11-07.)*
  The probe (`web/tests/d22-jvm-ergonomics.mjs`) gated its
  `-XshowSettings:all` run on the literal `Maximum heap size`
  and the `openjdk version` line. The Mindustry archive's
  bundled JDK 25 prints the value as `Max. Heap Size
  (Estimated):` inside its VM settings section, and prints
  the version line only after every settings section; the
  sections stream slowly enough through the guest console
  that the trailing sections had not printed when the run's
  900 s deadline expired — while the heap line itself had
  been in the stream since guest time 390.0 s
  (`test-results/d22-mindustry-20261106/console-settings.log`
  line 5956). The run therefore recorded a false timeout
  (markerHit=false) over a successful capture. FIXED on
  `lane-d22-mindustry` by `13d57b2a2`: the matcher keys on
  the VM settings section header and the heap-size label
  the JDK actually prints; both corrected patterns match
  the preserved settings stream exactly once each. The
  closure number settles when the batch composes. Record:
  `docs/reports/d22-mindustry-ergonomics-20261107.md`.


- **FD-193 — `BW64_BLOCKSTATS` prints garbage. CLOSED — see
  CL-135.** *(Low — diagnostic only. Filed by lane-fd176,
  2026-11-06; fixed by lane-fd193, 2026-11-07.)* The
  `tryBlockStep` atexit reporter kept a `static CPU64*` to the
  first CPU that allocated a block table, but every test context
  owns a new CPU that is destroyed at the end of its test — by
  process exit the pointer dangles, and the printed counters are
  nonsense (measured on the integration tip by lane-fd193: four
  identical lines, `built=281473917630304 ...
  coverage=5070359463874500.0%`). It also registered once per
  CPU, so the line repeats. No effect on any gate (the flag is
  off by default and was only set during diagnosis); the
  BLOCKSTATS numbers must not be used as proof of block
  execution. Closing terms were: the reporter reads live
  counters (or is removed), proven by a run with the flag set
  printing sane per-process totals. Fix: a CPU that allocates a
  block table while the flag is set joins a process-wide
  registry; on destruction it folds its counters into
  process-wide totals and leaves the registry; the reporter
  registers exactly once and prints the folded totals plus the
  counters of any CPUs still alive at exit. Proof: the
  block-invalidation harness (four CPU contexts) run with the
  flag set prints a single line, identical across runs —
  `built=7 execs=7 insnsInBlocks=13 totalInsns=14
  coverage=92.9%` — and totalInsns matches the harness's own
  independent total exactly (its contexts execute 4+4+2+4
  instructions; the one non-block instruction is the
  self-modifying test's tail, re-decoded outside the cached
  block by design). Found while proving the FD-176 environment
  fix; fixed on `lane-fd193` (commit `f5fac0447`); record:
  `docs/reports/fd193-blockstats-reporter-20261107.md`;
  evidence: `test-results/fd193/`.

- **FD-193 — Twelve `getRamPtr` call sites in the X11 bridge
  dereference the result for guest-controlled structs without
  checking the refusal case, so a page-straddling, unmapped or
  read-only guest struct crashes the host.** *(High — filed
  and FIXED on this branch, see CL-137.)* `KMemory::getRamPtr`
  returns nullptr when the page is unmapped or not writable
  or the span crosses a page boundary. An audit of all 19
  call sites in `source/x11` (and 0 in `source/x11wire`)
  found 12 that pass guest-controlled addresses (in two
  cases also a guest-controlled element count) and
  dereference the result unchecked:
  `XSetWindowAttributes::get` — which also handed the null
  to `ChangeWindowAttributes`, whose `setAttributes` call
  dereferences it — `x11_FreeModifierMap` (no null-argument
  guard either), `x11_XRRFreeCrtcInfo`,
  `x11_XRRFreeOutputInfo`, `x11_XRRFreeScreenResources`
  (the struct and its modes array), `x11_XRRGetCrtcInfo`
  (the struct and its modes array), `x11_XRRGetOutputInfo`,
  `x11_XRRSetCrtcConfig` (the struct and `modes[modeId-1]`),
  and `x11_XRRFreeProviderResources`. The remaining 7 sites
  pass emulator-allocated addresses (fresh `KProcess::alloc`
  results) whose spans cannot straddle or be unmapped, and
  are recorded in the Appendix. Guest struct fields are now
  read through the checked accessor (`readd` at
  `offsetof`), which takes the guest-fault path for a bad
  pointer like the rest of the file; `get()` falls back to
  its checked read when `getRamPtr` refuses; the
  null-attributes case in `ChangeWindowAttributes` returns
  BadValue (Success for a zero mask). Proof: four new native
  tests drive the real int 0x99 handlers with
  page-straddling structs in a forked child — on the
  unfixed code all four fail (the emulator logs the
  `getRamPtr` refusal at the exact straddle addresses,
  `addr=0x10000FF8 len=48` and `addr=0x10000FFC len=8`, and
  the handler children wedge on the null dereference and
  are killed; `get()` returned the null instead of falling
  back), and on the fixed code all four pass. Fix commit
  `cd030f389` on base `faf95d26c`. Record:
  `docs/reports/x11-getramptr-audit-20261106.md`; evidence
  `test-results/x11-getramptr-audit/` (`red-601..604.log`,
  `green-601..604.log`). Found by the
  x11-getramptr-audit lane, 2026-11-06. *(Lane-local
  numbers: FD-193 and CL-137; both settle at the next
  composition.)*

- **FD-193 — REP LODS executed only one iteration in the
  64-bit interpreter.** *(Low; lane-local number from
  `lane-cpudiff`, to be renumbered at composition)* The LODS
  handler (dsp_62) loaded a single element regardless of the
  REP prefix, under a comment asserting REP has no effect on
  LODS; hardware repeats the load until the count register
  reaches zero, leaving the last element in the accumulator.
  Found 2026-11-07 by the new REP-width string cases of the
  CPU differential harness (native RCX 3→0 and last-element
  accumulator vs emulator RCX unchanged and first element,
  at all four operand widths). Fixed on `lane-cpudiff` in
  commit `35889ff29`: the handler now repeats with the same
  partial-progress fault protocol as MOVS/CMPS. Proof:
  `test-results/cpu-differential/` — full suite 7,920/7,920
  cases pass including `lods rep-{b,w,d,q}`. Closing terms:
  composition takes the fix and the harness stays green.

- **FD-195 — The section-14.3 block-invalidation gate does
  not build at the integration tip: its fixed source list
  no longer links.** *(Medium)* `scripts/build-jit-wasm.sh`
  compiles the invalidation differential from a fixed file
  list (cpu64.cpp, jit64.cpp, jit64wasm.cpp,
  common_lock.cpp, ripSampler.cpp, kmemory64.cpp,
  bstring.cpp, fpu + softfloat + the test). At `faf95d26c`
  the link fails with undefined references to
  `KSystem::guestCpuCount()` (defined in
  `source/kernel/ksystem.cpp`),
  `FsOpenNode::noteMappedPagesReleased(unsigned int)`
  (defined in `source/io/fsopennode.cpp`) and
  `bwCurrentThreadRegistryLookup()` (defined in
  `source/kernel/kthread.cpp`) - none of the three files
  is in the list. Verified baseline-identical: a pristine
  checkout of the tip fails the same way (phase-1 873/0 and
  the generated-emitter check pass first), so the gate has
  been un-runnable since whichever change added the
  references without updating the list; the FD-176
  invalidation proof (54/0, byte-identical logs) predates
  the breakage. Effect: the script exits there, so a plain
  gate run also skips the 314-test emitted-code fixture
  stage behind it. Closing terms: the gate builds and the
  invalidation differential runs green at the tip again
  (fix the list, or move the three definitions onto files
  the list already carries). Found by lane `lane-fd26`,
  2026-11-07; evidence
  `test-results/fd26/inv_build_interp.log` (pristine-tip
  run) and the lane record. *(Lane-local number; settles
  at the next composition audit.)*

- **FD-196 — A guest double printed with `%.3f` comes out
  as ~1e66 garbage after the first print in a process, in
  both engines.** *(Low)* The sseprobe64 bench fixture
  originally computed its millisecond figure as a double
  (`(double)(end-start) * 1000.0 / (double)freq`) and
  printed it with `%.3f`. On the integration tip's build,
  the first sample in each process printed sane (504 ms,
  530 ms) and every later sample printed ~1e66-1e68
  garbage - interpreter and fast-engine sessions alike -
  while the same fixture's integer output and its
  double-sweep checksum stayed stable and correct. Which
  step is wrong (the int64-to-double conversion, the
  multiply/divide, or the C library's `%f` path, and
  whether the trigger is the second print or the packed
  logic loop's code) was not isolated. The fixture now
  reports integer milliseconds plus the raw tick delta.
  Closing terms: a minimal fixture isolates the failing
  step and the step is fixed or proven correct. Found by
  lane `lane-fd26`, 2026-11-07; evidence
  `test-results/fd26/console-fp-garbage-partial.log` (the
  killed first bench run's console: first sample sane,
  everything after garbage). *(Lane-local number; settles
  at the next composition audit.)*

- **FD-193 — On the 64-bit side `clock_gettime` ignored the
  clock id entirely.** *(Medium)* Every clock — including
  invalid ids 10, 12, −1 and 99 — returned wall-clock time with
  a success answer, so a guest asking for a monotonic or CPU
  clock silently received realtime, and nonsense ids were
  indistinguishable from real ones. The host's raw answers:
  ids 0–9 and 11 are valid, each in its own domain (realtime
  family, monotonic family, process CPU time, thread CPU
  time); anything else answers −EINVAL ahead of the result
  pointer check. Fixed on branch `lane-timing-waits`:
  `sys_clock_gettime64` validates through the shared
  `KSystem::isSupportedClockId` and reports per-clock domains
  mirroring the 32-bit side. Proof: guest probe
  `guestprobeC-run1` rows (clk10/12/−1/99 → −EINVAL, clk99
  with a null result pointer → −EINVAL, null pointer with a
  valid clock → −EFAULT, clocks 2/3/7/8/9/11 → 0, monotonic
  below realtime) against `hostref-timing.txt`; suite test
  `testTimingWaits64`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-194 — `clock_getres` answered for any clock, claimed
  the wrong resolution, and wrote through a null pointer.**
  *(Medium)* All three implementations (32-bit, 32-bit
  time64, 64-bit) returned success for invalid ids such as
  99; the 32-bit pair reported 1 ms for every clock and the
  64-bit case 1 ns for every clock; a null result pointer
  was written through. The host's raw answers: an invalid
  id answers −EINVAL; a null pointer is accepted and
  nothing is written; fine clocks report 1 ns and the
  coarse clocks (5, 6) report 1,000,000 ns. Fixed on branch
  `lane-timing-waits` on all three implementations. Proof:
  guest probe `guestprobeC-run1` clock_getres rows; suite
  tests `testTimingWaits32` / `testTimingWaits64`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-195 — An interrupted sleep never wrote its remaining
  time.** *(Medium)* `nanosleep`'s second argument and
  `clock_nanosleep`'s fourth are where the host reports how
  much of the sleep is left when a signal interrupts it;
  guests (and libc retry loops) rely on it. On the 64-bit
  side the dispatcher did not even pass the address; on the
  32-bit side `KThread::clockNanoSleep` ignored it and
  `KThread::nanoSleep` had no concept of it. An interrupted
  sleep answered −EINTR with the caller's buffer untouched.
  Fixed on branch `lane-timing-waits`: the wrappers compute
  the remainder from the requested duration minus the
  elapsed time of the call and write it on −EINTR, for
  relative sleeps only (the host writes nothing for
  TIMER_ABSTIME sleeps). Proof: the mechanism is in the fix
  commits; the interrupt-driven shapes cannot be driven
  synchronously in the native harness (a guest handler is
  never called — FD-213), so the regression vehicle is the
  guest probe family in `test-results/timing-waits/`, and
  the limitation is recorded in
  `docs/reports/timing-waits-20261107.md`.

- **FD-196 — 32-bit `clock_nanosleep` refused TIMER_ABSTIME
  outright and validated nothing.** *(Medium)*
  `KThread::clockNanoSleep` answered −EINVAL for ANY
  nonzero flags, so the valid TIMER_ABSTIME bit made every
  absolute sleep fail; the wrappers read the request fields
  before any check, so a null request read guest address 0
  and an out-of-range nanosecond field was silently
  accepted. Fixed on branch `lane-timing-waits`: the
  wrappers answer −EFAULT for a null request and −EINVAL
  for out-of-range fields, convert TIMER_ABSTIME against
  the same clock source `clock_gettime` reports, answer
  −EOPNOTSUPP for CLOCK_THREAD_CPUTIME_ID, and route
  through the interruptible sleep path. Proof: suite test
  `testTimingWaits32` (including an absolute monotonic
  sleep that elapses), guest probe rows in
  `guestprobe-run*`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-197 — 64-bit `clock_nanosleep` refused flag bits the
  host ignores, refused the process CPU clock, and used a
  different monotonic base than `clock_gettime`.**
  *(Medium)* Any flags value with bits outside TIMER_ABSTIME
  answered −EINVAL (the host, strace-verified, ignores the
  other bits); clock 2 answered −EINVAL (the host accepts
  it); clock 3 answered −EINVAL (the host answers
  −EOPNOTSUPP); and the TIMER_ABSTIME conversion used
  `getMilliesSinceStart` while `clock_gettime` reported
  `getMicroCounter`, so a deadline and a clock reading did
  not share a domain. Fixed on branch `lane-timing-waits`.
  Proof: guest probe `guestprobeC-run1` clock_nanosleep
  rows (clk3 → −95, clk2 → 0, flags 2/−1 → 0, absolute
  monotonic and realtime sleeps elapsing ~40 ms against a
  40 ms target); host rows in `hostref-timing.txt` and
  `hostref-timing3.txt`; suite test `testTimingWaits64`.
  Record: `docs/reports/timing-waits-20261107.md`.

- **FD-198 — `getrusage` accepted any `who`, accepted a
  null usage pointer, and (64-bit) reported zeros.**
  *(Medium)* The host validates `who` first (0, −1, 1
  valid; anything else −EINVAL) and then faults a null
  usage pointer. The 64-bit case zero-filled the structure
  and answered 0 for every input. The 32-bit side treated
  any `who` ≥ 1 as RUSAGE_THREAD and any negative value as
  RUSAGE_CHILDREN, and wrote through a null pointer. Fixed
  on branch `lane-timing-waits`: both widths validate in
  the host's order and report real summed thread CPU times
  (new shared helper `KProcess::cpuTimesMicroSeconds`).
  Proof: guest probe `guestprobeC-run1` getrusage rows;
  suite tests on both widths. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-199 — Interval timers: negative seconds were
  clamped instead of refused, the 32-bit side validated
  nothing, and `getitimer` checked its pointer before
  `which`.** *(Medium)* On the 64-bit side a setitimer
  value of −1 seconds was read as unsigned and clamped to
  ~49 days; the 32-bit `KProcess::setitimer` applied the
  same clamp with no field validation at all; and
  `getitimer` answered −EFAULT for a null result pointer
  even when `which` itself was invalid, where the host
  answers −EINVAL (the `which` check precedes the pointer
  check). Fixed on branch `lane-timing-waits` on both
  widths. Proof: guest probe `guestprobeC-run1` itimer
  rows (setitimer sec −1 → −EINVAL, getitimer(3, NULL) →
  −EINVAL, getitimer(0, NULL) → −EFAULT); suite tests on
  both widths. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-200 — `wait4` answered −ECHILD for unknown option
  bits instead of −EINVAL.** *(Medium)* The host validates
  the options word ahead of the child lookup: bits outside
  WNOHANG|WUNTRACED|WCONTINUED and the __W* clone-scope
  bits answer −EINVAL even when there are no children.
  Both widths answered −ECHILD. Fixed on branch
  `lane-timing-waits` at `KSystem::waitpid` (32-bit) and
  at the 64-bit wait4 case. Proof: guest probe
  `guestprobeC-run1` wait4 rows; suite test
  `testTimingWaits64`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-201 — The 64-bit side had no `alarm` syscall.**
  *(Low)* `alarm` shares the ITIMER_REAL slot with
  setitimer on the host; the 32-bit side implements it
  (`KProcess::alarm`) but the 64-bit dispatch table had no
  case and answered −ENOSYS. Fixed on branch
  `lane-timing-waits` by dispatching to the existing
  implementation. Proof: guest probe `guestprobeC-run1`
  (`alarm0` → 0); suite test `testTimingWaits64`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-202 — 64-bit `times` returned ticks at 1000× the
  real rate.** *(Low)* The return value was
  microseconds/10000 — 100,000 ticks per second — where
  the host's clock tick (USER_HZ) is 100 per second, so a
  guest dividing by CLK_TCK read durations 1000× too
  large. Fixed on branch `lane-timing-waits`
  (milliseconds/10 since start). Proof: guest probe
  `guestprobeC-run1` times rows; suite test
  `testTimingWaits64`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-203 — 32-bit `gettimeofday` wrote through a null
  time pointer.** *(Low)* The host accepts (NULL, NULL)
  and writes nothing; the 32-bit side wrote the timeval
  unconditionally, i.e. at guest address 0 when the caller
  passed null. The 64-bit case was already correct. Fixed
  on branch `lane-timing-waits`. Proof: suite test
  `testTimingWaits32`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-204 — `epoll_create` size and `epoll_create1` flags
  were not validated on either width.** *(Medium)* The
  host answers −EINVAL for a legacy create size ≤ 0 and
  for create1 flags outside EPOLL_CLOEXEC|EPOLL_NONBLOCK;
  the emulator returned a descriptor for size 0, size −5
  and flags 1 alike. Fixed on branch `lane-timing-waits`
  in both dispatchers' wrappers/cases. Proof: guest probe
  `guestprobeD-run2` rows (flags 1 → −EINVAL, flags
  0x10000 → −EINVAL, size 0/−5 → −EINVAL, valid forms →
  descriptors); suite tests on both widths. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-205 — 64-bit `epoll_wait` accepted maxevents < 1
  and returned events anyway.** *(Medium)* The host
  answers −EINVAL for maxevents 0 or negative, even with
  an event ready; the 64-bit case clamped the value
  (0 → proceeded, −1 → 64) and returned the ready event.
  The pwait/pwait2 body already validated. Fixed on branch
  `lane-timing-waits`. Proof: guest probe
  `guestprobeD-run2` rows (maxevents 0/−1 with data ready
  → −EINVAL, maxevents 4 → 1 event). Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-206 — `poll` set POLLNVAL in revents but did not
  count it, so polling a closed descriptor answered 0.**
  *(Medium)* The host answers 1 (the return counts every
  descriptor with nonzero revents). In
  `internal_poll_impl` the tally sat inside the
  descriptor-exists branch, while POLLNVAL is set exactly
  when the descriptor no longer exists. Fixed on branch
  `lane-timing-waits` by counting every entry with nonzero
  revents. Proof: guest probe `guestprobeC-run1` (`poll
  closedfd` → ret 1, revents 0x20; pre-fix ret 0). Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-207 — A negative or out-of-range timespec parked
  `pselect6` and `ppoll` instead of answering −EINVAL.**
  *(High)* Both 64-bit cases read the timespec fields as
  unsigned, so {−1, 0} became an effectively infinite wait
  and a guest passing a bad timeout hung the session
  instead of getting an error. ppoll also lacked the
  timespec permission check its siblings have, and
  pselect6 never validated its sigmask argument's size or
  pointer (the host copies the mask in: size ≠ 8 answers
  −EINVAL, an unreadable mask −EFAULT; the mask's
  non-application itself is FD-103). Fixed on branch
  `lane-timing-waits`. Proof: guest probes `guestprobeC-run1`
  and `guestprobeE-run2` (sec −1 → −EINVAL on both calls,
  bad nsec → −EINVAL, bad pointer → −EFAULT, mask size 4
  → −EINVAL, mask bad pointer → −EFAULT); the pre-fix
  parks are the tails of `guestprobeB-run1/2/5`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-208 — `pselect6` and `ppoll` never wrote back the
  unslept remainder of their timeout.** *(Medium)* Linux
  writes the remaining time back for select, pselect6 and
  ppoll alike; the emulator wrote it back only for select.
  This entry also corrects the FD-23-residue register note
  that "pselect6 never writes its timespec back, as on the
  host": the timing lane's raw-syscall host grid measured
  the write-back directly (ready-immediately: 49.999998120
  of 50 s left; interrupted at ~43 ms: 49.957649822;
  fully expired: 0), and the standing census test pin that
  encoded the old claim is updated in the same fix. Fixed
  on branch `lane-timing-waits` (the shared select core
  gained a nanosecond-fraction mode for pselect6; ppoll
  writes back at its case). Proof: guest probes
  `guestprobeE-run2` (ppoll ready: 9.999989 of 10 s left;
  expired shapes → ~0) and `guestprobeF-run1` (select and
  pselect6 ready returns leave ~the full timeout);
  updated pin in `testSyscallCensus64`. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-209 — 64-bit `timerfd_settime` rejected the valid
  TFD_TIMER_CANCEL_ON_SET flag and (in this lane's first
  correction) nearly traded that for accepting
  everything.** *(Medium)* The host's valid mask is
  ABSTIME|CANCEL_ON_SET (0x3): flags 0–3 answer 0, 4 and
  −1 answer −EINVAL (raw grid + strace, `hostref-timing3.txt`).
  The shipped 64-bit case rejected bit 1 outright; the
  32-bit path never validated. Fixed on branch
  `lane-timing-waits`: the 64-bit case accepts exactly the
  host mask (CANCEL_ON_SET is accepted; cancelling on a
  clock-set is not modelled, as nothing here sets the
  clock). The standing `testTimerfd` pin that asserted
  −EINVAL for flags=2 rested on the assumption that bit 1
  is invalid; it is updated in the same fix to pin the
  measured mask. Proof: suite tests `testTimerfd` and
  `testTimingWaits64`; guest probe `guestprobeC-run1`.
  Record: `docs/reports/timing-waits-20261107.md`.

- **FD-210 — The select core refused the all-null-sets
  sleep shape for nfds > 1024.** *(Low)* The 1024 cap
  exists because the fd-set buffers are fixed-size (a
  boundary FD-23 stated); with no set pointers at all
  there is nothing to buffer and the host simply sleeps
  (raw grid: nfds=100000 with empty sets answers 0 after
  the timeout). Fixed on branch `lane-timing-waits`: the
  cap now applies only when at least one set pointer is
  non-null; the stated boundary otherwise stands. Proof:
  guest probe `guestprobeC-run1` (`pselect6 nfds100000`
  → 0). Record: `docs/reports/timing-waits-20261107.md`.

- **FD-211 — The X64Headless target did not build at the
  integration tip.** *(Medium; found by this lane, outside
  its family)* `source/util/networkutils.cpp` began with
  an `#undef U64` block ahead of its includes; U64 is a
  macro from the platform types header, and under the
  batch-9 precompiled-header force-include the macro is
  already defined, so the #undef killed it for the whole
  translation unit and the header's include guard made
  the re-include a no-op — every use of the U64 type in
  the file then failed. The checking queue's native legs
  build only the test targets, which exclude this file,
  so the break shipped unnoticed. Fixed on branch
  `lane-timing-waits` by deleting the three-line block
  (no identifier in the file collides with the macro).
  Proof: `make x64` completes and the binary runs the
  guest probes in `test-results/timing-waits/`; the
  failure log is `build-x64.log` there. Record:
  `docs/reports/timing-waits-20261107.md`.

- **FD-212 — `waitid` is not implemented on either
  width.** *(Medium)* Every shape answers −ENOSYS. The
  host contract is measured (raw grid): option validation
  precedes the child lookup (bad options → −EINVAL ahead
  of −ECHILD; options=0 → −EINVAL; a bad idtype →
  −EINVAL; no children → −ECHILD); a bad siginfo pointer
  answers −EFAULT even with no children, and with an
  exited child the child is still reaped when the info
  write faults; WNOHANG on a running child answers 0 with
  the siginfo zeroed. Closing terms: implement waitid on
  both widths to that contract, proven by guest probes
  and suite tests per shape. Filed by the timing lane,
  2026-11-07; evidence `test-results/timing-waits/
  hostref-timing.txt` and the guest grid rows in
  `guestprobe-run*` / `guestprobeB-run*`. The census
  disposition (ordinary-unimplemented) already records
  the gap; this entry gives it a queue number and the
  measured contract.

- **FD-213 — Timer-expiry signals are never delivered to
  a guest handler, and the surrounding pending-signal
  machinery misbehaves.** *(High, scoped as measured)*
  Measured in the bare x86-64 ELF vehicle (single thread,
  full kernel): an interval timer counts down in real
  time (getitimer read-backs track the wall clock) and
  expires, but the expired SIGALRM never reaches a
  properly installed handler (install verified, 1000 ms
  flip test: never; 50 ms periodic over 400 ms: 0
  deliveries against the host's ~8). Sleeps and waits
  surface −EINTR late, at internal ~200 ms wake quanta or
  near the natural end. A timer signal that is BLOCKED
  still interrupts a nanosleep with −EINTR (the host
  completes the sleep), and the expired signal cannot be
  dequeued by `rt_sigtimedwait` (answers −EAGAIN);
  `rt_sigtimedwait` also fails to validate its timeout
  (bad nsec answers −EAGAIN, host −EINVAL). Separately,
  `pause()` in a fresh process with a 40 ms timer parked
  past a 240 s timeout instead of returning −EINTR at
  expiry. Direct delivery works: a tgkill'd signal runs
  the handler during a CPU loop, so the defect is in the
  timer-expiry → dispatch path and the pending-signal
  machinery, not in handler invocation. Scope caveat:
  measured in the bare-ELF vehicle; the production (Wine,
  multithreaded) impact is unmeasured. One guest run also
  died around a sigprocmask unblock following a timer
  expiry (single observation, in the same evidence).
  Closing terms: an expired interval timer delivers its
  signal to an installed handler in the vehicle, blocked
  signals do not interrupt sleeps, and an expired pending
  signal is dequeueable via rt_sigtimedwait. Filed by the
  timing lane, 2026-11-07; evidence
  `test-results/timing-waits/` (`timingprobe2-run2`,
  `timingprobe3-run2`, `timingprobe4-run1`,
  `guestprobeB-run*`).

- **FD-214 — A `wait4` stop report consumes the child
  and reports no stop status.** *(Medium)* On the host,
  wait4 with WUNTRACED on a stopped child reports the
  stop (status carries 0x7f) and does not reap. Measured
  in the guest: the call returned the child with
  stopped=0 and the child was gone afterwards (a later
  wait answered −ECHILD). Other wait4 shapes match the
  host (exit-code reaping, WNOHANG forms). Closing terms:
  stop reports carry a stop status and leave the child
  waitable. Filed by the timing lane, 2026-11-07;
  evidence the child-wait rows of the guest grid in
  `test-results/timing-waits/`.

- **FD-215 — In the full-kernel ELF vehicle, 64-bit
  `futex` is served by the threadless stub, not the real
  implementation.** *(High)* `sys_futex64` delegates to
  `KThread::futex64` only when the CPU carries a thread,
  process and memory64; in the `--x64-run-elf` /
  rootfs vehicle the stub answered instead: WAIT on a
  matching value returned −EAGAIN in 0 ms instead of
  blocking (50 ms timeout shape), WAIT with a bad address
  answered −EAGAIN instead of −EFAULT, WAKE with a bad
  address answered 0 instead of −EFAULT, CMP_REQUEUE
  with a bad second address answered 0 instead of
  −EFAULT. glibc's EAGAIN-retry behaviour masks this as
  spinning and CPU burn. Methodological consequence: futex
  contract shapes must be probed through the native
  harness's real path (the dispatch-guard test) or a
  vehicle known to take it — the suite pins in
  `testTimingWaits64` use FUTEX_PRIVATE_FLAG forms for
  exactly this reason. Closing terms: the futex dispatch
  condition holds in the full-kernel vehicle, proven by
  the grid shapes blocking and faulting as the host does.
  Filed by the timing lane, 2026-11-07; evidence the
  futex rows of `guestprobeB-run5` against
  `hostref-timing.txt`.

- **FD-216 — Guest page protections are not consulted on
  syscall memory accesses (cross-cutting).** *(Medium)*
  Across the timing family, a pointer to a mapped
  PROT_NONE page is treated as readable/writable:
  clock_gettime wrote its result there, getitimer and
  setitimer accepted it, nanosleep/clock_nanosleep read
  the request as zeros from it, getrusage wrote there,
  epoll_wait wrote events there, poll read its descriptor
  array as zeros, pselect6 accepted a sigmask there. Null
  pointers ARE faulted where the code checks them, so
  this is specifically the protection flags of mapped
  pages being ignored by the non-checked accessors, not
  missing null checks. Closing terms are subsystem-wide
  and belong to the memory owners; this entry records the
  measured shapes so the property has a queue number.
  Filed by the timing lane, 2026-11-07; evidence the
  bad-pointer rows of the guest grid in
  `test-results/timing-waits/`.

- **FD-217 — ITIMER_VIRTUAL and ITIMER_PROF are refused
  with −EINVAL.** *(Medium)* The host accepts them (they
  tick against process/thread CPU time); the emulator's
  shared itimer core refuses them, and the standing suite
  test pins the refusal deliberately. Closing them needs
  CPU-time timers (the CPU-time sources now exist —
  FD-198's helper and the per-thread times — but the
  expiry machinery is FD-213's territory). Closing terms:
  setitimer/getitimer accept VIRTUAL and PROF against CPU
  time and the pin is replaced. Filed by the timing lane,
  2026-11-07; evidence `hostref-timing.txt` and
  `testSetitimer64Syscalls`.

- **FD-218 — Four timing-family calls have no 32-bit
  dispatch at all: `pause`, `rt_sigtimedwait`, `ppoll`,
  `epoll_pwait`.** *(Medium)* All four are dispatched on
  the 64-bit side and answer −ENOSYS on the 32-bit side
  (census disposition: ordinary-unimplemented; the suite
  now pins the −ENOSYS answers in `testTimingWaits32` so
  the gap is executable, except pause, whose blocking
  shape cannot be pinned in the harness). The 64-bit
  implementations are open-coded in the 64-bit dispatcher
  rather than shared methods, so wiring the 32-bit side
  is real work, not a table entry. Closing terms: the
  four calls dispatch on the 32-bit side to the same
  contract as the 64-bit side. Filed by the timing lane,
  2026-11-07; evidence
  `tools/conformance/syscall_dispositions.json` and the
  suite pins.

- **FD-219 — In the bare-ELF vehicle the guest process
  dies at `epoll_ctl` DEL with a null event, and related
  deaths follow interrupt-heavy sequences.** *(High)*
  `epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)` is valid on
  the host (the event is ignored; the answer is 0). In
  the vehicle the run ends there: a
  `BW64-FAIL guest-memfault32 ... addr=0x0` report whose
  instruction pointer is the guest's own entry point,
  then emulator shutdown — reproduced deterministically
  in an isolation probe whose thirteen preceding calls all
  answer correctly (`guestprobeD-run1` on the base-era
  binary, `guestprobeD-run2` on the timing lane's fixed
  binary, same fault address). The ctl implementation
  itself is exonerated by reading: it already skips the
  event read for DEL and its removal path touches only
  host-side structures, so the death sits in the
  surrounding return/dispatch machinery. Related: the
  full-grid runs repeatedly died shortly after
  interrupt-heavy select sequences, in states the
  isolation probes (3/5/6/7) never reproduced. Closing
  terms: the isolation probe runs to completion with DEL
  answering 0. Filed by the timing lane, 2026-11-07;
  evidence `test-results/timing-waits/guestprobeD-run*`
  and `guestprobe-run*`.

- **FD-220 — POSIX timers are absent.**
  *(Medium)* timer_create and family answer −ENOSYS for
  every shape on both widths (the census disposition
  records subsystem-absent deliberately). The host
  contract is measured in the grid (creation validation,
  EINVAL/EFAULT orderings, the same lenient-flags pattern
  as the other timer calls). Closing terms: implement
  the subsystem or take a deliberate documented decision
  to keep it absent; either way this entry is the queue
  record. Filed by the timing lane, 2026-11-07; evidence
  `test-results/timing-waits/hostref-timing.txt` and the
  guest grid's timer rows.

- **FD-221 — Timing-family model limits and unmeasured
  residue (recorded, not defects of commission).**
  *(Low)* (a) The ALARM clocks (8, 9) are served from the
  wall/monotonic bases; their suspend semantics do not
  exist here. (b) `times` fills the tms accounting fields
  with zeros, and 64-bit `getrusage` fills only the two
  CPU-time fields. (c) 64-bit `epoll_pwait` refuses
  maxevents > 64 (scratch-page cap) where the host
  accepts more. (d) `clock_nanosleep` acceptance for
  clocks 4, 5, 6, 8, 9 follows the kernel's clock
  structure; this box's host grid measured 0, 1, 2, 3, 7,
  11 directly. (e) The monotonic clock's epoch is the
  emulator's start, so early guest readings are near
  zero; contract-legal, recorded because it surprises.
  (f) The remaining-time write on interrupted sleeps is
  computed at millisecond (32-bit) / microsecond (64-bit)
  fidelity from the call's elapsed wall time, not from
  the wait's internal deadline bookkeeping. Filed by the
  timing lane, 2026-11-07; evidence
  `docs/reports/timing-waits-20261107.md`.

- **FD-222 — 64-bit `epoll_pwait` rejects a sigsetsize
  other than 8 when a mask is passed; the host accepts
  any size.** *(Low)* Measured: mask non-null with size 4
  answers −EINVAL in the guest (`guestprobeD-run2`) and
  0 on the host (raw grid); ppoll likewise accepts any
  size on the host, and rt_sigtimedwait/pselect6 are the
  calls that genuinely require exactly 8. Closing terms:
  drop the size check in the epoll_pwait body (the mask
  copy itself is bounded by the read). Filed by the
  timing lane, 2026-11-07; evidence
  `test-results/timing-waits/hostref-timing.txt` and
  `guestprobeD-run2`.

- **FD-193 — Vectored reads accept segment lengths that are
  negative read as the ABI's signed size, and the combined
  length is never capped.** *(Medium)* In all four read walks
  (64-bit `sys_readv64` / `sys_preadv64`, 32-bit
  `KProcess::readv` / `KProcess::preadv` over
  `KObject::readv`) a segment length is taken as an unsigned
  count and walked as-is: a length with the top bit set is a
  request to read gigabytes into one segment, and on the
  32-bit side the running total is a U32 that wraps, so a
  crafted array can make the walk's accounting go backwards.
  The host contract, measured at the raw syscall level on
  this box's 6.17 kernel (native aarch64 and aarch32 compat
  binaries; the two widths agree on every row): any segment
  whose length is negative read as the ABI's signed size
  (64-bit: >= 2^63; 32-bit: >= 2^31) answers `-EINVAL`
  before any byte moves, wherever it sits in the array; the
  32-bit side has NO sum check (segments of 1 GiB + 1 GiB +
  1 byte, sum 2^31 + 1, read normally — only the
  per-segment sign is judged); and a successful transfer is
  capped at `MAX_RW_COUNT` = 0x7ffff000 bytes in total
  (measured exactly 2147479552 returned from /dev/zero for
  one 2^31 segment and for two segments summing to 2^31).
  Boundaries: the host additionally answers `-EFAULT` for
  segment ranges that exceed the address space
  ({2^62, 2^62}; {8, SSIZE_MAX}) — the emulator's flat
  guest memory has no equivalent range validation and this
  entry does not ask for one; the vectored-WRITE family
  shares the unguarded shape and is filed separately as
  FD-197. Closing terms: the per-segment signed check in
  all four validation paths (after the iovec array check,
  ahead of the preadv2 flags check) and the total cap in
  all four walks, proven by native tests on both suites.
  Found by the unfiled-gaps lane, 2026-11-06; evidence
  `test-results/unfiled4/` (host references
  `hostref-vec3.out`, `hostref-vec-arm32.out`,
  `hostref-vec-arm32b.out`, `hostref-pv2-final.out`; the
  early `hostref-vec.out` is qemu-polluted and
  `hostref-vec-arm64.out`'s preadv rows passed the loff_t
  offset as one register instead of the split pair — both
  kept as evidence of the artifacts, neither cited).
  CLOSED — see CL-137 (lane-local number; settles at the
  next composition audit). The signed check sits in all
  four validation paths and the 0x7ffff000 cap in all
  four walks (the 32-bit cap rides the shared
  `KObject::readv` walk). Filed and fixed by the
  unfiled-gaps lane, 2026-11-07. Record:
  `docs/reports/fd193-196-unfiled-gaps-20261107.md`.
- **FD-194 — A positioned read attempted on a pipe (or any
  unseekable object) answers the wrong thing, and on the
  64-bit side silently reads.** *(Medium)* The host
  contract, measured at the raw syscall level on the 6.17
  kernel (both widths identical on every row): a missing
  descriptor answers `-EBADF` ahead of everything; after
  that, an unseekable object answers `-ESPIPE` ahead of
  the iovcnt range, iovec array, and preadv2 flags answers
  (preadv on a pipe with iovcnt 2000, a null array, or bad
  flags all answer ESPIPE), and `pread64` answers ESPIPE
  ahead of its negative-offset `-EINVAL`; `preadv`'s own
  negative offset answers `-EINVAL` ahead of ESPIPE;
  `preadv2` with offset -2 answers `-EINVAL`, with offset
  -1 is the file-position mode and READS from a pipe (its
  flags are still validated first: a bad flag answers
  `-EOPNOTSUPP` ahead of the read); pipe bytes are never
  consumed by a refused positioned read. The emulator
  today: 64-bit `sys_pread64Impl` sends non-file objects
  through a seek-ignoring stream read, so a positioned
  read on a socketpair consumes data and returns it;
  64-bit `sys_preadv64` inherits that per segment; 32-bit
  `KProcess::preadv` answers `-EBADF` (write end),
  `-EINVAL` (bad iovcnt), `-EFAULT` (null array) or
  `-EOPNOTSUPP` (flags) ahead of ESPIPE, and its
  file-position mode fails on a pipe instead of reading;
  32-bit `KProcess::pread64` answers `-EINVAL` for
  unseekable objects that are not sockets (eventfd,
  timerfd, epoll, inotify). The positioned-WRITE twins
  (`pwrite64` / `pwritev`, both cores) carry the same
  non-file `-EINVAL` shape and are corrected alongside
  (the host answers ESPIPE there too, measured in the
  same pass). Closing terms: the seekability answer
  hoisted to immediately after descriptor resolution in
  `sys_preadv64`, `sys_pread64Impl`,
  `KProcess::preadv` and `KProcess::pread64` (position
  mode excepted, delegating to the plain vectored read
  after validation), proven by native tests on both
  suites covering the pipe matrix on both ABIs. Found by
  the unfiled-gaps lane, 2026-11-06; evidence
  `test-results/unfiled4/` (`hostref-pv2-final.out` is
  the authoritative matrix; FD-98's entry named this
  boundary and left it unfiled).
  CLOSED — see CL-138 (lane-local number; settles at the
  next composition audit). The write twins are corrected
  in the same fix: `sys_pwritev64` / `KProcess::pwritev`
  hoist the same answer ahead of their mode, range,
  array and flags checks, `sys_pwrite64` gains it after
  the descriptor check, and the 32-bit `pread64` /
  `pwrite64` non-file answer moves from `-EINVAL` to
  `-ESPIPE` (sockets already answered it). Two measured
  corners stay as boundaries: `pwritev2` with
  RWF_APPEND and the file-position mode on a pipe — the
  host's own ABIs disagree (native answers `-EBADF`,
  the compat path writes), so that combination keeps
  the walk's current answer; and `pwrite64` with a
  negative offset on a pipe, where the native path
  answers `-EINVAL` ahead of `-ESPIPE` and the compat
  path answers `-ESPIPE` — each core follows its own
  ABI (the 64-bit side keeps FD-93's offset-first
  order, the 32-bit side answers ESPIPE first). Write
  rows: `hostref-pwtwins64.out`, `hostref-pwtwins32.out`,
  `hostref-pwt2-64.out`, `hostref-pwt2-32.out`. Filed
  and fixed by the unfiled-gaps lane, 2026-11-07.
  Record: `docs/reports/fd193-196-unfiled-gaps-20261107.md`.
- **FD-195 — The 32-bit `linkat` system call has no helper
  at all.** *(Medium)* Slot 303 in the 32-bit dispatch
  table is `nullptr`: every `linkat` from a 32-bit guest
  answers `-ENOSYS`, whatever its arguments — the only
  one of the *at family missing (renameat, symlinkat,
  readlinkat and plain link are all wired). The host
  contract, measured at the raw syscall level on the 6.17
  kernel (i386-shape arguments driven through the aarch32
  compat path and the native path; identical on every
  row): flags outside `AT_SYMLINK_FOLLOW` (0x400) |
  `AT_EMPTY_PATH` (0x1000) answer `-EINVAL`, ahead of
  even the null-path fault; a null path answers
  `-EFAULT` (FD-132's rule); an empty path answers
  `-ENOENT` (FD-131's rule); a bad dirfd answers
  `-EBADF`, a dirfd that is not a directory `-ENOTDIR`;
  a missing source `-ENOENT`, an existing target
  `-EEXIST`, a directory source `-EPERM`; dirfd-relative
  resolution works on both sides; `AT_EMPTY_PATH` with
  an empty old path and a file descriptor links that
  file, and with a pipe descriptor answers `-EXDEV`;
  without `AT_SYMLINK_FOLLOW` the new name links the
  symlink itself, with it the symlink's target. Boundary:
  the 64-bit dispatcher HAS a linkat case but resolves
  both paths against the current directory only (dirfds
  ignored) and leaves the `AT_EMPTY_PATH` descriptor
  form at its current answer — a real divergence, not
  part of this entry's closing terms. Closing terms: a
  `KProcess::linkat` resolving each side through the
  established dirfd helper and delegating to
  `KProcess::link` (with the followed-source and
  descriptor forms above), a dispatcher that validates
  flags ahead of the path fault, wired at slot 303, and
  native tests proving the measured matrix. Found by
  the unfiled-gaps lane, 2026-11-06; evidence
  `test-results/unfiled4/` (`hostref-linkat64.out`,
  `hostref-linkat32.out`).
  CLOSED — see CL-139 (lane-local number; settles at the
  next composition audit). Filed and fixed by the
  unfiled-gaps lane, 2026-11-07. Record:
  `docs/reports/fd193-196-unfiled-gaps-20261107.md`.
- **FD-196 — Vulkan trap traffic that never completes a
  frame feeds no metrics counter.** *(Low)* FD-63 closed
  the emission gate's blindness for syscall-only guests;
  its entry named the remaining boundary: Vulkan counters
  move only inside `bw64_metrics_vk_frame`, which runs at
  present completion — a guest that issues Vulkan traps
  indefinitely without ever presenting (stuck before its
  first frame, the FD-63 shape in Vulkan form) produces
  no activity the gate can see, so nothing is ever
  emitted for exactly the sessions the panel exists to
  diagnose. The bridge already constructs a per-trap
  timer on every trap in both dispatchers
  (`VkProfTrapTimer` in `vk64Bridge` / `vk32Bridge`),
  but its accounting is gated on the profiling timers
  and reaches the registry only at frame completion.
  Closing terms: a per-trap metrics hook called from
  that timer ahead of its timing gate, counting into
  the registry's existing Vulkan trap counter on the
  64-bit path and a gate-only counter on the 32-bit
  path, with both counters feeding the emission gate's
  activity test and `bw64_metrics_vk_frame` no longer
  adding the frame's trap count a second time; the
  JSON schema does not change and the committed metrics
  fixture stays byte-identical. Boundaries: per-trap
  TIMING on the 32-bit Vulkan path stays unavailable
  (only the call count is knowable there), and trap
  calls the bridge rejects before its dispatcher are
  not counted. Proven by a new metrics-selftest section
  (trap-only activity emits; the snapshot reports the
  trap count with zero frames). Found by the
  unfiled-gaps lane, 2026-11-06 (characterised in
  FD-63's entry, 2026-11-01, and left unfiled).
  CLOSED — see CL-140 (lane-local number; settles at the
  next composition audit). The per-trap hook
  (`bw64_metrics_vk_trap`) is called from
  `VkProfTrapTimer`'s destructor ahead of the timing
  gate on both bridges, flushes any due snapshot before
  accounting (FD-63's rule), and feeds the gate through
  both trap counters; the committed fixture is
  byte-identical (the official regeneration differs
  only in a clock-jitter field and was not shipped).
  Filed and fixed by the unfiled-gaps lane, 2026-11-07.
  Record: `docs/reports/fd193-196-unfiled-gaps-20261107.md`.
- **FD-197 — The vectored-write family shares FD-193's
  unguarded shape.** *(Low)* Found by inspection while
  fixing FD-193: `sys_writev64` / `sys_pwritev64` and
  32-bit `KObject::writev` / `KProcess::pwritev` walk
  their segments with no per-segment signed check and no
  combined cap, and the 32-bit accumulator is a U32 that
  wraps — the same defect as FD-193 on the write side.
  The kernel's vector import is shared between the read
  and write families, so the host contract is expected
  to mirror FD-193's measured one, but this lane did not
  measure the write side and the entry stays open on
  that honesty: closing terms are the raw-syscall host
  matrix for writev/pwritev (both widths), then the
  same checks and cap in the four write walks, proven
  by native tests. Found by the unfiled-gaps lane,
  2026-11-06.

## B. Fixed on a lane branch
  by native tests. Found by the unfiled-gaps lane,
  2026-11-06.

- **FD-198 — The 64-bit sequential read path answers
  end-of-file for descriptor 0 whatever it names.**
  *(Low)* `sys_read64` opens with an unconditional
  stdin shortcut: descriptor 0 returns 0 before the
  descriptor is even resolved. In the browser product
  descriptor 0 is conventionally stdin, which carries
  no data, so the shortcut is invisible — until a guest
  closes stdin and opens a file onto descriptor 0 (a
  legal shape daemons and test harnesses use), after
  which every sequential read on that file silently
  answers end-of-file while positioned reads on the
  same descriptor return its contents. Measured by the
  unfiled-gaps lane, 2026-11-07, while staging its x64
  fixtures: the test's data file landed on handle 0,
  `pread64` read it correctly and `readv` answered 0
  with the position at 0 (the lane's tests now stage a
  throwaway descriptor first). The 32-bit model path
  has no such shortcut. Closing terms: resolve the
  descriptor first and apply the end-of-file answer
  only when descriptor 0 names no readable object,
  proven by a native x64 test reading a file staged on
  descriptor 0. Not fixed by the filing lane.
- **FD-201 — The shipped audio check verified only the
  captured tone's frequency, so wrong sample values could
  pass it.** *(Medium — test coverage. Filed by
  lane-audio-e2e, 2026-11-07.)*
  `web/tests/suite/browser/audio-tone.test.mjs` reduced the
  captured audio to two zero-crossing frequencies. The guest
  fixture (`tools/guest-fixtures/dsptone.c`) is fully
  deterministic — a 440 Hz square wave at ±12000/32768,
  identical stereo channels, exactly six seconds at
  44100 Hz — so the values themselves are checkable, and
  the FD-108 lesson is that values can be wrong while
  frequency measures right. FIXED on lane-audio-e2e — see
  CL-141: the check now measures peak and RMS level,
  RMS/peak shape, duty cycle, active duration, the longest
  silent gap inside the tone, and channel balance on both
  architectures, waits for the queued tone to drain before
  measuring, and writes every measured value into its
  per-architecture evidence JSON before asserting. Its
  first catch is FD-202.

- **FD-202 — Delivered audio interleaves the guest's sound
  with an equal amount of silence in 4096-frame blocks:
  sound plays stuttered and takes twice as long.**
  *(High — user-visible audio. Filed by lane-audio-e2e,
  2026-11-07.)* Measured with the strengthened audio-tone
  check (FD-201) against a fresh build of integration tip
  `334c70be4` (wasm `92b36180…`, byte-identical to the live
  batch-10 build): three runs, both architectures, the
  same signature every time. The guest's deterministic
  tone arrives with peak exactly 0.3662109375, channels
  sample-identical, pitch 440.0–441.9 Hz — but duty cycle
  0.249 (expected ~0.50), RMS 0.2583 (expected ~0.3662;
  exactly peak/√2), active span 11.94–12.02 s (expected
  6 s), and recurring silent gaps of exactly 4096 frames
  (92.88 ms) inside the tone; of ~530,000 delivered frames
  per channel, half are silence. Every guest sample
  arrives; an equal number of silence frames is
  interleaved in 4096-frame blocks, so anything a program
  plays is heard as ~93 ms of sound alternating with
  ~93 ms of silence, at twice the true duration. The
  write side is not the pacing: the session console shows
  the guest's six one-second writes (1,058,400 bytes
  total) accepted in ~0.62 s of audio-stage time. The
  mechanism is NOT identified by this filing; the numbers
  bound it to the delivery path between the guest's
  writes and the browser's audio output — downstream of
  sample generation (values and channel identity are
  bit-exact), upstream of the output node the check
  captures. The frequency-only check this replaces passed
  throughout, which is how this survived. Closing terms:
  the strengthened audio-tone check passes on both
  architectures — duty ~0.50, RMS ~peak, active duration
  ~6 s, no internal silent gap above 20 ms — against a
  build carrying the fix. Evidence: `test-results/audio-e2e/`
  (run3-evidence/, run4-evidence/, run logs) and
  `test-results/suite-audio-tone/` in wt-audio-e2e;
  record `docs/reports/audio-tone-values-20261107.md`.

  **Resolution (2026-11-07, lane-fd202-audio): the
  delivered audio is whole; the signature above is an
  artifact of the check's own capture, and this entry
  is FIXED as a check defect -- see CL-142.** The
  check folded two different output objects into one
  measured array: the AudioWorkletNode's output (the
  runtime's real output path, tapped) and every
  ScriptProcessorNode's output buffer -- including
  SDL's own emscripten pump node, which is not the
  output in this build (writeAudio bypasses SDL's
  queue via the bwPump ring), so its buffers are
  silence by design, in blocks of exactly its
  4096-frame buffer. A source-separated capture
  against the live build (wt-fd202-audio,
  `test-results/suite-fd202-split/`) measures the
  worklet path alone at peak 0.3662109375 exactly,
  RMS 0.3662109375, duty 0.500, active 5.93 s,
  longest internal gap 0 frames, the worklet's own
  metrics at framesWritten 264,600 (the guest's
  exact frame count) and underrunFrames 0; the
  ScriptProcessor path alone is 266,240 frames of
  pure silence from one node created with
  bufferSize 4096, all 65 of its buffers all-zero;
  the merged array reproduces this filing's
  signature to three decimals. The closing terms
  are met by the fixed check passing on both
  architectures (CL-142); no product code changed,
  because the product was not the defect.

- **FD-203 -- audio-tone check: in the no-worklet
  fallback configuration the capture still merges
  SDL's silent pump node with the runtime's
  ScriptProcessor output.** *(Low -- check only, in
  a configuration the project does not ship or gate.
  Filed by lane-fd202-audio, 2026-11-07.)* FD-202's
  fix separates the two capture sources and measures
  the worklet output in the shipped configuration;
  when no AudioWorkletNode exists, the runtime's own
  output is itself a ScriptProcessorNode and the
  check's fallback array still folds SDL's pump-node
  buffers into it, so the FD-202 conflation can
  recur there. No runtime mark distinguishes the
  two ScriptProcessor nodes today. Closing terms:
  the fallback measurement identifies the runtime's
  output node (or the runtime marks it) and the
  strengthened check passes in a no-worklet run.
  The shipped, gated configuration is unaffected.


- **FD-201 — A first-visit 32-bit start can be killed at the
  moment a new program starts inside the session: a session
  worker dies with a guest memory error and the desktop never
  appears.** *(High — start reliability; kills a whole
  session. Filed by the x86 start-fault lane, 2026-11-07.)*
  In the batch-10 deploy's live acceptance, the first 32-bit
  certification series certified 3 of 5 starts; boots 4 and 5
  each suffered a delivered wasm `RuntimeError: memory access
  out of bounds` in a session worker at the identical console
  position — immediately after the wineserver-side socketpair
  wiring for a newly starting program, where clean boots log
  the new program's stack setup — with identical JS glue
  offsets (15105 / 6378) in both. The desktop program's main
  thread then sat frozen at one identical state snapshot for
  the rest of each run, the desktop never reported ready, and
  each boot exhausted its 900 s budget with 0 restarts. An
  adjudication re-run on the same live build certified 5 of 5
  clean, so the fault site is deterministic but its incidence
  is not a persistent property of the build; the 64-bit series
  (3 of 3) and the six consumer sessions were clean in the
  same hours. The shipped program code did not change in
  batch 10 (the deployed wasm is byte-identical to batch 9),
  and one earlier instance of a delivered guest memory fault
  during a start is on record against an earlier shipped
  build (batch-4 deploy, consumer run a4, unattributed), so
  the defect predates batch 10. The FD-115 first-visit tally
  stands at 3 in 60 with these two boots counted. Evidence:
  deploy record `docs/reports/deploy-batch10-20261107.md`;
  preserved consoles
  `wt-deploy-batch10/test-results/deploy-batch10-20261107/cert-x86/boot-4/console.log`
  and `wt-deploy-batch10/test-results/deploy-batch10-20261107/cert-x86/boot-5/console.log`
  (clean controls boot-1 to boot-3 and the re-run series in
  the same directory). Closing terms: the cause is
  identified and fixed, and repeated 32-bit starts against a
  build carrying the fix pass with zero occurrences of the
  failure.
  **RESOLVED on this branch (2026-11-07, lane-x86-start-fault;
  see the section B row and the lane record's resolution
  addendum).** A third instance, captured the same morning by
  the FD-169 remainder lane's 32-bit suite run on a build
  byte-identical to the shipped one, preserved the full wasm
  stack the deploy consoles lacked (evidence:
  `wt-fd169-remainder/test-results/suite-fd169r-smf32-fault1/console.log`):
  the trap fires in `KSystem::guestRamInfo`, called from the
  32-bit `sysinfo` handler, on the `process->memory64` field
  load. Cause: `guestRamInfo` iterated the process table
  (`KSystem::processes`, a BHashTable) with no lock while
  `addProcess`/`eraseProcess` mutate it under
  `processesCond`; an insert crossing the table's load-factor
  limit replaces the whole slot array and frees the old one
  (BHashTable::rehash), so a scan racing a process start could
  keep reading the freed array and dereference a wild
  KProcess pointer — the first field load traps and kills the
  calling worker, the desktop never reports ready, and the
  start is lost. A starting process's own sysinfo (issued by
  every starting Wine process) is the usual caller, which is
  why the fault lands exactly at program starts and only in
  bursts (a rehash happens only when an insert crosses a
  power-of-two process count). Fix commit `5602c1dd5`:
  `guestRamInfo` and `processCount` now hold `processesCond`
  for the table access — the pattern every other access in
  the class already used — and the 32-bit sysinfo reads the
  process count through `processCount`; a source guard in the
  guest-topology test pins both locks (fails on the unfixed
  source, passes with the fix). Proof on the branch: two
  series of 12 consecutive 32-bit starts against the fixed
  build (wasm `4be34516…`), 24/24 started, zero occurrences,
  zero captured exceptions (driver logs and per-boot consoles
  in `test-results/x86-start-fault/repro-fixed-1/` and
  `repro-fixed-2/`); a control series against the live
  unfixed build the same day ran 12/12 clean, consistent with
  the bursty incidence (the preserved fault instances are the
  positive control). The closure number settles when batches
  combine.
- **FD-201 — The bridge-coverage probes cannot run: they import the probe-slot helper from the project's pre-move path.** *(Medium)* `web/tests/vkcov-probe.mjs`, `web/tests/glcov-probe.mjs`, and `web/tests/vkcov-probe-matrix.mjs` imported `acquireProbeSlot` from `/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs`, a tree deleted by the 2026-11-06 project move, so every Vulkan/OpenGL coverage run died at import time and the coverage matrices could not be re-measured at all. Fixed on lane-vkgl-coverage (relative `./probe-slots.mjs` import in all three probes). Closing terms: a coverage probe runs end-to-end from a fresh worktree and writes its results matrix; the fix is committed but the closing proof is outstanding — the vkgl-coverage lane was stopped by the 2026-11-06 disk emergency before its dist build, and the resume steps are recorded in docs/reports/vkgl-spec-coverage-20261106.md. Found by the vkgl-coverage lane, 2026-11-06; evidence docs/reports/vkgl-spec-coverage-20261106.md + test-results/vkgl-census/before.json (force-added). Number note: filed as FD-195 on this lane branch off the pre-batch-10 base; the batch-10 composition settled FD-195 to the bus-discovery filing, so on the 2026-11-07 rebase onto the batch-10 tip this entry is FD-201 (provisional; final settlement at batch-11 composition).
- **FD-201 — The bridge-coverage probes cannot run: they import the probe-slot helper from the project's pre-move path.** *(Medium)* `web/tests/vkcov-probe.mjs`, `web/tests/glcov-probe.mjs`, and `web/tests/vkcov-probe-matrix.mjs` imported `acquireProbeSlot` from `/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs`, a tree deleted by the 2026-11-06 project move, so every Vulkan/OpenGL coverage run died at import time and the coverage matrices could not be re-measured at all. Fixed on lane-vkgl-coverage (relative `./probe-slots.mjs` import in all three probes). Closing terms: a coverage probe runs end-to-end from a fresh worktree and writes its results matrix; the fix is committed but the closing proof is still outstanding. Run phase attempted 2026-11-07: the branch was rebased onto the batch-10 tip and prep verified clean, but the dist build never acquired the shared build lock (the build script gave up after its wait while lane builds relayed) and free disk fell to the lane guardrail line the same morning; the remaining steps are recorded in docs/reports/vkgl-spec-coverage-20261106.md. Found by the vkgl-coverage lane, 2026-11-06; evidence docs/reports/vkgl-spec-coverage-20261106.md + test-results/vkgl-census/before.json (force-added). Number note: filed as FD-195 on this lane branch off the pre-batch-10 base; the batch-10 composition settled FD-195 to the bus-discovery filing, so on the 2026-11-07 rebase onto the batch-10 tip this entry is FD-201 (provisional; final settlement at batch-11 composition).


## B. Fixed on a lane branch — awaiting merge

**Status correction (2026-11-05, cleanup/status lane):** this table
is now a historical ledger, not a live queue. Every branch row in it
has since been composed into the integration branch — through the
2026-10-31 cleanup merge wave and fix batches 1–7 (last composed
2026-11-04) — and the entries those rows answer are closed in
Section C. Read a row here as "this lane's work, its branch, and its
proof", never as work still outstanding. The branches whose work is
genuinely not in the integration tree today are the ones with no row
here yet: `lane-fd162-164` (in progress) and `lane-cleanup-status`
(this lane). The original preamble follows, kept for the record.

**Batch-8 audit note (2026-11-05):** the two branches named in
the correction above have since been composed into this tree
(batch 8, together with `lane-buildrel`): `lane-fd162-164`'s
entries are closed in Section C (FD-162 as CL-119, FD-163 as
CL-120, FD-164 as CL-121; its filing FD-165 stays open) and
`lane-buildrel`'s as CL-122 to CL-124 (FD-31 to FD-33), and
both lanes have rows in the table below. The correction stands
as written for the moment it was made.

These are findings whose fixes are written and proven on their
branches but are **not** in the integration tree. The entries
they answer stay open until the branch lands and, where the fix
is behavioural, is re-proven there.

| Branch | What it fixes | Proof on the branch |
| --- | --- | --- |
| `lane-perf2` | *(Batch-9 settled numbers: filings FD-175, FD-176.)* FD-26 (partial): the fast engine compiles FF/FE INC/DEC instead of falling back block-wide (see the FD-26 progress note) | Guest bench warm medians: fast engine 491/958 ms before -> 89/102 ms after (interpreter 310/546 -> 287/298 in the same runs), checksum identical across modes and builds; native jit64 tests 873/0; emitted-code differential 314/0; native main 686/0, x64 111/0; JS unit 353/0; Doom demo-matrix: mapped + gameplay, 132 frames presented (13 before, different load). Record: `docs/reports/perf-incdec-fastpath-20261105.md` |
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
| `lane-gamepad` | *(Batch-9 settled numbers: FD-187 closed as CL-133; filings FD-188, FD-189.)* FD-24 ADVANCED, NOT CLOSED + FD-187 CLOSED (lane-local CL-133) + FD-188 FILED + FD-189 FILED: the emulator's controller chain is complete — SDL host feed into the kernel evdev gamepad, a synthetic /sys input class, statfs naming the /sys and /proc trees with their real magics (FD-187), and the device's udev data synthesised — with the purpose-built witness (XInput + DirectInput) as the proof vehicle; guest visibility remains blocked inside Wine by FD-188 (kernel drivers fail to initialise: ZwLoadDriver c0000142 / StartService error 1114, present on unmodified builds too) and FD-189 (the shipped 64-bit prefix has no PlugPlay/bus registration). | Commits `6b6186d93` + `bcff6378b` on base `48d27952a`; native suites serial, 0 FAILED (main 690 tests, x64 114 tests) (including the two new FD-187 pinning tests); JS unit suite 353/0; browser build 'Browser export verified' (wasm `42bd5a2a…`). Guest measurements: the SDL feed opens the simulated pad in the browser build; libudev enumerates the gamepad after the statfs fix; the WineBus service start fails with error 1114 on the 32-bit side, and the bus services do not exist (error 1060, empty root enumeration) on the 64-bit side. Record: `docs/reports/fd24-gamepad-20261106.md` |
| `lane-fd63` | FD-63 CONVICTED + FIXED: the METRICS emission gate counted presents, Vulkan frame completions, GL and audio calls as activity but not syscall dispatch, so a session that stopped presenting had every later window discarded unreported by the no-activity branch — the panel froze at the last present-driven snapshot while the session executed on; the gate now counts syscall dispatch as activity | New section of the native metrics selftest drives six successive syscall-only intervals against the real registry: RED on unfixed code (24 PASS, exactly the two FD-63 assertions FAIL) -> GREEN with the fix `6d711e6a6`; writer fixture reproduced byte-identically modulo clock fields (committed fixture unchanged); reader contract + panel tests 21/21; browser build 'Browser export verified' / BUILD COMPLETE, wasm 1809b7d7, support c4dbe35986a5 unchanged. Record: `docs/reports/fd63-metrics-emission-20261101.md`; evidence `test-results/fd63/` |
| `lane-fd79-80` | FD-79: a file description's close event (IN_CLOSE_WRITE / IN_CLOSE_NOWRITE) is delivered at the host's moment — held while any mapping of the description is live, delivered at the last unmap (the emulator lost it in-session: destructor-driven close, pinned by the mapping entries and the system file cache); FD-80: the 64-bit dispatcher's preadv (295) and preadv2 (327) wired over pread64's factored core with the host's validation and one-IN_ACCESS-per-success shape; FD-82 (64-bit materialized mappings outside the deferral), FD-83 (readv silent on 0-byte success) and FD-84 (wrong-mode reads answer EINVAL, host EBADF) filed | New tests testCloseWriteDeferral (both suites; RED on the base — five arrival assertions failing; the first fix version missed write-fault CopyOnWrite pages and the test caught it) and testPreadv64Syscalls (x64 suite; RED on the base dispatch — 'unimplemented syscall #295/#327', -ENOSYS); host-kernel reference programs measured first (close-event matrix, preadv/preadv2 validation and notification matrices); full native suites serial: main 668/668 and x64 68/68, 0 FAILED; browser build compiles on the fixed tree (see the lane record). Record: `docs/reports/fd79-fd80-kernel-semantics-20261101.md`; evidence `test-results/fd79-80/` |
| `lane-fd66` | FD-66: `setupReadbacks` discarded still-pending readback plans without freeing their heap blocks (latent — every production submit pairs setup with completion, so the discard was reachable only from the harness's planning sections), and the harness's sanitizer flavour reported the fixture's abandoned blocks so it could never finish clean | RED on the base reproduced the filed measurement exactly (LeakSanitizer 1,132 bytes in 5 allocations from setupReadbacks, exit 1); GREEN fixed: sanitizer flavour 57 PASS, exit 0, zero leak reports, via the standard runner too (its sanitizer run budget raised to 180 s); sabotage (prologue alone reverted) reopens exactly 328 bytes in 2 allocations; native serial 667/667 + 66/66; JS 348/0; citations 352/0 after re-pins; browser build BUILD COMPLETE (wasm df27c5e0…). Record: `docs/reports/fd66-readback-disposal-20261101.md`; evidence `test-results/fd66/` |
| `lane-fd65` | FD-65: Gecko's "unable to find a usable font" — two layers: the 64-bit rootfs carried no font faces, and the shipped 64-bit win32u.so was built without freetype/fontconfig so GDI could enumerate and realize no font at all; faces staged (fonts64) and win32u rebuilt with its font engine (the build now fails loudly if either SONAME is absent); FD-85 + FD-86 filed | Guest step probe x64: EnumFontFamiliesEx 0 → 85 families, GetTextMetrics fail → tmHeight 17 (the 32-bit stack's numbers); the real common-dependencies Gecko stage green in two runs (MSHTML: RESULT 0, zero abort lines, test untouched — its remaining audio-capture failure is FD-85); completeness 0 failures; JS 348/0; dist 'Browser export verified'. Record: `docs/reports/fd65-gecko-fonts-20261102.md` |
| `lane-fd85` | FD-85: the audio-capture failure convicted as two defects — the test's capture watched SDL's bypassed ScriptProcessor node while guest audio flows through the bwPump AudioWorklet (FD-87: fixed, closed CL-42), and beneath it Wine's mmdevapi → winealsa → ALSA leg delivers silence to the emulator's audio input for every Windows audio client tried (FD-85 stays open, break point proven by measurement); wasapi-tone + winmm-tone fixtures added | Fixed capture hears the direct-DSP control tone at peak 0.3662 where the shipped capture counted 0 non-zero in 266,240 samples; shipped test post-fix still fails at its audio wait (Gecko green, `OPENAL: RESULT 0`, first push peak 0.0000, no S16 non-zero line); JS 348/0; dist 'Browser export verified' (wasm byte-identical to shipped). Record: `docs/reports/fd85-audio-leg-20261102.md` |
| `lane-fd77-stall-surface` | FD-77 remainder: a default session whose boot completes the desktop handshake but never maps a window and stops executing is now surfaced as a reported failure (the launcher takes its own thread dumps and convicts on the dump's measured zero-progress, two consecutive dumps, no fetch in flight); the held dump evidence is flushed to the preserved session log ahead of one BW64-FAIL line; slow-but-alive starts, armed diagnostic sessions, and every readiness rule are untouched | Unit tests over the shipped watchdog section: 8/8 green, 8/8 red on the unpatched launcher; shaped-session browser probe on the built dist: the park shape reports the failure with evidence in the log, the slow-but-alive and pre-handshake shapes raise no alarm; launcher UI regression green. Record: `docs/reports/fd77-stall-surfacing-20261101.md`; evidence `test-results/fd77-stall-surfacing/` |
| `lane-fd87` | FD-89 CLOSED (CL-43): the Godot second-wave sibling classes — capability 29 (SampledImageArrayDynamicIndexing) named and admitted exactly when its image arrays are specializer-servable (spirvfix R19 strips the stray declaration), dead descriptor declarations stripped (R18) so the captured 2,340 B module translates end to end, and FD-78's ExtInst literal position bounded in both orphan sweeps so the 369,400 B module's texel conversion runs and SampledBuffer is served; FD-88 filed (data-dependent descriptor-array indexing, translator-stack-gated); FD-78 updated (ExtInst position proven live, fixed at two sweeps) | Both modules are committed fixtures (`testdata/spirv/godot-fs-f2b738c259a79b4a.spv`, `godot-fs-f1b4e0027d98e2e4.spv`): new suite `spirvfix-fd87.test.mjs` 5/5 (small module translates; big module loses SampledBuffer, keeps FD-88's pair by name); corpus test green over all 74 modules with per-module rewrite sets re-measured (every R18 verdict independently verified dead by raw-word scan); native run-native 60 PASS incl. 3 new FD-87 guards (cap-29 admit/refuse-by-name both forms, dead descriptors, ExtInst sweep position); JS unit suite 351/0; vulkan citations 352 rows 0 drift after re-pinning through the change's line map; browser build 'Browser export verified', wasm `f66e592c`. The two pipelines' in-vivo creations remain CL-43's owed clause for the carrying deploy gate. Record: `docs/reports/fd87-godot-sibling-classes-20261102.md`; evidence `test-results/fd87/` |
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
| `lane-fd105-108` | *(Batch-4 settled numbers: fixes FD-111..114; filings FD-116..119. Row text keeps the lane's own numbers.)* FD-105: the dispatcher's recvfrom copyback scatters page by page with a writability check per piece, answering -EFAULT at the first unwritable piece with the prefix delivered; FD-106: sys_sendmsg64 imports the whole control area before validating, then walks the whole chain (malformed lengths -EINVAL, rights accumulating under the cumulative 253 cap with descriptor values resolved in walk order, credentials only at cmsg_len exactly 28, other SOL_SOCKET types -EINVAL, other levels skipped) and translates every rights header; FD-107: sys_recvmsg64's name copyback is deferred past the control translation and answers -EFAULT for an unwritable target, with the data and passed descriptors delivered and no msghdr field writebacks; FD-108: KUnixSocketObject::sendto queues a real datagram on a connected datagram pair, mirroring sendmsg's queueing tail; FD-109 (recvmsg fabricates the source name for queued messages), FD-110 (the message queue is invisible to read/recvfrom), FD-111 (named datagram delivery has no route) and FD-112 (a zero-length sendmsg datagram queues nothing) filed | New x64 tests testRecvfromBadBuffer64Syscalls, testSendmsgControlChain64Syscalls, testRecvmsgNameCopyback64Syscalls, testSendtoDatagram64Syscalls (RED on the base — recvfrom reported 16 into an unmapped buffer and 200 into a crossing one, a second rights header's descriptors never arrived and malformed tails sent silently, an unwritable name target reported success with its fields written back, datagram sendto delivered nothing; GREEN on the fix); host reference programs measured first (the chain walk rules incl. the credential length and the import/resolution order, the name-copyback field behaviour, the recvfrom crossing prefix); full native suites serial: main 668/668 and x64 77/77, 0 FAILED; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = db1bd2e22, wasm 77b9c090, staged archive c4dbe35986a5 as pinned); JS unit 348/0 (reviewed silent-failure allowance 117 -> 119). Record: `docs/reports/fd105-fd108-message-faults-20261102.md`; evidence `test-results/fd105-108/` |

| `lane-fd109-112` | *(Batch-4 settled numbers: fixes FD-116..119; filings FD-120..122. Row text keeps the lane's own numbers.)* FD-109: queued messages carry the sender's bound address (recorded at bind; abstract names resolve through a length-exact registry), recvmsg reports it with the host's length rules and the 64-bit bridge behaves accordingly; FD-110: read/recvfrom (object and native paths) drain the message queue with host datagram semantics (whole datagram per call, truncation discards, MSG_PEEK repeats) and report the queued sender; FD-111: datagram connect links the named peer (reconnect switches, AF_UNSPEC disconnects), addressed sendto/sendmsg deliver through one resolver matching the host's error matrix, and the dispatcher's bare sendto/recvfrom bounce their address arguments; FD-112: a zero-length datagram queues as a real empty message with its credentials and descriptors; FD-113 (closed pathname socket answers ENOENT instead of ECONNREFUSED), FD-114 (stream queue/buffer interleave order) and FD-115 (writev gathers into one datagram) filed; the FD-107 copyback test's fixture now uses a named sender (with an unnamed pair peer its fault scenarios fired only through FD-109's fabrication) and FD-107's own closure carries the note | New x64 tests testRecvmsgSenderName64Syscalls, testReadQueuedDatagram64Syscalls, testNamedDatagram64Syscalls, testSendmsgZeroLength64Syscalls (RED on the base — pair recvmsg reported a 128-byte fabricated name, every queued read answered -EWOULDBLOCK, addressed sendto reported sent but delivered nothing and datagram connect answered -EISCONN, the zero-length datagram never became readable; GREEN on the fix); host reference programs measured first (sender-name rules incl. abstract names and no-autobind, the named-delivery error matrix, read/peek/truncation semantics, the zero-length and descriptor cases); full native suites serial: main 668/668 and x64 81/81, 0 FAILED; JS unit at the lane's un-built-tree baseline (272 passed / 76 failed, every sampled failure a missing generated web/vendor file, matching the FD-58..60 lane's accounting on an un-bootstrapped tree; the socket-relevant silent-failure tests pass: no-silent-failure, no-silent-drops, silent-failure-scanner); browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 0523a2d85, wasm a691ae8f, staged archive c4dbe35986a5 as pinned). Record: `docs/reports/fd109-fd112-message-delivery-20261102.md`; evidence `test-results/fd109-112/` |
| `lane-fd113-115` | *(Batch-4 settled numbers: fixes FD-120..122; filings FD-123, FD-124. Row text keeps the lane's own numbers.)* FD-113: the binding socket's destructor no longer unlinks its pathname node, so a closed pathname socket answers -ECONNREFUSED (and rebind -EADDRINUSE) until a guest unlink, exactly the host's answers; FD-114: the stream receive stores keep call order — sendmsg flushes the peer's buffered bytes into the message queue ahead of its own, and the plain write paths queue behind a non-empty queue, so reads serve one seniority across both stores (the X11 wire path, whose peers never queue, is untouched); FD-115: writev gathers into a single datagram on both paths (the object writev inside its critical section, the 64-bit dispatcher validating every segment before one writeNative), an unreadable segment answering -EFAULT with nothing queued and a zero-total gather queueing nothing; FD-116 (a zero-length plain 64-bit write queues no empty datagram) and FD-117 (the unix-connect-handoff JS slice test's stub outgrown by FD-111's destructor/connect growth; verified failing on the unmodified base) filed | New x64 tests testStalePathnameSocket64Syscalls, testStreamWriteSendmsgOrder64Syscalls, testWritevDatagram64Syscalls (RED on the base — stale path sendto/connect -2 and rebind 0, every interleave served queue-first, writev arriving as two datagrams with invented bytes for a bad segment; GREEN on the fix); the FD-107/FD-109 pathname tests' fixtures unlink before re-binding, the host discipline FD-113's fix now enforces; host reference programs measured first (the stale-path answer matrix incl. EADDRINUSE, the interleave and ancillary-position cases, the gather cases incl. the atomic -EFAULT and the plain zero-length write behind FD-116); full native suites serial: main 668/668 and x64 84/84, 0 FAILED; JS unit 347 passed / 1 failed — the one failure is FD-117, pre-existing on the base; this lane's two harness touches: the reviewed silent-failure allowance for syscall64.cpp 119 -> 120 (FD-115's dispatcher gate is one counted guest-validation errno return) and the socket64-message slice anchor tightened to the helper's definition (the fix's forward declaration had become its first match); browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = b80adf804, wasm ac291689, staged archive c4dbe35986a5 as pinned). Record: `docs/reports/fd113-fd115-socket-followups-20261103.md`; evidence `test-results/fd113-115/` |
| `lane-fd116-117` | *(Batch-4 settled numbers: fixes FD-123, FD-124; filings FD-125, FD-126. Row text keeps the lane's own numbers.)* FD-116: the 64-bit dispatcher's plain write no longer returns 0 unseen on a zero count — the call runs the descriptor gates and reaches the kobject, so a zero-length write on a datagram socket queues the empty datagram the host queues, an unknown or wrong-mode descriptor answers first, and a buffer address outside the user range answers -EFAULT with nothing queued; FD-117: the unix-connect-handoff JS slice test's stub completed for FD-111's growth (the abstract registry helpers, the abstractName / abstractRegistered / oneWayPeerLink members, the named-peer resolver carrying the product's answer matrix, S32 and K_EPROTOTYPE), so the extracted destructor/connect/accept compile and the test passes again; FD-118 (SOCK_SEQPACKET unix sockets unmodelled) and FD-119 (the first datagram send after a peer close answers -ENOTCONN where the host answers -ECONNREFUSED once) filed | New x64 test testWriteZeroLength64Syscalls (RED on the base — write0 answered 0 with nothing queued on every queueing case, 0 for an out-of-range buffer, 0 for an unknown descriptor; GREEN on the fix; the closed-peer stream case flags the shared test process 64-bit as a real guest process is, the socket layer's broken-pipe split being keyed on the thread's process width); host reference program measured first (the zero-length matrix: queueing for valid/NULL/unmapped/PROT_NONE buffers, -EFAULT only out-of-range, descriptor checks before the buffer check, stream and seqpacket shapes, the peer-close answers behind FD-119); full native suites serial: main 668/668 and x64 85/85, 0 FAILED; JS unit 348 passed / 0 failed (the base's one failure was FD-117, reproduced there first); browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 76eeb0adc, wasm 2fb5b2e3, staged archive c4dbe35986a5 as pinned); the silent-failure allowance for syscall64.cpp stays 120 — the removed early return was never a counted site and the added range-check answer is in the family's reviewed shape. Record: `docs/reports/fd116-fd117-zerowrite-handoff-20261103.md`; evidence `test-results/fd116-117/` |

| `lane-fd87loss` | FD-108 closed (CL-67); FD-85's closing gate discharged on this fix: the filed 64-bit write-loss was a wrong-values defect — four x87 memory-load instructions (FLD_F32/FLD_F64/FLD_I16/FLD_I32) left the register slot's cached double authoritative, so FSIN and every other cache-honouring op computed on the slot's previous occupant; the four loads now clear the cache flag on the 80-bit path. A second filing (native test binaries crashing under default parallel workers, A/B-proven pre-existing) was folded into FD-32 at composition | Guest fixtures fpprobe.c..fpprobe21 exact on the fixed build (accumulator 184.1776; the iterated-sine loop constant on every iteration); native suites serial 0 FAILED; the shipped common-dependencies test passes unloosened on the batch-3 tip plus this fix (OPENAL: RESULT 0) and is re-proven on the composed tree by the batch-4 gate. Record: `docs/reports/fd87-x87-stale-cache-20261103.md`; evidence `test-results/fd87loss/` |
| `lane-wedge-incidence` | FD-115 filed (stays open): a first-visit 32-bit start of the shipped batch-2 build wedged before the session handshake — the winedevice-host thread looped delivering a memory fault (fault address 0xfffffd08, at least 1,024 delivered faults) and the start never surfaced a failure | Ten further first-visit starts on the same live build all certified (combined incidence 1 in 15 at filing); the batch-3 deploy's 5/5 run on the succeeding build brought the running tally to 1 in 20. Record: `docs/reports/fd103-wedge-incidence-20261102.md` |
| `lane-fd118-119` | *(Batch-5 settled numbers: fixes FD-125, FD-126; closures CL-82, CL-83; filings FD-130, FD-141, FD-142, FD-143. Row text keeps the lane's own numbers.)* FD-118: SOCK_SEQPACKET unix sockets are modelled as record sockets — socketpair accepts the type, a record predicate (datagram or seqpacket) drives the data paths so message boundaries survive end to end and an empty write or sendmsg is delivered as an empty record, connect() routes seqpacket through the mutual branch and refuses a bound socket of a different type with -EPROTOTYPE (stream clients too), an addressed send on a connected seqpacket socket ignores the address and delivers to the connected peer, and the 64-bit dispatcher's writev gather covers seqpacket so one writev is one record; FD-119: a datagram socket remembers a just-closed peer distinctly from never having connected — the pair destructor arms a one-shot refusal the send tails answer -ECONNREFUSED once then -ENOTCONN, a one-way connected socket spends the same shot lazily, receiving and addressed sends do not spend it, and the addressed send paths no longer answer a pair survivor's outClosed as a broken pipe | New x64 tests testSeqpacket64Syscalls and testDatagramPeerCloseRefusal64Syscalls (RED on the base for the filed reasons — the pair refused -EINVAL, every first send -ENOTCONN; GREEN on the fix), and the FD-116 zero-length test's closed-peer pin flipped to the same accounting; host reference programs measured first (pair/record behaviour, truncation, gather, peek, the connect type matrix, the one-shot on every send path and what does not spend it); full native suites serial: main 668/668 and x64 87/87, 0 FAILED; JS unit 348 passed / 0 failed (the unix-connect-handoff stub completed for the new constant and members); browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = cbb334643, wasm bdc64f37, staged archive c4dbe35986a5 as pinned); the silent-failure allowance for syscall64.cpp stays 120. Record: `docs/reports/fd118-fd119-seqpacket-peerclose-20261103.md`; evidence `test-results/fd118-119/` |
| `lane-fd120-123` | *(Batch-5 settled numbers: fixes FD-130, FD-141, FD-142, FD-143; closures CL-84, CL-85, CL-86, CL-87; filing FD-144. Row text keeps the lane's own numbers.)* FD-120: a drained datagram read after a peer close takes the would-block path instead of end-of-stream (the read, readNative and recvmsg wait loops exempt datagram from the inClosed end-of-stream return; stream and seqpacket survivors keep their 0); FD-121: a receive on a socket that is not connected answers per type -- -EINVAL stream (fresh, listening, or connect in flight alike), -ENOTCONN seqpacket -- at the read and readNative gates, and recvmsg gains the matching entry gate (it answered -EWOULDBLOCK or waited); FD-122: a connect to a bound but not listening socket answers -ECONNREFUSED at once and queues nothing; FD-123: an addressed send on a connected stream socket answers -EISCONN (sendto and sendmsg; a peer's death does not change it), reconciling FD-111's -EOPNOTSUPP as the unconnected shape, which stands; FD-124 (a peer-closed stream survivor's unnamed sendmsg answers -ENOTCONN where the host answers -EPIPE) filed | New x64 test testSocketStateAnswers64Syscalls (RED on the base for the filed reasons -- drained reads 0, unconnected receives -EPIPE/-EWOULDBLOCK, non-listening connects -EINPROGRESS with an acceptable pending connection, connected addressed sendto -EOPNOTSUPP and sendmsg delivering; GREEN on the fix); the FD-119 test's drained-read pin corrected to the host answer and the FD-111 guard's connected pair end corrected to -EISCONN in the reconciliation; host reference programs measured first (the drained matrix with stream/seqpacket controls, fresh/listening/in-flight receives, both connect modes, the addressed-send matrix incl. sendmsg and the peer-closed survivor); the unix-connect-handoff JS harness gained the listening member and a listening staged destination; full native suites serial: main 668/668 and x64 88/88, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = d13bad99b, wasm 10d65ac0, staged archive c4dbe35986a5 as pinned); the silent-failure allowance for syscall64.cpp stays 120. Record: `docs/reports/fd120-fd123-socket-state-answers-20261103.md`; evidence `test-results/fd120-123/` |
| `lane-fd124` | *(Batch-5 settled numbers: fix FD-144; closure CL-88; filing FD-145. Row text keeps the lane's own numbers.)* FD-124: a peer-closed stream survivor's unnamed sendmsg answers -EPIPE instead of -ENOTCONN (sendmsg's no-peer branch gains the outClosed distinction FD-118 gave seqpacket; a never-connected stream socket keeps -ENOTCONN; sendmmsg inherits through the shared path); FD-125 (the browser guest CPU self-test's four standing failures) filed | New x64 test testPeerClosedStreamSendmsg64Syscalls (RED on the base for the filed reason -- the six unnamed sendmsg/sendmmsg shapes on the survivor answer -ENOTCONN, every neighbour control already passing; GREEN on the fix); a host reference program measured first (the full send matrix around the filed shape, test-results/fd124/hostref-fd124.c); full native suites serial: main 668/668 and x64 89/89, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 118d2fc85, wasm 07d5b927, staged archive c4dbe35986a5 as pinned); the silent-failure allowance for syscall64.cpp stays 120. Record: `docs/reports/fd124-peerclosed-stream-sendmsg-20261103.md`; evidence `test-results/fd124/` |
| `lane-fd126` | *(Batch-5 settled numbers: fix FD-146; closure CL-89; filing FD-131. Row text keeps the lane's own numbers.)* FD-126: the 64-bit dispatcher's chmod and fchmodat guard a null path to -EFAULT and an empty path to -ENOENT ahead of any lookup (the shared guest-string reader maps null to "", and "" resolves to the directory node itself in the path resolver, so the null shape took the empty-path route and its answer depended on the working directory); the reader and its other consumers are untouched (execve's path already guarded; the argv/envp walk never passes the reader a null element); FD-131 (an empty path resolves to the directory node in the shared path resolver, so path calls generally answer as if the directory had been named) filed | New x64 test testChmodNullPath64Syscalls (RED on the base — the null shapes 0 or -EBADF against the host's -EFAULT, the empty shapes 0 against -ENOENT, the real-name and mode controls already passing; GREEN on the fix); a host reference program measured first (the chmod/fchmodat null and empty matrix, incl. AT_EMPTY_PATH shapes and the unopened-dirfd ordering, test-results/fd126/hostref-fd126.c); full native suites serial: main 668/668 and x64 90/90, 0 FAILED; JS unit 348 passed / 0 failed on the built tree (273 passed / 75 failed on the un-built tree first — every sampled failure a missing generated build file, the standing lane-tree pattern); browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 7ecaf95db, wasm e097e1cf, staged archive c4dbe35986a5 as pinned); the silent-failure allowance for syscall64.cpp stays 120. Record: `docs/reports/fd126-nullpath-efault-20261103.md`; evidence `test-results/fd126/` |
| `lane-fd131` | *(Batch-5 settled numbers: FD-131 closed as CL-90; filings FD-132, FD-133, FD-134, FD-135 keep their numbers.)* FD-131: an empty path answers -ENOENT at the path calls generally — the shared resolver is unchanged (internal callers pass an empty path deliberately: the zip root, empty currentDirectory arguments), so the refusal sits at the guest boundary: guards at the KProcess method entries both dispatchers funnel into (open, openat, access, chdir, chmod, fchmodat, execve, stat64, lstat64, fstatat64, statx, rmdir, mkdir, mkdirat, rename, renameat, link, symlink/symlinkat, readlink, unlinkFile, unlinkat, utimes, utimesat, utimesat64, faccessat, inotify_add_watch after its descriptor check, statfs, statfs64; the AT_EMPTY_PATH fd forms exempt at fstatat64/statx/faccessat/utimesat), at the 64-bit dispatcher sites that bypass KProcess or join the path first (sys_openat64, sys_stat_path64, sys_statx64 before dirfd resolution, sys_readlink64, the open-coded access family with faccessat2 + AT_EMPTY_PATH exempt, mkdir, symlink and link on both arguments with linkat + AT_EMPTY_PATH on the old path exempt, unlink, and the utimensat and statfs stubs for a readable empty first byte), and at the 32-bit getxattr/lgetxattr pair; FD-132 (null path erased to "" at the 32-bit reader and the dispatcher's open-coded cases), FD-133 (64-bit xattr cases never resolve the path), FD-134 (KProcess::statx substitutes the descriptor's path under AT_EMPTY_PATH) and FD-135 (64-bit statfs ignores nonexistent paths) filed | New tests testEmptyPath64Syscalls (x64) and testEmptyPathProcessApi (main), RED on the base (28 and 34 assertions failing with the base answers the audit predicted — an empty openat returning a descriptor to the working directory, stat/access/chdir of "" answering 0, mkdir "" -EEXIST, symlink with an empty target created, bad-descriptor orderings answering -EBADF where the host answers -ENOENT; every control, including the AT_EMPTY_PATH fd forms and the descriptor-first inotify/readlinkat shapes, already passing) and GREEN on the fix; host reference programs measured first (the empty-path matrix across the path calls and the AT_EMPTY_PATH/orderings compounds, test-results/fd131/hostref-fd131.c and hostref-fd131b.c); full native suites serial: main 669/669 and x64 91/91, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = a8d43046c, wasm f60de38a, staged archive c4dbe35986a5 as pinned); the known-gaps audit --check passes on the fixed tree. Record: `docs/reports/fd131-empty-path-20261103.md`; evidence `test-results/fd131/` |
| `lane-fd132` | *(Batch-5 settled numbers: FD-132 closed as CL-91; filings FD-136, FD-137 keep their numbers.)* FD-132: a null path pointer answers -EFAULT at the path calls generally — the pointer is checked on the raw guest address at each dispatcher site, ahead of the string reader on the 32-bit side (KMemory::readString returns "" for a null address) and ahead of the memcpyFromGuest copies at the 64-bit dispatcher's open-coded cases (access family, mkdir, symlink, link, rename, unlink, plus chdir and inotify_add_watch); the measured exceptions are honoured: fstatat64/statx keep the null descriptor form under AT_EMPTY_PATH (the 64-bit stat helpers already did), faccessat2/fchmodat/linkat fault null even with the flag, utimensat answers -EINVAL for null on the 32-bit side (the filing's -EFAULT corrected by measurement for that one family), inotify_add_watch keeps its descriptor-first order, and the getxattr/lgetxattr pair faults a null attribute name after the path resolves; the reader, the resolver and the process layer are unchanged; FD-136 (the 64-bit utimensat stub answers success for a null path) and FD-137 (a null memfd_create name is erased to "" on both dispatchers) filed | New tests testNullPath32Syscalls (main) and testNullPath64Syscalls (x64), RED on the base (40 and 24 assertions failing with the erased answers — the empty route's -ENOENT, -EEXIST at the create-style cases, unlink answering success, the descriptor form taken for a null pointer; every control already passing) and GREEN on the fix; host reference programs measured first (the null-path matrix incl. the AT_EMPTY_PATH compounds and the utimensat grid, test-results/fd132/hostref-fd132.c; the memfd name shapes, hostref-fd132-memfd.c); full native suites serial: main 670/670 and x64 92/92, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = a6718ffd7, wasm 650f80f9, staged archive c4dbe35986a5 as pinned); the known-gaps audit --check passes on the fixed tree. Record: `docs/reports/fd132-null-path-efault-20261103.md`; evidence `test-results/fd132/` |
| `lane-fd136-137` | *(Batch-5 settled numbers: FD-137 closed as CL-92; filing FD-138 keeps its number; FD-136 amended in place and left open by this lane.)* FD-137: a null memfd_create name answers -EFAULT on both dispatchers — the raw guest address is checked ahead of the name copy (64-bit) and the string-reader read (32-bit) before either can erase the null to the empty string, and the guards reproduce the host validation order (invalid flags answer -EINVAL first) through the flags mask the process layer now shares as KProcess::MEMFD_SUPPORTED_FLAGS; FD-136 investigated and NOT fixed: its filing host claim (-EINVAL in every shape) is the libc wrapper answer, contradicted at the raw-kernel level (per-shape matrix measured and strace-verified; the (fd, NULL, 0) shape is glibc futimens' own syscall shape and the kernel answers 0), so the entry is amended in place with corrected closing terms and stays open for the merge audit; FD-138 (memfd_create names longer than the host 249-char limit are accepted on both dispatchers) filed | New tests testMemfdNullName32Syscalls (main) and testMemfdNullName64Syscalls (x64), RED on the base (a null name created the file on both dispatchers) and GREEN on the fix; the empty name still creates; host reference programs measured first (hostref-fd137.c: null answers -EFAULT, the empty string returns a descriptor, the flags verdict precedes the name fault, the 249-char boundary; hostref-fd136.c and hostref-fd136-rawgrid.c: the raw utimensat null matrix, strace-verified); full native suites serial: main 671/671 and x64 93/93, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 0e5d33859, wasm 6e877d79, staged archive c4dbe35986a5 as pinned); the known-gaps audit --check passes. Record: `docs/reports/fd136-137-memfd-null-name-20261103.md`; evidence `test-results/fd136-137/` |
| `lane-fd136-138` | *(Batch-5 settled numbers: FD-136 closed as CL-93; FD-138 closed as CL-94; CL-91's utimensat exception reconciled; filing FD-139 keeps its number. Row text keeps the lane's own numbers.)* FD-136: utimensat with a null path answers the raw kernel's per-shape matrix on both dispatchers — the FD-132-era -EINVAL-in-every-shape guard (a glibc wrapper measurement, not a kernel one) is replaced: flag bits outside AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH answer -EINVAL first, the AT_FDCWD shape faults on the name fetch, any other flag with a null path answers -EINVAL, an unopened descriptor with flags 0 answers -EBADF, and an open descriptor is the working descriptor form (futimens' shape) and succeeds, applying the times on the 32-bit side through the new KProcess::utimensatNullPath; CL-78's utimensat exception is reconciled in place. FD-138: memfd_create refuses names past the host's 249-char limit with -EINVAL on both dispatchers via one check in KProcess::memfd_create behind the flags verdict; names at the limit still create. FD-139 (utimensat accepts undefined flag bits on non-null paths) filed | Host measured first at the raw syscall level with an inline svc (test-results/fd136-138/hostref-fd136b.c): the full null matrix including the times-copy and flags-verdict ordering, the descriptor form shown really setting the file's and directory's times, the libc cross-checks reproducing the wrapper's -EINVAL beside the kernel's 0, and the 249/250/300 memfd boundary; extended tests testNullPath32Syscalls, testNullPath64Syscalls, testMemfdNullName32Syscalls and testMemfdNullName64Syscalls RED on the base (main 669 OK + the 2 target tests failing, 14 + 2 assertions; x64 91 OK + the 2 target tests failing, 11 + 2 assertions) and GREEN on the fix; full native suites serial: main 671/671 and x64 93/93, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = 4abf51d88, wasm e4d4260b, staged archive c4dbe35986a5 as pinned); the known-gaps audit --check passes. Record: `docs/reports/fd136-138-utimensat-null-matrix-20261103.md`; evidence `test-results/fd136-138/` |
| `lane-fd125-restage` | *(Batch-5 settled numbers: FD-145 discharged as CL-95; FD-150 discharged as CL-96; filing FD-151. Row text keeps the lane's own numbers.)* FD-125 test-side discharge: the cpu64 self-test's JIT end block switches on the engine surface the run selected (engine=off asserts the all-zero record; engine-selected runs keep the original checks), the chmod case expects the layer's real -ENOENT (null path), the fchmod case expects -EBADF as staged (the settlement's first-choice open-state stagings are measured impossible inside the isolated fixture — no KThread/KProcess, openat answers -ENOSYS — so its alternative dispositions were applied); FD-130: cpu32-jit-growth's statistics assertions gated on the cpu32 section's available surface, its boot stages jit32=1 (the parameter that enables the engine it measures; jit=1 selects CPU64 only) with an enabled assertion inside the gate; FD-131 filed (Node wasm64-selftest target does not compile, pre-existing on the base) | Standard build (BUILD_INFO git_sha 0d4463b53 — the test-fix tip this register commit sits on): cpu64-selftest PASS, guest summary 953 passed / 0 failed, 'PASS: chmod → -ENOENT (null path)' and 'PASS: fchmod → -EBADF (unopened descriptor)' in the console; cpu32-jit-growth PASS; JS unit 348 passed / 0 failed. JIT build (wasm64-mt-jit, wasm 9bb606a0): cpu64-selftest with BW_TEST_EXPECT_JIT=1 PASS 953/0 ('JIT64: engine=wasm blocks=319 insns=816', 'WASM64: blocks=319 insns=816 entered=319'); cpu32-jit-growth PASS with the gate open (section measured enabled:true, retiredCounting:true, blocksEntered 105339927, instructionsRetired 452013423; the original ungated assertions pass verbatim in a scratch probe). Both dist builds 'Browser export verified' (archive c4dbe35986a5). Record: `docs/reports/fd125-fd130-test-discharge-20261103.md`; evidence `test-results/fd125-restage/` |
| `lane-fd133-135` | *(Batch-5 settled numbers: closures CL-97, CL-98, CL-99; filings FD-152, FD-140. Row text keeps the lane's own numbers.)* FD-133: the 64-bit xattr cases (188-199) resolve their target before answering — the set forms validate their flags word (-EINVAL for bits outside CREATE|REPLACE), every name-taking form faults a null name (-EFAULT, ahead of the path and descriptor verdicts), then the path is resolved (null -EFAULT; empty, missing, or — for the following forms — dangling -ENOENT; the l-forms resolve the link itself) or the descriptor is checked (-EBADF) — and only a resolved target reaches the kept stub answers (get -ENODATA, list 0, set/remove -ENOTSUP); FD-134: KProcess::statx substitutes the descriptor's path only for an empty path under AT_EMPTY_PATH, so a non-empty path stats the named path; FD-135: the 64-bit statfs case resolves its path first (null -EFAULT, unresolvable -ENOENT, resolvable keeps the fabricated statistics); FD-139 (the 64-bit fstatfs case ignores its descriptor) and FD-140 (a path through a non-directory answers -ENOENT where the host answers -ENOTDIR) filed | New tests testXattrResolve64Syscalls and testStatfsResolve64Syscalls (x64) and testStatxAtEmptyPathProcessApi (main), RED on the base (37 + 3 guest assertions and 4 process-API assertions failing with the stub/substituted answers; every control passing) and GREEN on the fix; host reference programs measured first at the raw-syscall level (test-results/fd133-135/hostref-fd133-135.c and companions: the xattr resolution matrix and its entry order — flags, name copy, resolution — the statfs matrix, the statx AT_EMPTY_PATH contract); full native suites serial: main 672/672 and x64 95/95, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha = d8ca8945a, wasm bd6816da, staged archive c4dbe35986a5 as pinned); the known-gaps audit --check passes. Record: `docs/reports/fd133-135-xattr-statfs-statx-20261103.md`; evidence `test-results/fd133-135/` |
| `lane-fd127fix` | *(Batch-5 settled numbers: FD-127 closed as CL-100; filing FD-128 keeps its number.)* FD-127 CLOSED (closure number settles at composition): socket creation took the descriptor's owning process from the ambient current thread; serviced under the unix-side dispatch binding, that process is a degenerate id-0 object whose fd-table mutex sits at linear address ~0x10, and descriptor allocation deadlocked re-acquiring it inside the fd-table handle scan - the Godot demo's device-setup thread creation (a pipe2 from Wine's NtCreateThreadEx) never returned. `ksocket` now takes the owning thread explicitly and allocates in that thread's process at all three creation sites (AF_UNIX, AF_NETLINK, AF_INET); the socketpair helper and the 32/64-bit socket handlers pass the thread they are servicing (fix `f670bd7de`; the step-ring instrument that convicted it is retained, gated on BW_THREAD_DUMP=1) | Conviction by the gated step ring across the lane's diag1-diag4 captures on the batch-4 code line: healthy allocations complete the full span on the real table (mutex at a heap address, handle returned); the frozen call's ring stops after the allocation lock on the id-0 object (mutex address bits 1, about 0x10) with the handle scan's own lock never acquired, host CPU flat (blocked, not spinning). Fixed build, same capture (diag6, tip `f6f010eb4`, BUILD_INFO dirty 0): caps 257.6 s -> first vkCreateShaderModule 261.6 s (1,216 B, hash f598618b6233f21a) -> pipelines create (32 traps) -> first window mapped 299.3 s, guest progressing; native suites serial main 675/675 and x64 95/95, 0 FAILED; JS unit 352/0 (the two harness updates the change required are in the branch); browser build 'Browser export verified'. FD-128 filed (the demo's next blocker: a worker unaligned-access trap after the window maps). Record: `docs/reports/fd127-ksocket-owner-fix-20261103.md`; evidence `test-results/fd127/` |
| `lane-fd128` | *(Batch-5 settled numbers: FD-128 closed as CL-101; filing FD-129 keeps its number.)* FD-128 CLOSED (closure number settles at composition): with FD-127 fixed, the Godot demo's servicing worker died ~1-2 s after the window mapped on an unaligned-access trap. The trapping operations are the shared_ptr release inside `BoxedWineCondition::wait` (`i32.atomic.rmw.add` on the previous `waitingCond` control-block word; profiling-build CDP stack + disassembly, capture diag-prof) and, in repeat captures, the `FaultStageScope` atomic loads in `KFile::preadNative` — both consume `KThread::currentThread()`, and on the dying worker that value had been overwritten in its TLS slot with a small integer (0x25, then 0x9; the slot's only legitimate writer, `setCurrentThread`, never carried the value — tripwire-proven; a 1 kHz slot monitor watched it change at Vulkan-call frequency during the first vkAllocateMemory/vkCreateImage burst). Identity consumers then operated on low static memory (the "waitingCond" pair landed in the static string table). `KThread::currentThread()` now validates the slot (a genuine KThread is heap-allocated: 8-aligned, above the static region) and falls back to the identity the host thread registered for itself at platform-thread start (pthread_self()-keyed registry, RAII registration in `platformThread`), healing the slot (fix `849a55412`; the slot writer itself is filed separately as FD-129) | Fixed build, same capture (diag-fix1; standard build of the committed tree, BUILD_INFO dirty 0, wasm sha256 511e3151cdb9 — byte-identical to the capture build): banner 154.4 s -> window mapped 227.4 s -> **no trap** (no exceptions file; zero "unaligned" in the full console) -> guest alive and working at the 1,200 s deadline (elapsed 1,212.3 s), passing the filed death point (window +1.0/+1.4/+2.2 s across the three unfixed captures) by ~983 s. Pipeline account toward FD-89/CL-43 (full account in the lane record): 93 shader modules created, zero refusals; 6 graphics + 68 compute pipelines; 6 vkQueueSubmit + 6 vkQueueSubmit2; 3 bridge DROPPED lines total (vs 8,386 in the FD-62-era baseline); the FD-72 helper-invocation module met in vivo at 337.6 s and refused by name under FD-75; no swapchain/present — the first present remains gated on FD-72/FD-70 per FD-55's entry, and the guest settles into the downstream shader-retry cycle that entry holds open. Native suites serial main 675/675 and x64 95/95, 0 FAILED; JS unit 352/0; browser build 'Browser export verified'. Record: `docs/reports/fd128-current-thread-identity-20261103.md`; evidence `test-results/fd128/` |
| `lane-fd129` | *(Batch-5 settled numbers: FD-129 closed as CL-102.)* FD-129 CLOSED (closure number settles at composition): the writer that overwrote guest workers' `runningThread` TLS slots with small integers during the graphics burst is the emulator's own guest-memory allocation. Instrumented pre-fix captures (branch `lane-fd129pre`, the pre-FD-128 tree) attribute it exactly: a slot registers with its genuine KThread pointer; a burst of `K64Page::commit()` calls follows, each allocating a fresh guest page's 4 KB backing buffer with `new U8[K64_PAGE_SIZE]`, and in the browser build those heap allocations land inside live host threads' stack/TLS spans — an allocation-order ring shows the page commits marching across the heap until one fresh buffer lands exactly over a live slot (buffer base = slot - 0x170, on two different victim threads in two independent captures). `commit()` then memsets the new buffer, zeroing the slot, and the guest stores ordinary data through the now-aliased page: two attributed stores via the TLB fast-path write funnel from the graphics-servicing guest thread (pid 10 tid 11) — 8 bytes of 0xffffffffffffffff onto one slot's page during vkAllocateDescriptorSets/vkUpdateDescriptorSets, and 4 bytes of 0x242424 onto the other's during vkGetBufferDeviceAddress, the same value/call pairing FD-128's lane saw — after which identity consumers read the garbage, as FD-128 measured. Fix `f94ceb4c5`: page backing buffers come from a dedicated arena (16 MB chunks obtained once and never freed; mutex-guarded free list; per-buffer provenance so each buffer returns to its own allocator; heap fallback only when the arena is disabled via BW64_PAGE_ARENA=0 or cannot grow) — an arena slot can never alias a live thread's span, and no thread span can be carved from an arena chunk, closing the class in both directions. FD-128's `currentThread()` validation is untouched and is now defence in depth, as the filing's closing terms envisaged. The memory64-backing-lifetime unit test's standalone slice was adapted to the new allocator interface (declarations moved inside the sliced header region; the test stubs the arena to its heap fallback, keeping its retain/free assertions on the real code under ASan). | Identification evidence (pre-fix tree, full slot surveillance + allocation ring): `wt-fd129pre/test-results/fd129pre/diag1` and `diag3` — diag3 final tallies REG 37, STOMP 14, WATCH 23 with the two live-slot hits above, guest alive at the 1,200 s deadline. Post-fix surveillance of the unmodified FD-128 tip was already silent (diag1 ~500 s, diag2 full 1,200 s: CORRUPT/ZERO/STOMP/WATCH/BRACKET all 0), consistent with the transient, burst-local nature of the aliasing. Decisive proof — the same Godot capture with the full diagnostic applied on top of the fix (diag commit since reverted; branch tip is the fix): `test-results/fd129/proof1` — banner 100.5 s → window mapped 152.6 s → guest alive and working at the 1,200 s deadline (elapsed 1,205.4 s); 36 slots registered; **CORRUPT 0, ZERO 0, STOMP 0, WATCH 0, BRACKET 0, SCAN 0** across the whole correlated burst (339 vkGetPhysicalDeviceFormatProperties2, 167 vkAllocateMemory/vkCreateImage-class calls). Native suites serial on the fixed tip: main 675/675 and x64 95/95, 0 FAILED (run twice — before and after the header-consistency amend — same result); JS unit 352 passed / 0 failed; browser build 'Browser export verified' (fixed wasm sha256 4453757f0699b527). No new defects filed by this lane: the only anomalies met were artefacts of the lane's own diagnostics (a monitor polling freed threads' recycled TLS) and a pre-existing infrastructure pin rotation, neither a product defect. Record: `docs/reports/fd129-page-backing-arena-20261103.md`; evidence `test-results/fd129/` and `wt-fd129pre/test-results/fd129pre/` |
| `lane-d3d11lit` | *(Batch-5 settled numbers: the D3D10/11 draws defect closed as CL-103 under composed FD-147; filing FD-153. Row text keeps the lane's own numbers.)* FD-130 CLOSED (closure number settles at composition; the defect is the D3D10/11 LIT=0 draws, filed on lane-fd125-ab's register as its lineage FD-127): the Vulkan bridge's frame-overflow guards for the image/attachment clear calls compared the raw 64-bit trap slot against their count caps, but the Vulkan counts are uint32 — only the slot's low half is the value. Measured: DXVK's init-image vkCmdClearColorImage (its only call site passes rangeCount 1; the range array read back as exactly one range) arrived with the count slot holding 0x200000000001, the guard fired vk64FrameOverflow, the flag rode the next frame's FRAME_BEGIN header, the page tier rejected the whole 4460-byte draw frame, the pending readback never completed, and the guest mapped zeroes — LIT=0 on d3d10-comprehensive, d3d11-triangle and d3d11-comprehensive, while D3D12 (which never issues the guarded calls) passed. Fix `738d882ca`: all six guards (clear colour, clear depth/stencil, clear attachments; 64- and 32-bit dispatchers) compare the (U32)-truncated count; caps and the loud refusal of genuine over-cap counts unchanged. | Base for this lane is lane-fd129 tip `ea93a6ef3`, not the bare integ tip: the first unmodified-integ reproduction was consumed by the already-filed FD-127 thread-creation freeze (console preserved), and on `ea93a6ef3` (build byte-identical to the FD-129 lane's, wasm 4453757f) d3d11-triangle fails with the full chain (overflow warning, LIT=0). Fixed build (738d882ca, wasm d3937eb9, BUILD_INFO dirty 0, 'Browser export verified'): d3d11-triangle PASS (LIT=8192, RESULT 0), d3d11-comprehensive PASS (LIT=8192; the identical 4460-byte frame renders), d3d10-comprehensive PASS (`tri10: LIT=8192`, RESULT 0), d3d12-comprehensive PASS (control). Native suites serial main 675/675 and x64 95/95, 0 FAILED; JS unit 352/0, legacy 115/0; known-gaps audit: no undeclared gaps. FD-131 filed (the drop log's two no-op shapes, found while exonerating this defect's console signals). Record: `docs/reports/d3d1011-draws-land-nothing-20261103.md`; evidence `test-results/fd127dxvk/` |
| `lane-fd153` | *(Batch-6 settled numbers: FD-153 closed as CL-104; the FD-155 filing keeps its number.)* FD-153 CLOSED (closure number settles at composition): the Vulkan bridge's drop logging reported legal no-op shapes as dropped guest commands — the vkCmdBeginRendering attachment helper's `!address` clause on a colour-only pass (both ABIs), captureStageSpec's lumped empty/over-cap guard on DXVK's per-stage default VkSpecializationInfo, resolveFrame's unbound-set-0 lookup, the 32-bit copy guards' zero-region-with-null-array case, and the VKPROF trap scans counting idle slots as drops. All five now return ahead of the guard or skip without the macro; genuine refusals (over-cap counts, claimed-but-absent arrays and data, unreadable structs, unresolvable views and set ids) stay loud, and the rest of the 257-site census was surveyed and classified genuine, unchanged. Census 257 -> 255 (the two VKPROF sites), pin updated in `vk-drop-macro-guard.test.mjs`. FD-155 filed (captureStageSpec32's silent header guard — the survey's opposite-sign find). | New native FD-153 section (five PASS lines), RED on the unmodified bridge at the colour-only drop assertion, GREEN on the fix; run-native 67 PASS; native suites serial main 679/679 and x64 105/105, 0 FAILED; browser build 'Browser export verified' (wasm 7a0f9ba4, BUILD_INFO git_sha a8720731e, staged archive c4dbe35986a5 as pinned); JS unit 352/0, legacy 115/0; citation audit 352 rows, 0 drift (46 cited lines re-pinned). Record: `docs/reports/fd153-drop-log-noop-shapes-20261104.md`; evidence `test-results/fd153/` |
| `lane-fd151` | *(Batch-6 settled numbers: FD-151 closed as CL-105; its filing is FD-156.)* FD-151: the Node wasm64-selftest target compiles again — in the single-threaded configuration BOXEDWINE_MUTEX is void* with empty lock/unlock macros, and two sites written against the multi-threaded configuration touched the type directly (faultstage.h's MemoryDemandLock, the filed site; ksocket.cpp's socketpair descriptor bracket, surfaced on the rebuild) — both now use the configuration's own idioms, multi-threaded behaviour byte-identical; the self-test's two standing failing cases were stale staging (the chmod null-path case expected the pre-FD-126 -ENOENT where the dispatcher now answers -EFAULT; the ENOSYS case included pidfd_getfd, which is implemented and answers -EBADF for a non-pidfd) and are restaged to the host-measured answers; the target's documented run instructions corrected (the .js invocation cannot run under the repo's type:module package.json). FD-155 filed (the mem64 variant's separate compile break in the SDL audio bridge). | Host reference program measured first at the raw syscall (hostref-fd151.c, aarch64 asm-generic numbers): fchmodat null -EFAULT / empty -ENOENT; pidfd_getfd non-pidfd -EBADF, invalid flags -EINVAL first. Standard build: target compiles, self-test 953 passed / 0 failed, exit 0. Speed-engine build (scripts/build-jit-wasm.sh's exact recipe, isolated build dir): compiles; 953/0 interpreted; 953/0 with emitted execution (JIT64 blocks=319 insns=816, WASM64 entered=319 rejected=0, mode table). The only output difference in JIT mode is three added METRICS telemetry lines (wall-clock cadence), characterised in the record, not a defect. Native suites serial: main 0 FAILED (679 OK lines, 382s), x64 0 FAILED (105 OK lines, 28s); JS unit 352 passed / 0 failed; browser build BUILD COMPLETE 'Browser export verified' (BUILD_INFO git_sha fae50a27e, dirty 0, wasm 2fd905f6, archive c4dbe35986a5); suite browser cpu64-selftest PASS. Record: `docs/reports/fd151-node-selftest-target-20261103.md`; evidence `test-results/fd151/` |
| `lane-fd139-152` | *(Batch-6 settled numbers: FD-152 closed as CL-106, FD-139 as CL-107, FD-140 as CL-108; filings FD-157 to FD-161.)* FD-152 CLOSED (see CL-104), FD-139 CLOSED (see CL-105), FD-140 CLOSED (see CL-106); filings FD-155, FD-156, FD-157, FD-158, FD-159 (lane numbers, provisional). FD-152: the 64-bit dispatcher's fstatfs case (138) shared the statfs helper and ignored its descriptor, answering the fabricated statistics for any descriptor; it now checks the descriptor first (host order: -EBADF ahead of even the buffer fault) and keeps the fabricated answer for any open one. FD-139: utimensat flag bits outside AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH answered -EINVAL on the host for every path shape (raw-syscall measured: the kernel validates the flags word ahead of the name fetch, after the times copy-in) but only the null-path shapes validated on the guest; the 64-bit stub hoists the verdict ahead of its path branches with the FD-136 null matrix unchanged, and KProcess::utimesat/utimesat64 validate at entry, so a refused call no longer applies the times. FD-140: Fs::getNodeFromLocalPath gains an optional notDir out-report — a miss whose stopping parent is not a directory — and the guest-boundary path calls on both dispatchers translate it to -ENOTDIR (resolver, process API and both dispatchers' own resolution sites; creation-under-a-file is refused before any node is fabricated; parent verdicts are judged on followed resolutions so symlinked directories still work); callers that do not ask are unchanged and asking callers always walk, since a cached miss cannot carry the verdict. Fix `00d8f5369` on tests `c178a704e`. | Host measured first at the raw syscall level (asm-generic numbers, strace-verified): fstatfs answers 0 for any open descriptor and -EBADF for -1, 9999 and a just-closed descriptor, with the descriptor verdict ahead of the buffer fault; the utimensat flags verdict precedes the name fetch for every shape and follows the times copy-in; -ENOTDIR is uniform across 20+ path calls for a regular-file prefix, with missing-prefix -ENOENT and symlink-to-dir controls (evidence `test-results/fd139-152/hostref-fd139-152{,b,c}.c/.out`). RED on base `7a2308cd6`: main — exactly the three new tests failing (54 assertions: utimensat rows answering 0/-ENOENT and the refused call applying the times; ENOTDIR rows answering -ENOENT and kin); x64 — exactly the three new tests failing (39 assertions, the fstatfs rows answering 0/-EFAULT among them). GREEN on the fix: native suites serial main 682/682 and x64 108/108, 0 FAILED; JS unit 352 passed / 0 failed; browser build 'Browser export verified' (BUILD_INFO git_sha `e8f120b22`, wasm sha256 c6499c3c, staged archive `c4dbe35986a5` as pinned); known-gaps audit `--check` passes. Record: `docs/reports/fd139-140-152-enotdir-utimensat-flags-fstatfs-20261104.md`; evidence `test-results/fd139-152/` |
| `lane-fd148-149` | *(Batch-6 settled numbers: FD-148 closed as CL-109, FD-149 as CL-110.)* FD-148 CLOSED + FD-149 CLOSED (closure numbers settle at composition; lane-local CL-104/CL-105): FD-148 — the emulator's Vulkan stack dropped the guest's unnormalizedCoordinates sampler flag (DXVK's D3D9 present blit samples the rendered image in texel space through a usePixelCoord sampler with CLAMP_TO_BORDER addressing), so the presented swapchain reached the compositor ~black although the clear landed exactly in its render target; fix `5ac7f7431` carries the flag from both vkCreateSampler ABIs through the bridge SAMPLER record and the wire protocol into a per-draw WGSL coordinate rewrite in the page tier (record-layout test pin `82e3ebb51`; Vulkan-register citation re-pins `57651cfea`). FD-149 — the shipped 32-bit win32u backport was configured --without-vulkan by its own build script (its sysroot carried only the font deb closure and its fail-closed markers checked only font defines), so every 32-bit session failed its Vulkan graphics-driver load and no 32-bit D3D9 capture ever became ready; fix `6e3dba081` rebuilds the module with the sha256-pinned bookworm libvulkan sysroot debs, a SONAME_LIBVULKAN fail-closed marker and a stub-string product check, restaged through regen-wine32-zip.sh into runtime-assets-20261104. | Base (integ tip `7a2308cd6`, wasm 05298a62): d3d9-comprehensive FAIL (219,863 ms; redDominant 4 vs >100000 required), d3d9-32 FAIL (CAPTURE_READY never seen in 300 s; stub error + VK_KHR_surface abort in the console). Fixed build (wasm 2eccd757, 'Browser export verified'): d3d9-comprehensive PASS (all three stages RESULT 0, redDominant 722880; re-run PASS on the final restaged tree), d3d9-32 PASS (62,685 ms, redDominant 722880, zero stub lines), controls d3d11-triangle / d3d11-comprehensive / d3d10-comprehensive / d3d12-comprehensive PASS. Native suites serial main + x64 both 0 FAILED; JS unit 353/0; runtime completeness gate 0 failures; bootstrap verifies the new retained archive (13 files). No new defects filed: the settlement run's intermittent 632x1 swapchain extent collapse was not reproduced on the current base and is recorded as observed-not-reproduced in the lane record. Record: `docs/reports/fd148-fd149-d3d9-20261104.md`; evidence `test-results/fd148-149/` |
| `lane-fd155` | *(Batch-7 settled numbers: FD-155 closed as CL-111.)* FD-155 CLOSED (closure number settles at composition): the 32-bit specialization capture's header guard silently discarded refused guest input — an unreadable VkSpecializationInfo, a mapEntryCount over the capture cap of 16, and claimed entries whose map-entry array or data is absent — where the 64-bit twin reports all three through VK64_DROP_IF. The header now mirrors the 64-bit path (fix `5bd07e141`): null pointer and empty spec return silently as legal no-ops, the three refusals drop loudly; VK64_DROP_IF census 255 -> 258, pin updated in vk-drop-macro-guard.test.mjs. | New native FD-155 section (two PASS lines), RED on the unmodified bridge at the over-cap drop assertion, GREEN on the fix; run-native 69 PASS; native suites serial main 682 OK / 0 FAILED and x64 108 OK / 0 FAILED; browser build 'Browser export verified' (wasm e0b6f721, BUILD_INFO git_sha 0dc8a0c7f, staged archive c4dbe35986a5 as pinned); JS unit 353/0, legacy 115/0; citation audit 352 rows, 0 drift (two re-pins). Record: `docs/reports/fd155-capturespec32-silent-header-20261104.md`; evidence `test-results/fd155/` |
| `lane-fd156` | *(Batch-7 settled numbers: FD-156 closed as CL-112.)* FD-156 CLOSED (closure number settles at composition): the MEMORY64 variant of the Node wasm64-selftest target compiles again -- the SDL audio bridge's bridge-arming EM_ASM call cast the ring index addresses (&bwRingWrite, &bwRingRead) to int, which a 64-bit-pointer build rejects; both now go as pointers (sig 'p', delivered to JS as the Number byte offset, the treatment the ring base at $0 already gets), byte-identical values on wasm32 hosts; the target's run instructions corrected to the .cjs form with the measured Node-version behaviour. No new defects filed. | Base (integ tip `3ea2fd97d`): build fails at platform/sdl/kdspaudio.cpp:371, the two filed cast errors in both compilations of the file, no other error site. Fixed: `make wasm64-selftest-mem64` MAKE_EXIT=0; run under official Node v24.11.0 (tarball sha256 verified against the release SHASUMS256.txt; the box's Node 22.22.3 rejects the module at instantiation -- 64-bit table limits) **953 passed, 0 failed**, exit 0. Standard variant build + run 953/0; native suites serial main 682 tests 0 FAILED, x64 108 tests 0 FAILED; JS unit 353/0, legacy 115/0; browser build 'Browser export verified' (BUILD_INFO git_sha `b4d3f2e8f`, dirty 0, wasm aba69204..., archive c4dbe35986a5). Record: `docs/reports/fd156-mem64-selftest-target-20261104.md`; evidence `test-results/fd156/` |
| `lane-fd157-161` | *(Batch-7 settled numbers: FD-157 closed as CL-113, FD-158 as CL-114, FD-159 as CL-115, FD-160 as CL-116, FD-161 as CL-117; filings FD-162 to FD-164 keep their numbers.)* *(Provisional numbers: closures settle at the next composition.)* FD-157 CLOSED + FD-158 CLOSED + FD-159 CLOSED + FD-160 CLOSED + FD-161 CLOSED (lane-local CL-111..CL-115): FD-157 -- the 32-bit dispatcher's xattr path forms resolved only for the modelled DOSATTRIB name and had no handlers at all for six of the eight forms; all eight now resolve before answering (miss -ENOENT, through a file -ENOTDIR, unresolved attribute -ENODATA; the set/remove forms keep -ENOTSUP and the list forms an empty list for a resolved target), and a null attribute name faults ahead of the path verdicts as the host's entry order measures. FD-158 -- readlink/readlinkat of a missing path answers -ENOENT on both dispatchers; a resolved non-link keeps -EINVAL. FD-159 -- a path whose final component is erased by normalization ("a.txt/", "a.txt/.", "a.txt/..") answers -ENOTDIR: the resolver judges the anchor component when the caller asks for the notDir verdict. FD-160 -- the 64-bit dispatcher's rmdir (84) gained its missing case, routed to KProcess::rmdir. FD-161 -- Fs::getParentPath of a slash-less path returns the working directory, so a hard link to a bare filename succeeds; every other consumer audited. New filings from this lane's measurements: FD-162 (the 32-bit xattr descriptor forms answer the emulator-private -EBADFD for an unopened descriptor, and flistxattr never consults its descriptor), FD-163 (an unmodelled xattr namespace answers -ENODATA where the host answers -ENOTSUP), FD-164 (the mid-path erased-.. family). | Tests `c22bc6805`, fix `7239fe69c` on base `3ea2fd97d`: RED -- the seven new guest tests fail on the base for the filed reasons (evidence `test-results/fd157-161/red-main.log`, `red-x64.log`); GREEN -- targeted tests pass and the full native suites run serially with 0 FAILED (main 686 tests, x64 111 tests); JS unit suite 353/0; browser build 'Browser export verified' on the fixed tree. Host behaviour measured first at the raw syscall level (asm-generic numbers, strace cross-checked): `test-results/fd157-161/hostref-fd157-161.out`. Record: `docs/reports/fd157-161-path-verdicts-20261104.md`; evidence `test-results/fd157-161/` |
| `lane-fd154` | *(Batch-7 settled numbers: FD-154 closed as CL-118.)* FD-154 ADJUDICATED + CLOSED: the five browser-suite failures from the batch-5 gate, each identified to a product or harness cause and fixed. winsock-http PRODUCT (fix `d26f606a1`): browser-build INET sockets are marked loopbackStream and connect() refused loopback destinations internally, never using the native transport that reaches the relay; it now falls through to the native connect when no guest listener exists. input-mouse-rel PRODUCT (fix `3cce91bcb`): both relative-motion producers advanced the sink's tracked pointer at queue time and the delivering connection added the delta again, doubling the first motion of every stream (injected 100,50 -> guest 200,100); producers no longer advance, and deliveries are reported back through the new noteDeliveredPointer(). input-fullscreen HARNESS over three PRODUCT layers: harness `e482eb6a6` keeps the page unlocked (headless grants ClipCursor's pointer lock without a gesture and clicks under lock arrive at (0,0)) and delivers the press through the injection channel; product `f610e90b2` (a GrabModeSync grab froze input at grab time), `dbe7705f3` (grab-routed events dropped as foreign-target because Wine grabs on the root window owned by another connection -- own-grab targets now deliver on the grabbing connection, and grab parameters plus every pointer-path drop reason log under BW64_XWIRE), `25471296c` (the delivery loop re-froze after the first event under a sync grab while this client never sends XAllowEvents -- sync grabs treated as async, matching the native X11 layer). jit64 HARNESS (`189f6b302`, `41de3c327`): the wineboot wait demanded `wineboot.exe--init`, a substring the space-joined argv rendering never produces; pattern fixed and the generated-code assertion gated on engine availability. jitbench HARNESS (`b99c92154`, `260545f5b`, `1d2c059d8`, `87d9e021b`): the boot never passed ?jit32 so CPU32 mode-1 could never latch, and the suite's interpreter-target build has no JIT engine to latch; assertions gate on the stats' availability report, JIT-variant builds keep the full assertions. Two xwire-pointer-grab unit contracts updated to the measured semantics (`58f759879`). | All five reproduced stock on the unmodified base `3ea2fd97d` first. Fixed build (wasm `3b3fd4f9`): winsock-http passes with relay stats accepted 1, 76 bytes up / 324 down; input-mouse-rel and input-fullscreen pass on both architectures (the guest prints the move, BUTTON UP, and the clipped 1500-count move exactly once); jit64 passes end to end in both modes; the jitbench focused run passes. JS unit 353/0; native suites serial main 682 tests 0 FAILED, x64 108 tests 0 FAILED. Whether the shipped build should switch to the JIT variant is flagged in the lane record as a build decision outside this filing. Record: `docs/reports/fd154-adjudication-20261104.md`; evidence `test-results/fd154/` |
| `lane-fd162-164` | *(Batch-8 settled numbers: FD-162 closed as CL-119, FD-163 as CL-120, FD-164 as CL-121; filing FD-165 keeps its number.)* *(Provisional numbers: closures settle at the next composition.)* FD-162 CLOSED + FD-163 CLOSED + FD-164 CLOSED (lane-local CL-119..CL-121): FD-162 -- the 32-bit xattr descriptor forms answered the emulator-private -EBADFD for an unopened descriptor and flistxattr never consulted its descriptor; the four forms now share the host's entry order (flags, name fetch, descriptor -EBADF) and flistxattr answers the empty list for an open descriptor. FD-163 -- the xattr get forms answered -ENODATA for every name on a resolved target; both dispatchers now classify the fetched name (unmodelled prefix -ENOTSUP, recognised namespace with an absent attribute -ENODATA, a bare recognised prefix -EINVAL) and the empty name answers -ERANGE at fetch time. FD-164 -- a ".." away from the end of a path was erased with its predecessor sight unseen; the resolver now judges each non-trailing ".." by resolving the path ahead of it when the notDir verdict is asked, and ".." clamps at the root. New filing from this lane's measurements: FD-165 (the set/remove forms' namespace classification, unmeasured and unmodelled). | Tests `ec62ca78e`, fixes `9b4ce67b1` + `872d1b474` on base `4006ddb58`: RED -- the five new guest tests fail on the base for the filed reasons (evidence `test-results/fd162-164/red-main.log`, `red-x64.log`); GREEN -- full native suites run serially with 0 FAILED (main 689 tests, x64 113 tests); JS unit suite 353/0; browser build 'Browser export verified' on the fixed tree (wasm `0e5fa5dee8b2`). Host behaviour measured first at the raw syscall level (asm-generic numbers, strace cross-checked): `test-results/fd162-164/hostref-fd162-164.out`, `hostref-fd163b/c/d.out`. Record: `docs/reports/fd162-164-xattr-namespaces-dotdot-20261105.md`; evidence `test-results/fd162-164/` |
| `lane-buildrel` | *(Batch-8 settled numbers: FD-31 closed as CL-122, FD-32 as CL-123, FD-33 as CL-124.)* *(Provisional numbers: closures settle at the next composition.)* FD-33 CLOSED + FD-32 CLOSED + FD-31 CLOSED (lane-local CL-119..CL-121): FD-33 -- the native release build failed to link on three symbols defined behind the 64-bit-guest gate (msgScratch, tickXWirePresent, hasMainThreadWork); the message scratch, its caches and the execve invalidation move ahead of the gate in syscall64.cpp, the kprocess invalidation call is ungated, the multi-threaded mainloop guards its wire-present calls like the single-threaded one, and the test build's msgScratch stub (returning 0, silently routing tests to a fallback) is deleted so tests link the real scratch; part (b), the filed JIT compile error, is not reproduced on the current tree (the threshold helper's outside uses already carry the guard; the configuration's full compile is error-free). FD-32/FD-31 -- multi-worker native suite runs were in-process threads and died exit 139 (main suite after 2 tests amid a runaway guest-memfault loop; 64-bit suite after 79 of 111 at the clone tests); parallel runs are now process shards, each shard running a strided slice through the serial mode's unchanged fork-per-test isolated path, output replayed in shard order, failure counts returned over a pipe, a dead shard failing all its tests loudly. | Fix `05ab65469` on base `4006ddb58`: release build RED on base (the three undefined symbols at link) -> GREEN (links, binary starts and shuts down cleanly); all five native configurations build exit 0 (release, test, x64, x64-test, x64-cache); browser build 'Browser export verified' on the same commit (wasm `7daab6b2`, BUILD_INFO dirty 0). Suites: main serial 686/0, 4 shards 686/0, default 686/0, preserved thread path exit 139 after 2; x64 serial 111/0, default shards 111/0, 4 shards 111/0 twice with one futex-race flake in a third run at load 13-17 (test 6/6 standalone; failing log preserved). All runs on the shared box, loads recorded per log. Record: `docs/reports/buildrel-fd31-32-33-20261105.md`; evidence `test-results/buildrel/` |
| `lane-sharedbuild` | *(Batch-9 settled numbers: FD-178 closed as CL-125.)* FD-178: generated payload inputs (~2 GB per tree) deduplicated into one shared pin-verified store, linked not copied; the prep helper fixed (it failed partway on a fresh tree, sourced DXVK from a stale checkout, and overwrote the tree's runtime-assets manifest with stale 20261013 pins) | Side-by-side fresh trees of integ 48d27952a: byte-identical product wasm 9b51d60101ab, both BUILD COMPLETE with the pinned archive c4dbe35986a5 verified; JS suites — old-way tree ALL TESTS PASSED (legacy exit 0, unit 353/0), new-way tree legacy exit 0 and unit 349/4 in a full run under heavier box load, the 4 failures (boot-progress-snapshot + three x11 tests, none reading a payload path) all passing on filtered re-runs in the same tree (36/0, 1/0); known-gaps audit exit 0 in both trees; new tree holds 557 real payload bytes vs old tree's 1,981,059,339; store /home/ubuntu/boxedwine64-shared 1,980,622,511 B / 18,718 files, SHA256SUMS self-check OK, 13/13 pins match. Record: `docs/reports/shared-build-store-20261105.md`; evidence `test-results/sharedbuild/` |

| `lane-fd115` | *(Batch-9 settled numbers: FD-115 closed as CL-127; FD-166 filed and closed as CL-128.)* *(Provisional numbers: closures settle at the next composition.)* FD-115 CLOSED (as CL-127) + FD-166 FILED AND CLOSED (as CL-128): the two pre-handshake shapes in which a first-visit 32-bit start could sit as an endless silent wait are now reported failures. FD-115's delivered-fault loop (a winedevice-host thread faulting over and over before the handshake): the launcher's stall watchdog counts printed delivered-fault lines per guest thread before the handshake and reports the start failed at 32 from one thread (reason=prehandshake-fault-loop); the fault's mechanism was not identified — the 2026-11-02 capture remains its only instance (1 in 54 measured shipped-build starts). FD-166, filed from this lane's live capture on shipped batch 7 (a start whose earliest processes showed min_noprogress_ms=780078 in the closing dumps, no fault lines, nothing surfaced): the watchdog also reports a pre-handshake start failed when every process in two consecutive complete dumps has made no guest progress for 300 s with no fetch in flight (reason=prehandshake-no-progress). | Fixes `de0144c72` (FD-115) + `3a4d59793` (FD-166), probes `4ce0dc424` + `b7f5d4003`, on base `4006ddb58`: unit tests RED on the base for both rules, GREEN with the fixes (stall-watchdog.test.mjs 14/14; full JS unit suite 353/0; `npm test` ALL TESTS PASSED); shaped-session probe 5/5 scenarios against the built dist (BUILD COMPLETE 'Browser export verified', git_sha `b7f5d4003`, dirty 0, wasm `c0af957a` unchanged — both fixes are launcher-JS only); first-visit x86 series: live shipped build 19 measured (18 certified, 1 FD-166 silent stall, no fault-loop recurrence), FD-115-only build 34/34 measured certified, final build 36/36 certified; 0 BW64-FAIL and 0 guest-memfault32 lines in every preserved console, 0 restarts. Record: `docs/reports/fd115-fd165-prehandshake-surfacing-20261105.md`; evidence `test-results/fd115/` |

| `lane-machine-definition` | FD-03 PARTIAL (the entry stays open): floors (c) and (d) raised — machine definition 1 GiB / 1 CPU → 4 GiB / 2 vCPUs from the single KSystem sources with every face made to agree (sysinfo both ABIs, /proc/meminfo, /proc/cpuinfo, /proc/stat, sysfs, both affinity ABIs, CPUID leaf 1), and the Vulkan 64-bit device-local heap 2 GiB → 4 GiB (the single-allocation cap honestly left at 2 GiB). Floors (a) OpenCL/CUDA and (b) AVX/AVX2 recorded as not deliverable in a browser, with the fixture pinning the CPUID AVX bit clear | Tests `a241fdc00` + `c314fd8eb` + `85880ca2c`, fixes `90a2e8ea3` + `938c684d4` (getcpu), citation re-pins `6160ca64c`, JS harness follow-up `5cc01cc9d`, on base `4006ddb58`: RED — main suite fails exactly the two new tests, run-native aborts at the new 4 GiB heap assert, guest fixture reports 5 FAILs on the raised answers; GREEN — native 686/686 + 111/111 serial, run-native 70 PASS, fixture ALL PASS (2 GiB committed and verified page by page; affinity pinned to vCPU 1 alone and read back), smoke 27/27, browser build 'Browser export verified' (wasm `ece9a0f0`), JS 353/0 + legacy 115/0 (116, one skipped), citations 352 rows 0 drift. Record: `docs/reports/fd03-machine-definition-20261105.md`; evidence `test-results/machine/` |

| `lane-dll-census` | Standing export-implementation census built (no emulator code changed): `tools/conformance/dll_export_census.py` classifies every export of the shipped system libraries as real code / forwarder / data / validated unimplemented stub (winebuild's generated stub body, counted only when the module and function name strings embedded in the body match the export exactly), with the census of the shipped batch-7 vintage committed as `tools/conformance/dll_export_baseline.json` and a `--check` ratchet (exit 0 match / 1 drift / 2 new vintage) wired into the JS unit suite. Coverage: 161,676 named exports across 1,171 DLL tables — 127,920 (79.1%) provided, 33,756 (20.9%) validated stubs; no demo program imports a stub on its static load path. Filed FD-167 (40 demo-reachable stub pairs), FD-168 (win32u family, 2,037), FD-169 (CRT family, 10,698), FD-170 (90 whole-DLL-absence tables, 6,734), FD-171 (long tail, 14,287, + the standing guard). | Census self-test GREEN on synthetic x86-64/i386 PEs; `--check` exact match against the gate-archive runtime dir AND against the live deployed runtime dir; tampered baseline fails exit 1 naming DLL+function, swapped wine zip exits 2. Detector validation on the shipped set: 34,990 pattern hits, 0 unvalidated. JS: legacy 115/0 (1 skip); unit suite complete detached run 353 files passed / 1 failed (provenance-artifacts — requires a built dist/ this tree never had, fails identically unbuilt at base); census file PASS in-suite with the real check active (7.2 s) and 3/3 standalone. Record: `docs/reports/dll-export-census-20261105.md`; evidence `test-results/dll-export-census/` |
| `lane-fd167` | FD-167 disposition — the 40 demo-reachable stub pairs: 27 implemented with real behaviour (Wine 10.0 i386 bodies: bluetoothapis GATT ×8 and bthprops.cpl ×3 under the documented no-radio contract, winusb ×8 under the no-device contract, mf MFCreateDeviceSource and wevtapi EvtRender upstream bodies verbatim, mfplat MFCreateMediaBufferWrapper as a genuine offset/length window over an inner buffer, propsys VariantCompare as the real comparison; Wine 8.0 x86-64 backports: msvcp140 _Throw_C_error, _XGetLastError, _Xbad_function_call from upstream master, setupapi SetupDiGetDeviceInterfaceAlias as a genuine implementation). The 12 esent Jet pairs are proven unreachable in practice (sole caller is Chromium's legacy-Edge profile importer; the shipped demo has no such profile and no import path) and recorded as such. The one remaining pair, msvcp140 _Throw_Cpp_error, has no authoritative semantics in any source and is refiled as FD-172 rather than guessed. Both runtime pipelines gained pins plus checker pins; the retained archive was re-cut as runtime-assets-20261105-fd167 and the manifest re-pinned. Record: `docs/reports/fd167-demo-reachable-stub-contracts-20261105.md`. | Commits 620720f7f, e2d6142ec, 81b45ae65, c8bf5ca89, 152c7cc5b. Runtime-completeness gate 0 failures / 1 skip; census named stubs 33,756 → 33,726, with 27 of 28 implemented pairs resolved in the demo's architecture; JS unit suite 354 passed / 0 failed; browser build "Browser export verified"; 32-bit guest contract fixture PASS (`test-results/fd167/green-run.log`). Open proof item, stated plainly: the 64-bit guest fixture produced no verdict — across a sixteen-attempt requeue loop plus earlier runs it was blocked by probe-slot contention from the batch-8 gate chain and, in the slot-acquiring attempts, by the x86-64 session wedging during boot before the fixture started (thirteen preserved guest consoles); the RED run against the pre-change zips was likewise never captured. Both are owed to the batch-9 gate on a quieter box; the four x86-64 pairs carry build-level proof meanwhile (build export check, objdump at shipped ordinals 623/642/647/345, census resolution). Evidence: `test-results/fd167/`. |

| `lane-fd165` | *(Batch-9 settled numbers: FD-165 closed as CL-129.)* *(Provisional numbers: closures settle at the next composition.)* FD-165 CLOSED (lane-local CL-129): FD-165 -- the set/remove xattr forms answered their standing -ENOTSUP for every name on a resolved target, beyond the one modelled name; both dispatchers now classify names as the host does (unknown family and system.* outside the posix_acl names -ENOTSUP, a bare recognised prefix -EINVAL, a posix_acl set -EINVAL and now log-reported, a posix_acl remove on a file or directory a no-op success, on the link itself user.* -EPERM), the set forms fault on a null value ahead of the target verdicts, and recognised namespaces are stored as native sidecars and read back through the get/list forms with CREATE/REPLACE verdicts. | Tests `09bd3f452`, fixes `a2899c5b5` + `37dba7b69` on base `c4dd96ecd`: RED -- the two new guest tests and the three updated standing tests fail on the base for the filed reasons (evidence `test-results/fd165/red-main.log`, `red-x64.log`); GREEN -- full native suites run serially with 0 FAILED (main 690 tests, x64 114 tests); JS unit suite 353/0; browser build 'Browser export verified' on the fixed tree (wasm `521937b979`). Host behaviour measured first at the raw syscall level (asm-generic numbers, strace cross-checked): `test-results/fd165/hostref-fd165.out`, `hostref-fd165b/c/d.out`. Record: `docs/reports/fd165-xattr-write-classification-20261105.md`; evidence `test-results/fd165/` |

| `lane-audit-l1l2` | *(Batch-9 settled numbers: FD-19 closed as CL-130; filings FD-179, FD-180, FD-181.)* *(Provisional numbers: FD-19 closed as CL-130 — lane-fd162-164 claims CL-130..CL-121 on its branch; filings FD-179, FD-180, FD-181 keep their numbers if free at composition.)* FD-19 CLOSED as DISPROVEN: the 64-bit guest shim resolves the promoted queries' KHR spellings to real alias wrappers that forward to the core wrappers, so a 64-bit guest's KHR call traps under the core fn id and is served; the 32-bit shim traps the KHR id itself, which is why only the 32-bit dispatcher carries the paired cases. New filings: FD-179 (the proc-address lookup returns different addresses for a KHR spelling and its core spelling — both served, answers identical), FD-180 (a 32-bit benchmark boot died in a delivered-fault loop — 65 guest-memfault32 lines at one eip, printed count to 1024 — and the host fault path recursed to a JavaScript stack overflow; the batch-7 gate's one-off 'memory access out of bounds' trap on the same fixture cross-referenced, identity unproven), FD-181 (a 64-bit benchmark guest spawned into a healthy session produced no output in 180 s, its only thread parked in a socket-condition wait, 3 of 3 attempts under recorded heavy load; the gate's focused run passed the fixture, so intermittent). | FD-19: new GUARD section in the native bridge test (`64e7e3c0e`) calling through the shim-returned KHR pointers — core trap id pinned, answers byte-identical to the core names for all three query pairs; run-native 70 PASS, exit 0. L1: bounded repetition series on a build whose wasm `c0af957a` is byte-identical to the batch-7 gate build — x86 mix: 7 clean boots, then the FD-180 fault-loop boot; x64 mix: 3 stalled first-boots (FD-181). Native suites serial main 686 tests 0 FAILED, x64 111 tests 0 FAILED; JS unit 353/0; browser build 'Browser export verified'. Record: `docs/reports/audit-followup-l1-l2-20261105.md`; evidence `test-results/audit-l1l2/`, `test-results/audit-l1/`, `test-results/suite-audit-l1*/` |

| `lane-vkhost-gen` | FD-28 CLOSED as CL-137 (provisional; settles at the next composition): the Vulkan host bridge's generator now emits every implementation that used to be a hand-written edit in the generated vk_host.cpp — init-failure propagation, map-failure handling, push-descriptor-template marshaling, the log-once graceful degrades for eight unsupported functions, and the log-once-and-skip unknown-pNext defaults — so regenerating the bridge reproduces the shipped files exactly. The next-pointer part of FD-27 is discharged by the same commit; FD-27's two union types remain open. | Fix commit `26a90c5b6` on base `faf95d26c`. Baseline regeneration on the base changed vk_host.cpp by 66 insertions / 69 deletions (the divergence); with the fixed generator all six outputs (vk_host.cpp, vk_host.h, vk_host_marshal.cpp, vk_host_marshal.h, vk.c, vkdef.h) regenerate sha256-identical to the committed blobs, a second run identical again, no function routed to the not-implemented marshal, and the regenerated vk_host.cpp passes g++ -fsyntax-only with the project flags. Checking-queue pipeline 18, submitted at the fix commit, was voided by the concurrent-submission mount race filed as FD-194 (its verify-source mounted the commit of a sibling lane; pipelines 19-21 died the same way), and a re-submission (pipeline 28) was still queued behind the submission flood when this lane closed; the light checks were therefore run by hand on this tree with the exact commands the pipeline runs: citations exit 0 (352 rows, 0 drift), JS unit 352 passed / 2 failed (only the two known bare-checkout artifacts: provenance-artifacts, runtime-completeness), JS legacy 115/0. The native suites were not re-run by hand because the change touches no compiled source (generator tooling only; every generated output byte-identical to the base). Record: `docs/reports/fd28-vkhost-generator-20261106.md`; evidence `test-results/fd28/` |

| `lane-fd168` | FD-168 first draw-down (all section-A numbers provisional until the batch-9 audit settles them; no CL claimed — FD-168 stays open). Fourteen x86-64 win32u placeholders became real syscall implementations in the Wine 8 backports build (`tools/buildWine/patches/wine8-win32u-fd168-backports.patch`: spec lines to the syscall shape, syscall-table entries, unix-side bodies ported from the newest upstream source and adapted to this vintage's server protocol): NtUserAlterWindowStyle, NtUserBuildHimcList, NtUserBuildNameList, NtUserBuildPropList, NtUserGetClipCursor, NtUserGetCurrentInputMessageSource, NtUserGetPointerType, NtUserGetThreadState, NtUserQueryDisplayConfig, NtUserQueryWindow, NtUserRegisterWindowMessage, NtGdiGetMiterLimit, NtGdiSetMiterLimit, NtGdiIcmBrushInfo. Runtime restaged with re-pinned hashes (checker + README), retained runtime-assets archive rebuilt and re-pinned, census baseline regenerated in the same change: x64 win32u 926 -> 912, i386 1,111 unchanged, FD-168 total 2,037 -> 2,023, shipped named stubs 33,756 -> 33,742, only x64/win32u.dll moved across 1,171 tables. Remaining 2,023 carry recorded per-family dispositions in the FD-168 entry (31 D3DKMT-layer-only, 6 blocked on absent server/driver/state machinery, 8 upstream FIXME semi-stubs not faked, 1 upstream-removed misspelling, 866 never implemented upstream, i386 vendor-build disposition). | Guest test `win32u-fd168` PASS — fixture `tools/guest-fixtures/win32u-fd168.c`, 17 checks, every check ok=1 and RESULT 0 in the run's preserved console log; the first run (16/17) caught the miter-limit pair using Wine 8's stale FLOAT parameter form — rewritten to Wine 10's DWORD bit-pattern form and re-proven 17/17. Census `--check` exit 0; completeness checker + `bootstrap.py --check` 0 failures; dist build "Browser export verified". JS unit suite 354 passed / 0 failed on the final tree; legacy 115/0. Record: `docs/reports/fd168-win32u-drawdown-20261105.md`; evidence `test-results/suite-win32u-fd168/` |

| `lane-fd170` | *(Batch-9 settled numbers: FD-170 discharged as CL-131.)* FD-170 DISCHARGED (provisional CL-131; settles at the batch-9 composition): a recorded per-DLL decision for all 50 whole-DLL-absence DLLs (90 tables, 6,734 named stubs). Implemented on the x64 side via `tools/buildWine/patches/wine8-fd170-contracts.patch` in the Wine 8.0 backport pipeline (built by `tools/rootfs64/build-wine8-backports.sh`, staged by `stage-wine8-backports.sh` / `build-wine64-zip.sh`, hash-pinned fail-closed in `tools/rootfs64/check_runtime_completeness.py`): bluetoothapis 95→71 named stubs (Wine 10.0's radio/SDP code backported + the FD-167 no-radio/no-device contract bodies, with the headers they need), winusb 21→13 (the FD-167 no-device contract bodies), netutils 22→17 (Wine 10.0's `NetApiBuffer*` heap-buffer functions). The other 47 DLLs are accepted as documented absences, each with its concrete missing substrate recorded in the FD-170 entry and the lane record. | Census run against the built binaries differs from the base baseline in exactly those three tables (33,756 → 33,719 named stubs total; FD-170 remainder 6,697); baseline regenerated in the same change, `--check` exit 0. Runtime assets re-cut as `runtime-assets-20261105-fd170`; `bootstrap.py --check` passes (13 files verified, completeness 0 failures). JS unit suite 352 passed / 2 failed — both failures (provenance-artifacts, binary-patch-registry) attributable to the borrowed batch-8 gate dist this lane tested against, not to the change; the census guard test passes. Guest fixture `web/tests/suite/browser/fd170-contracts64.test.mjs` committed; its verdict is owed to the batch-9 gate — every attempt on 2026-11-05/06 died in the shared probe queue or in a guest boot that outlasted the hung-probe cap at load 13–19. Record: `docs/reports/fd170-whole-dll-decisions-20261105.md`; evidence `test-results/fd170/` |

| `lane-incremental-build` | *(Batch-9 settled numbers: FD-173 closed as CL-126; FD-174 fixed.)* *(Provisional numbers: FD-173 closed as CL-126, FD-174 fixed in the same commit; both settle at the next composition.)* FD-173 CLOSED: a fresh working copy's first build compiled all ~270 files from scratch even though builds are byte-for-byte reproducible and an already-built copy is incremental (0 files recompiled with no change, exactly 1 after a one-file change, both measured on the base). The shared store now holds per-commit build seeds; the prep step verifies and installs a seed into fresh copies of that commit, and a publish script stores a seed from any verified clean build. FD-174 FIXED in the same commit: the prep step's payload directory symlinks showed as untracked files (the .gitignore entries were trailing-slash forms, which do not match symlinks) and inflated BUILD_INFO's git_dirty_files; the same paths are now ignored in both forms. | Commit `1dae2e9d5` on base `48d27952a`. Proof (every build's program file sha256 `9b51d60101ab`): fresh unseeded copy, first build 270 files, 2,491 s wall on the shared box; seeded fresh copy + one-file change: 213 s, exactly 1 file recompiled; same seeded tree from scratch: 2,345 s, all 270 files, byte-identical program file, BUILD_INFO git_dirty_files 2 (was 14 for the same tree state before the FD-174 fix); JS unit suite 353/0 and legacy suite 115/0 (1 skipped) on the seeded copy's incremental output; a seedless commit preps exactly as before (note printed, exit 0, no build directory). Record: `docs/reports/incremental-build-seed-20261105.md`; evidence `test-results/incbuild/` |

| `lane-pch` | *(Batch-9 settled numbers: filings FD-184, FD-185.)* Build speed (improvement, no FD closed): both project makefiles precompile the shared `boxedwine.h` chain once per build directory instead of re-parsing it in every one of the 330 project translation units that include it - clang `-include-pch` (project/emscripten/makefile), GCC `.gch` (project/linux/makefile). The precompiled set is computed by membership (project sources under source/ and platform/ only, minus the six sources that do not include the header); third-party sources under lib/ never see it. Generated dependency files make every header in the chain a prerequisite of the precompiled header, so editing one rebuilds it and all consumers. FD-184 and FD-185 filed from this lane's measurements (cpu.h two-letter macros poison later asmjit header inclusions; the Emscripten `test` target does not compile on the base - six multi-threaded-only test units in a non-multi-threaded target, pre-existing). | Base `48d27952a`, code `73e0232cd`. Clean-build compiler CPU (user time from `time`, shared box, loads recorded in the logs): wasm64-mt 853.6s -> 406.2s (-52%), native Test 812.9s -> 405.5s (-50%), native X64Test 943.1s -> 530.4s (-44%). Outputs byte-identical in all three arms (wasm `9b51d60101ab`, JS glue `9fe550765f`, Test binary `fcb1c3d959`, X64Test binary `2ab4830b0c` - same hashes before and after). Native suites serial on the precompiled-header binaries: main 689/0, x64 113/0. JS unit 353/0; legacy 115/0 (1 skip). Full `tools/bw64-build.sh` run on the committed tip produced dist with BUILD_INFO git_sha `73e0232cd` and wasm `9b51d60101ab` (its dirty count 13 is exactly the shared-store prep symlinks; zero tracked modifications). Dependency wiring proven by dry runs: a no-op build schedules 0 compiles; touching one chain header schedules exactly 1 precompiled-header rebuild plus all 189 of its consumers in the wasm64-mt configuration. Emscripten `test` target census: every consumer compiles; the only failures are FD-185's six, identical without the change. Record: `docs/reports/pch-build-20261105.md`; evidence `test-results/pch/` |

| `lane-buildcache` | *(Batch-9 settled numbers: FD-186 closed as CL-132.)* *(Provisional numbers: FD-186 closed as CL-132; both settle at the next composition.)* FD-186 CLOSED: compiles in both project makefiles now route through ccache when it is installed — one cache directory shared by every working copy on the box, keys relativised to each tree's root so identical files in different copies hit; `BW64_CCACHE=0` disables, command-line CC/CXX still win, a missing ccache changes nothing. The `winbox-wt-prep.sh` step creates the shared directory and asserts its size cap (2.0 GB) + compression, so new copies get it automatically. | Proof on base `48d27952a`, loads recorded per run in the record: browser build uncached 1,434 s; cold cache (emptied first) 1,084 s with 270 misses / 0 hits and BUILD COMPLETE; warm same tree 182 s with 270/270 hits; warm FRESH tree's first build 175 s with 270/270 hits; the product wasm byte-identical in all four runs (sha256 `9b51d60101ab…`, 5,688,949 B — the figure the shared-build-store lane measured independently for this tip). Native release: uncached 747 s, cold 543 s (377 misses), warm 3 s (377/377 hits); cached native binaries identical to each other, differing from an uncached build only in embedded source-path annotation strings (relativised by the cache; object-level diff in the record) — code identical. Suites on the cache-built tree: native serial main 689 tests 0 FAILED, x64 0 FAILED, JS unit 353/0. Record: `docs/reports/build-cache-20261105.md`; evidence `test-results/buildcache/` |
| `lane-fd03` | *(Provisional number: FD-03 closed as CL-134; settles at the next composition — sibling lanes also claim CL-134 provisionally.)* FD-03 CLOSED (remaining terms): floors (a) OpenCL/CUDA and (b) AVX/AVX2 proven not deliverable in the browser target by a layer-by-layer audit at the base tip — for (a), the shipped set carries Wine's opencl thunk pair and the Khronos loader but no vendor ICD (no .icd file, no registry vendor key, no emulator-source implementation) and no CUDA binary at all; for (b), the CPU's SIMD state is 128-bit only, XGETBV is hardwired to the legacy x87+SSE mask with XSETBV unimplemented, CPUID leaf 7 is all zeros, and neither engine decodes VEX. No product code changed. | Record commit `3bf935b48` on base `faf95d26c`. Evidence: the audit's listings, readelf output, and source citations are quoted in the record from the pinned payload set (retained archive `winbox-runtime-assets-20261106`) and the source at the base tip; the negatives are pinned by `tools/testdata/machine_definition_check.c` (CPUID AVX bit clear) and `cpu64SelfTest.cpp` Test 66 (XGETBV(0) = 0x3); checking-queue pipeline #17 ran the light checks at the base tip. Record: `docs/reports/fd03-remaining-terms-20261106.md` |

| `lane-fd176` | *(Batch-10 settled numbers: FD-176 closed as CL-134; FD-193 filed.)* FD-176 CLOSED as CL-134 (lane-local number; settles at the next composition): the block-invalidation differential gate links again — the script's invalidate-test link now carries the kernel core set (fsopennode, fsnode, kthread, ksystem, kinotify, kscheduler, synchronization, bfile, platform/linux, with sdl2-config flags) and the three harness stubs that stood in for the unlinked kernel are removed (one, `useF64 = true`, also held the wrong value for the 64-bit guest, whose product value is false). Second defect in the same gate, fixed in the same commit: as scripted the differential was vacuous — both binaries interpreted, since tryBlockStep is gated on jit64Enabled, fast dispatch defaults on, and the engine defaults to interp — so the blocks binary now runs with `BW64_JIT=1 BW64_JIT_ENGINE=off BW64_JIT_FAST_DISPATCH=0`, proven by probe to reach tryBlockStep and by sabotage to fail T1/T3/T4 with stale values. FD-193 filed from this lane (BW64_BLOCKSTATS prints garbage from a dangling CPU pointer; diagnostic only). | Commit `5bec2fa3e` on base `faf95d26c`. Full `./scripts/build-jit-wasm.sh --selftest-only` exit 0: jit64 phase-1 873/0; gen emitters byte-identical; invalidate interp+blocks pass=54 fail=0, logs byte-identical; wasm fixture tests 314/0. Sensitivity re-verified on the new recipe (bw64NoteWriteSlow neutered: T1/T3/T4 fail with stale values, pass=17 fail=37). The emcc phases were not re-run (the change does not touch the emcc recipe). Record: `docs/reports/fd176-invalidate-gate-link-20261106.md` |

| `lane-fd189` | *(Batch-10 settled numbers: FD-189 closed as CL-135; FD-194 filed.)* *(Provisional numbers: FD-189 closed as CL-134; settles at the next composition.)* FD-189 CLOSED: the shipped 64-bit prefix now registers the PlugPlay service and the ROOT\WINE bus devices the way the 32-bit prefix does — the fragment `tools/rootfs64/prefix64-reg.txt` carries the seven PnP sections verbatim from the 32-bit prefix's system.reg, and the shipped prefix guard (`tools/rootfs64/prepare_prefixes.py`) now requires them on win64 too. The old leniency is how this shipped green: the guard only required a missing service registration on the native-boot packaging path and the Enum keys only for win32/full-init, so the shipped `check` path passed a prefix with no PnP stack at all. Runtime assets re-pinned to `runtime-assets-20261106-fd189` (only prefix64.zip changed). | Fix commit `44ab5edcf` on base `faf95d26c`. Proof: checker RED on the unfixed set (the new FD-189 check names all five missing pieces) and the fd189-services64 browser fixture RED on the base build (three OpenService 1060s + empty root enumeration, RESULT 4), both GREEN after the fix — the 64-bit fixture reports all three services present and both bus devices enumerating (RESULT 0) and the 32-bit control stays green; the fixed browser build's wasm is byte-identical to the baseline (payload-only change); the completeness checker passes 0 failures / 0 skips and `bootstrap.py --check` verifies 13 files; the citation audit is 352 rows, 0 drift; JS checks ALL TESTS PASSED (legacy exit 0, unit 354/0); native suites serial main 692 tests 0 FAILED, x64 116 tests 0 FAILED; run-native 71 PASS, 0 FAIL. The services are registered and present but do not reach running on 64-bit — the PlugPlay start failure is filed as FD-194 (provisional) and FD-188's entry is annotated with the 64-bit driver-load signature. Record: `docs/reports/fd189-plugplay-prefix64-20261106.md`; evidence `test-results/fd189/` |

| `lane-fd188` | *(Batch-10 settled numbers: FD-188's driver-load layer closed as CL-137; FD-195 filed.)* FD-188 FIXED on the driver-load layer (provisional CL-134 — COLLISION: the fd189 lane also claimed provisional CL-134 on its branch; settled as CL-137 at the batch-10 composition): Wine's driver loader opens every driver by the absolute path `C:\windows\system32\drivers\<name>.sys`, but the shipped prefix redirects system32 to the Wine lib dir, which carried the .sys files and no `drivers/` subdirectory — every driver load failed STATUS_DLL_NOT_FOUND (c0000135) and ZwLoadDriver failed c0000142. Fix mirrors every .sys file into a `drivers/` subdirectory in both wine zips (wine32: 20, wine64: 17) and ships the five wine.inf [InfFiles] driver .inf files in the wine zips' `share/wine/` + prefix32's `C:\windows\inf\`, so setupapi can install a function driver for an enumerated WINEBUS HID device. Guards: completeness checker `t_drivers_dir` + `t_driver_inf` tests; prefix validation requires the five .inf files in the 32-bit prefix. Re-pinned as `runtime-assets-20261107-fd188b`; shared store refreshed to the new pins. NOT closed on its literal terms: the FD-24 witness still sees no controller — the guest enumerates no HID device at all, filed as FD-195 (winebus device discovery; witness proof vehicle). | Fix commit `7b0c2a50b` on base `faf95d26c`. Regression probe `fd188-driver-probe` PASS twice (runs 9, 10): no c0000135/c0000142 lines, all four boot drivers init done, drivers dir lists 22 entries (the full mirror), WineBus service state 4. Browser build 'Browser export verified', wasm byte-identical to the pre-fix build (`92b36180…`). Queue pipeline 16 on the fix commit: citations 0 drift; js-unit 352 passed / 2 failed — the same two known bare-checkout artifacts as pipeline 11 (`provenance-artifacts`, `runtime-completeness`); js-legacy 0 fail; native main 692 tests 0 FAILED, x64 116 tests 0 FAILED; run-native all PASS. Record: `docs/reports/fd188-drivers-initialise-20261107.md`; evidence `test-results/fd188/` |
| `lane-x86-start-fault` | FD-201 FIXED (closure number settles at the next composition): the occasional dead 32-bit start. `KSystem::guestRamInfo` iterated the process table with no lock while `addProcess`/`eraseProcess` mutate it under `processesCond`; an insert crossing the table's load-factor limit makes BHashTable replace its whole slot array and free the old one (rehash), so a sysinfo scan racing a process start could keep reading the freed array and dereference a wild KProcess pointer — the first field load (`process->memory64`) traps and kills the calling worker. Fix: `guestRamInfo` and `processCount` hold `processesCond` for the table access; the 32-bit sysinfo reads the count through `processCount`; a source guard in the guest-topology test pins both locks. | Fix commit `5602c1dd5` on base `334c70be4`. Identification: the FD-169 lane's faulted 32-bit suite run (shipped-identical wasm `92b36180…`) preserved the full wasm stack — `sysinfo` → `guestRamInfo`, trap on the `process->memory64` load — matched field-for-field against the source (full analysis in the lane record's resolution addendum). Guard red/green in the native suite: the topology test fails on the unfixed source with the FD-201 message and passes with the fix. Browser proof: fixed build (BUILD_INFO git_sha `94a2970c6` + the fix, wasm `4be34516…`, 'Browser export verified') — two series of 12 consecutive 32-bit starts, 24/24 started, zero fault occurrences, zero CDP-captured exceptions (`test-results/x86-start-fault/repro-fixed-1/`, `repro-fixed-2/`); control series on the live unfixed build the same day 12/12 clean (bursty incidence; preserved instances are the positive control). Record: `docs/reports/fd201-x86-start-fault-20261107.md` (resolution addendum) |

| `lane-fd193` | *(Provisional number: FD-193 closed as CL-135; settles at the next composition.)* FD-193 CLOSED: the BW64_BLOCKSTATS exit reporter no longer dereferences a CPU that may already be destroyed — a CPU that allocates a block table under the flag joins a process-wide registry, folds its counters into process-wide totals on destruction, and the once-registered reporter prints the folded totals plus the counters of any CPUs still alive at exit. | Fix commit `f5fac0447` on base `faf95d26c`. RED on the base: four identical garbage lines (coverage=5070359463874500.0%) from the four-context block-invalidation harness. GREEN: a single line, identical across runs — built=7 execs=7 insnsInBlocks=13 totalInsns=14 coverage=92.9% — with totalInsns matching the harness's own independent instruction total exactly; interp/blocks differential byte-identical (pass=54 fail=0); flag unset prints nothing; CPU-lifetime gate PASS with block execution off and on; jit64 phase-1 tests 873/0. Record: `docs/reports/fd193-blockstats-reporter-20261107.md`; evidence `test-results/fd193/` |
| `lane-x11-getramptr-audit` | *(Lane-local numbers: FD-193 closed as CL-137; both settle at the next composition.)* FD-193 CLOSED: audit of all 19 `getRamPtr` call sites in the window-system bridge (`source/x11`; none in `source/x11wire`) — 12 received guest-controlled addresses/lengths and dereferenced the result unchecked (getRamPtr returns null for unmapped, read-only or page-straddling spans, so such a guest struct crashed the host); guest fields now read through the checked accessor, `XSetWindowAttributes::get` falls back to its checked read on refusal, and `ChangeWindowAttributes` guards the null-attributes case. The 7 remaining sites pass emulator-allocated addresses and are safe by construction (Appendix). | Fix commit `cd030f389` on base `faf95d26c`. Proof: four new native tests drive the real int 0x99 handlers with page-straddling structs in a forked child — unfixed: all four fail (refusals logged at the exact straddle addresses; children wedged on the null deref, killed after budget); fixed: all four pass (logs `test-results/x11-getramptr-audit/red-601..604.log`, `green-601..604.log`). Record: `docs/reports/x11-getramptr-audit-20261106.md` |

| `lane-d278` | *(Provisional numbers: FD-09 closed as CL-134, FD-193 as CL-135, FD-194 as CL-136 — all lane-local. FD-193 is also claimed on `lane-fd176` for an unrelated filing, FD-194 in the batch-10 settlement for the PlugPlay filing, and CL-134 on the batch-10 branches; every number settles at the next composition.)* FD-09 CLOSED (frontier review D8): `modify_ldt` answers the host's measured matrix instead of panicking — byte count other than one descriptor -EINVAL, unreadable descriptor pointer -EFAULT, out-of-range index -EINVAL, each rejection one counted `BW64-FAIL syscall.modify_ldt` line. FD-193 FILED + CLOSED (frontier review D2): futex operations on an unmapped word answer the host's measured matrix (-EFAULT for every operation except a 64-bit private WAKE / WAKE_BITSET, which returns 0) instead of the 32-bit panic, the null-page park, and the 64-bit phantom page; a zero bitset mask is -EINVAL on both word sizes. FD-194 FILED + CLOSED (frontier review D7): divide quotient overflow delivers SIGFPE / FPE_INTDIV / trap 0 and x87 exceptions deliver through the SIGFPE slot with trap 16, both as the host does; INTO via SIGSEGV trap 4 pinned unchanged. Fix commit `f998e2e01` on base `faf95d26c`. | Host reference programs measured first (raw futex and modify_ldt syscalls, raw signal delivery; sources and outputs in `test-results/d278/hostref/`). Six new native tests (main 614-618, x64 6) fail on the base — the shared-futex and modify_ldt index cases with the filed panic text — and pass with the fix, the rejections visible as counted BW64-FAIL lines in the green logs; control test 613 passes on the base and with the fix. Checking queue: pipeline #27 verified the source mount on the exact tip but was invalidated when a concurrent submission replaced the shared source tree mid-run, and a further attempt was stood down at 5.6G free disk — the queue verdicts are owed to a rerun (see the record's Checks section). Record: `docs/reports/d278-futex-modifyldt-fpe-20261107.md`; evidence `test-results/d278/` |
| `lane-ditems` | *(Provisional numbers: FD-11 closed as CL-137, FD-14 closed as CL-138, FD-37 closed as CL-139, FD-195 filed and closed as CL-140, FD-196 filed and closed as CL-141; all settle at the next composition.)* The frontier review's five remaining D-items, worked as one lane: D16 = FD-11 (the empty-separator split guard moved inside `BString::split` itself), D11 = FD-14 (`raiseSyncFault` logs its own deliveries at the delivery point; the 32-bit core's two recover-gate sites confirmed in context, no change needed), D21 = FD-37 (fresh export of the repository's only stash to `evidence/stash-export-20261106/`; the stash was not dropped), D5 filed as FD-195 (caller audit of the no-throw segment-fault entry points: the invalid page's accessors silently dropped writes after an on-demand mapping — fixed to re-dispatch, with a loud log for a write that still cannot land; all other call sites proven sound), D17 filed as FD-196 (`translateOpenError` widened from 4 errnos to 25 now that the filesystem layer preserves the host errno). | Commits `5dc0b8513` (tests) + `14783353f` (fixes) on base `faf95d26c`. Proof: four new/extended native tests, each RED on the unpatched code and GREEN after — `testBStringSplit` (RED = hang, timeout-killed), `testTranslateOpenError` (28 rows), `testFaultDelivery32` additions (write/read-back through the on-demand-mapped window), `testRaiseSyncFaultDeliveryLogged64` (the emitted `BW64-FAIL sync-fault64 ... delivered=1` line is in the GREEN log) — restored-tree GREEN re-confirmed for all four; full suites via the checking queue on the submitted branch. Evidence `test-results/ditems/`; record `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md` |
| `lane-fd26` | *(Lane-local numbers: filings FD-195, FD-196; FD-26 stays open.)* FD-26 (partial): the fast engine compiles the scalar SSE moves (MOVSD / MOVSS / MOVQ load) and the packed FP logic ops (AND / ANDN / OR / XOR, PS and PD forms) instead of falling back block-wide (see the FD-26 progress note). FD-195 filed: the section-14.3 invalidation gate does not link at the tip (pre-existing, baseline-identical). FD-196 filed: a guest `%.3f` double print goes to ~1e66 garbage after the first print in a process. | Guest bench (sseprobe64, warm medians, modes 0,1,1,0): fast-engine scalar loop 967/416 ms before -> 34/40 ms after (interpreter 408/489 -> 293/433 in the same runs), logic loop 357/330 -> 82/34 (interpreter 246/219 -> 221/205); battery + loop checksums identical across modes and builds. Native jit64 tests 919/0 (base 873/0); generated emitters byte-identical; wasm fixtures 323/0 (base 314/0) incl. a 500-iteration differential fuzz against the interpreter model and denied-access fault tests; sabotage (new kinds decoded as UNKNOWN) turns exactly the 16 new checks red (903/16). JS unit 353 passed / 1 failed (provenance-artifacts - no dist in the tree at run time; passes once the build exists). Invalidation gate not runnable at the tip (FD-195). Record: `docs/reports/fd26-scalar-logic-fastpath-20261107.md`; evidence `test-results/suite-sseprobe-bench/`, `test-results/fd26/` |

| `lane-unfiled4` | *(Lane-local numbers: FD-193 closed as CL-137, FD-194 as CL-138, FD-195 as CL-139, FD-196 as CL-140; FD-197 and FD-198 filed open.)* Four characterised-but-unfiled gaps filed and fixed: FD-193 (vectored-read segment lengths judged as the ABI signed size in all four validation paths; combined transfers capped at MAX_RW_COUNT 0x7ffff000 in all four walks), FD-194 (positioned reads and writes on unseekable objects answer -ESPIPE immediately after descriptor resolution on both cores, the file-position mode delegating to the plain vectored call after validation; two measured write corners left as stated boundaries in the entry), FD-195 (32-bit linkat implemented and wired at slot 303, flags validated ahead of the path fault), FD-196 (a per-trap metrics hook feeds the emission gate from both Vulkan bridges; schema unchanged, committed fixture byte-identical). | Host contracts measured at the raw syscall level (native aarch64 and aarch32 compat binaries) before any fix; new native tests testUnfiledGaps32 (main suite) and testUnfiledGaps64 (x64 suite): RED on the base (29 failing assertions in the main suite; the x64 run cannot complete on the unfixed code - its positioned read consumes pipe bytes the test then waits for), GREEN with the fix; metrics selftest extended (trap-only windows emit) and passing. Evidence `test-results/unfiled4/`; record `docs/reports/fd193-196-unfiled-gaps-20261107.md` |
| `lane-fd39-residue` | FD-39 CLOSED as CL-137 (lane-local number; settles at the next composition): the effect-only residue (sampling effects of LOD bias, border color, texture swizzle, depth-texture mode) is proven unfixable as texture/sampler state in either shipped backend -- the WebGPU sampler descriptor has no such fields (its texture-view swizzle is behind an optional device feature the backend never requests) and the legacy route's WebGL2/ES 3.0 host defines none of the pnames; swizzle on the legacy route alone is expressible and was deliberately not taken (the default route still could not express it; rendering would differ by route). Correction in the same lane: CLAMP_TO_BORDER never reached a real WebGPU sampler (no such address mode exists); the backend now decodes it to clamp-to-edge with a once-log, raw mode retained. | Fix commit `8258111c6` on base `faf95d26c`. Proof: gl-fd39-texparams red on the old sampler mapping ('clamp-to-border' actual vs 'clamp-to-edge' expected) and green on the fix, raw-mode retention asserted; neighbouring GL/Vulkan sampler tests 21/21; evidence in `test-results/fd39-residue/`. Record: `docs/reports/fd39-residue-20261107.md` |

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
| `lane-timing-waits` | *(Lane-local numbers FD-193..FD-222; composition settles them.)* FD-193..FD-211 FIXED: the timing/wait family measured against the raw-syscall host grid — 64-bit clock_gettime per-clock domains and id validation; clock_getres on all three implementations; remaining-time write-back on interrupted sleeps; 32-bit clock_nanosleep TIMER_ABSTIME; 64-bit clock_nanosleep flags/clock domains; getrusage validation and real CPU times; itimer validation order both widths; wait4 options validation; 64-bit alarm; 64-bit times tick rate; 32-bit gettimeofday null-tv; epoll create/create1 validation both widths; epoll_wait maxevents; poll POLLNVAL counting; negative-timespec parking in pselect6/ppoll (+sigmask copy-in validation); pselect6/ppoll timeout write-back (corrects the FD-23-residue register note); timerfd_settime flag mask; select-core nfds boundary; FD-211 the X64Headless build break at the tip (networkutils U64 macro). FD-212..FD-222 FILED: waitid unimplemented; timer-expiry signals never delivered (FD-213); wait4 stop-report consumes the child; futex stub path in the ELF vehicle; PROT_NONE not consulted on syscall accesses; ITIMER_VIRTUAL/PROF refused; i386 pause/rt_sigtimedwait/ppoll/epoll_pwait gaps; guest death at epoll_ctl DEL; POSIX timers absent; model-limit residue; epoll_pwait sigsetsize | Host reference programs measured first (raw syscall grid, 243 + 11 + 11 rows incl. strace-verified clock_nanosleep flags and the timerfd 0x3 flag mask); new tests testTimingWaits32/testTimingWaits64 plus corrected standing pins in testTimerfd and testSyscallCensus64; full native suites serial: main 693/693 and x64 117/117, 0 FAILED; guest probes pre/post fix in `test-results/timing-waits/` (hostref grids, guestprobe A-F runs, suite logs). Record: `docs/reports/timing-waits-20261107.md`; evidence `test-results/timing-waits/` |

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

- **CL-69 — The 64-bit recvfrom copyback
  answers for unwritable guest memory:
  the dispatcher branch scatters the
  received bytes page by page, validating
  each page piece before its copy, and
  answers -EFAULT at the first unwritable
  piece with the prefix already copied,
  instead of dropping the bytes silently
  through memcpyToGuest while reporting
  them delivered (FD-105).** *(Provisional
  number, filed on lane-fd105-108,
  2026-11-02 — CL-53..CL-55 are this
  register's previous lane closures and
  other lanes hold colliding claims; the
  merge lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd105-108`, commits `39d7761c4`
  (test) and `73d2ddc2f` (fix). Measured
  first
  (`test-results/fd105-108/hostref-name-fd107.c`
  N5/N6 and `hostref-n6-fd105.c`, host
  kernel 6.17.0-1020-oracle): an
  unwritable buffer answers -EFAULT, a
  crossing buffer faults with the
  writable prefix landed, a datagram is
  consumed. Consumption follows FD-104's
  settled shape — the object receive
  consumes into scratch with no pushback,
  so a faulted plain receive consumes on
  a stream socket too (the host leaves
  the data queued; MSG_PEEK consumes
  nothing on either side and is pinned),
  and in the 46-writable-byte crossing
  geometry the host leaves 40 bytes to
  the scatter's 46 (its copy engine drops
  the final partial block) — answer and
  consumption match, only the sub-page
  prefix length on a faulted call
  differs. Proof: new test
  testRecvfromBadBuffer64Syscalls RED on
  the base (16 into an unmapped buffer,
  16 for a peeking receive, 200 into a
  crossing buffer) and GREEN on the fix;
  full native suites serial — main
  668/668, x64 77/77; browser build
  verified (BUILD_INFO git_sha =
  `db1bd2e22`, wasm 77b9c090, staged
  archive c4dbe35986a5 as pinned); JS
  unit 348/0. Record:
  `docs/reports/fd105-fd108-message-faults-20261102.md`.

- **CL-70 — The 64-bit sendmsg validates
  the whole control chain the way the
  host does: the entire control area is
  imported first (an unreadable byte
  anywhere in msg_controllen answers
  -EFAULT ahead of every chain verdict),
  then headers are walked at 8-aligned
  steps while at least 16 bytes remain —
  a walked header under 16 or past the
  remaining room answers -EINVAL,
  SCM_RIGHTS headers accumulate under
  one cumulative 253 cap with their
  descriptor values resolved as the walk
  reaches each header (a bad value
  answers -EBADF ahead of a later
  header's malformed length),
  SCM_CREDENTIALS is accepted only at
  cmsg_len exactly 28, any other
  SOL_SOCKET type answers -EINVAL, and
  other levels are skipped; translation
  emits every rights header's
  descriptors (FD-106).** *(Provisional
  number, filed on lane-fd105-108,
  2026-11-02 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd105-108`, commits
  `39d7761c4` (test), `73d2ddc2f` (fix)
  and `eb57da3d9` (slice surface).
  Measured first
  (`test-results/fd105-108/hostref-chain-fd106.c`,
  `hostref-chain2-fd106.c`,
  `hostref-order-fd106.c`): sub-header
  slack is ignored, two rights headers
  deliver both descriptor sets on stream
  and datagram pairs, 200 + 100
  descriptors answer -EINVAL while
  200 + 53 deliver, credentials at 20 /
  24 / 32 answer -EINVAL, and the iovlen
  cap precedes the control import.
  FD-102's first-header validation is
  subsumed; its test passes unmodified.
  Proof: new test
  testSendmsgControlChain64Syscalls RED
  on the base (a second header's
  descriptors dropped, malformed tails
  and over-cap chains sent silently,
  the import order inverted) and GREEN
  on the fix; suites and build as
  CL-69. Record:
  `docs/reports/fd105-fd108-message-faults-20261102.md`.

- **CL-71 — sys_recvmsg64's source-name
  copyback answers for unwritable guest
  memory: the copyback is deferred past
  the control translation and the name
  range must land whole, else the call
  answers -EFAULT with the received data
  and any passed descriptor still
  delivered and none of the msghdr's
  namelen, flags or controllen fields
  written back (FD-107).** *(Provisional
  number, filed on lane-fd105-108,
  2026-11-02 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd105-108`, commits
  `39d7761c4` (test) and `73d2ddc2f`
  (fix). Measured first
  (`test-results/fd105-108/hostref-name-fd107.c`
  N1/N2): the fault leaves the msghdr
  fields exactly as the guest set them
  while the descriptor is installed.
  Proof: new test
  testRecvmsgNameCopyback64Syscalls RED
  on the base (success reported with the
  address dropped and the fields written
  back) and GREEN on the fix; suites and
  build as CL-69. Fixture note from
  lane-fd109-112: the test's sender is now
  a socket bound to a pathname (connected
  through FD-111's datagram connect)
  instead of an unnamed socketpair peer —
  with an unnamed sender the reported
  length on the base was the offered
  capacity only through FD-109's
  fabrication, and once FD-109 is fixed
  the copyback has no bytes to land and
  the fault scenarios cannot fire; the
  host faults only when a name is
  actually copied, and FD-107's own host
  reference used a named sender. Filed
  from this lane:
  FD-109, FD-110, FD-111, FD-112. Record:
  `docs/reports/fd105-fd108-message-faults-20261102.md`.

- **CL-72 — A datagram sent with sendto
  on a connected unix socketpair is
  actually delivered: KUnixSocketObject::sendto
  queues one datagram on the peer,
  mirroring sendmsg's queueing tail —
  the peer-closed answer, the socket
  filter (a filtered packet still
  reports its submitted length), one
  length-prefixed segment, the queue,
  signal and readiness notification —
  instead of routing to the stream write
  path that discards the payload while
  reporting it sent (FD-108).**
  *(Provisional number, filed on
  lane-fd105-108, 2026-11-02 — the merge
  lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd105-108`, commits `39d7761c4`
  (test) and `73d2ddc2f` (fix). Proof:
  new test testSendtoDatagram64Syscalls
  RED on the base (nothing arrived after
  a sendto reporting its full length)
  and GREEN on the fix — one datagram
  whole and exactly once, two in order
  with their boundaries, an oversized
  datagram truncating with the remainder
  discarded, a zero-length datagram
  delivered as a real empty message (its
  sendmsg twin is FD-112), MSG_DONTWAIT
  accepted, the stream-pair guard
  unchanged; suites and build as CL-69.
  Record:
  `docs/reports/fd105-fd108-message-faults-20261102.md`.

- **CL-73 — A queued message's source is
  reported as the sender the host reports:
  every queued message carries the address
  its sender was bound to (recorded at
  bind; pathname names as the literal path
  plus its NUL, abstract names through a
  length-exact registry), recvmsg reports
  that address with the host's length
  rules — the real length even when the
  guest's buffer truncates the copy, 0 for
  an unnamed sender — and the stream-buffer
  fallback reports the connected peer's
  bound address the same way.** *(Provisional
  number, filed on lane-fd105-108,
  2026-11-02 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd109-112`, commits
  `901efb53a` (test), `3a04d7682` (fix)
  and `0523a2d85` (fixture/compatibility
  follow-ups) — see
  `docs/reports/fd109-fd112-message-delivery-20261102.md`.
  Host behaviour measured first
  (`test-results/fd109-112/hostref-names-fd109.c`):
  pathname and abstract length rules, no
  autobind for an unbound sender, the pair
  peer reports 0, a relative-path bind is
  reported literally, a zero-length sender
  address cannot be replied to. Proof:
  new test testRecvmsgSenderName64Syscalls
  RED on the base (a socketpair peer
  reported a 128-byte fabricated name;
  a named sender's recvmsg reported the
  offered 128 instead of its real length)
  and GREEN on the fix; full native suites
  serial: main 668/668 and x64 81/81,
  0 FAILED; browser build as the lane
  record states.

- **CL-74 — Data queued as a message is
  visible to the plain read paths: read
  and recvfrom (the object path and the
  64-bit native path alike) drain the
  message queue with the host's datagram
  semantics — one whole datagram per call
  with its segments concatenated, a small
  buffer truncating and discarding the
  rest, MSG_PEEK repeating without
  consuming, an empty datagram consumed
  by a read returning 0 — and recvfrom
  reports the queued message's
  sender.** *(Provisional number, filed
  on lane-fd105-108, 2026-11-02 — the
  merge lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd109-112`, commits as CL-73.
  Host behaviour measured first
  (`test-results/fd109-112/hostref-read-fd110.c`):
  the read/recvfrom/peek/truncation matrix
  above, read() on a stream pair serving
  sendmsg bytes, and the one measured
  divergence kept as FD-114 (stream
  write/sendmsg call order). One measured
  compatibility rule is kept from the
  base: recvfrom from an unnamed connected
  STREAM peer reports family + length 2
  (host-measured,
  `test-results/fd109-112/hostref-stream-addr.c`;
  the main suite's testKernelGrace pins
  it) while a datagram peer reports 0.
  Proof: new test
  testReadQueuedDatagram64Syscalls RED on
  the base (every queued read answered
  -EWOULDBLOCK) and GREEN on the fix;
  suites and build as CL-73.

- **CL-75 — Datagrams addressed to a named
  peer actually arrive: datagram connect
  links the peer object (it returns 0,
  re-connect switches the peer, an
  AF_UNSPEC connect disconnects,
  getpeername reports the peer's name),
  and sendto/sendmsg with a destination
  deliver through one resolver that
  matches the host's answers — ENOENT for
  a missing pathname, ECONNREFUSED for a
  node that is not a live socket of a
  matching type's peer, EPROTOTYPE for a
  type mismatch, EOPNOTSUPP for an
  addressed stream send, ENOTCONN for an
  unaddressed unconnected send — while
  the dispatcher's bare sendto/recvfrom
  now bounce their address arguments at
  all.** *(Provisional number, filed on
  lane-fd105-108, 2026-11-02 — the merge
  lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd109-112`, commits as CL-73.
  Host behaviour measured first
  (`test-results/fd109-112/hostref-connect-fd111.c`,
  `hostref-tail-fd111.c`,
  `hostref-unspec-fd111.c`): the connect,
  addressed-send and error matrix above.
  Two measured host behaviours are NOT
  matched and are filed: FD-113 (a closed
  pathname socket's node answers
  ECONNREFUSED on the host; the emulator
  unlinks at close and answers ENOENT).
  Proof: new test
  testNamedDatagram64Syscalls RED on the
  base (addressed sendto reported its
  length with nothing delivered, connect
  answered -EISCONN, unconnected sends
  answered -EDESTADDRREQ) and GREEN on
  the fix; suites and build as CL-73.

- **CL-76 — A zero-length datagram is
  delivered as a real empty message:
  sendmsg's queueing tail no longer reads
  the filter's 0 for an empty packet as
  'filtered', so the empty message queues
  with its credentials and any passed
  descriptors, the receiver becomes
  readable, and recvmsg returns 0 for
  it; an empty datagram ahead of a real
  one is consumed in order; a zero-length
  stream sendmsg still queues
  nothing.** *(Provisional number, filed
  on lane-fd105-108, 2026-11-02 — the
  merge lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd109-112`, commits as CL-73.
  Host behaviour measured first (the
  zero-length cases of
  `test-results/fd109-112/hostref-names-fd109.c`
  and `hostref-read-fd110.c`): the empty
  datagram readable with recvmsg 0 and
  flags 0, descriptors delivered with an
  empty datagram, the stream guard.
  Proof: new test
  testSendmsgZeroLength64Syscalls RED on
  the base (the receiver never became
  readable; recvmsg answered
  -EWOULDBLOCK) and GREEN on the fix;
  suites and build as CL-73. Filed from
  this lane: FD-113, FD-114, FD-115.

- **CL-77 — A closed pathname socket
  answers like the host: the binding
  socket's destructor no longer unlinks
  its filesystem node, so the node
  outlives the socket; addressed sends
  and connects find the node with its
  socket gone and answer -ECONNREFUSED
  through the FD-111 resolver, a bind
  over the node answers -EADDRINUSE, and
  only an explicit guest unlink removes
  it (after which the answer is
  -ENOENT).** *(Provisional number, filed
  on lane-fd109-112, 2026-11-03 — the
  merge lane's audit settles the final
  number.)* Fixed on branch
  `lane-fd113-115`, commits `8550a9823`
  (tests), `1fcfe3018` (fix) and
  `b80adf804` (fixture corrections). Host
  behaviour measured first
  (`test-results/fd113-115/hostref-fd113.c`):
  sendto, datagram connect and stream
  connect to the stale path all
  -ECONNREFUSED, bind over it
  -EADDRINUSE, after unlink -ENOENT, and
  a rebind after unlink delivers. Proof:
  new test testStalePathnameSocket64Syscalls
  RED on the base (sendto -2, connect -2,
  bind over the stale node 0) and GREEN
  on the fix; the FD-107 and FD-109
  pathname tests, whose fixtures re-bind
  one path across cases, unlink before
  binding now — the host discipline the
  fix enforces. Suites and build as
  CL-79. Filed from this lane: FD-116,
  FD-117.

- **CL-78 — Stream bytes keep call order
  across the two receive stores: once
  the message queue holds anything it is
  the ordered store — sendmsg first
  moves the peer's buffered bytes into
  the queue ahead of its own message,
  and the plain write paths (object
  write, writeNative, the native nowait
  write) queue their bytes as a
  payload-only message whenever the
  queue is non-empty, buffering exactly
  as before when it is empty. The read
  paths are unchanged; queue-then-buffer
  is now a true seniority order, a read
  can span a flushed chunk and the
  message behind it, and ancillary data
  still arrives with the first byte of
  its carrying chunk.** *(Provisional
  number, filed on lane-fd109-112,
  2026-11-03 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd113-115`, commits as
  CL-77. Host behaviour measured first
  (`test-results/fd113-115/hostref-fd114.c`):
  write-then-sendmsg reads "ZW12",
  sendmsg-then-write "W12Z", a four-way
  interleave "ABCD", the descriptor
  arriving with its chunk's first byte,
  and a plain read over a carrying chunk
  discarding the descriptor. Proof: new
  test testStreamWriteSendmsgOrder64Syscalls
  RED on the base (every interleave
  served the queue first, and no read
  spanned the stores) and GREEN on the
  fix. Suites and build as CL-79. Filed
  from this lane: FD-116, FD-117.

- **CL-79 — A gathered write queues one
  datagram: both writev paths gather
  before queueing. The object writev
  collects its segments into one packet
  inside its critical section and queues
  once through the datagram send tail
  factored out of internal_write
  (sendDatagramToPeer); the 64-bit
  dispatcher, which cannot use the
  object writev, validates every segment
  with the FD-101 readable-prefix helper
  and makes one writeNative of the
  gathered packet. An unreadable segment
  answers -EFAULT with nothing queued,
  zero-length segments are skipped
  without touching their base, a gather
  totalling zero queues nothing, and
  /dev/log keeps its print behaviour on
  the gathered bytes.** *(Provisional
  number, filed on lane-fd109-112,
  2026-11-03 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd113-115`, commits as
  CL-77. Host behaviour measured first
  (`test-results/fd113-115/hostref-fd115.c`):
  two segments arrive as one datagram
  "ABCD", an empty middle segment
  changes nothing, the all-zero and
  zero-count gathers queue nothing, the
  bad-segment gather answers -EFAULT
  atomically. Proof: new test
  testWritevDatagram64Syscalls RED on
  the base (two datagrams, "AB" then
  "CD"; the bad-segment gather queued
  invented bytes) and GREEN on the fix.
  Suites: full native suites serial,
  main 668/668 and x64 84/84, 0 FAILED;
  JS unit 347 passed / 1 failed, the one
  failure FD-117 (pre-existing on the
  base `963272707`, verified there);
  browser build BUILD COMPLETE, "Browser
  export verified" (BUILD_INFO git_sha
  `b80adf804`, wasm sha256 `ac291689`,
  staged dependency archive `c4dbe35986a5`
  as pinned). Filed from this lane:
  FD-123, FD-124.

- **CL-80 — A zero-length plain write
  takes the common path: sys_write64 no
  longer returns 0 on a zero count before
  the descriptor is resolved. The call
  stages nothing, runs the standing gates
  (an unknown descriptor -EBADF, a
  wrong-mode one this family's -EINVAL),
  answers -EFAULT for a buffer address
  outside the user range with nothing
  queued, and reaches the kobject, whose
  writeNative queues a real empty
  datagram on a datagram socket and
  answers 0 with nothing queued on every
  other object — the same empty datagram
  the 32-bit write and sendmsg (FD-112)
  already queue.** *(Provisional number,
  filed on lane-fd113-115, 2026-11-03 —
  the merge lane's audit settles the
  final number.)* Fixed on branch
  `lane-fd116-117`, commits `7c6ce04ae`
  (test), `b9050125e` (the test's 64-bit
  flag for the closed-peer stream case)
  and `76eeb0adc` (fix). Host behaviour
  measured first
  (`test-results/fd116-117/hostref-fd116.c`):
  queueing for valid, NULL, unmapped and
  PROT_NONE buffers; -EFAULT only for an
  address outside the user range; the
  descriptor checks ahead of the buffer
  check; -ENOTCONN on an unconnected
  socket; a stream socket answering 0
  with nothing queued and -EPIPE once
  its peer is gone; a datagram peer
  close answered -ECONNREFUSED once,
  then -ENOTCONN (the emulator's standing
  answer for that shape is -ENOTCONN for
  every count — pinned by the test, filed
  as FD-119). Proof: new test
  testWriteZeroLength64Syscalls RED on
  the base (write0 0 with the peer's
  recv at -EWOULDBLOCK on every queueing
  case, 0 for the out-of-range buffer, 0
  for an unknown descriptor) and GREEN
  on the fix. Suites: full native suites
  serial, main 668/668 and x64 85/85, 0
  FAILED; JS unit 348 passed / 0 failed;
  browser build BUILD COMPLETE, "Browser
  export verified" (BUILD_INFO git_sha
  `76eeb0adc`, wasm sha256 `2fb5b2e3`,
  staged dependency archive
  `c4dbe35986a5` as pinned). Filed from
  this lane: FD-125, FD-126.

- **CL-81 — The unix-connect-handoff
  slice test's stub covers the current
  kunixsocket.cpp: the harness mirrors
  the abstract registry helpers and the
  members FD-111 added (abstractName,
  abstractRegistered, oneWayPeerLink),
  declares and defines the named-peer
  resolver with the product's answer
  matrix (an abstract lookup with the
  type split, pathname resolution
  through the filesystem node,
  -EINVAL / -ENOENT / -ECONNREFUSED /
  -EPROTOTYPE in the product's order),
  and adds the S32 alias and
  K_EPROTOTYPE the extracted connect
  uses, so the extracted destructor,
  connect and accept compile again and
  the test's cleanup and accept
  assertions run.** *(Provisional
  number, filed on lane-fd113-115,
  2026-11-03 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd116-117`, commit
  `7c6ce04ae` (the harness completion
  rides the FD-116 test commit). No
  product code changed. Proof: the
  failure first reproduced on the
  unmodified base — the generated
  check.cpp fails with ten errors, the
  missing registry members and helpers,
  oneWayPeerLink, resolveNamedPeer and
  S32 (`test-results/fd116-117/js-handoff-base.log`);
  with the completed stub the test
  passes standalone and in the full JS
  unit suite, 348 passed / 0 failed
  against the base's 347 passed / 1
  failed, the one failure this entry.
  Suites and build as CL-80. Filed from
  this lane: FD-125, FD-126.

- **CL-67 — The writes were never lost:
  an x87 register-cache defect computed
  the wrong values (FD-108), and FD-85's
  closure stands on its fix.** *(Settled
  at the batch-4 composition; the closure
  was written by lane-fd87loss under a
  provisional number.)* Fixed on branch
  `lane-fd87loss`, commit `dd6a6855d`
  (composed as `d5b024272`):
  `FPU::FLD_F32`, `FLD_F64`, `FLD_I16`
  and `FLD_I32` now clear
  `isRegCached[slot]` on the 80-bit
  path, so a load replaces the slot's
  cached double instead of leaving the
  previous occupant authoritative for
  FSIN and every other cache-honouring
  op. The filed memory-loss mechanism
  was disproven first by a page-level
  store/read trace: every store landed
  and every read returned what was
  stored; the stored values themselves
  were already wrong. Proof (lane record
  `docs/reports/fd87-x87-stale-cache-20261103.md`;
  evidence `test-results/fd87loss/`):
  fpprobe6's accumulator reads exactly
  184.1776, fpprobe7's post-loop arrays
  hold the true values, fpprobe21's
  iterated-sine loop returns
  0.47942553860420301 on every
  iteration; native suites run serially
  at 0 FAILED; and FD-85's closing term
  — the shipped common-dependencies
  test passing unloosened — was verified
  on the batch-3 integration tip plus
  this fix in a scratch tree (OPENAL:
  RESULT 0) and is re-proven on the
  composed tree by the batch-4 gate
  (see CL-68).

- **CL-68 — FD-85 closed: with the x87
  stale cache fixed (CL-67), the
  common-dependencies audio stage passes
  unloosened and the tone is heard.**
  *(Settled at the batch-4 composition;
  this closure is written by the merge
  lane on the composed tree's own gate
  evidence, per the composition terms.)*
  FD-85's entry records the full
  conviction: every payload layer
  exonerated, the samples lost because
  they were computed from a stale x87
  register slot (FD-108). Proof on the
  composed tree (batch-4 gate, evidence
  `test-results/merge-batch4-20261103/`):
  browser build from the composed tip,
  'Browser export verified', wasm
  c274128e; the common-dependencies
  browser test passes as shipped — no
  loosened threshold — with OPENAL:
  RESULT 0 in its console and 0
  BW64-FAIL lines; native suites run
  serially at 0 FAILED; JS unit 352
  passed / 0 failed; JS legacy 115
  passed / 0 failed. The same test on
  the batch-3 tip plus the fix, in a
  scratch tree, had already passed the
  same way (lane-fd87loss); this closure
  rests on the composed tree's run.

- **CL-82 — SOCK_SEQPACKET unix sockets
  are modelled as the record sockets
  they are: socketpair accepts the type;
  a record predicate (datagram or
  seqpacket) drives the data paths --
  plain write, the writev gather in the
  object and in the 64-bit dispatcher,
  the native write, one-message reads
  and queue copies, sendmsg's
  empty-message queueing — so message
  boundaries are preserved end to end
  and a zero-length write or sendmsg is
  delivered as an empty record;
  connect() routes seqpacket through
  the mutual branch (the X11 wire-server
  shortcut stays stream-only) and
  refuses a bound socket of a different
  type with -EPROTOTYPE, for stream
  clients too; an addressed send on a
  connected seqpacket socket ignores
  the address and delivers to the
  connected peer; sends with no peer
  answer -ENOTCONN, or -EPIPE once the
  write side is closed.** *(Provisional
  number, filed on lane-fd118-119,
  2026-11-03 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd118-119`, commits
  `e29b91cc2` (tests), `07dbfd12a`
  (fix), `29e91057c` (the record
  predicate defined out-of-line, a
  build fix), `635249dfe` (the
  unix-connect-handoff stub completion)
  and `ea69b4a06` with `cbb334643`
  (the dispatcher's writev gather; a
  test address-staging correction).
  Host behaviour measured first
  (`test-results/fd118-119/hostref-fd118-119.c`,
  `hostref-fd118-119b.c`,
  `hostref-fd118-119c.c`): pair
  creation, boundary preservation,
  truncation discarding the rest of a
  record, empty records, writev and
  sendmsg gathering, MSG_PEEK, an
  unnamed recvmsg source, shutdown and
  peer-close answers, named
  bind/listen/connect/accept, the
  connect type matrix, and addressed
  sends ignoring the address on a
  connected socket. Proof: new test
  testSeqpacket64Syscalls RED on the
  base (the pair refused -EINVAL; a
  singly created socket took the stream
  paths) and GREEN on the fix. Suites: full native suites serial, main 668/668 and x64 87/87, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE, "Browser export verified" (BUILD_INFO git_sha `cbb334643`, wasm sha256 `bdc64f37`, staged dependency archive `c4dbe35986a5` as pinned); the silent-failure allowance for syscall64.cpp stays 120 — no counted site added or removed. Filed from this lane: FD-130, FD-141, FD-142, FD-143.

- **CL-83 — A datagram socket
  remembers a just-closed peer
  distinctly from never having
  connected: the pair destructor arms
  a one-shot refusal flag on the
  survivor, and every no-peer send tail
  — plain write and the writev gather
  through sendDatagramToPeer, the
  native write, sendto, sendmsg --
  answers through one accounting,
  -ECONNREFUSED for the first send
  after the close and -ENOTCONN after,
  whichever path sends first; a one-way
  connected datagram socket (FD-111's
  shape, which has no back-pointer for
  a destructor to mark) spends the same
  shot lazily at its first no-peer
  send; receiving queued data first and
  addressed sends to other peers do not
  spend it. The same pass removed a
  conflation the lane's own test
  caught: sendto's addressed branch and
  sendmsg's body answered the
  broken-pipe path whenever outClosed
  was set, but for a datagram socket
  outClosed is only ever its pair
  peer's death (its own shutdown is
  unimplemented), so an addressed
  datagram to a live peer after a pair
  close was refused instead of
  delivered.** *(Provisional number,
  filed on lane-fd118-119, 2026-11-03
  — the merge lane's audit settles the
  final number.)* Fixed on branch
  `lane-fd118-119`, commits
  `e29b91cc2` (tests, including the
  FD-116 test's flipped pin),
  `07dbfd12a` (fix) and `ea69b4a06`
  (the addressed-send sites). Host
  behaviour measured first
  (`test-results/fd118-119/hostref-fd118-119b.c`):
  the one-shot on every connected send
  path (write, sendto, sendmsg,
  writev, the zero-length write),
  per-socket not per-path; receiving
  first does not spend it; an addressed
  send to a live third peer succeeds
  and does not spend it; the one-way
  connected shape shows the same
  one-shot. Proof: new test
  testDatagramPeerCloseRefusal64Syscalls
  RED on the base (every first send
  -ENOTCONN) and GREEN on the fix; the
  FD-116 zero-length test's closed-peer
  datagram case flipped to the same
  accounting and passes. Suites: full native suites serial, main 668/668 and x64 87/87, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE, "Browser export verified" (BUILD_INFO git_sha `cbb334643`, wasm sha256 `bdc64f37`, staged dependency archive `c4dbe35986a5` as pinned); the silent-failure allowance for syscall64.cpp stays 120 — no counted site added or removed. Filed from this lane: FD-130, FD-141, FD-142, FD-143.

- **CL-84 — A datagram socket whose pair
  peer closed no longer answers end of
  stream for a drained read: the read,
  readNative and recvmsg wait loops'
  inClosed end-of-stream return exempts
  datagram, whose drained read takes the
  would-block path like any empty
  datagram read (a blocking one waits
  for the next datagram); stream and
  seqpacket survivors keep their 0,
  pinned as controls in the same test.
  Fixed on branch `lane-fd120-123` in
  `49e7c983b` (composed as `c40a73c0f`);
  host-measured in
  `test-results/fd120-123/hostref-fd120-123.c`
  (drained survivor: recv, read, recvfrom
  and recvmsg all -EAGAIN; the seqpacket
  and stream controls answer 0). Proof: new test testSocketStateAnswers64Syscalls RED on the base (drained datagram recvfrom/recvmsg/read all 0) and GREEN on the fix; the FD-126 guest test's drained-read pin was corrected from the standing 0 to the host answer in the same lane. Suites: full native suites serial, main 668/668 and x64 88/88, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE, "Browser export verified" (BUILD_INFO git_sha `d13bad99b`, wasm sha256 `10d65ac0`, staged dependency archive `c4dbe35986a5` as pinned); the silent-failure allowance for syscall64.cpp stays 120 — no counted site added or removed. Filed from this lane: FD-144.

- **CL-85 — A receive on a socket that
  is not connected answers per socket
  type, the way the host answers: the
  read and readNative gates answer
  -EINVAL for stream (fresh, listening,
  or connect in flight alike), -ENOTCONN
  for seqpacket, keep -EPIPE for other
  types, and exempt datagram as before;
  recvmsg, which had no such gate and
  answered -EWOULDBLOCK (or waited on a
  blocking socket), gains the matching
  entry gate. Fixed on branch
  `lane-fd120-123` in `49e7c983b`
  (composed as `c40a73c0f`);
  host-measured in
  `test-results/fd120-123/hostref-fd120-123.c`
  and `hostref-fd120-123b.c` (a stream
  socket whose connect is still in
  flight behind a full backlog answers
  the same -EINVAL, so no connecting
  state needs a carve-out). Proof: new test testSocketStateAnswers64Syscalls RED on the base (stream and seqpacket recvfrom -EPIPE, recvmsg -EWOULDBLOCK) and GREEN on the fix. Suites: full native suites serial, main 668/668 and x64 88/88, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE, "Browser export verified" (BUILD_INFO git_sha `d13bad99b`, wasm sha256 `10d65ac0`, staged dependency archive `c4dbe35986a5` as pinned); the silent-failure allowance for syscall64.cpp stays 120 — no counted site added or removed. Filed from this lane: FD-144.

- **CL-86 — A connect to a bound but not
  listening socket is refused at once:
  the fresh-connect branch answers
  -ECONNREFUSED when the destination is
  not listening, immediately after
  FD-125's type check, blocking and
  nonblocking alike, and queues nothing
  a later listen could accept (the guest
  test listens afterwards and accepts
  nothing). Fixed on branch
  `lane-fd120-123` in `49e7c983b`
  (composed as `c40a73c0f`);
  host-measured in
  `test-results/fd120-123/hostref-fd120-123.c`
  (stream and seqpacket destinations,
  both modes; the listening control
  connects). The unix-connect-handoff JS
  harness gained the listening member
  the connect now consults, and its
  staged destination is set listening —
  the pending handoff it protects can
  only be staged against a listening
  socket now (`180e48080`, composed as
  `11c2962e3`). Proof: new test testSocketStateAnswers64Syscalls RED on the base (-EINPROGRESS, with the queued client acceptable after a later listen) and GREEN on the fix. Suites: full native suites serial, main 668/668 and x64 88/88, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE, "Browser export verified" (BUILD_INFO git_sha `d13bad99b`, wasm sha256 `10d65ac0`, staged dependency archive `c4dbe35986a5` as pinned); the silent-failure allowance for syscall64.cpp stays 120 — no counted site added or removed. Filed from this lane: FD-144.

- **CL-87 — An addressed send on a
  connected stream socket answers
  -EISCONN: sendto's addressed branch
  splits on the connection state, and
  sendmsg gains the matching header-name
  check; the connected flag, not the
  peer pointer, decides, so a survivor
  whose peer closed answers the same,
  and nothing is delivered under either
  answer. RECONCILIATION with FD-111:
  its -EOPNOTSUPP measurement did not
  record the socket's state; -EOPNOTSUPP
  is the host's answer for an
  UNCONNECTED stream socket, which
  stands and is pinned in that shape by
  this lane's guest test, while the
  connected shape FD-111's own guard
  staged (a socketpair end) is corrected
  to -EISCONN (`d13bad99b`). Fixed on
  branch `lane-fd120-123` in
  `49e7c983b` (composed as `c40a73c0f`);
  host-measured in
  `test-results/fd120-123/hostref-fd120-123.c`
  and `hostref-fd120-123b.c` (sendto and
  sendmsg, connected, peer-closed and
  unconnected shapes). Proof: new test testSocketStateAnswers64Syscalls RED on the base (connected sendto -EOPNOTSUPP, connected sendmsg delivering the byte, survivor sendmsg -ENOTCONN, unconnected sendmsg -ENOTCONN) and GREEN on the fix. Suites: full native suites serial, main 668/668 and x64 88/88, 0 FAILED; JS unit 348 passed / 0 failed; browser build BUILD COMPLETE, "Browser export verified" (BUILD_INFO git_sha `d13bad99b`, wasm sha256 `10d65ac0`, staged dependency archive `c4dbe35986a5` as pinned); the silent-failure allowance for syscall64.cpp stays 120 — no counted site added or removed. Filed from this lane: FD-144.

- **CL-88 — A peer-closed stream
  survivor's sendmsg with no header
  destination answers -EPIPE:
  sendmsg's no-peer branch gains the
  outClosed distinction FD-125 gave
  seqpacket (a peer that closed
  answers -EPIPE; a socket that was
  never connected, outClosed clear,
  keeps -ENOTCONN), so the unnamed
  message path matches the host on
  both shapes, and sendmmsg, which
  loops the same path, with it.**
  Fixed on branch `lane-fd124` in
  `118d2fc85` (composed as
  `9bda0e8ea`); host-measured in
  `test-results/fd124/hostref-fd124.c`
  (the full send matrix: every send
  path on the survivor -EPIPE at
  every length, the shutdown shapes,
  never-connected -ENOTCONN, and the
  sibling answers — -EISCONN for the
  addressed shape, -ECONNREFUSED
  then -ENOTCONN for datagram,
  -EPIPE for seqpacket). Proof: new
  test testPeerClosedStreamSendmsg64Syscalls
  RED on the base (the six unnamed
  sendmsg/sendmmsg shapes on the
  survivor -ENOTCONN; every neighbour
  control already passing) and GREEN
  on the fix. Suites: full native
  suites serial, main 668/668 and
  x64 89/89, 0 FAILED; JS unit 348
  passed / 0 failed; browser build
  BUILD COMPLETE, "Browser export
  verified" (BUILD_INFO git_sha
  `118d2fc85`, wasm sha256
  `07d5b927`, staged dependency
  archive `c4dbe35986a5` as pinned);
  the silent-failure allowance for
  syscall64.cpp stays 120 — no
  counted site added or removed.
  Filed from this lane: FD-145.

- **CL-89 — A null path pointer at
  the 64-bit chmod and fchmodat
  calls answers -EFAULT, and an
  empty path -ENOENT, both ahead of
  any lookup: the dispatcher guards
  the address before the shared
  guest-string reader can map null
  to "", and guards the empty
  string before the path resolver
  can resolve it to the directory
  node.** Fixed on branch
  `lane-fd126` in `7ecaf95db`
  (composed as `145185d18`);
  host-measured in
  `test-results/fd126/hostref-fd126.c`
  (chmod NULL -EFAULT, "" -ENOENT;
  fchmodat NULL -EFAULT at every
  dirfd and under AT_EMPTY_PATH,
  "" -ENOENT likewise, and an
  unopened dirfd still answers
  -EFAULT for a null path and
  -ENOENT for an empty one, so both
  answers precede dirfd resolution;
  execve(NULL) -EFAULT is the
  control the dispatcher already
  matched). The reader and its
  other consumers are untouched:
  execve's path was already
  guarded, and the argv/envp walk
  never passes the reader a null
  element. One correction to
  FD-146's filed measurement: the
  emulator's null answer is its
  empty-path answer, which is
  -ENOENT only where the working
  directory's path does not
  resolve -- where it resolves, the
  empty path resolves to the
  directory node and the answer was
  0; that resolver behaviour is
  filed as FD-131. Proof: new test
  testChmodNullPath64Syscalls RED
  on the base (the null shapes 0 or
  -EBADF against the host's
  -EFAULT, the empty shapes 0
  against -ENOENT, the real-name
  controls already passing) and
  GREEN on the fix. Suites: full
  native suites serial, main
  668/668 and x64 90/90, 0 FAILED;
  JS unit 348 passed / 0 failed on
  the built tree (273 passed / 75
  failed on the un-built tree
  first — every sampled failure a
  missing generated build file,
  the standing lane-tree pattern;
  both runs' logs preserved);
  browser build BUILD COMPLETE,
  "Browser export verified"
  (BUILD_INFO git_sha `7ecaf95db`,
  wasm sha256 `e097e1cf`, staged
  dependency archive `c4dbe35986a5`
  as pinned); the silent-failure
  allowance for syscall64.cpp stays
  120 — the guards return
  -EFAULT/-ENOENT through the
  existing ret idiom and add no
  counted site. Filed from this
  lane: FD-131.

- **CL-90 — An empty path answers
  -ENOENT at the path calls
  generally: the refusal sits at the
  guest boundary, call by call,
  because the shared resolver's
  internal callers pass an empty
  path deliberately (the zip root
  lookup, empty currentDirectory
  arguments) and a resolver-level
  refusal would break them.** Fixed
  on branch `lane-fd131` in
  `a8d43046c` (tests `9946c1023`;
  composed as `03409a497`, tests
  composed as `97c6cdf44`);
  host-measured in
  `test-results/fd131/hostref-fd131.c`
  and `hostref-fd131b.c`: every
  path-only call with an empty path
  answers -ENOENT (open including
  the O_CREAT form, stat, lstat,
  access, chown, lchown, truncate,
  mkdir, rmdir, unlink, rename and
  link on either side, symlink with
  an empty linkpath and with an
  empty target — no link is created,
  readlink, chdir, utimes, statfs,
  inotify_add_watch, getxattr,
  lgetxattr, mknod, execve); the
  AT_EMPTY_PATH fd forms work at
  newfstatat, statx, faccessat2,
  fchownat and utimensat and are
  exempted where the emulator
  implements them; openat does not
  honour AT_EMPTY_PATH on this
  kernel and is guarded with the
  rest; utimensat's NULL fd form
  answers -EINVAL on this kernel,
  so no working feature is lost by
  guarding the empty shape at the
  stub. Ordering compounds measured
  and pinned: the empty answer
  precedes descriptor resolution at
  statx, fchmodat, fstatat64,
  mkdirat and renameat, and follows
  it at inotify_add_watch and
  readlinkat. Sites: the KProcess
  method entries both dispatchers
  funnel into; the 64-bit
  dispatcher's bypass and join sites
  (sys_openat64, sys_stat_path64,
  sys_statx64, sys_readlink64, the
  open-coded access/mkdir/symlink/
  link/unlink cases, the utimensat
  and statfs stubs); the 32-bit
  getxattr/lgetxattr pair. Not
  changed: the shared resolver, both
  guest string readers, the ignored
  32-bit stubs that never resolve a
  path (the chown family, mount,
  utime), and the 64-bit xattr stub
  cases (FD-133). One correction to
  the filing's framing: the 64-bit
  open call already answered -ENOENT
  for an empty path on the base,
  through its -1 dirfd sentinel
  hitting the unsupported-relative-
  dirfd route; openat with AT_FDCWD
  was the genuinely open shape (it
  returned a descriptor to the
  working directory), and the guard
  now answers for the stated reason
  at both. Proof: new tests
  testEmptyPath64Syscalls and
  testEmptyPathProcessApi RED on the
  base (28 and 34 assertions failing
  with the predicted base answers;
  every control passing) and GREEN
  on the fix. Suites: full native
  suites serial, main 669/669 and
  x64 91/91, 0 FAILED; JS unit
  348 passed / 0 failed; browser build BUILD
  COMPLETE, "Browser export
  verified" (BUILD_INFO git_sha
  `a8d43046c`, wasm sha256
  `f60de38a`, staged dependency
  archive `c4dbe35986a5` as pinned);
  the known-gaps audit --check
  passes on the fixed tree — the
  guards return -ENOENT through the
  existing idioms and add no counted
  silent-failure site. Filed from
  this lane: FD-132, FD-133, FD-134,
  FD-135.

- **CL-91 — A null path pointer
  answers the bad-address fault at
  the path calls generally: the
  pointer is checked on the raw
  guest address at each dispatcher
  site, ahead of the string reader
  on the 32-bit side and ahead of
  the open-coded copies on the
  64-bit side, because both erase a
  null pointer to the empty string
  before the process layer can see
  it, and FD-131's empty-path guards
  then answer for the wrong string.**
  Fixed on branch `lane-fd132` in
  `a6718ffd7` (tests `33be78c82`;
  composed as `6a74c7d9b`, tests
  composed as `2bd6973a9`);
  host-measured in
  `test-results/fd132/hostref-fd132.c`:
  a null path answers -EFAULT at
  every plain call (open, stat,
  lstat, access, chmod, chdir, mkdir,
  rmdir, unlink, rename and link on
  either side, symlink on either
  side, readlink, utimes, statfs,
  getxattr, lgetxattr) and at every
  *at form, ahead of descriptor
  resolution (an unopened dirfd
  still answers -EFAULT); three
  measured exceptions shape the fix:
  newfstatat and statx treat a null
  path WITH AT_EMPTY_PATH as the
  working descriptor form (a valid
  descriptor or AT_FDCWD answers the
  descriptor's stat, an unopened one
  -EBADF), faccessat2/fchmodat/linkat
  have NO null descriptor form (null
  faults even with AT_EMPTY_PATH),
  and utimensat answers -EINVAL for
  a null path in every shape (the
  kernel special-cases the null
  filename before any name fetch).
  inotify_add_watch validates the
  descriptor first (a bad descriptor
  with a null path answers -EBADF).
  One correction to the filing: it
  stated the host's answer as
  -EFAULT; that holds for the named
  families, but utimensat's measured
  answer is -EINVAL and the
  fstatat64/statx descriptor form
  accepts the null pointer, so those
  shapes are fixed to their measured
  answers, not to -EFAULT. Sites:
  on the 32-bit side, a guard on the
  path address at each helper that
  reads a path through the string
  reader (open, openat, link, unlink,
  execve, chdir, chmod, access,
  rename, mkdir, rmdir, symlink,
  readlink, statfs, statfs64, stat64,
  lstat64, utimes, mkdirat, unlinkat,
  symlinkat, renameat, fchmodat,
  faccessat, faccessat2; conditioned
  on the flag at fstatat64 and
  statx; -EINVAL at utimensat and
  utimensat_time64; descriptor-first
  at inotify_add_watch; the attribute
  name at getxattr/lgetxattr, after
  FD-131's empty-path resolution);
  on the 64-bit side, a guard on the
  raw address at the open-coded
  cases (the access family, mkdir/
  mkdirat, symlink/symlinkat and
  link/linkat on both arguments,
  rename/renameat/renameat2 ahead of
  the process-layer call, unlink/
  unlinkat, chdir, and
  inotify_add_watch with the
  descriptor check mirrored). Not
  changed: the string reader itself
  (FD-146's precedent — its other
  consumers are not this defect),
  the shared resolver, the process
  layer (FD-131's guards already
  answer for the empty string),
  the never-resolving ignored stubs,
  and the 64-bit stat helpers (they
  already honoured both null shapes:
  the descriptor form under
  AT_EMPTY_PATH, -EFAULT without).
  Observed, not fixed: i386 linkat
  has no helper at all (a nullptr
  dispatch entry answering -ENOSYS
  for every input — an unimplemented
  call, not this erasure). Proof:
  new tests testNullPath32Syscalls
  (main) and testNullPath64Syscalls
  (x64) RED on the base (40 and 24
  assertions failing with the erased
  answers — the empty route's
  -ENOENT, the create-style cases'
  -EEXIST against the directory the
  empty string resolves to, unlink
  answering success, the descriptor
  form taken for a null pointer;
  every control already passing) and
  GREEN on the fix. Suites: full
  native suites serial, main 670/670
  and x64 92/92, 0 FAILED; JS unit
  348 passed / 0 failed; browser
  build BUILD COMPLETE, "Browser
  export verified" (BUILD_INFO
  git_sha `a6718ffd7`, wasm sha256
  `650f80f9`, staged dependency
  archive `c4dbe35986a5` as pinned);
  the known-gaps audit --check
  passes on the fixed tree — the
  guards answer through the existing
  idioms and add no counted
  silent-failure site. Filed from
  this lane: FD-136, FD-137.
  Reconciliation (lane-fd136-138,
  2026-11-03): this entry's utimensat
  exception — "utimensat answers
  -EINVAL for a null path in every
  shape (the kernel special-cases the
  null filename before any name
  fetch)", and the -EINVAL guard at
  utimensat/utimensat_time64 listed
  under Sites — rested on the same
  wrapper-shaped measurement FD-136's
  amendment documents: glibc's
  utimensat wrapper refuses a null
  path in userspace and never issues
  the call, so the measured -EINVAL
  was the wrapper's contract, not the
  kernel's. Re-measured at the raw
  syscall level, the kernel's answers
  are per-shape (the current-directory
  shape faults; an open descriptor
  with flags 0 is the working
  descriptor form and succeeds; flag
  forms and unopened descriptors as
  FD-136's amendment records). The
  32-bit guard this entry installed
  is replaced under CL-93, which
  closes FD-136 on the corrected
  matrix; the rest of this entry
  stands as written.

- **CL-92 — A null memfd_create
  name answers the bad-address
  fault on both dispatchers: each
  dispatcher checks the name
  pointer on the raw guest address
  ahead of its name copy/read,
  because both erased a null
  pointer to the empty string —
  which is a legal name that
  creates the file — before the
  process layer could see it.**
  Fixed on branch `lane-fd136-137`
  in `0e5d33859` (tests
  `dd47a7a50`; composed as
  `166c15780`, tests composed as
  `72eb36e36`); host-measured in
  `test-results/fd136-137/
  hostref-fd137.c`: a null name
  answers -EFAULT with flags 0,
  MFD_CLOEXEC or MFD_ALLOW_SEALING;
  the empty string and a labelled
  name return descriptors; a null
  name with an invalid flags word
  answers -EINVAL, because the host
  validates the flags before it
  faults the name copy. The fix
  reproduces that order: the
  dispatchers' null guards ask the
  same flags verdict the process
  layer gives, via the mask now
  shared as
  KProcess::MEMFD_SUPPORTED_FLAGS
  (declared in kprocess.h, consumed
  by KProcess::memfd_create and
  both dispatcher guards). Sites:
  the 64-bit dispatcher's
  memfd_create case, ahead of the
  memcpyFromGuest name copy; the
  32-bit syscall_memfd_create
  helper, ahead of the readString
  name read. Not changed: the
  string reader itself (FD-146's
  precedent), the process layer's
  create path, and the legal empty
  name. Proof: new tests
  testMemfdNullName32Syscalls
  (main) and
  testMemfdNullName64Syscalls (x64)
  RED on the base (a null name
  created the file and returned a
  descriptor on both dispatchers;
  every control already passing)
  and GREEN on the fix. Suites:
  full native suites serial, main
  671/671 and x64 93/93, 0 FAILED;
  JS unit 348 passed / 0 failed;
  browser build BUILD COMPLETE,
  'Browser export verified'
  (BUILD_INFO git_sha `0e5d33859`,
  wasm sha256 `6e877d79`, staged
  dependency archive `c4dbe35986a5`
  as pinned); the known-gaps audit
  --check passes on the fixed tree.
  Filed from this lane: FD-138.
  NOT closed by this lane: FD-136 —
  its filing's host claim was
  contradicted by this lane's raw
  re-measurement; the entry is
  amended in place and stays open
  for the merge audit (see FD-136).

- **CL-93 — utimensat with a null
  path answers the raw kernel's
  per-shape matrix on both
  dispatchers: flag bits outside
  AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH
  answer -EINVAL first; the
  current-directory shape faults
  (-EFAULT); any other flag with a
  null path answers -EINVAL; with
  flags 0 an unopened descriptor
  answers -EBADF and an open one is
  the descriptor form and succeeds,
  applying the times on the 32-bit
  side.** Fixed on branch
  `lane-fd136-138` in `4abf51d88`
  (tests `e2f19ce23`; composed as
  `5533960af`, tests composed as
  `435782b5a`); host-measured
  at the raw syscall level in
  `test-results/fd136-138/
  hostref-fd136b.c` — an inline svc,
  so no wrapper answer can masquerade
  as a kernel one; the libc
  futimens/utimensat cross-checks
  reproduce the wrapper's -EINVAL
  beside the kernel's 0, and the
  descriptor form is shown really
  setting the file's and the
  directory's times. The matrix
  confirms FD-136's amendment in
  full and adds the measured
  ordering: the kernel copies the
  times ahead of every other
  verdict, so an unreadable times
  pointer faults even against an
  invalid flags word (pinned at the
  64-bit dispatcher; on the 32-bit
  side an unreadable times pointer
  travels the same guest-memory
  fault delivery the non-null path
  already used). Sites: the 64-bit
  dispatcher's utimensat stub (the
  matrix inline, including the
  times-readability verdict ahead of
  it — the stub applies no times on
  any shape, so the descriptor form
  is the success answer alone); on
  the 32-bit side the two helpers'
  FD-132 -EINVAL guard now routes
  the null path to a new process
  method, KProcess::utimensatNullPath,
  which answers the matrix and, for
  the descriptor form, applies the
  times to the descriptor's node
  through the parse-and-set tail
  shared with the path form
  (extracted as
  KProcess::setNodeTimes);
  descriptors whose node stores no
  times (pipes, memfds) still answer
  the kernel's success. The stale
  pins and the wrapper claim in the
  FD-132 guest test's header are
  corrected in the same tests
  commit. Not changed: the string
  reader, the shared resolver,
  FD-131's empty-path answers, and
  every non-null shape (the non-null
  invalid-flags divergence the same
  measurement found is filed as
  FD-139). Proof: extended tests
  testNullPath32Syscalls (main) and
  testNullPath64Syscalls (x64) RED
  on the base — main 669 passing
  with those two tests failing (14
  utimensat assertions answering
  the FD-132 guard's -EINVAL, plus
  the times-application pin), x64
  91 passing with 11 utimensat
  assertions answering the stub's
  blanket success — and GREEN on
  the fix; full native suites
  serial, main 671/671 and x64
  93/93, 0 FAILED; JS unit 348
  passed / 0 failed; browser build
  BUILD COMPLETE, "Browser export
  verified" (BUILD_INFO git_sha
  `4abf51d88`, wasm sha256
  `e4d4260b`, staged dependency
  archive `c4dbe35986a5` as pinned);
  the known-gaps audit --check
  passes on the fixed tree.
  Reconciles CL-91: its utimensat
  exception rested on the same
  wrapper measurement (see the
  reconciliation note appended
  there). Filed from this lane:
  FD-139.

- **CL-94 — memfd_create refuses
  names longer than the host's
  249-char limit with -EINVAL on
  both dispatchers; a name at the
  limit still creates.** Fixed on
  branch `lane-fd136-138` in
  `4abf51d88` (tests `e2f19ce23`;
  composed as `5533960af`, tests
  composed as `435782b5a`);
  host-measured at the raw syscall
  level in `test-results/fd136-138/
  hostref-fd136b.c`: 249 chars
  returns a descriptor, 250 and 300
  answer -EINVAL. Site: the single
  check lives in
  KProcess::memfd_create, behind
  the existing flags verdict, so
  both dispatchers inherit it (the
  64-bit case's 255-char copy cap
  sits above the limit, so an
  over-limit name still measures
  over the limit on arrival).
  Proof: the FD-137 tests
  (testMemfdNullName32Syscalls
  main, testMemfdNullName64Syscalls
  x64) convert their printed
  300-char observation into pins
  and add the 249/250 boundary
  pair — RED on the base (the 250-
  and 300-char names returned
  descriptors on both dispatchers)
  and GREEN on the fix; suites,
  build and audit as in CL-93.
  Record for both closures:
  `docs/reports/fd136-138-utimensat-null-matrix-20261103.md`;
  evidence `test-results/fd136-138/`.

- **CL-95 — The browser guest CPU
  self-test's four standing failures
  are discharged test-side (FD-145):
  the JIT assertions now switch on
  the engine surface the run itself
  selected, the chmod case expects
  the layer's real -ENOENT for a
  null path, and the fchmod case
  expects -EBADF as staged — the
  suite reports 953 passed / 0
  failed on both the standard and
  the JIT builds.** *(Settled at the
  batch-5 composition; this record
  is written by the merge lane from
  the two lanes' evidence.)* The
  settlement lane (lane-fd125-ab;
  its register commit composed as
  `080a58bc5`) proved all four
  failures pre-existing on the
  unmodified base — identical 951/4
  summaries and identical failure
  lines on the base and lineage
  builds — and recorded the
  dispositions FD-145's entry
  carries: the JIT pair a fixture
  expectation defect, fchmod's
  -EBADF the host's own answer, and
  chmod's null-path divergence the
  real defect, closed separately as
  FD-146 (CL-89). The restage lane
  (lane-fd125-restage) applied the
  dispositions in `027688ad0`
  (composed as `a17074952`),
  cpu64SelfTest.cpp only, no product
  code changed: with no engine
  selected the end block asserts the
  off flavour's all-zero record;
  the standard build prints
  "JIT64: engine=off blocks=0
  insns=0" with no failure line, and
  the wasm64-mt-jit build under
  BW_TEST_EXPECT_JIT=1 prints the
  engine-selected record; the guest
  summary is 953 passed, 0 failed,
  suite PASS both ways. The
  settlement's first-choice
  open-state stagings are measured
  impossible inside this fixture
  (no KThread/KProcess; openat
  answers -ENOSYS), as FD-145's
  entry records. The composed
  tree's batch-5 gate re-runs the
  suite; see the batch-5 merge
  record. Records:
  `docs/reports/fd125-settlement-20261103.md`
  and
  `docs/reports/fd125-fd130-test-discharge-20261103.md`;
  evidence `test-results/fd125ab/`
  and `test-results/fd125-restage/`
  in the lanes' worktrees.

- **CL-96 — The cpu32-jit-growth
  browser test's statistics
  assertions are discharged
  test-side (FD-150): they now run
  only when the statistics JSON's
  cpu32 section declares itself
  available, and the test's boot
  stages the engine parameter that
  actually enables the CPU32 engine
  (jit32=1; jit=1 selects CPU64
  only), with an enabled assertion
  inside the gate.** *(Settled at
  the batch-5 composition; written
  by the merge lane from the lanes'
  evidence.)* The settlement lane
  proved the failure pre-existing
  on the base: the guest reports
  RESULT 0 on both builds, then the
  harness read statistics fields
  the standard build's JSON does
  not carry. The restage lane
  (lane-fd125-restage) fixed the
  test in `0d4463b53` (composed as
  `46d25fb12`), cpu32-jit-growth
  .test.mjs only: standard build
  PASS with the gate closed over
  the stub section; wasm64-mt-jit
  build PASS with the gate open —
  the section reports enabled:true,
  retiredCounting:true,
  blocksEntered 105339927,
  instructionsRetired 452013423,
  and the original ungated
  assertions pass verbatim there.
  The composed tree's batch-5 gate
  re-runs the suite; see the
  batch-5 merge record. Records and
  evidence as CL-95.

- **CL-97 — The 64-bit dispatcher's
  extended-attribute cases resolve
  the path or descriptor they name
  before answering: the set forms
  validate their flags word, every
  name-taking form copies the
  attribute name, and only then is
  the target resolved, so the stub
  answers (get -ENODATA, list 0,
  set/remove -ENOTSUP) are reached
  only by a target that exists.**
  Fixed on branch `lane-fd133-135`
  in `d8ca8945a` (tests
  `4ebfe696c`; composed as
  `cd0e030e7`, tests composed as
  `739e4420d`); host-measured in
  `test-results/fd133-135/
  hostref-fd133-135.c` (companions
  hostref-fd133b.c, hostref-fd133c.c,
  hostref-fd133d.c): every path form
  answers -ENOENT for a missing or
  empty path; a null path -EFAULT;
  a null attribute name -EFAULT
  ahead of the path verdict (even
  for a missing or empty path); an
  invalid setxattr flags word
  -EINVAL ahead of the name copy;
  the following forms answer
  -ENOENT for a dangling symlink
  while the l-forms resolve the
  link itself; the descriptor
  forms answer -EBADF for an
  unopened descriptor, with the
  name copy and the flags verdict
  ahead of that check too. The fix
  reproduces that order in the
  twelve cases in
  source/kernel/syscall64.cpp.
  Not changed: the stub answers
  for a resolved target (the guest
  filesystem models no attributes —
  a host setxattr actually stores,
  and its removexattr of an absent
  attribute answers -ENODATA; both
  stay the stub's -ENOTSUP by the
  filing's terms), the 32-bit
  side's xattr handlers, and the
  no-process standalone answers.
  Proof: new test
  testXattrResolve64Syscalls RED
  on the base (37 assertions
  failing with the stub answers;
  every control passing) and GREEN
  on the fix. Suites: full native
  suites serial, main 672/672 and
  x64 95/95, 0 FAILED; JS unit 348
  passed / 0 failed; browser build
  BUILD COMPLETE, "Browser export
  verified" (BUILD_INFO git_sha
  `d8ca8945a`, wasm sha256
  `bd6816da`, staged dependency
  archive `c4dbe35986a5` as pinned);
  the known-gaps audit --check
  passes on the fixed tree.

- **CL-98 — KProcess::statx
  substitutes the descriptor's path
  for the caller's path only when
  the path is empty: with
  AT_EMPTY_PATH and a non-empty
  path it now stats the named path
  relative to the descriptor, as
  the host does.** Fixed on branch
  `lane-fd133-135` in `d8ca8945a`
  (tests `4ebfe696c`; composed as
  `cd0e030e7`, tests composed as
  `739e4420d`); host-measured
  in `test-results/fd133-135/
  hostref-fd133-135.c`:
  statx(dirfd, "inner",
  AT_EMPTY_PATH) returns the named
  file's mode and size,
  statx(dirfd, "", AT_EMPTY_PATH)
  the descriptor's own, and an
  absolute path under the flag
  stats itself. The fix gates the
  substitution in KProcess::statx
  (source/kernel/kprocess.cpp) on
  the path being empty; the
  empty-path refusal without the
  flag (FD-131) and the descriptor
  form are unchanged. The 64-bit
  dispatcher's own statx helper
  never shared the behaviour.
  Proof: new test
  testStatxAtEmptyPathProcessApi
  RED on the base (the named-path
  shapes returned the directory's
  mode and inode instead of the
  file's, and a missing named path
  answered 0)
  and GREEN on the fix. Suites:
  full native suites serial, main
  672/672 and x64 95/95, 0 FAILED;
  JS unit 348 passed / 0 failed;
  browser build BUILD COMPLETE,
  "Browser export verified"
  (BUILD_INFO git_sha `d8ca8945a`,
  wasm sha256 `bd6816da`, staged
  archive `c4dbe35986a5` as pinned);
  the known-gaps audit --check
  passes on the fixed tree.

- **CL-99 — The 64-bit dispatcher's
  statfs case resolves its path
  before answering: a null path
  faults -EFAULT and an
  unresolvable one answers -ENOENT,
  while resolvable paths keep the
  fabricated statistics.** Fixed
  on branch `lane-fd133-135` in
  `d8ca8945a` (tests `4ebfe696c`;
  composed as `cd0e030e7`, tests
  composed as `739e4420d`);
  host-measured in
  `test-results/fd133-135/
  hostref-fd133-135.c`: statfs
  answers -ENOENT for a missing or
  empty path, -EFAULT for a null
  one, -ENOENT for a dangling
  symlink, and 0 for files,
  directories and symlinks to them.
  The fix resolves in the
  dispatcher's statfs case
  (source/kernel/syscall64.cpp)
  through the shared resolver from
  the working directory, subsuming
  FD-131's readable-empty-first-byte
  guard; the process layer's
  statfs/statfs64 methods already
  resolved this way. Without a
  process the fabricated answer
  stands as before. Proof: new
  test testStatfsResolve64Syscalls
  RED on the base (missing,
  dangling and null paths all
  answered 0) and GREEN on the
  fix. Suites: full native suites
  serial, main 672/672 and x64
  95/95, 0 FAILED; JS unit 348
  passed / 0 failed; browser build
  BUILD COMPLETE, "Browser export
  verified" (BUILD_INFO git_sha
  `d8ca8945a`, wasm sha256
  `bd6816da`, staged archive
  `c4dbe35986a5` as pinned); the
  known-gaps audit --check passes.
  Filed from this lane: FD-152,
  FD-140.

- **CL-100 — FD-127 closed: socket
  creation took the descriptor's
  owning process from the ambient
  current thread; serviced under
  the unix-side dispatch binding,
  that process is a degenerate id-0
  object whose fd-table mutex sits
  at linear address ~0x10, and
  descriptor allocation deadlocked
  re-acquiring it inside the
  fd-table handle scan — the Godot
  demo's device-setup thread
  creation (a pipe2 from Wine's
  NtCreateThreadEx) never returned.
  `ksocket` now takes the owning
  thread explicitly and allocates
  in that thread's process at all
  three creation sites (AF_UNIX,
  AF_NETLINK, AF_INET); the
  socketpair helper and the
  32/64-bit socket handlers pass
  the thread they are servicing.**
  *(Settled at the batch-5
  composition; written by the merge
  lane from the lane's Section B
  account, the lane's own register
  commit having carried the closure
  in that row.)* Fixed on branch
  `lane-fd127fix` in `f670bd7de`
  (composed as `d8b899703`); the
  step-ring instrument that
  convicted it is retained, gated
  on BW_THREAD_DUMP=1. Proof:
  conviction by the gated step ring
  across the lane's diag1-diag4
  captures on the batch-4 code
  line: healthy allocations
  complete the full span on the
  real table (mutex at a heap
  address, handle returned); the
  frozen call's ring stops after
  the allocation lock on the id-0
  object (mutex address bits 1,
  about 0x10) with the handle
  scan's own lock never acquired,
  host CPU flat (blocked, not
  spinning). Fixed build, same
  capture (diag6, tip `f6f010eb4`,
  BUILD_INFO dirty 0): caps 257.6 s
  -> first vkCreateShaderModule
  261.6 s (1,216 B, hash
  f598618b6233f21a) -> pipelines
  create (32 traps) -> first window
  mapped 299.3 s, guest
  progressing; native suites
  serial main 675/675 and x64
  95/95, 0 FAILED; JS unit 352/0
  (the two harness updates the
  change required are in the
  branch); browser build 'Browser
  export verified'. Filed from
  this lane: FD-128. Record:
  `docs/reports/fd127-ksocket-owner-fix-20261103.md`;
  evidence `test-results/fd127/`.

- **CL-101 — FD-128 closed: with
  FD-127 fixed, the Godot demo's
  servicing worker died ~1-2 s
  after the window mapped on an
  unaligned-access trap. The
  trapping operations are the
  shared_ptr release inside
  `BoxedWineCondition::wait`
  (`i32.atomic.rmw.add` on the
  previous `waitingCond`
  control-block word; profiling-
  build CDP stack + disassembly,
  capture diag-prof) and, in repeat
  captures, the `FaultStageScope`
  atomic loads in
  `KFile::preadNative` — both
  consume `KThread::currentThread()`,
  and on the dying worker that
  value had been overwritten in its
  TLS slot with a small integer
  (0x25, then 0x9; the slot's only
  legitimate writer,
  `setCurrentThread`, never carried
  the value — tripwire-proven; a
  1 kHz slot monitor watched it
  change at Vulkan-call frequency
  during the first
  vkAllocateMemory/vkCreateImage
  burst). Identity consumers then
  operated on low static memory
  (the "waitingCond" pair landed in
  the static string table).
  `KThread::currentThread()` now
  validates the slot (a genuine
  KThread is heap-allocated:
  8-aligned, above the static
  region) and falls back to the
  identity the host thread
  registered for itself at
  platform-thread start
  (pthread_self()-keyed registry,
  RAII registration in
  `platformThread`), healing the
  slot.** *(Settled at the batch-5
  composition; written by the merge
  lane from the lane's Section B
  account.)* Fixed on branch
  `lane-fd128` in `849a55412`
  (composed as `f349388a7`); the
  slot writer itself is FD-129,
  closed as CL-102. Proof: fixed
  build, same capture (diag-fix1;
  standard build of the committed
  tree, BUILD_INFO dirty 0, wasm
  sha256 511e3151cdb9 —
  byte-identical to the capture
  build): banner 154.4 s -> window
  mapped 227.4 s -> **no trap** (no
  exceptions file; zero "unaligned"
  in the full console) -> guest
  alive and working at the 1,200 s
  deadline (elapsed 1,212.3 s),
  passing the filed death point
  (window +1.0/+1.4/+2.2 s across
  the three unfixed captures) by
  ~983 s. Pipeline account toward
  FD-89/CL-43 (full account in the
  lane record): 93 shader modules
  created, zero refusals; 6
  graphics + 68 compute pipelines;
  6 vkQueueSubmit + 6
  vkQueueSubmit2; 3 bridge DROPPED
  lines total (vs 8,386 in the
  FD-62-era baseline); the FD-72
  helper-invocation module met in
  vivo at 337.6 s and refused by
  name under FD-75; no
  swapchain/present — the first
  present remains gated on
  FD-72/FD-70 per FD-55's entry,
  and the guest settles into the
  downstream shader-retry cycle
  that entry holds open. Native
  suites serial main 675/675 and
  x64 95/95, 0 FAILED; JS unit
  352/0; browser build 'Browser
  export verified'. Record:
  `docs/reports/fd128-current-thread-identity-20261103.md`;
  evidence `test-results/fd128/`.

- **CL-102 — FD-129 closed: the
  writer that overwrote guest
  workers' `runningThread` TLS
  slots with small integers during
  the graphics burst is the
  emulator's own guest-memory
  allocation. Instrumented pre-fix
  captures (branch `lane-fd129pre`,
  the pre-FD-128 tree) attribute it
  exactly: a slot registers with
  its genuine KThread pointer; a
  burst of `K64Page::commit()`
  calls follows, each allocating a
  fresh guest page's 4 KB backing
  buffer with
  `new U8[K64_PAGE_SIZE]`, and in
  the browser build those heap
  allocations land inside live host
  threads' stack/TLS spans — an
  allocation-order ring shows the
  page commits marching across the
  heap until one fresh buffer lands
  exactly over a live slot (buffer
  base = slot - 0x170, on two
  different victim threads in two
  independent captures). `commit()`
  then memsets the new buffer,
  zeroing the slot, and the guest
  stores ordinary data through the
  now-aliased page. Fix: page
  backing buffers come from a
  dedicated arena (16 MB chunks
  obtained once and never freed;
  mutex-guarded free list;
  per-buffer provenance so each
  buffer returns to its own
  allocator; heap fallback only
  when the arena is disabled via
  BW64_PAGE_ARENA=0 or cannot grow)
  — an arena slot can never alias a
  live thread's span, and no thread
  span can be carved from an arena
  chunk, closing the class in both
  directions. FD-128's
  `currentThread()` validation is
  untouched and is now defence in
  depth, as the filing's closing
  terms envisaged.** *(Settled at
  the batch-5 composition; written
  by the merge lane from the lane's
  Section B account. FD-129's
  number settles unchanged.)*
  Fixed on branch `lane-fd129` in
  `f94ceb4c5` (composed as
  `c4fbf9969`). Identification
  evidence (pre-fix tree, full slot
  surveillance + allocation ring):
  `wt-fd129pre/test-results/fd129pre/diag1`
  and `diag3` — diag3 final tallies
  REG 37, STOMP 14, WATCH 23 with
  the two live-slot hits above,
  guest alive at the 1,200 s
  deadline. Post-fix surveillance
  of the unmodified FD-128 tip was
  already silent (diag1 ~500 s,
  diag2 full 1,200 s:
  CORRUPT/ZERO/STOMP/WATCH/BRACKET
  all 0), consistent with the
  transient, burst-local nature of
  the aliasing. Decisive proof —
  the same Godot capture with the
  full diagnostic applied on top of
  the fix (diag commit since
  reverted; branch tip is the fix):
  `test-results/fd129/proof1` —
  banner 100.5 s -> window mapped
  152.6 s -> guest alive and working
  at the 1,200 s deadline (elapsed
  1,205.4 s); 36 slots registered;
  **CORRUPT 0, ZERO 0, STOMP 0,
  WATCH 0, BRACKET 0, SCAN 0**
  across the whole correlated burst
  (339 vkGetPhysicalDeviceFormatProperties2,
  167 vkAllocateMemory/vkCreateImage-class
  calls). Native suites serial on
  the fixed tip: main 675/675 and
  x64 95/95, 0 FAILED (run twice —
  before and after the
  header-consistency amend — same
  result); JS unit 352 passed / 0
  failed; browser build 'Browser
  export verified' (fixed wasm
  sha256 4453757f0699b527). No new
  defects filed by this lane: the
  only anomalies met were artefacts
  of the lane's own diagnostics (a
  monitor polling freed threads'
  recycled TLS) and a pre-existing
  infrastructure pin rotation,
  neither a product defect.
  Record:
  `docs/reports/fd129-page-backing-arena-20261103.md`;
  evidence `test-results/fd129/` and
  `wt-fd129pre/test-results/fd129pre/`.

- **CL-103 — FD-147 closed: the
  Vulkan bridge's frame-overflow
  count guards for the image and
  attachment clear calls compared
  the raw 64-bit trap slot against
  their count caps, but the Vulkan
  counts are uint32 — only the
  slot's low half is the value — so
  a legal clear whose slot carried
  unrelated high bits fired the
  overflow, the flag poisoned the
  draw's frame, the page tier
  rejected the whole frame, and
  D3D10/D3D11 draws landed nothing
  (LIT=0) while the D3D12 route,
  which never issues the guarded
  calls, passed.** *(Settled at the
  batch-5 composition; the lane
  closed the defect on its own
  register under its FD-130, the
  same defect the settlement lane
  filed and this register carries
  as FD-147 — substance-keyed at
  the audit.)* Fixed on branch
  `lane-d3d11lit` in `738d882ca`
  (composed as `129e739cf`): all
  six guards (vkCmdClearColorImage,
  vkCmdClearDepthStencilImage,
  vkCmdClearAttachments; 64- and
  32-bit dispatchers) compare the
  (U32)-truncated count; caps and
  the loud refusal of genuinely
  over-cap counts are unchanged.
  Measured conviction: DXVK's
  init-image vkCmdClearColorImage
  (its only call site passes
  rangeCount 1; the range array
  read back as exactly one range)
  arrived with the count slot
  holding 0x200000000001.
  Proof: on the lane's base
  (lane-fd129 tip `ea93a6ef3`, build
  byte-identical to the FD-129
  lane's, wasm 4453757f)
  d3d11-triangle fails with the
  full chain (overflow warning,
  LIT=0); on the fixed build (wasm
  d3937eb9, BUILD_INFO dirty 0,
  'Browser export verified'):
  d3d11-triangle PASS (LIT=8192,
  RESULT 0), d3d11-comprehensive
  PASS (LIT=8192; the identical
  4460-byte frame renders),
  d3d10-comprehensive PASS
  (`tri10: LIT=8192`, RESULT 0),
  d3d12-comprehensive PASS
  (control). Native suites serial
  main 675/675 and x64 95/95, 0
  FAILED; JS unit 352/0, legacy
  115/0; known-gaps audit: no
  undeclared gaps. The composed
  tree's batch-5 gate re-runs the
  three D3D tests; see the batch-5
  merge record. Filed from this
  lane: FD-153. Record:
  `docs/reports/d3d1011-draws-land-nothing-20261103.md`;
  evidence `test-results/fd127dxvk/`
  in the lane's worktree.

- **CL-104 — FD-153 closed: the
  Vulkan bridge's drop log no longer
  reports legal no-op shapes as dropped
  guest commands — a colour-only
  rendering pass's null depth/stencil
  attachment pointers, a stage's empty
  VkSpecializationInfo, a frame whose
  descriptor set 0 was never bound, a
  zero-region 32-bit copy with a null
  region array, and the profiling trap
  scan's idle slots.** *(Provisional
  number, filed on lane-fd153,
  2026-11-04 — the merge lane's audit
  settles the final number.)* Fixed on
  branch `lane-fd153`, commits
  `f31f44d15` (tests), `a8720731e`
  (fix + census pin): the attachment
  helpers and captureStageSpec return
  ahead of their guards on the legal
  null/empty input (over-cap counts,
  claimed-but-absent arrays and data,
  unreadable structs stay loud);
  resolveFrame returns when no set is
  bound at slot 0 (a stale recorded id
  stays loud); the 32-bit copy guards
  drop only on an over-cap count or a
  claimed-but-absent array; the
  VKPROF scans skip idle slots without
  the macro — the census's only
  removals (VK64_DROP_IF sites
  257 -> 255, `continue` 63 -> 61,
  pin updated in
  `vk-drop-macro-guard.test.mjs`).
  Proof: new native FD-153 section
  (five PASS lines) RED on the base
  (aborts at the colour-only drop
  assertion) and GREEN on the fix;
  run-native 67 PASS; native suites
  serial main 679/679 and x64 105/105,
  0 FAILED; browser build 'Browser
  export verified' (wasm `7a0f9ba4`,
  BUILD_INFO git_sha `a8720731e`);
  JS unit 352/0, legacy 115/0;
  citation audit 352 rows, 0 drift
  (46 cited lines shifted by the fix
  and re-pinned). Filed from this
  lane: FD-155 (the 32-bit
  specialization capture's silent
  header guard — the survey's
  opposite-sign find). Record:
  `docs/reports/fd153-drop-log-noop-shapes-20261104.md`;
  evidence `test-results/fd153/` in
  the lane's worktree.

- **CL-105 — FD-151 closed: the Node
  wasm64-selftest target compiles
  again, and the cpu64 self-test
  reports full marks on the standard
  and speed-engine (JIT) builds.**
  Fixed on branch `lane-fd151` in
  `7d422cb8f` (fixture restaging
  `20129b456`; run-instruction
  correction `fae50a27e`). The build
  break was a configuration family,
  not the single filed site: the
  target builds without
  BOXEDWINE_MULTI_THREADED, where
  synchronization.h typedefs
  BOXEDWINE_MUTEX to void* with
  empty lock/unlock macros, and two
  call sites written against the
  multi-threaded configuration
  touched the type directly —
  faultstage.h's MemoryDemandLock
  (the filed site) and ksocket.cpp's
  socketpair descriptor bracket
  (surfaced on the rebuild). Both
  now use the configuration's own
  idioms (the LOCK/UNLOCK macros;
  an #ifdef BOXEDWINE_MULTI_THREADED
  bracket of the tryGetFileDescriptor
  shape), so multi-threaded
  behaviour is byte-identical. The
  self-test's two standing failing
  cases were stale test staging,
  the product correct in both: the
  chmod null-path case expected the
  pre-FD-126 -ENOENT where the
  dispatcher now answers -EFAULT
  (host-measured at the raw syscall:
  fchmodat(AT_FDCWD, NULL) answers
  -EFAULT, the empty path -ENOENT),
  and the ENOSYS case included
  pidfd_getfd, which is implemented
  and answers -EBADF for a
  non-pidfd descriptor (host-measured
  the same way; the flags-first
  -EINVAL order also matches).
  Runs: standard build 953 passed /
  0 failed; the speed-engine build
  953/0 interpreted and 953/0 with
  emitted execution verified
  (blocks=319 insns=816 entered=319
  rejected=0). Native suites serial,
  0 FAILED (main 679 OK lines, x64
  105); JS unit 352/0; browser build
  verified (BUILD_INFO git_sha
  fae50a27e, dirty 0, wasm
  2fd905f6); the suite's browser
  cpu64-selftest PASS. Files FD-156
  (the mem64 variant's separate
  compile break). Record:
  `docs/reports/fd151-node-selftest-target-20261103.md`;
  evidence `test-results/fd151/`.

- **CL-106 — FD-152 closed: the
  64-bit dispatcher's descriptor-
  form filesystem-statistics call
  (fstatfs, 138) shared the path
  form's helper and ignored its
  descriptor, answering the
  fabricated statistics for any
  descriptor, valid or not.**
  Fixed on branch `lane-fd139-152`
  in `00d8f5369`: the case checks
  the descriptor against the
  process first (any open
  descriptor — file, directory,
  pipe, socket — keeps the
  fabricated answer; a descriptor
  that is not open answers
  -EBADF, ahead of the helper's
  null-buffer -EFAULT, matching
  the host's ordering and the
  process layer's own fstatfs64);
  without a process the fabricated
  answer stands, as in statfs.
  Host measured at the raw
  syscall level (test-results/
  fd139-152/hostref-fd139-152.c):
  fstatfs answers 0 for every
  open descriptor kind, -EBADF
  for -1, 9999 and a just-closed
  descriptor, -EFAULT for an open
  descriptor with a null buffer,
  -EBADF when both are wrong.
  Proof: guest test
  testFstatfsDescriptor64Syscalls
  (tests `c178a704e`) fails on the
  base with the four descriptor
  rows answering 0 or -EFAULT and
  passes on the fix; native
  suites serial main 682/682 and
  x64 108/108, 0 FAILED; JS unit
  352 passed / 0 failed; browser build 'Browser
  export verified' (BUILD_INFO
  git_sha `e8f120b22`, wasm sha256
  c6499c3c, staged archive
  `c4dbe35986a5` as pinned);
  known-gaps audit `--check`
  passes. Record:
  `docs/reports/fd139-140-152-enotdir-utimensat-flags-fstatfs-20261104.md`;
  evidence `test-results/fd139-152/`.

- **CL-107 — FD-139 closed:
  utimensat accepted flag bits
  outside AT_SYMLINK_NOFOLLOW |
  AT_EMPTY_PATH on non-null paths
  on both dispatchers, answering
  success (and, on the 32-bit
  process path, applying the
  times) where the host answers
  -EINVAL.** Fixed on branch
  `lane-fd139-152` in `00d8f5369`:
  the 64-bit stub hoists the
  flags verdict ahead of its
  path branches (the times
  copy-in still precedes it; the
  FD-136 null-path matrix is
  unchanged answer-for-answer),
  and KProcess::utimesat and
  utimesat64 validate the word
  at entry, before the FD-131
  empty-path guard and any
  resolution. Host measured at
  the raw syscall level,
  strace-verified
  (test-results/fd139-152/
  hostref-fd139-152.c): valid
  path with 0x2000/0x1/0x800/-1
  answers -EINVAL; a missing
  path, an empty path, a
  through-file path and a
  dirfd-relative path with
  0x2000 all answer -EINVAL
  (the verdict precedes the name
  fetch); an unreadable times
  pointer with 0x2000 answers
  -EFAULT (the copy-in precedes
  the verdict); null times with
  0x2000 answers -EINVAL. Proof:
  guest tests
  testUtimensatFlags64Syscalls
  and testUtimensatFlags32Syscalls
  (tests `c178a704e`) fail on the
  base (rows answering 0 or
  -ENOENT; the refused 32-bit
  call demonstrably changed the
  fixture's mtime) and pass on
  the fix, including the pin
  that a refused call leaves the
  times untouched; suites and
  gates as in CL-106. Record and
  evidence as in CL-106.

- **CL-108 — FD-140 closed: a
  path through a non-directory
  answered -ENOENT at the path
  calls where the host answers
  -ENOTDIR.** Fixed on branch
  `lane-fd139-152` in `00d8f5369`:
  Fs::getNodeFromLocalPath gains
  an optional notDir out-report
  (set when the walk stops
  because a component was looked
  up inside a node that is not a
  directory); callers that do
  not pass it are unchanged, and
  a caller that asks always
  walks (a cached miss cannot
  carry the verdict). The
  guest-boundary path calls
  translate the miss: the
  process API (stat64/lstat64,
  the statfs pair, statx,
  fstatat64, access/faccessat,
  chmod/fchmodat, chdir,
  unlink/unlinkat, rmdir,
  mkdir/mkdirat, rename/
  renameat, link, symlink,
  readlink, utimes/utimesat,
  inotify_add_watch, open/
  openat including the create
  path — refused before any
  node is fabricated under a
  file — and execve), the
  64-bit dispatcher's own
  resolution sites (statfs,
  statx, the stat family,
  readlink, the xattr cases,
  the access family) and the
  32-bit DOSATTRIB xattr
  branches. Parent verdicts are
  judged on followed
  resolutions, so creation
  through a symlinked directory
  still works (control-pinned).
  The resolver's other consumers
  (loader, socket layer, sysfs,
  startup) do not ask and are
  unchanged. Host measured at
  the raw syscall level
  (test-results/fd139-152/
  hostref-fd139-152{,b}.c):
  -ENOTDIR uniform across 20+
  calls for a regular-file
  prefix (including through a
  symlink to a file, deeper
  nesting, and dirfd-relative
  forms); missing prefix
  -ENOENT; through a symlink to
  a directory succeeds. Two
  carve-outs, filed rather than
  folded in: the 64-bit
  utimensat stub probes only
  absolute and working-
  directory-relative paths (its
  descriptor-relative shapes
  keep the stub's standing
  success answer), and the
  normalization-erased shapes
  ("a.txt/", "a.txt/.",
  "a.txt/..") are FD-159. Proof:
  guest tests testNotDir64Syscalls,
  testNotDir32Syscalls and
  testNotDirProcessApi (tests
  `c178a704e`) fail on the base
  (69 assertions across the
  suites, answering -ENOENT and
  kin; two further RED rows in
  the same tests were traced to
  unrelated pre-existing
  defects — the unimplemented
  64-bit rmdir case and the
  bare-filename link
  destination, filed as FD-160
  and FD-161 — and their rows
  were corrected in `e8f120b22`)
  and pass on the fix;
  suites and gates as in CL-106.
  Filed from this lane: FD-157
  (32-bit xattr cases do not
  resolve for non-DOSATTRIB
  names), FD-158 (readlink of a
  missing path answers -EINVAL),
  FD-159 (normalization-erased
  -ENOTDIR shapes), FD-160 (the
  64-bit dispatcher's rmdir has
  no case), FD-161 (link to a
  bare filename answers
  -ENOENT). Record and
  evidence as in CL-106.

- **CL-109 — FD-148 closed: the
  emulator's Vulkan stack dropped
  the guest's unnormalised-
  coordinate sampler flag, so
  DXVK's D3D9 present blit — which
  samples the rendered image in
  texel space through a
  usePixelCoord sampler — read
  out of range everywhere except a
  sub-texel corner, and the
  swapchain reached the compositor
  ~black although the guest's
  clear had landed in its render
  target exactly.** *(Lane
  closure; the number settles at
  the next composition.)* Fixed on
  branch `lane-fd148-149` in
  `5ac7f7431` (test pin
  `82e3ebb51`): the bridge stores
  the flag on its Sampler record
  from both vkCreateSampler ABIs
  and appends it to the SAMPLER
  record; `web/vk-protocol.mjs`
  decodes it when bytes remain;
  the page tier divides the
  affected sample calls'
  coordinates (and textureSampleGrad
  gradients) by the texture
  dimensions in a per-draw WGSL
  pipeline variant keyed on the
  frame's sampler set, applied
  ahead of the border-colour
  rewrite so its [0,1] tests see
  normalised coordinates. Proof:
  on the base (integ tip
  `7a2308cd6`, wasm 05298a62)
  d3d9-comprehensive FAILs
  (219,863 ms; redDominant 4
  against >100000 required; the
  per-pass readback diagnostic
  showed the cleared target
  constant [232,40,100,255] while
  the presented swapchain read
  ~black); on the fixed build
  (wasm 2eccd757, 'Browser export
  verified') d3d9-comprehensive
  PASSes with all three stages
  RESULT 0 and redDominant
  722,880, and d3d11-triangle,
  d3d11-comprehensive,
  d3d10-comprehensive and
  d3d12-comprehensive PASS as
  controls. Native suites serial
  main and x64 both 0 FAILED; JS
  unit 353/0, including the new
  vk-unnorm-emulation suite (7/7).
  The line shifts the fix caused
  in the Vulkan gap register's
  citations are re-pinned in
  `57651cfea` (audit classification
  at exact parity with the base:
  232 MATCH / 14 MATCH-INHERITED /
  99 UNCHECKABLE / 7 EXEMPT).
  Record:
  `docs/reports/fd148-fd149-d3d9-20261104.md`;
  evidence `test-results/fd148-149/`
  in the lane's worktree.

- **CL-110 — FD-149 closed: the
  shipped 32-bit win32u module was
  the project's own backport,
  configured --without-vulkan by
  its build script, so it carried
  the built-without-Vulkan stub at
  the Vulkan driver entry: every
  32-bit session failed its
  graphics-driver load, Wine
  reported no Vulkan graphics
  driver, DXVK aborted on
  VK_KHR_surface, and the 32-bit
  D3D9 capture never became
  ready.** *(Lane closure; the
  number settles at the next
  composition.)* Fixed on branch
  `lane-fd148-149` in `6e3dba081`:
  the backport build's i686
  sysroot now carries the bookworm
  libvulkan dev+runtime debs
  (sha256-pinned in the script),
  SONAME_LIBVULKAN joins its
  fail-closed configure markers,
  and the build refuses a product
  carrying the stub string. The
  rebuilt module (sha256
  9d538e79..., 1,502,028 bytes;
  exports the Vulkan driver entry,
  dlopens libvulkan.so.1, which
  the guest tree already ships)
  restaged through
  regen-wine32-zip.sh — member
  verification identical except
  the four win32u members at the
  new pin — into the retained
  archive runtime-assets-20261104
  (514,457,600 bytes, sha256
  3c80c8a1...); the runtime
  completeness gate passes with 0
  failures and the bootstrap
  verifies all 13 assets. Proof:
  on the base, d3d9-32 FAILs
  (CAPTURE_READY never seen within
  300 s; console carries the stub
  error and the VK_KHR_surface
  abort); on the fixed build
  d3d9-32 PASSes (62,685 ms; the
  presented clear reads
  redDominant 722,880; zero stub
  lines in the console) and
  d3d9-comprehensive re-run PASSes
  on the same tree. Native suites
  serial main and x64 both 0
  FAILED; JS unit 353/0. Record:
  `docs/reports/fd148-fd149-d3d9-20261104.md`;
  evidence `test-results/fd148-149/`
  in the lane's worktree.

- **CL-111 — FD-155 closed: the
  32-bit specialization capture
  discarded refused guest input
  silently — its header guard was
  one lumped plain return shared by
  the legal empty shape and three
  genuine refusals (an unreadable
  VkSpecializationInfo, a
  mapEntryCount over the capture cap
  of 16, and claimed entries whose
  map-entry array or data is
  absent), where the 64-bit twin
  reports all three through
  VK64_DROP_IF.** *(Lane closure; the
  number settles at the next
  composition.)* Fixed on branch
  `lane-fd155` in `5bd07e141`:
  captureStageSpec32's header now
  mirrors captureStageSpec — the
  null-pointer and empty-spec shapes
  return ahead of the guards (legal
  no-ops, silent per FD-153) and the
  three refusals drop loudly through
  VK64_DROP_IF; census 255 -> 258
  sites, pin updated in
  vk-drop-macro-guard.test.mjs.
  Proof: new native FD-155 section
  (tests `64dbe21e5`) — RED on the
  unmodified bridge (aborts at the
  over-cap drop assertion, line
  2459, after the served control pin
  passes;
  `test-results/fd155/red-native.log`),
  GREEN on the fix (run-native 69
  PASS, 67 standing + 2 new;
  `test-results/fd155/green-native.log`):
  through vkCreateComputePipelines
  the over-cap, absent-data,
  absent-array and zero-data-size
  shapes each drop +1 with nothing
  captured and a valid entry is
  captured verbatim with no drop;
  direct pins keep the null spec
  pointer silent and make the
  unreadable struct drop +1. Native
  suites serial main 682 OK / 0
  FAILED and x64 108 OK / 0 FAILED;
  JS unit 353/0, legacy 115/0;
  browser build 'Browser export
  verified' (wasm `e0b6f721`,
  BUILD_INFO git_sha `0dc8a0c7f`);
  citation audit 352 rows, 0 drift
  (two re-pins, `0dc8a0c7f`). No new
  defects filed. Record:
  `docs/reports/fd155-capturespec32-silent-header-20261104.md`;
  evidence `test-results/fd155/` in
  the lane's worktree.

- **CL-112 -- FD-156 closed: the
  MEMORY64 variant of the Node
  self-test target did not compile
  because the SDL audio bridge's
  EM_ASM call cast the ring index
  addresses to int, a width a
  64-bit-pointer build cannot
  narrow to.** *(Lane closure; the
  number settles at the next
  composition.)* Fixed on branch
  `lane-fd156` in `3ee906b3a` (run
  instructions corrected in
  `b4d3f2e8f`): the addresses go to
  EM_ASM as pointers (sig `p`, the
  byte offset the ring base at $0
  already uses), which the MEMORY64
  runtime delivers to JS as a plain
  Number. Proof: the base build
  fails with exactly the two filed
  errors at kdspaudio.cpp:371 and
  no other error site; the fixed
  build is MAKE_EXIT=0, and the
  run under Node v24.11.0 (the
  box's Node 22 refuses the
  module's 64-bit table limits at
  instantiation) reports 953
  passed, 0 failed, exit 0 -- the
  other variants' standard. The
  standard variant still builds
  and runs 953/0; native suites
  serial main 682 / x64 108, 0
  FAILED; JS unit 353/0, legacy
  115/0; browser build 'Browser
  export verified' (BUILD_INFO
  git_sha `b4d3f2e8f`, dirty 0,
  wasm `aba69204...`). Record:
  `docs/reports/fd156-mem64-selftest-target-20261104.md`;
  evidence `test-results/fd156/`
  in the lane's worktree.

- **CL-113 — FD-157 closed: the
  32-bit dispatcher's xattr path
  forms did not resolve their
  target (and six of the eight
  forms had no handlers at all).**
  *(Lane closure; the number
  settles at the next
  composition.)* Fixed on branch
  `lane-fd157-161` in `7239fe69c`
  (tests `c22bc6805`): getxattr and
  lgetxattr resolve every name (a
  miss -ENOENT, through a file
  -ENOTDIR, an unresolved
  attribute on a resolved file
  -ENODATA), and setxattr,
  lsetxattr, listxattr, llistxattr,
  removexattr and lremovexattr
  share one helper that follows
  the host's measured entry order
  (set-form flags -EINVAL first,
  null path -EFAULT, null name
  -EFAULT, empty path -ENOENT,
  then resolution), answering
  -ENOTSUP (set/remove) or an
  empty list (list) for a resolved
  target. Proof: the guest test
  fails on the base with the filed
  answers (-ENOTSUP for the get
  forms, -ENOSYS for the six
  unhandled forms) and passes with
  the fix; native suites serial
  main 686 and x64 111, both 0
  FAILED; JS unit 353/0; browser
  build 'Browser export verified'
  (wasm fdc06d01). Host behaviour
  measured first at the raw
  syscall level: `test-results/
  fd157-161/hostref-fd157-161.out`
  in the lane's worktree. Record:
  `docs/reports/fd157-161-path-
  verdicts-20261104.md`.

- **CL-114 — FD-158 closed:
  readlink and readlinkat answered
  -EINVAL for a path that does not
  exist, on both dispatchers.**
  *(Lane closure; the number
  settles at the next
  composition.)* Fixed on branch
  `lane-fd157-161` in `7239fe69c`
  (tests `c22bc6805`): both resolver
  sites (KProcess::
  readlinkInDirectory and the
  64-bit dispatcher's
  sys_readlink64) split the missing
  case (-ENOENT) from the resolved
  non-link case (-EINVAL, kept);
  the through-a-file case keeps
  -ENOTDIR. Proof: the guest tests
  fail on the base (-EINVAL for a
  missing path on both dispatchers)
  and pass with the fix; suite
  tallies as in CL-113. Record:
  `docs/reports/fd157-161-path-
  verdicts-20261104.md`.

- **CL-115 — FD-159 closed: a path
  whose final component is erased
  by normalization (a trailing
  "/", "/." or "/.." through a
  regular file) resolved to the
  file itself where the host
  answers -ENOTDIR.** *(Lane
  closure; the number settles at
  the next composition.)* Fixed on
  branch `lane-fd157-161` in
  `7239fe69c` (tests `c22bc6805`): the
  resolver (Fs::
  getNodeFromLocalPath) judges the
  anchor component before the
  marker when the caller asks for
  the notDir verdict -- a
  non-directory anchor sets the
  verdict, a plain-missing anchor
  under "/.." misses, a directory
  anchor falls through to the
  ordinary walk -- and both
  readlink sites judge a marked
  link's followed target (-EINVAL
  to a directory, -ENOTDIR to a
  file). Proof: the guest tests
  fail on the base (the shapes
  resolve to the file and answer
  0; readlink of a marked link
  returns its target string) and
  pass with the fix; suite tallies
  as in CL-113. The mid-path form
  of the same erasure is filed
  separately as FD-164. Record:
  `docs/reports/fd157-161-path-
  verdicts-20261104.md`.

- **CL-116 — FD-160 closed: the
  64-bit dispatcher's rmdir (84)
  had no case and answered -ENOSYS
  for every path.** *(Lane
  closure; the number settles at
  the next composition.)* Fixed on
  branch `lane-fd157-161` in
  `7239fe69c` (tests `c22bc6805`): the
  dispatcher gained the case,
  routed to KProcess::rmdir with
  the neighbouring cases'
  null-path (-EFAULT) and
  empty-path (-ENOENT) verdicts.
  Proof: the guest test fails on
  the base (-ENOSYS for every
  shape, with the dispatcher's
  'unimplemented syscall #84' log
  line) and passes with the fix;
  suite tallies as in CL-113.
  Record: `docs/reports/fd157-161-
  path-verdicts-20261104.md`.

- **CL-117 — FD-161 closed: a hard
  link whose destination is a bare
  filename in the working directory
  answered -ENOENT even when the
  source exists.** *(Lane closure;
  the number settles at the next
  composition.)* Fixed on branch
  `lane-fd157-161` in `7239fe69c`
  (tests `c22bc6805`): Fs::
  getParentPath of a slash-less
  path returns the empty relative
  path (the working directory)
  instead of the string itself;
  every other consumer audited
  (the absolute-path callers are
  unchanged; the rename inotify
  parent lookups take the same
  shape and now name the working-
  directory node, as the host
  does). Proof: the guest test
  fails on the base (-ENOENT, no
  native destination created) and
  passes with the fix (the link is
  created, shares the source's
  inode, and a repeat answers
  -EEXIST); suite tallies as in
  CL-113. Record: `docs/reports/
  fd157-161-path-verdicts-
  20261104.md`.

- **CL-118 — FD-154 closed: the
  five browser-suite failures
  filed at the batch-5 gate
  (winsock-http, input-mouse-rel,
  input-fullscreen, jit64 and
  jitbench), each adjudicated to
  a product or harness cause and
  fixed.** *(Merge-audit closure:
  the lane annotated FD-154 with
  per-item verdicts rather than
  adding a closure.)* Fixed on
  branch `lane-fd154`.
  winsock-http — PRODUCT,
  `d26f606a1` (composed
  `41a1fa6f8`): every browser-build
  INET socket is marked
  loopbackStream and connect()
  refused loopback destinations
  internally, never using the
  native transport that reaches
  the relay; it now falls through
  to the native connect when no
  guest listener exists. Proof:
  the test passes and the relay
  reports accepted 1, 76 bytes
  guest-to-TCP, 324 bytes back --
  the filing's closing term for
  this item. input-mouse-rel —
  PRODUCT, `3cce91bcb` (composed
  `431376004`): both relative-
  motion producers advanced the
  sink's tracked pointer at queue
  time and the delivering
  connection added the delta
  again, doubling the first motion
  of every stream (injected 100,50
  -> guest 200,100); producers no
  longer advance and the
  connection reports deliveries
  back through the new
  noteDeliveredPointer().
  input-fullscreen — HARNESS over
  three PRODUCT layers: harness
  `e482eb6a6` (composed
  `0582a2310`) keeps the page
  unlocked (headless grants
  ClipCursor's pointer lock
  without a gesture, and clicks
  under lock arrive at (0,0)) and
  delivers the press through the
  injection channel; product
  `f610e90b2` (composed
  `a6f21e248`) -- a GrabModeSync
  grab no longer freezes input at
  grab time; `dbe7705f3` (composed
  `310300ce1`) -- own-grab pointer
  events deliver on the grabbing
  connection instead of dropping
  as foreign-target (Wine grabs on
  the root window, owned by
  another connection), and grab
  parameters plus every
  pointer-path drop reason now log
  under BW64_XWIRE; `25471296c`
  (composed `d1067de50`) -- sync
  grabs are treated as async in
  the delivery loop, matching the
  native X11 layer, since this
  client never sends XAllowEvents.
  jit64 — HARNESS, `189f6b302` +
  `41de3c327` (composed
  `354a94df6` and `f05288921`): the
  wineboot wait demanded the
  substring `wineboot.exe--init`,
  which the space-joined argv
  rendering never produces; the
  pattern matches the rendered
  form and the generated-code
  assertion gates on engine
  availability. jitbench —
  HARNESS, `b99c92154` +
  `260545f5b` + `1d2c059d8` +
  `87d9e021b` (composed
  `d1a9638ee`, `eca29cf14`,
  `e213fa86c` and `bb88bfcc4`): the
  boot never passed ?jit32, so
  CPU32 mode-1 could never latch,
  and the suite's interpreter-
  target build has no JIT engine
  to latch; assertions now gate on
  the stats' availability report
  and JIT-variant builds keep the
  full assertions. Two
  xwire-pointer-grab unit
  contracts updated to the
  measured semantics (`58f759879`,
  composed `df7356eb7`). Proof:
  all five reproduced stock on the
  unmodified base `3ea2fd97d`
  first; on the fixed build all
  five pass (the input tests on
  both architectures); JS unit
  353/0; native suites serial main
  682 tests 0 FAILED, x64 108
  tests 0 FAILED. Record:
  `docs/reports/fd154-adjudication-20261104.md`;
  evidence `test-results/fd154/`
  in the lane's worktree.

- **CL-119 — FD-162 closed: the
  32-bit xattr descriptor forms
  answer -EBADF for an unopened
  descriptor, flistxattr consults
  its descriptor, and the set
  form validates its flags word
  first.** *(Lane-local closure
  on `lane-fd162-164`; the number
  settles at the next
  composition.)* Fixed on branch
  `lane-fd162-164`: tests
  `ec62ca78e`, fix `9b4ce67b1` on
  base `4006ddb58`. The four
  descriptor forms now share the
  host's measured entry order —
  flags (-EINVAL outside 0x3),
  name fetch, descriptor — the
  descriptor verdict is -EBADF
  (the emulator-private -EBADFD
  is gone), and flistxattr
  answers -EBADF for an unopened
  descriptor and the empty list
  (0) for any open one. Proof:
  guest test
  testXattrDescriptor32Syscalls
  red on the base (the three new
  main-flavour tests together
  showed 41 wrong answers in the
  filed shapes, `test-results/
  fd162-164/red-main.log`), green
  on the fix; full native suites
  serial with 0 FAILED (main 689
  tests, x64 113 tests); JS unit
  suite 353/0; browser build
  'Browser export verified' on
  the fixed tree (wasm
  `0e5fa5dee8b2`, built from
  `872d1b474` with a clean tree).
  Host behaviour measured first
  at the raw syscall level
  (asm-generic numbers, strace
  cross-checked): `test-results/
  fd162-164/hostref-fd162-164
  .out`. Record: `docs/reports/
  fd162-164-xattr-namespaces-
  dotdot-20261105.md`; evidence
  `test-results/fd162-164/`.

- **CL-120 — FD-163 closed: the
  xattr get forms classify the
  name's namespace on a resolved
  target, and the empty name
  answers -ERANGE at fetch
  time.** *(Lane-local closure
  on `lane-fd162-164`; the number
  settles at the next
  composition.)* Fixed on branch
  `lane-fd162-164`, fix
  `9b4ce67b1`. One classifier per
  dispatcher, kept in step by
  hand: a prefix with no handler
  answers -ENOTSUP, a recognised
  namespace (user.*, trusted.*,
  security.*,
  system.posix_acl_access,
  system.posix_acl_default) with
  an absent attribute -ENODATA, a
  bare recognised prefix ("user."
  itself) -EINVAL; the empty name
  answers -ERANGE at fetch time
  on every name-taking form,
  ahead of the path and
  descriptor verdicts. The
  set/remove forms keep their
  standing -ENOTSUP (FD-157);
  their namespace classification
  is filed as FD-165. Proof:
  guest tests
  testXattrNamespace32Syscalls
  and testXattrNamespace64Syscalls
  red on the base, green on the
  fix; suite tallies as in
  CL-119. Record and evidence as
  in CL-119.

- **CL-121 — FD-164 closed: a
  non-trailing ".." steps through
  its predecessor in the
  resolver, and ".." at the root
  clamps.** *(Lane-local closure
  on `lane-fd162-164`; the number
  settles at the next
  composition.)* Fixed on branch
  `lane-fd162-164`, fixes
  `9b4ce67b1` and `872d1b474`.
  When a caller asks for the
  notDir verdict, the resolver
  judges each non-trailing ".."
  by resolving the path ahead of
  it (a miss propagates as
  -ENOENT, a predecessor
  resolving to a non-directory
  sets the -ENOTDIR verdict), and
  the normalisation clamps a ".."
  at the root instead of failing
  the walk; the trailing-marker
  family stays FD-159's rule and
  callers not asking for the
  verdict keep the erased walk.
  The second fix is the same scan
  in the restricted source subset
  the fs-dynamic-link-cache unit
  test compiles; behaviour
  identical. Proof: guest tests
  testMidDotDot32Syscalls and
  testMidDotDot64Syscalls red on
  the base, green on the fix;
  suite tallies as in CL-119.
  Record and evidence as in
  CL-119.

- **CL-122 — FD-31 closed: the
  64-bit native test binary's
  single-process full run died
  partway (exit 139, clone/fork
  neighbourhood).** The dying mode
  was the harness's multi-worker
  thread mode; it is replaced by
  process sharding (CL-123), under
  which the binary completes its
  full run at the default worker
  count. Fixed on branch
  `lane-buildrel`, commit
  `05ab65469`. Proof: on the fixed
  tree the preserved thread path
  still dies exit 139 after 79 of
  111 tests (last output in the
  clone3 neighbourhood), while the
  default shard run completes 111
  tests, 0 FAILED (22 s), the
  serial run 111/0, and two of
  three explicit 4-shard runs
  111/0 -- the third had one
  futex wait/wake race failure
  under peak load (13-17 on 4
  CPUs), the test passing 6/6
  standalone and in both re-runs;
  recorded in the lane record as a
  load-sensitive race-test flake
  with the failing log preserved,
  not a shard-mode defect. Record:
  `docs/reports/buildrel-fd31-32-33-20261105.md`;
  evidence `test-results/buildrel/`
  in the lane's worktree.
- **CL-123 — FD-32 closed:
  multi-worker native suite runs
  died with exit 139 inside the
  first few tests.** Parallelism
  in the test harness is now
  process sharding instead of
  in-process threads
  (`testRunParallel` in
  source/test/cpu/testCPU.cpp):
  workerCount shard processes each
  run a strided slice through the
  serial mode's unchanged
  fork-per-test isolated path, so
  every test keeps its standalone
  environment; a shard's output is
  captured and replayed in shard
  order, its failure count returns
  over a pipe, and a shard that
  dies before reporting fails all
  its tests loudly, so the final
  total can never silently shrink.
  Fixed on branch `lane-buildrel`,
  commit `05ab65469`. Proof: the
  preserved thread path
  (BW64_TEST_NO_SHARD=1) dies exit
  139 after 2 tests amid a runaway
  guest-memfault loop (count past
  26,624); 4 shards complete the
  main suite 686 tests, 0 FAILED
  (341 s vs 510 s serial), the
  default invocation likewise
  686/0, and the 64-bit suite
  completes in parallel too
  (CL-122) -- every parallel total
  equals its serial total. Record:
  `docs/reports/buildrel-fd31-32-33-20261105.md`;
  evidence `test-results/buildrel/`
  in the lane's worktree.
- **CL-124 — FD-33 closed: the
  native release build failed to
  link, and its configuration had
  a filed JIT compile error.**
  (a) reproduced on the base
  exactly as filed (undefined
  `msgScratch`, `tickXWirePresent`,
  `hasMainThreadWork`): the
  per-thread message scratch, its
  caches and the execve
  invalidation entry point were
  defined inside syscall64.cpp's
  BOXEDWINE_GUEST_X64 gate while
  the 32-bit dispatcher's
  sendmsg/recvmsg slow paths call
  the scratch unguarded -- they
  move ahead of the gate; the
  execve invalidation call in
  kprocess.cpp is ungated with
  them (a 32-bit execve previously
  left the cache stale in
  non-64-bit builds); the
  multi-threaded SDL mainloop
  guards its wire-present calls
  with the same gate the
  single-threaded mainloop already
  used; and the test build's
  msgScratch stub (which returned
  0 and silently routed tests to a
  fallback path) is deleted, so
  the test build links the real
  scratch. (b) not reproduced:
  the JIT threshold helper's uses
  outside the WASM JIT
  configuration already carry the
  guard on the current tree, and
  the configuration's full compile
  is error-free. Fixed on branch
  `lane-buildrel`, commit
  `05ab65469`. Proof: the release
  build failed to link on the base
  and links with the fix, the
  binary starting and shutting
  down cleanly; all five native
  configurations of project/linux
  build (release, test, x64,
  x64-test, x64-cache, each exit
  0); the browser build completes
  'Browser export verified' on the
  same commit (wasm `7daab6b2`,
  BUILD_INFO dirty 0, support
  archive at pin). Record:
  `docs/reports/buildrel-fd31-32-33-20261105.md`;
  evidence `test-results/buildrel/`
  in the lane's worktree.

- **CL-127 — FD-115 closed: a
  first-visit 32-bit start that
  wedges before the desktop handshake
  in a delivered-fault loop is now
  reported as a failed start instead
  of an endless wait.** Closed on
  the entry's second closing term
  (surfacing); the fault's mechanism
  was not identified and no fix to
  the fault itself is claimed — the
  single 2026-11-02 capture remains
  the only instance of the shape,
  which did not recur in 54 measured
  shipped-build starts or in 70
  measured starts on builds carrying
  the surfacing. Fixed on branch
  `lane-fd115`: fix `de0144c72`,
  probe `4ce0dc424`. The launcher's
  stall watchdog counts printed
  delivered-fault lines per guest
  thread before the handshake; at 32
  from one thread the start is
  reported failed
  (reason=prehandshake-fault-loop)
  with the held dump evidence
  flushed to the session log. Proof:
  unit tests RED on the base and
  GREEN with the fix
  (stall-watchdog.test.mjs); the
  shaped-session probe passes all
  five scenarios against the built
  dist (`b7f5d4003`, BUILD COMPLETE
  'Browser export verified', wasm
  unchanged `c0af957a`); JS unit
  suite 353/0 and `npm test` ALL
  TESTS PASSED on the fixed tree;
  first-visit x86 series on fixed
  builds 34/34 measured certified
  (FD-115-only build `4ce0dc424`) and
  36/36 certified (final build
  `b7f5d4003`), 0 BW64-FAIL and 0
  guest-memfault32 lines in any
  preserved console, 0 restarts.
  Record:
  `docs/reports/fd115-fd165-prehandshake-surfacing-20261105.md`;
  evidence `test-results/fd115/` in
  the lane's worktree.

- **CL-128 — FD-166 closed: a
  first-visit start that stalls
  before the desktop handshake with
  no guest process making progress
  is now reported as a failed start
  instead of an endless wait.** The
  defect was filed from this lane's
  live capture on the shipped
  batch-7 build (see FD-166's entry:
  boot 10 of the reproduction
  series, closing dumps at
  min_noprogress_ms=780078 across
  the session's earliest processes,
  no fault lines, no surfacing).
  Fixed on branch `lane-fd115`: fix
  `3a4d59793`, probe `b7f5d4003`.
  The stall watchdog gains a
  pre-handshake dump rule: before
  the handshake, when every process
  in a complete thread dump (footer
  seen, no lazy fetch in flight) has
  made no guest progress for
  300,000 ms and the next dump shows
  the same, the start is reported
  failed
  (reason=prehandshake-no-progress)
  with the held dump evidence
  flushed to the session log. One
  progressing process anywhere, or
  a fetch in flight, resets the
  count; the threshold sits far past
  every healthy start on record (the
  slowest certified boot in the
  lane's series took 199 s of wall
  time at box load 38, and its dumps
  still showed min_noprogress_ms of
  0 — the rule measures progress,
  not wall time). Proof: the three
  FD-166 unit tests fail on the base
  launcher and pass with the fix
  (14/14 in stall-watchdog.test.mjs);
  the shaped-session probe's
  pre-handshake no-progress scenario
  passes against the built dist; the
  final first-visit series on that
  build: 36/36 certified, 0
  BW64-FAIL lines, 0 restarts,
  median certified-ready 40.3 s. No
  mechanism is claimed for the
  underlying guest stall. Record:
  `docs/reports/fd115-fd165-prehandshake-surfacing-20261105.md`;
  evidence `test-results/fd115/` in
  the lane's worktree.

- **CL-129 — FD-165 closed: the
  xattr set/remove forms classify
  names as the host does, and
  recognised namespaces are
  stored and read back.**
  *(Lane-local closure on
  `lane-fd165`; the number
  settles at the next
  composition.)* Fixed on branch
  `lane-fd165`: tests
  `09bd3f452`, fixes `a2899c5b5`
  and `37dba7b69` on base
  `c4dd96ecd`. Both dispatchers
  now run the host's measured
  entry order for the write
  forms — flags, name fetch,
  value copy (a null value with
  a nonzero size answers
  -EFAULT ahead of the target
  verdicts), target resolution —
  then judge the name: an
  unknown family and system.*
  outside the posix_acl names
  answer -ENOTSUP, a bare
  recognised prefix
  (user./trusted./security.)
  answers -EINVAL, a posix_acl
  set answers -EINVAL (and now
  reports in the log: ACL values
  are unmodelled) and a posix_acl
  remove on a file or directory
  is a no-op success, and on the
  link itself every user.* name
  answers -EPERM while
  trusted.*/security.* behave as
  on a file. Recognised
  namespaces are stored (one
  native sidecar per attribute
  beside the modelled
  attribute's own; sidecars are
  hidden from listings and move
  and delete with their file),
  honouring CREATE (-EEXIST) and
  REPLACE (-ENODATA); the
  get/list forms serve stored
  values and names with the
  host's size semantics, and the
  64-bit get forms serve the
  modelled user.DOSATTRIB they
  never had. The 32-bit
  descriptor set form no longer
  stores the modelled attribute
  and still reports -ENOTSUP.
  Proof: guest tests
  testXattrWriteClassify32Syscalls
  and
  testXattrWriteClassify64Syscalls
  red on the base (with the three
  updated standing tests: main 3
  FAILED, x64 2 FAILED —
  `test-results/fd165/red-main
  .log`, `red-x64.log`), green on
  the fix; full native suites
  serial with 0 FAILED (main 690
  tests, x64 114 tests); JS unit
  suite 353/0; browser build
  'Browser export verified' on
  the fixed tree (wasm
  `521937b979`, built from
  `37dba7b69` with a clean tree).
  Host behaviour measured first
  at the raw syscall level
  (asm-generic numbers, strace
  cross-checked):
  `test-results/fd165/hostref-
  fd165.out`, `hostref-fd165b/c/d
  .out`. Record: `docs/reports/
  fd165-xattr-write-
  classification-20261105.md`;
  evidence `test-results/fd165/`.

- **CL-125 — FD-178 closed: fresh
  worktrees link their generated
  payload inputs from one shared,
  pin-verified store instead of
  copying ~2 GB per tree.** The
  store `/home/ubuntu/boxedwine64-shared`
  (1,980,622,511 bytes, 18,718
  files) holds the six pinned
  runtime zips and Slang assets
  — extracted from the retained
  runtime-assets-20261104
  archive after its sha256 was
  verified against the release
  pin, every file then matching
  the committed
  `runtime-assets.json` pins —
  plus the DXVK build outputs
  (full-tree digests
  byte-identical to the pinned
  wt-fd38 copies), the DXVK
  install set, the vkd3d runtime
  sets, the `tools/rootfs64`
  payload extractions and the
  zig/disasm payloads, with a
  MANIFEST.json recording
  provenance and a SHA256SUMS
  covering every file.
  `winbox-wt-prep.sh` (repo copy
  `tools/winbox-wt-prep.sh`;
  installed over
  `/home/ubuntu/scripts/winbox-wt-prep.sh`,
  the old script kept beside it
  as `.pre-shared-20261105`)
  verifies the 13 pins and the
  full sums before linking,
  creates 23 payload links plus
  the node_modules and
  web/vendor links, and no
  longer overwrites the
  runtime-assets manifest.
  Proof, side by side on integ
  48d27952a: the old-way tree
  (1,981,059,339 real payload
  bytes; its scripted prep
  failed partway and its
  main-checkout DXVK failed the
  build's manifest gate before
  the pinned-donor correction)
  and the new-way tree (557 real
  payload bytes) both reach
  BUILD COMPLETE with the
  pinned dependency archive
  c4dbe35986a5 verified and
  byte-identical product wasm
  9b51d60101ab; JS suites run
  solo per tree — old-way ALL
  TESTS PASSED (legacy exit 0,
  unit 353/0), new-way legacy
  exit 0 and unit 349/4 in a
  full run under heavier box
  load, the four failures
  (boot-progress-snapshot and
  three x11 tests, none reading
  a payload path) all passing
  on filtered re-runs in the
  same tree (36/0 and 1/0);
  the known-gaps audit exits 0
  in both trees (its counts
  differ only inside the linked
  payload extractions, which it
  does not follow symlinks
  into). A D3D browser
  conformance run in the new-way
  tree stayed queued behind
  other lanes' builds for the
  whole proof window and was
  stood down unstarted — not
  claimed as proof; payload
  consumption rests on the
  build's staging checks, the
  manifest gate and the suites
  above. Existing trees, the
  preserved vkd3d source trees
  and the retained release
  archives were not touched;
  adoption is by new trees.
  Record:
  `docs/reports/shared-build-store-20261105.md`;
  evidence `test-results/sharedbuild/`.

- **CL-130 — FD-19 closed as
  disproven: the KHR spelling of
  the promoted physical-device
  properties query IS served on
  the 64-bit guest path.**
  *(Provisional number —
  lane-fd162-164 claims
  CL-130..CL-121 on its branch;
  settles at the next
  composition.)* Fixed on branch
  `lane-audit-l1l2` (test commit
  `64e7e3c0e`). The entry said the
  64-bit Vulkan dispatcher has no
  `vkGetPhysicalDeviceProperties2KHR`
  case, unlike its 32-bit sibling.
  The case is absent — and no
  64-bit guest can reach the
  switch under that id: the 64-bit
  guest shim's proc-address table
  maps the KHR name to a real
  alias wrapper that forwards to
  the core wrapper, so the trap
  arrives as
  `VK64_fn_vkGetPhysicalDeviceProperties2`,
  which the switch serves. The
  32-bit shim instead traps the
  KHR id itself, and the 32-bit
  dispatcher carries the paired
  cases for exactly that reason.
  Proof: the native bridge test
  gained a GUARD section that, for
  each of the three promoted
  query pairs (Properties2,
  Features2, MemoryProperties2),
  resolves the KHR name through
  the real shim's
  `vkGetInstanceProcAddr`, calls
  through the returned pointer,
  and pins (a) the trap arriving
  under the core fn id and (b)
  byte-identical guest-memory
  answers against the core name,
  root struct and pNext chain
  included, with spot checks that
  the fills really happened
  (driver name, timeline-semaphore
  flag, heap count). run-native
  70 PASS (69 standing + this
  section), exit 0; evidence
  `test-results/audit-l1l2/runnative-5.log`
  in the lane's worktree. Record:
  `docs/reports/audit-followup-l1-l2-20261105.md`.
  The pointer-identity nuance the
  test's first run exposed is
  filed as FD-166.

- **CL-131 — FD-170 discharged:
  ninety shipped DLL tables were
  ≥90% unimplemented stubs (6,734
  named stubs over 50 distinct
  DLLs), and the entry's closing
  terms required a recorded
  per-DLL decision —
  implemented, substituted, or
  accepted as a documented
  absence.** Every one of the 50
  now carries its decision in the
  FD-170 entry. The stub state of
  all 50 was measured in Wine
  8.0, Wine 10.0 and upstream
  master and is essentially
  identical in all three for 49
  of them. Implemented on the
  x64 side on branch
  `lane-fd170`, commit
  `d7fb7340a`
  (`wine8-fd170-contracts.patch`
  in the Wine 8.0 backport
  pipeline, the three DLLs
  hash-pinned fail-closed and
  staged into wine64.zip):
  bluetoothapis 95 → 71 named
  stubs (Wine 10.0's radio/SDP
  code + the FD-167 contract
  bodies), winusb 21 → 13 (the
  FD-167 no-device contracts),
  netutils 22 → 17 (Wine 10.0's
  NetApiBuffer*). Proof: the
  census run against the built
  binaries differs from the base
  baseline in exactly those
  three tables (33,756 → 33,719
  named stubs; baseline
  regenerated in the same
  change, `--check` exit 0);
  `bootstrap.py --check` passes
  on the re-cut
  `runtime-assets-20261105-fd170`
  archive; the JS unit suite
  passes 352 with 2 failures
  both attributable to the
  borrowed gate dist this lane
  tested against (the census
  guard test itself passes).
  The guest fixture
  `fd170-contracts64.test.mjs`
  is committed; its verdict is
  owed to the batch-9 gate —
  every 2026-11-05/06 attempt
  died in the shared probe queue
  or in a guest boot that
  outlasted the hung-probe cap
  at load 13–19, the same
  handling FD-167's x64 side
  received (build-level proof only — the FD-167 lane claims no closure
  number; FD-167 stays open). The
  other 47 DLLs are accepted as
  documented absences, each
  with its concrete missing
  substrate recorded in the
  FD-170 entry (service-bound,
  hardware-bound, DRM-bound,
  app-model-bound, or
  never-implemented-anywhere
  surfaces). Record:
  `docs/reports/fd170-whole-dll-decisions-20261105.md`;
  evidence `test-results/fd170/`
  in the lane's worktree.

- **CL-126 — FD-173 closed:
  fresh working copies start
  from a verified per-commit
  build seed, so their first
  build is incremental too.**
  *(Lane-local closure on
  `lane-incremental-build`; the
  number settles at the next
  composition.)* Fixed on branch
  `lane-incremental-build`,
  commit `1dae2e9d5`. The shared
  store now also holds build
  seeds (`build-seed/<commit>/`:
  the verified
  `project/emscripten/Build/Wasm64Mt`
  of one exact commit, with a
  SEED.json recording the commit
  and the program file's hash);
  `tools/winbox-wt-prep.sh`
  verifies and installs the seed
  into fresh copies of that
  commit (mirrored source paths
  relocated to the new tree,
  dependency files rewritten,
  timestamps refreshed), and
  `tools/winbox-publish-build-seed.sh`
  publishes a seed from any
  verified clean build, refusing
  dirty or mismatched ones. A
  missing or mismatched seed
  leaves a copy exactly as
  before. Proof (base and proof
  trees at `48d27952a`; every
  build's program file sha256
  `9b51d60101ab`, the value other
  lanes recorded for this commit
  from their own trees): a fresh
  unseeded copy's first build
  compiled all 270 files
  (2,491 s wall on the shared
  box); in an already-built copy,
  no-change and one-file-change
  rebuilds recompiled 0 and
  exactly 1 files; a seeded fresh
  copy's first build after the
  same one-file change took
  213 s and recompiled exactly 1
  file (`crc.cpp.o`), and the
  same seeded tree rebuilt from
  scratch took 2,345 s, compiled
  all 270, and produced the
  byte-identical program file;
  the JS unit suite passed
  353/0 and the legacy suite
  115/0 (1 skipped) on the seeded
  copy's incremental output; a
  seedless commit prepared
  exactly as before. The
  FD-174 fix rides in the same
  commit. Record:
  `docs/reports/incremental-build-seed-20261105.md`;
  evidence `test-results/incbuild/`
  in the lane's worktree.

- **CL-132 — FD-186 closed:
  compiles route through one
  machine-wide compiler cache
  shared by every working
  copy.** ccache 4.9.1 (the
  box's package) remembers each
  compile keyed on the
  preprocessed source and the
  exact command line; identical
  guarded blocks in
  `project/emscripten/makefile`
  and `project/linux/makefile`
  prefix CC/CXX with `ccache`
  when it is installed and
  `BW64_CCACHE` is not `0`,
  export `CCACHE_BASEDIR` as
  the tree's own root (without
  it the rules' absolute source
  paths make every copy miss —
  measured), and use the shared
  directory
  `/home/ubuntu/boxedwine64-cache/ccache`
  when it exists. The worktree
  preparation step creates that
  directory and asserts its
  2.0 GB cap + compression, so
  future copies are covered
  automatically. Proof (base
  `48d27952a`, loads in the
  record): browser build
  1,434 s uncached, 1,084 s
  cold cache, 182 s warm, 175 s
  for a fresh tree's first
  build, the product wasm
  byte-identical in all four
  (sha256 `9b51d60101ab…`);
  native release 747 s, 543 s
  cold, 3 s warm (377/377
  hits), cached binaries
  identical to each other and
  differing from uncached only
  in embedded source-path
  annotation strings; suites on
  the cache-built tree: native
  main 689 tests 0 FAILED, x64
  0 FAILED, JS unit 353/0.
  Record:
  `docs/reports/build-cache-20261105.md`;
  evidence
  `test-results/buildcache/`.

- **CL-133 — FD-187 closed: statfs named one flat
  filesystem for every path, which hid every synthetic
  device from guest udev.** *(Lane-local closure on
  `lane-gamepad`; the number settles at the next
  composition.)*
  The 32-bit process layer answered f_type 0xEF53 (EXT3)
  everywhere and the 64-bit layer the tmpfs magic
  everywhere. The guest's libudev verifies a device's
  sysfs path by requiring fstatfs to report SYSFS_MAGIC
  there (measured by disassembling the staged libudev:
  its set-syspath verification refuses the device
  otherwise), so every synthetic device — the gamepad
  included — was rejected before enumeration. All four
  entry points (32-bit statfs/statfs64/fstatfs, 64-bit
  statfs64/fstatfs64) now report SYSFS_MAGIC (0x62656572)
  for nodes under `/sys` and PROC_SUPER_MAGIC (0x9fa0)
  under `/proc`, keeping the previous answers elsewhere.
  Proof: native tests `testStatfsMagicByMount32Calls` and
  `testStatfsMagicByMount64Syscalls` pin the per-tree
  magics on both widths (path and descriptor forms);
  native suites serial, 0 FAILED (main 690 tests, x64 114 tests); JS unit suite
  353/0; and in the browser build a guest probe then
  enumerated the gamepad through libudev (device creation
  from its sysfs path succeeds, subsystem "input") where
  the unmodified build enumerated nothing. Fix commits
  `6b6186d93` + `bcff6378b` on base `48d27952a`. Record:
  `docs/reports/fd24-gamepad-20261106.md`.
- **CL-137 — FD-28 closed: one generated graphics file
  had silently diverged from its generator.**
  *(Provisional closure on `lane-vkhost-gen`; the number
  settles at the next composition. CL-137 is also claimed
  by `lane-x11-getramptr-audit` for its FD-193 closure.)*
  `vk_host.cpp` presented itself as autogenerated but
  carried hand-written implementations (initVulkan error
  propagation, mapVkMemory failure handling,
  push-descriptor-template support, log-once graceful
  degrades for eight unsupported operations, and
  log-once-and-skip handling of unknown pNext structures)
  that the Java generator in `tools/vulkan/gen` did not
  emit; a wholesale regeneration silently reverted them
  (baseline diff on the base tip: 66 insertions / 69
  deletions in that one file). The generator now emits
  all of them, through its own name-keyed special cases:
  the guarded initVulkan preamble and prologue, a
  writeCustomFunctionBody for the eight degrade bodies,
  failure-checked map after() emissions, a new
  VkCmdPushDescriptorSetWithTemplateMarshal, and
  log-once-and-skip unknown-sType defaults in the three
  pNext writers (which also discharges the next-pointer
  part of FD-27). Proof: regeneration with the rebuilt
  generator reproduces all six outputs byte-identically
  (sha256-verified against the committed blobs;
  vk_host.cpp `30339455c348…`), a second run identical,
  and the regenerated vk_host.cpp passes
  `g++ -fsyntax-only` with the project flags. Checking-queue pipeline 18, submitted at the fix commit, was voided by the concurrent-submission mount race filed as FD-194 (its verify-source mounted the commit of a sibling lane; pipelines 19-21 died the same way), and a re-submission (pipeline 28) was still queued behind the submission flood when this lane closed; the light checks were therefore run by hand on this tree with the exact commands the pipeline runs: citations exit 0 (352 rows, 0 drift), JS unit 352 passed / 2 failed (only the two known bare-checkout artifacts: provenance-artifacts, runtime-completeness), JS legacy 115/0. The native suites were not re-run by hand because the change touches no compiled source (generator tooling only; every generated output byte-identical to the base).
  Fix commit `26a90c5b6` on base `faf95d26c`. Record:
  `docs/reports/fd28-vkhost-generator-20261106.md`;
  evidence: `test-results/fd28/`.

- **CL-134 — FD-176 closed: the block-invalidation
  differential gate links again — and actually executes blocks.**
  *(Lane-local closure on `lane-fd176`; the number settles at the
  next composition.)*
  The gate script's invalidate-test link predated the FD-82/FD-129
  cross-references and failed with undefined references
  (`FsOpenNode::noteMappedPagesReleased`,
  `bwCurrentThreadRegistryLookup()`, `KSystem::guestCpuCount()`).
  The recipe now links the kernel core set (fsopennode, fsnode,
  kthread, ksystem, kinotify, kscheduler, synchronization, bfile,
  platform/linux, with sdl2-config flags) and the three harness
  stubs that stood in for the unlinked kernel are removed — one of
  them (`useF64 = true`) also held the wrong value for the 64-bit
  guest, whose product value is false. The same gate carried a
  second, vacuity defect: as scripted, both binaries interpreted
  (tryBlockStep is gated on `jit64Enabled`, fast dispatch defaults
  on, the engine defaults to interp), so the differential proved
  nothing. The blocks binary now runs with `BW64_JIT=1
  BW64_JIT_ENGINE=off BW64_JIT_FAST_DISPATCH=0`, proven by probe
  to reach `tryBlockStep` and by sabotage (the bw64NoteWriteSlow
  funnel neutered) to fail T1/T3/T4 with stale values.
  Proof: full `./scripts/build-jit-wasm.sh --selftest-only` exit 0
  (jit64 phase-1 873/0; gen emitters byte-identical; invalidate
  interp+blocks pass=54 fail=0, logs byte-identical; wasm fixture
  tests 314/0). Fix commit `5bec2fa3e` on base `faf95d26c`.
  Record: `docs/reports/fd176-invalidate-gate-link-20261106.md`.

- **CL-135 — FD-189 closed: the shipped 64-bit guest prefix now
  registers the PlugPlay service and the ROOT\WINE bus devices, the
  way the 32-bit prefix does.** *(Lane-local closure on
  `lane-fd189`; the number settles at the next composition.)*
  The shipped prefix64.zip carried exactly one service registration
  (MountMgr) and no PlugPlay, winebus or wineusb sections and no
  ROOT\WINE enumeration, so a 64-bit guest probe answered
  OpenService error 1060 for all three services and found an empty
  root enumeration. The fragment `tools/rootfs64/prefix64-reg.txt`
  now carries the seven PnP sections verbatim from the 32-bit
  prefix's system.reg, and the shipped prefix guard now requires
  them on win64 too. Runtime assets re-pinned to
  `runtime-assets-20261106-fd189` (only prefix64.zip changed).
  Proof: the checker is RED on the unfixed set (the new FD-189
  check names all five missing pieces) and the fd189-services64
  browser fixture is RED on the base build (three 1060s + empty
  enumeration, RESULT 4), both GREEN after the fix — the 64-bit
  fixture reports all three services present and both bus devices
  enumerating (RESULT 0) and the 32-bit control stays green; the
  fixed browser build's wasm is byte-identical to the baseline
  (payload-only change); the completeness checker passes 0 failures
  / 0 skips and `bootstrap.py --check` verifies 13 files; the
  citation audit is 352 rows, 0 drift; JS checks ALL TESTS PASSED
  (legacy exit 0, unit 354/0); native suites serial main 692 tests
  0 FAILED, x64 116 tests 0 FAILED; run-native 71 PASS, 0 FAIL.
  Fix commit `44ab5edcf` on base `faf95d26c`. Record:
  `docs/reports/fd189-plugplay-prefix64-20261106.md`.

- **CL-136 — FD-01 closure proof: the first-visit start fix
  stands proven on the live batch-9 build.** *(Settled at the
  batch-10 composition; the proof is the `lane-fd01-proof`
  lane's work, composed in this batch.)*
  FD-01's closure terms required the fix merged, deployed, and
  the live first-visit rate re-measured through the public site
  under the hardened FD-47 protocol. The proof lane re-ran that
  protocol against the live batch-9 build (git_sha `d50bcc02f`,
  wasm `92b36180...`): fresh profile per boot, restart budget
  pre-exhausted (bwretry=2), 900 s window, offline FD-47
  classification. The batch-9 deploy lane certified 3/3 x86-64
  (median ready 88.1 s) and 5/5 x86 (median 18.8 s) on the live
  build; the proof lane independently certified a further 3/3
  x86-64 (95.7/105.4/126.8 s) — a batch-9 total of 6/6 x86-64
  (median 100.5 s) and 5/5 x86, every boot with the real handle
  (0x10048 / 0x10052), explorer opened, zero restarts, zero
  rescues, zero false-ready, zero failure signatures. The
  closure stands on batch 9. The restart stopgap was needed
  zero times; retiring it remains a separate decision, not
  part of this closure. Record:
  `docs/reports/fd01-proof-batch9-20261106.md`; evidence
  `test-results/fd01-proof-batch9-20261106/` and
  `test-results/deploy-batch9-20261106/cert-x64`, `cert-x86`.

- **CL-137 — FD-188's driver-load layer closed: kernel-mode
  Wine drivers initialise in the emulator.** *(Settled at the
  batch-10 composition from `lane-fd188`. FD-188's entry
  remains open on its remaining terms — see FD-195.)*
  Wine's driver loader opens every driver by the absolute path
  `C:\windows\system32\drivers\<name>.sys`, but the shipped
  prefix redirects system32 to the Wine lib dir, which carried
  the .sys files and no `drivers/` subdirectory — every driver
  load failed STATUS_DLL_NOT_FOUND (c0000135) and ZwLoadDriver
  failed c0000142. The fix mirrors every .sys file into a
  `drivers/` subdirectory in both wine archives (wine32: 20,
  wine64: 17) and ships the five driver .inf files in the wine
  archives' `share/wine/` and the 32-bit prefix's
  `C:\windows\inf\`; completeness guards (`t_drivers_dir`,
  `t_driver_inf`) pin the shape. Proof on the lane: the
  fd188-driver-probe guest test PASS twice (no c0000135 /
  c0000142 lines, all four boot drivers initialise, the drivers
  directory lists 22 entries, WineBus service state 4); the
  browser build's program file byte-identical to the pre-fix
  build (payload-only change); queue pipeline 16 on the fix
  commit (citations 0 drift; js-unit 352 passed / 2 failed —
  the two known bare-checkout artifacts; native main 692
  tests 0 FAILED, x64 116 tests 0 FAILED; run-native all
  PASS). Fix commit `7b0c2a50b` on base `faf95d26c`, composed
  in this batch as `89d9b8c87`. Record:
  `docs/reports/fd188-drivers-initialise-20261107.md`.
  What did NOT close: the FD-24 witness still sees no
  controller — the guest enumerates no HID device at all.
  That remainder is FD-195, and FD-188's entry stays open
  on it.

- **CL-138 — FD-196 closed: the prefix-packaging test fixtures
  carry FD-188's required members.** *(Batch-10 composition.)*
  The batch-10 composition ran
  `tools/rootfs64/test_prepare_prefixes.py` on the composed
  tree and found 3 of 11 tests in error; the same failure
  reproduces on the `lane-fd188` tip and on neither the base
  tip nor `lane-fd189` — FD-188's validation tightening
  (CL-137) had left the synthetic fixtures behind. The fix
  touches fixtures only: the win32 valid-prefix fixture now
  includes the five driver .inf files, and the synthetic wine
  runtime mirrors its three .sys modules under `drivers/`.
  The validators are unchanged. Proof:
  `test_prepare_prefixes.py` runs 11 tests OK on the composed
  tree.

- **CL-139 — FD-198 closed: the known-gaps allowlist covers
  both documentary mentions in the gate script.**
  *(Batch-10 composition.)* Commit `9c42e22b0`: the
  allowlist entry for `scripts/build-jit-wasm.sh` now counts
  2 mentions and its reason covers both — the ripSampler
  inert-stubs note and FD-176's note recording the removed
  harness stubs. Both mentions are documentary comments, not
  gaps. Proof: the known-gaps audit unit test passes on the
  composed tree (the batch-10 gate's targeted re-run after
  the repair commit, log
  `test-results/merge-batch10-20261106/unit-targeted-repair.log`).

- **CL-140 — FD-199 closed: the flattened wineserver entry
  in the shipped 64-bit runtime archive is a symlink
  again.** *(Batch-10 composition.)* The repair is at the
  byte level: the central-directory external attributes of
  `usr/lib/wine/wineserver` were restored from regular-file
  0o600 to symlink 0o120755; a central-directory comparison
  against the defective zip shows exactly that one field
  differing across all 1,280 members — names, sizes, CRCs
  and compressed sizes identical — and the repaired zip
  (`133bfd70840e6c3dc60d63a9cc5aacc2d75b66e814914299ccac3dd5ea5c8604`,
  194,844,190 bytes, size unchanged) passes a full testzip.
  The retained combined archive was re-cut around it
  (`browser-runtime-20261107.tar`,
  `f54d0550bc413a34b91c1c66785a546d5122f5bdc7141ef0b374877191dab9ca`,
  491,182,080 bytes); commit `9c42e22b0` points
  `tools/browser/runtime-assets.json` and
  `docs/known-gaps/runtime-artifact-manifest.md` at the
  repaired hashes, and the shared store carries the repaired
  zip with its checksum records updated. Proof: the
  packaging-prefix unit test passes on the composed tree
  (targeted re-run, same log as CL-139), and the
  completeness checker passes the retained-archive
  agreement and shipped-archive checks with 0 failures
  (log `test-results/merge-batch10-20261106/completeness-repair.log`).
  The producer path that created the flattened entry is
  FD-200, still open.

- **CL-134 — FD-03 closed: the two floors the machine-
  definition raise could not move — no GPU-compute API and
  no AVX/AVX2 — are proven not deliverable in the browser
  target.** *(Lane-local closure on `lane-fd03`; the number
  settles at the next composition — sibling lanes also
  claim CL-134 provisionally.)*
  (a) OpenCL/CUDA: the shipped set stages Wine's
  `opencl.dll` thunk and its unix forwarder (which NEEDs
  `libOpenCL.so.1`) and the Khronos loader itself, but no
  vendor ICD exists at any layer — no `.icd` file or
  `vendors` entry in any of the four runtime zips, no
  Khronos OpenCL vendor key in either prefix's
  `system.reg`, and no OpenCL code anywhere in the emulator
  source; CUDA is not staged at all (no `nvcuda` or
  `cudart` entry in either wine zip). A loader with zero
  ICDs can only answer `CL_PLATFORM_NOT_FOUND_KHR`, and
  the guest's sole GPU path (Vulkan→WebGPU) offers no
  OpenCL or CUDA surface to implement against. (b)
  AVX/AVX2: the CPU's SIMD register state is a 128-bit
  union (`source/emulation/cpu/common/cpu.h:184`, held at
  `cpu.h:253`); CPUID leaf 1 advertises at most SSE4.2
  with OSXSAVE and AVX clear (`common/cpu.cpp:449-452`)
  and leaf 7 returns all zeros (`cpu.cpp:475-481`); XGETBV
  is hardwired to the legacy x87+SSE mask
  (`cpu64.cpp:5164`) with XSETBV unimplemented; and no VEX
  decoding exists in either engine (`grep -i vex
  source/emulation/cpu` is empty), so the first AVX
  instruction would fault even if advertised. The
  negatives stay pinned by the guest fixture
  (`machine_definition_check.c` fails if the AVX bit is
  ever set) and the native self-test (XGETBV(0) = 0x3).
  Either term reopens if its missing layer ever exists: a
  browser-exposed compute surface an ICD could target, or
  a CPU subsystem with 256-bit state, an XSAVE extended
  area, and VEX decoding in both engines. Record:
  `docs/reports/fd03-remaining-terms-20261106.md`
  (evidence audit on base `faf95d26c`, record commit
  `3bf935b48`; no product code changed).

- **CL-135 — FD-193 closed: the `BW64_BLOCKSTATS` exit
  reporter read a dangling CPU pointer and printed garbage.**
  *(Lane-local closure on `lane-fd193`; the number settles at
  the next composition — CL-134 is claimed on the lane-fd176
  and lane-fd189 branches.)* The reporter's static `CPU64*`
  pointed at the first CPU that allocated a block table; test
  contexts destroy their CPUs before process exit, so the
  atexit line read freed memory, and one handler was registered
  per CPU so the line repeated (four identical garbage lines,
  coverage=5070359463874500.0%, measured on the integration
  tip). A CPU that allocates a block table while the flag is
  set now joins a process-wide registry and, on destruction,
  folds its counters into process-wide totals; the reporter
  registers exactly once and prints the folded totals plus the
  counters of any CPUs still alive at exit. Proof: the
  block-invalidation harness run with the flag set prints a
  single line, identical across runs (built=7 execs=7
  insnsInBlocks=13 totalInsns=14 coverage=92.9%), with
  totalInsns matching the harness's own independent
  instruction total exactly; interp/blocks differential
  byte-identical (pass=54 fail=0); flag unset prints nothing;
  CPU-lifetime gate PASS in both block modes; jit64 phase-1
  tests 873/0. Fix commit `f5fac0447` on base `faf95d26c`.
  Record: `docs/reports/fd193-blockstats-reporter-20261107.md`;
  evidence `test-results/fd193/`.

- **CL-137 — FD-193 closed: twelve X11 bridge call sites
  dereferenced getRamPtr's refusal (null) for guest
  structs.** *(Lane-local closure on
  `lane-x11-getramptr-audit`; the number settles at the
  next composition.)*
  Guest struct fields in the eight affected handlers are
  now read through the checked accessor (`readd` at
  `offsetof`, counts snapshotted before use);
  `XSetWindowAttributes::get` falls back to its checked
  read when `getRamPtr` refuses instead of returning the
  null; `ChangeWindowAttributes` returns BadValue for a
  null attributes pointer with a nonzero mask (Success
  for a zero mask); `x11_FreeModifierMap` gained its
  missing null-argument guard. The 7 call sites that pass
  emulator-allocated addresses were audited safe by
  construction and are unchanged (Appendix). Proof: the
  four new native tests in
  `source/test/x11/testX11RamPtrAudit.cpp` — unfixed code
  all four fail, fixed code all four pass, logs in
  `test-results/x11-getramptr-audit/`. Fix commit
  `cd030f389` on base `faf95d26c`. Record:
  `docs/reports/x11-getramptr-audit-20261106.md`.

- **CL-134 — FD-09 closed: `modify_ldt` answers the
  host's measured error matrix instead of panicking on
  guest input.** *(Lane-local closure on `lane-d278`;
  CL-134 is also claimed on the batch-10 branches and
  the number settles at the next composition.)* A
  write whose byte count is not one descriptor returns
  -EINVAL (checked first), an unreadable descriptor
  pointer returns -EFAULT, and an out-of-range index
  returns -EINVAL; each rejection logs one counted
  `BW64-FAIL syscall.modify_ldt` line. The answers were
  measured with raw `modify_ldt` syscalls on a real
  kernel (host-reference program and output in
  `test-results/d278/hostref/`). Proof: the new native
  tests `testModifyLdtInvalid32` (main suite 616,
  argument answers) and `testModifyLdtIndex32` (617,
  invalid-index answers) fail on the base — 617 with
  the filed panic text, "syscall_modify_ldt invalid
  index: 8192" — and pass with the fix. Fix commit
  `f998e2e01` on base `faf95d26c`. Record:
  `docs/reports/d278-futex-modifyldt-fpe-20261107.md`;
  evidence `test-results/d278/`.

- **CL-135 — FD-193 closed: a futex on an unmapped
  word answers the host's error matrix instead of
  panicking, parking on the null page, or operating on
  a phantom page.** *(Lane-local closure on
  `lane-d278`; FD-193 is also claimed on `lane-fd176`
  for an unrelated filing and CL-135 on sibling
  branches — both numbers settle at the next
  composition.)* On the 32-bit side the shared path
  returns -EFAULT with a counted `BW64-FAIL
  syscall.futex` line where it panicked ("Could not
  find futex address"), and a private WAIT proves the
  word and the timeout pointer before parking; the
  bitset zero-mask rule the 64-bit side already had is
  now enforced on the 32-bit side too. On the 64-bit
  side the word's page is proven mapped before the
  pointer lookup can materialize a phantom zero page:
  every operation returns -EFAULT except a private
  WAKE / WAKE_BITSET, which wakes nothing and returns
  0. The matrix was measured with raw futex syscalls
  on real kernels (host-reference program and outputs
  in `test-results/d278/hostref/`). Proof: the new
  native tests `testFutexUnmapped32` (main suite 614),
  `testFutexSharedUnmapped32` (615), and
  `testFutexUnmapped64` (x64 suite 6) fail on the base
  and pass with the fix. Fix commit `f998e2e01` on base
  `faf95d26c`. Record:
  `docs/reports/d278-futex-modifyldt-fpe-20261107.md`;
  evidence `test-results/d278/`.

- **CL-136 — FD-194 closed: floating-point faults are
  delivered in the host's signal slot with the host's
  code.** *(Lane-local closure on `lane-d278`; FD-194
  is taken by the PlugPlay filing in the batch-10
  settlement and CL-136 is likewise claimed on sibling
  branches — both numbers settle at the next
  composition.)* A divide quotient overflow now
  delivers SIGFPE with FPE_INTDIV and trap 0, exactly
  like divide-by-zero, where it delivered FPE_INTOVF
  and trap 4; and `CPU::prepareFpuException` delivers
  x87 exceptions through the SIGFPE slot with trap 16,
  where it wrote into the SIGSEGV slot — a slot whose
  contents the guest's handler never classifies as an
  FPU fault. INTO keeps its host-correct SIGSEGV /
  trap-4 delivery, pinned unchanged. Both behaviours
  measured with raw signals on a real kernel
  (host-reference programs and outputs in
  `test-results/d278/hostref/`). Proof: the new native
  test `testFpeDelivery32` (main suite 618) fails on
  the base showing both wrong deliveries and passes
  with the fix. Fix commit `f998e2e01` on base
  `faf95d26c`. Record:
  `docs/reports/d278-futex-modifyldt-fpe-20261107.md`;
  evidence `test-results/d278/`.

- **CL-137 — FD-11 closed: splitting a string with an
  empty separator looped forever.** *(Lane-local closure
  on `lane-ditems`; the number settles at the next
  composition.)* The guard now lives inside
  `BString::split(const char*)` itself (empty or null
  delimiter yields the whole string once), covering every
  caller rather than only the filesystem layer's entry
  point. Fix `14783353f`; test `testBStringSplit`
  (`5dc0b8513`): RED = the test hangs on unpatched code
  (timeout kill), GREEN after; ordinary-delimiter
  controls unchanged. Evidence: `test-results/ditems/`.
  Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

- **CL-138 — FD-14 closed: fault reporting's two blind
  spots.** *(Lane-local closure on `lane-ditems`; the
  number settles at the next composition.)*
  `CPU64::raiseSyncFault` logs its own deliveries at the
  delivery point (rate-limited `BW64-FAIL sync-fault64 ...
  delivered=1`), ending the silence of direct kernel-side
  callers; and the 32-bit core's two recover-gate sites
  were confirmed in context to mirror the 64-bit ones
  (skip logged once per RIP; fall-through delivered and
  logged through `signalIllegalInstruction`) — no change
  needed there. Fix `14783353f`; test
  `testRaiseSyncFaultDeliveryLogged64` (`5dc0b8513`):
  RED on unpatched code, GREEN after with the emitted
  delivery line in the log. Evidence:
  `test-results/ditems/`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

- **CL-139 — FD-37 closed: the rotted stash drop-safe
  proof replaced with a fresh export.** *(Lane-local
  closure on `lane-ditems`; the number settles at the
  next composition.)* The repository's only stash was
  exported fresh on 2026-11-06 to
  `/home/ubuntu/winbox-project/evidence/stash-export-20261106/`
  (working-tree patch, index patch, untracked-tree
  archive, hashes and restore instructions); the stash
  was not dropped, and no future drop may cite the
  2026-10-25 proof. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

- **CL-140 — FD-195 closed: the no-throw segment-fault
  caller audit (D5) — one silent write-drop fixed, the
  remaining call sites proven sound.** *(Lane-local
  closure on `lane-ditems`; the number settles at the
  next composition.)* The invalid page's accessors
  re-dispatch after an on-demand mapping instead of
  dropping writes / fabricating read zeros, and an
  unlandable write logs loudly. Fix `14783353f`; proof
  in the `testFaultDelivery32` additions (`5dc0b8513`):
  RED on unpatched code, GREEN after. Evidence:
  `test-results/ditems/`. Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

- **CL-141 — FD-196 closed: open() failures no longer
  collapse into EINVAL (D17).** *(Lane-local closure on
  `lane-ditems`; the number settles at the next
  composition.)* `translateOpenError` maps the 25
  errnos the filesystem layer can actually deliver;
  the default remains for node types with no host
  errno. Fix `14783353f`; test `testTranslateOpenError`
  (`5dc0b8513`), 28 pinned rows: RED on unpatched code,
  GREEN after. Evidence: `test-results/ditems/`.
  Record: `docs/reports/ditems-d5-d11-d16-d17-d21-20261106.md`.

- **CL-137 — FD-39 closed as proven unfixable in the
  shipped backends: the sampling effects of LOD bias,
  border color, texture swizzle and depth-texture mode
  are texture/sampler state that neither backend's state
  model contains.** *(Lane-local closure on
  `lane-fd39-residue`; the number settles at the next
  composition.)*
  The 2026-10-31 fix (the CL-07 residue) made the set
  honest state -- set, validated, queryable -- on both
  GL routes. This lane proved the remaining effects
  cannot be modelled as state: the WebGPU sampler
  descriptor (the project's vendored @webgpu/types)
  carries no borderColor, LOD-bias or swizzle field,
  GPUAddressMode has no border mode at all, and the
  texture-view swizzle in those types is gated on an
  optional device feature the backend never requests;
  the legacy route's WebGL2 host implements ES 3.0,
  whose header (Emscripten 6.0.9's GLES3/gl3.h) defines
  none of the four pnames or the border wrap. The
  project's Vulkan tier records the same limits in
  shipped code (maxSamplerLodBias = 0, "WebGPU has no
  sampler LOD bias"; "WebGPU has no border addressing").
  Texture swizzle on the legacy route alone is
  expressible (ES 3.0 defines it and Emscripten forwards
  texParameteri verbatim); it was not taken, because the
  default route still could not express it and guest
  rendering would then differ by route. One correction
  landed with the closure: the FD-39 record's claim
  that CLAMP_TO_BORDER reached the sampler as "WebGPU
  clamp-to-border" was false on real hardware (no such
  address mode exists); the backend now decodes it to
  clamp-to-edge with a once-log, the raw mode retained
  (fix `8258111c6`, red/green in the updated
  gl-fd39-texparams test). Record:
  `docs/reports/fd39-residue-20261107.md`.
- **CL-141 — FD-201 closed: the shipped audio check
  measures the captured sample values, not only the
  frequency.** *(lane-audio-e2e; number lane-local,
  settles at batch-11 composition.)* Commit `bcf24f33a`:
  `web/tests/suite/browser/audio-tone.test.mjs` now
  measures, on both architectures, peak and RMS level,
  RMS/peak shape ratio, duty cycle, active duration, the
  longest silent gap inside the tone, and channel balance,
  waiting for the queued tone to drain first and writing
  every measured value into the per-architecture evidence
  JSON before asserting; both architectures are captured
  before any sample-value assertion runs. Proof: against
  a fresh build of `334c70be4` ("Browser export verified";
  wasm `92b36180…`, byte-identical to the live build) the
  strengthened check caught FD-202's signature in three
  runs while the pre-existing checks (channels, isolation,
  worklet, frequency) passed — a check that cannot fail is
  no proof, and this one demonstrably fails on the current
  product for measured reasons. It passes once FD-202 is
  fixed. Record: `docs/reports/audio-tone-values-20261107.md`;
  evidence `test-results/audio-e2e/`.
- **CL-142 -- FD-202 closed: the shipped audio check
  folded a silent pump node's buffers into the
  measured stream; it now measures the runtime's
  output node only.** *(lane-fd202-audio; number
  lane-local, settles at batch-11 composition.)*
  Commit `bf1a97710`:
  `web/tests/suite/browser/audio-tone.test.mjs`
  keeps ScriptProcessor output buffers in a separate
  array and measures the AudioWorkletNode's tapped
  output -- the delivered stream; the ScriptProcessor
  array is measured only when no worklet node exists
  (the runtime's fallback output path). No tolerance,
  fixture, or assertion changed; only which stream is
  measured. Proof: the strengthened check PASSES on
  both architectures against the live batch-10 build
  (run with the live tree as the probe dist; the
  audio path's product code is byte-identical there):
  x64 peak 0.36621, RMS 0.36621, duty 0.50038,
  active 5.954 s, longest gap 0 ms, 440.75 Hz,
  channels sample-identical; x86 duty 0.50046,
  active 5.991 s, gap 0 ms, 440.47 Hz. The same
  statistics over the pre-fix merged array fail every
  value assertion (duty 0.248, RMS 0.258, duration
  11.97 s, gap 92.88 ms -- preserved in the split
  evidence), so the fixed check still catches the
  signature when it is in the measured stream.
  Evidence: `test-results/suite-audio-tone/` and
  `test-results/suite-fd202-split/` in wt-fd202-audio.
  Record: `docs/reports/fd202-audio-capture-20261107.md`.


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
- sys_pwrite64's zero-count shape: audited by the
  FD-116..117 lane next to FD-116 — unlike sys_write64 it
  resolves the descriptor and runs its mode gate before its
  zero-count return, so the check order FD-116 filed
  (descriptor answers first) already holds there; not a
  defect. (`lane-fd116-117` record.)
- The 7 `getRamPtr` call sites in the X11 bridge that pass
  emulator-allocated addresses (the Xinerama/XRandR reply
  structs and mode arrays the handlers build themselves,
  and `DisplayData::pCurrentRequest`): a fresh
  `KProcess::alloc` result is backed by the guest heap
  allocator's page subdivisions or whole-page mappings, so
  the span cannot straddle a page boundary or be unmapped
  and the returned pointer cannot be null. Audited call by
  call by the x11-getramptr-audit lane; the
  guest-controlled sites in the same files are FD-193.
  (`lane-x11-getramptr-audit` record.)
- `BHeap::free` trusting the size word stored in front of a
  guest heap block: every access goes through the checked
  MMU path into the guest's own address space, so a corrupt
  size word can only corrupt or unmap the guest's own heap
  — which the guest can already do with its own allocator.
  No host impact. (`lane-x11-getramptr-audit` record.)

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

