# Merge record — batch: hardened readiness (FD-47) + FD-39 texture store + FD-23 syscall census (2026-10-31)

Branches merged into `integ/winbox-20261022b` (pre-batch integ tip
`f3fd57d58`):

- `lane-fd39` — fast-forward to `463ccc3bd` (fix `ad93c01ea`).
- `lane-fd23-syscalls` — merge `bb3024fd7` (fix `98dee4d6f`,
  register/report `4a94017f7`); register auto-merged.
- `lane-readygate` — merge `bf6322691` (single commit `2e6d0f9a4`);
  code merged clean, register conflicted and was composed by hand
  (below).

All five branch commits verified ancestors of HEAD. Gate evidence:
`test-results/merge-batch3-20261031/`.

## Register composition

- **Renumber (the collision this batch was briefed on):** the
  readiness branch registered its certification defect as FD-44;
  FD-44 on integ is the thread-lifetime entry. The incoming entry is
  renumbered **FD-47** everywhere it is referenced: the register
  (entry, FD-43's resolution paragraph, FD-01's update, CL-10, CL-11,
  the branch-table row), the lane's record
  (`docs/reports/readiness-hardening-20261031.md`), and the code
  comments (launcher, readiness unit test, measurement tools). The
  entry carries the standard renumber note. FD-44's thread-lifetime
  entry is untouched.
- **FD-43** stands closed as **CL-10** with the branch's RESOLUTION
  appended (four-arm rates: stock 0/10, stamped 0/10, baseline
  692875bd8 0/10; control false-ready 10/10 under the old
  certification, 0/10 under the new). **FD-47** stands closed as
  **CL-11**. CL-01..CL-11 all present, no earlier closure or
  attribution lost; FD-01 remains OPEN (updated by the branch: its
  prerequisites are met, the race itself is unfixed).
- **FD-45 placement corrected:** the FD-39 branch had placed this
  open, uncharacterized entry at the head of the Closed section; it
  now sits in the Open section with its text unchanged.
- **FD-46 closed as CL-12 at this gate:** its entry's own closing
  condition was "closes when the branch lands and is re-proven at
  the merge gate" — discharged by the full native gate below (the
  x64 flavour's 61 tests include the new dispatch-level accept4
  test). The section-A entry was moved to CL-12 naming `98dee4d6f`
  and this gate.
- **FD-39** stays open, narrowed in place by its branch (RESOLVED IN
  PART; effect-only residue). **FD-23** stays open, narrowed in
  place (dispositions recorded by its branch).
- **NEW FD-48 (found by this gate, see below):** after a stopgap
  session restart, a genuinely started session may never be
  certified by the hardened readiness test.
- Post-composition audit: section A headers FD-01..FD-48 each naming
  exactly one defect; closures CL-01..CL-12.

## Container re-pin (commits `9849c8cf1`, `44de9be31`)

The pinned wine64.zip (`e777d052`) exists nowhere on the box (the
background search over `/home/ubuntu` found no copy; the earlier
FD-41 reproduction was stripped with its tree). Following the FD-40
merge's precedent: the other runtime zips were staged by
hash-verified copies (four from the live base tree, three from the
retained archive `browser-runtime-20261026.tar`, sha matching the
manifest's archive pin), and wine64.zip was rebuilt from the live
base `78dde7c2` with the project's staging script
(`stage-wine8-backports.sh`; one staged run was lost to an SSH
timeout mid-merge and redone detached — the partial `.merged`
artifact was discarded and the base re-copied). Member audit: 1257
members, name set identical, **exactly one content difference vs
base — win32u.dll `a8ca5fbb` -> `49b87438`, the FD-40 fix**. New
pin `8ea33c43`, 209,678,123 B (the same size as the FD-40 merge's
staged container). The first dist build failed honestly on the
resulting manifest drift; the artifact manifest was regenerated
(w64 row only) and committed before the passing build — the same
sequence both previous merges recorded.

## Gate on the merged tree

- **Dist build: PASS.** "Browser export verified: 34 .mjs syntax
  files; runtime version substituted; dependency archive
  de592964f772 verified" + "BUILD COMPLETE" (build3.log; build1
  stopped on the manifest drift above, build2 lost the build lock
  to another lane's build and exited per the lock protocol).
- **JS unit suite: 342 passed, 0 failed** (340 at the previous
  gates + FD-39's two new test files; the readiness contract tests
  pass on the merged launcher).
- **Native suite, main flavour — full serial run COMPLETED
  end-to-end: 661 executed, 661 OK, 0 FAILED, 248 s.** This
  discharges the full native gate the FD-23 branch deliberately
  skipped. (One earlier attempt in the default 4-worker mode died
  with the FD-44 leaked-thread signature — preserved as
  native-main-parallel-attempt.log; the parallel mode is documented
  as non-gate on both the FD-41 and FD-23 branches, in before and
  after logs alike, and the serial gate standard was then run.)
- **Native suite, x64-test flavour: 61 executed, 61 OK, 0 FAILED,
  8 s** — the count grew 58 -> 61: FD-23's three new dispatch tests
  (epoll_pwait / accept4 / dup3) run and pass on the merged tree.
- **Recover-gate audit:** 45 tolerated, 0 violations.
  **Runtime completeness:** 0 failures, 1 skip.
- **boot64-smoke: PASS** (213 s), with the FD-43-corrected
  criterion verified in the raw console log
  (boot64-console.log): win32u.dll at `image_flags=44`,
  `create_winstation` reached (16 requests), winex11 loaded,
  **zero** `0xc000000d` occurrences.
- **D3D12 oracles (guest-workload results):** 64-bit VERDICT
  passed, tri12Result 0 — and its harness reported SESSION READY,
  i.e. a genuine start certified under the hardened readiness
  rules on the merged tree. 32-bit: see FD-48 below (its session
  never reported ready; the run was terminated by this lane after
  ~15 min so the probe slot would not be held indefinitely).

## Guest checks that did not pass — and what they actually show

- **boot32-vkboot: FAILED twice** (600 s timeout inside bootGuest,
  i.e. certification never completed). Anatomy, identical both
  runs (vkboot-attempt1-artifacts/, boot32-vkboot2.log): the FD-01
  race killed boot attempt 1 at the documented stall point (X11
  accepts stopped at 2; "The explorer process failed to start"
  printed; the stopgap scheduled its clean restart); the restarted
  session reached the full handshake in the guest stream — deskpin
  READY with a plausible handle (`0x10054`), the restarted
  session's X11 accepts climbing to 5, explorer running in the
  server traces — and was still never certified. The blocked
  element is the observer's explorer-open line, which has no
  counterpart in this shape's persisted guest stream (zero OPENED
  lines in either run's console; also zero in the readiness lane's
  own passing run, where a single-session boot certified with
  handle `0x10052`). Registered as **FD-48**. Bisect reasoning:
  the merged launcher differs from the branch's only by the FD-47
  comment renumber; the branch certified this test single-session;
  the gate failures occur only in the race -> restart path, which
  the pre-merge code "passed" by certifying sessions with no
  desktop at all (FD-43). The batch makes this signal fail closed
  and loud; it does not reduce real start capability (the four-arm
  measurement: first-attempt rates identical on base and merged
  builds). No readiness rule was weakened to make this pass.
- **dbwin-echo: FAILED twice** (360 s timeouts). Attempt 1 starved
  in early wineboot under box load ~18 (711 console lines, no
  winstation — the recorded guest-crawl family). Attempt 2 reached
  the FD-01 signature, the stopgap fired its restart, and the
  restart did not complete inside the test's budget under load
  ~16. The same test passed at the FD-41/FD-42 gate on the
  pre-batch tree hours earlier, and its subject (the DBWIN
  channel) is untouched by all three branches; both failures are
  the FD-01 race plus restart budget under extreme load, not a
  batch mechanism.
- The D3D12 32-bit oracle's non-ready session is the FD-48 shape
  in a second harness (its 64-bit sibling certified and passed in
  the same gate, under the same load).

## State after the merge

Register: FD-01..FD-48 (FD-48 new and open; FD-44 thread-lifetime
still open; FD-01 still open), closures CL-01..CL-12. Generated
state (`dist`, `web/runtime`, `project/linux/Build`,
`__pycache__`) stripped from the integ tree after the gate;
test-results evidence retained (including both failed vkboot
attempts' artifacts and the killed oracle's log). NOT deployed,
NOT pushed. Master and the live website untouched.
