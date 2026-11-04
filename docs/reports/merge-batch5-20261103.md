# Batch-5 merge record — 2026-11-03

Composition of the batch-5 pool onto the integration branch
`integ/winbox-20261022b` (worktree
`/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022`), base tip
`0c7e2f3fa` (the batch-4 tip). 55 cherry-picks, then the register
audit commits `00586af7a` and `cd5625a87`, then the gate filing
`86f40a443` (FD-154, register-only). The full gate ran on the
composed tree at `cd5625a87`; the two commits after it are
documentation-only (this record and the FD-154 filing), so the gate
figures below are the composed tree's figures. Deployment is a
separate later step; nothing here was pushed or deployed.

## Pool, in composition order (lineage tip → composed commits)

| Lane (lineage tip) | Composed commits |
|---|---|
| lane-fd118-119 (`60359b324`) | `0d39a4f3b`, `6ce5099fe`, `515446ea1`, `bc7d61975`, `56d21de3c`, `e0d5095f7`, `f1f39e803` |
| lane-fd120-123 (`daf5ec34e`) | `6fd52b6c8`, `eb8dc8f56`, `c40a73c0f`, `dc3ceccaa`, `11c2962e3`, `ae5a35d49` |
| lane-fd124 (`1651c7845`) | `64d1b3401`, `9bda0e8ea`, `236925d36` |
| lane-fd125-ab (`b298ed6eb`) | `080a58bc5` |
| lane-fd126 (`20d2e8e01`) | `d93619b84`, `929d96963`, `145185d18`, `145cd43bd` |
| lane-fd131 (`8efc6f564`) | `97c6cdf44`, `03409a497`, `6cb4982b4` |
| lane-fd132 (`dcb9c93a0`) | `2bd6973a9`, `6a74c7d9b`, `fa48a4847` |
| lane-fd136-137 (`a4b1ce100`) | `72eb36e36`, `166c15780`, `d5c6c8973` |
| lane-fd136-138 (`e9b38d368`) | `435782b5a`, `5533960af`, `c4b4ddd3c` |
| lane-fd125-restage (`a3e00f103`, stacked on lane-fd125-ab) | `a17074952`, `46d25fb12`, `e224bed80` |
| lane-fd133-135 (`31ad091ce`, stacked on lane-fd136-137) | `739e4420d`, `cd0e030e7`, `0bafacaa7` |
| lane-godotpark (`eef642795`) | `b6e91bdd9` |
| lane-fd127fix (`6ba55a560`) | `675af9ea0`, `0285c3daa`, `ea2ccf6e0`, `bfd3577ac`, `d8b899703`, `1fab4746c`, `26a13d8ed`, `27ba9152a`, `0396cdaec` |
| lane-fd128 (`f21582c5e`) | `f349388a7`, `6f3f472e7` |
| lane-fd129 (`ea93a6ef3`) | `c4fbf9969`, `9cecf85b2` |
| lane-d3d11lit (`72eea7450`, stacked on lane-fd129; joined by coordinator amendment mid-composition) | `129e739cf`, `4b5fdea4b` |

lane-fd129pre was NOT composed (identification-evidence branch
only; its evidence stays in its worktree). lane-fd129's fix commit
is cited as `f94ceb4c5` (composed `c4fbf9969`): an earlier hash,
`f8cde3469`, was amended away on the lane and no longer exists.

Three cherry-picks needed hand resolution: the sys_writev64 hunk
in lane-fd118-119's `ea69b4a06` (composed `56d21de3c`), and the
testMain.cpp declaration/runner hunks in lane-fd132's `33be78c82`
(composed `2bd6973a9`) and lane-fd133-135's `4ebfe696c` (composed
`739e4420d`) — both resolved as the union, keeping lane-fd136-138's
extended test descriptions. Register commits were composed by the
batch-4 method (`cherry-pick -n`, register restored from HEAD,
commit with `-C <original>`) so only each lane's record landed from
them; every register edit was then made in the audit pass from the
lanes' own diff texts.

## Final number map (settled by audit of the composed register)

Register after the audit and the gate filing: **151 FD entries,
numbers 1..154 each exactly once except the pre-existing gaps
46/56/57; 103 closure records, CL-01..CL-103 each exactly once.**
First-composed wins a contested number; lane texts keep their own
(lineage) numbers, with a settled-numbers note in each batch-5
Section B row.

Kept their numbers: the Godot stack FD-127/128/129; lineage
FD-131..139 and FD-140; FD-125 (seqpacket) and FD-126 (peer-close)
were already composed entries and gained FIXED markers. Fresh
numbers assigned at the audit:

- FD-130 — drained-datagram refusal (lane-fd120-123's first filing)
- FD-141 — unconnected datagram receive (lane-fd120-123)
- FD-142 — connect to a non-listening unix peer (lane-fd120-123)
- FD-143 — connected stream sendto with a redundant address (lane-fd120-123)
- FD-144 — peer-closed sendmsg descriptor form (lane-fd124)
- FD-145 — CPU self-test cleanup (lane-fd124; DISCHARGED, CL-95)
- FD-146 — null-path chmod/chown/lchown (lane-fd131)
- FD-147 — D3D10/11 draws land nothing (settlement branch's filing,
  substance-keyed; CLOSED by lane-d3d11lit, CL-103)
- FD-148 — D3D9 clear colour readback (settlement branch; open)
- FD-149 — d3d9-32 never runs in suite staging (settlement branch; open)
- FD-150 — 32-bit JIT growth statistics (lane-fd133-135; DISCHARGED, CL-96)
- FD-151 — test-build compile break + two failing discharge cases
  (lane-fd125-restage's filing; open)
- FD-152 — fstatfs name forms (lane-fd133-135's second filing; open)
- FD-153 — Vulkan bridge drop logging of legal no-ops (lane-d3d11lit; open)
- FD-154 — five browser-suite tests failing identically on base
  and composed trees (gate filing; open)

Closures CL-82..CL-103, in order: CL-82 seqpacket (FD-125),
CL-83 peer-close (FD-126), CL-84 FD-130, CL-85 FD-141, CL-86 FD-142,
CL-87 FD-143, CL-88 FD-144, CL-89 null-path chmod (FD-146),
CL-90 empty path (FD-131), CL-91 null-path siblings (FD-132, with
lane-fd136-138's utimensat reconciliation folded in — its lineage
CL-78 exception rested on the glibc-wrapper measurement and is
corrected in CL-91: host references for null/edge arguments must
issue the raw syscall), CL-92 memfd null name (FD-137),
CL-93 utimensat raw matrix (FD-136), CL-94 memfd length (FD-138),
CL-95 CPU-self-test discharge (FD-145, written by the merge lane),
CL-96 JIT-growth discharge (FD-150, merge-written), CL-97 xattr
(FD-133), CL-98 statx (FD-134), CL-99 statfs (FD-135), CL-100 Godot
thread-creation park (FD-127, merge-written from the lane's
Section B row), CL-101 unaligned thread-identity trap (FD-128,
merge-written), CL-102 page-storage writer (FD-129, merge-written),
CL-103 D3D10/11 draws (FD-147, merge-written; substance-keyed from
lane-d3d11lit's lineage FD-130).

Audit repairs beyond renumbering: FD-115's dangling pointer
"see CL-42's entry" now points at FD-89's entry (where the memfault
account lives); the lane-fd87 Section B row's "remain CL-42's owed
clause" now reads CL-43's; FD-89 gained a dated batch-5 update
(the FD-128 branch's pipeline account: 93 shader modules created
with zero refusals, 6 graphics + 68 compute pipelines, the FD-72
module refused by name under FD-75, no present — the CL-43 clause
is still owed to the batch-5 deploy on a shipped build); FD-55
gained a dated batch-5 update (FD-127/128/129 closed as
CL-100/101/102; the first present stays gated on FD-72/FD-74/FD-88
and the downstream submit behaviour; entry stays open).

## Gate on the composed tree (`cd5625a87`)

Bootstrap from the retained archive only
(`/home/ubuntu/backups/winbox-runtime-assets-20261103/browser-runtime-20261103.tar`):
`bootstrap.py --local` restored and verified 13 runtime assets;
`--check` exit 0 before and after the build (post-build: 0 failures,
1 skip — the standing prefix64 built-artifact skip). Browser build:
'Browser export verified', BUILD_INFO git_sha
`cd5625a87cd4ec063aa20d7676305d8efadbb3a6`, git_dirty_files 0,
wasm `05298a62` (5,682,170 B), exactly one support archive
(`support-c4dbe35986a5…zip`, the standing pin). Citations
(`tools/vk-register-citation-audit.py`): exit 0, 352 rows —
232 MATCH, 14 MATCH-INHERITED, 99 UNCHECKABLE, 7 EXEMPT — zero
drift, the batch-4 distribution exactly.

Silent-failure baseline MEASURED on the composed tree, not carried:
syscall64.cpp {emptyCatch 0, unloggedDefault 7, unloggedErrno 134}
equals the standing allowance in `web/tests/suite/known-silent.json`
— no allowance change; vk64bridge.cpp {0, 10, 0} also matches its
allowance exactly (the FD-147 fix added no counted site). The two
allowance files the scanner does not return (web/package-archive.mjs,
web/shader.mjs) are adjudicated by the JS unit suite's scanner test,
which passed.

Native suites, serial: main (`boxedwine 0 0 1`) **679 tests,
0 FAILED** (675 base + testNullPath32Syscalls,
testMemfdNullName32Syscalls, testEmptyPathProcessApi,
testStatxAtEmptyPathProcessApi); x64 **105 tests, 0 FAILED**
(95 + 10 new). JS unit (`runner.mjs --unit`): **352 passed,
0 failed**. JS legacy (`node --test web/tests/*.test.mjs`):
**115 passed, 0 failed**. run-native (vkd3d): **62 PASS**.

Browser suite: the chain's `--filter <name>` (space form) does not
match the runner's `--filter=<text>` syntax, so the first
invocation ran the ENTIRE 32-test browser suite on the composed
build — broader than the batch-4 gate set. Result: **24 passed,
8 failed**, with the boot census at the standing expectation:
boot64-smoke console 0 BW64-FAIL; boot32-vkboot console 0 BW64-FAIL
and exactly 5 served-link lines. Passed include boot64-smoke,
boot32-vkboot, celeste-boot, common-dependencies, cpu32-jit-growth
(the CL-96 discharge re-proven), d3d10-comprehensive,
d3d11-triangle, d3d11-comprehensive (the CL-103 gate re-run;
lane figures were LIT=8192, RESULT 0), d3d12-comprehensive
(control), gl32-triangle, gl32-webgpu-clear, memory-shared,
win32-clock, win32-fonts.

Every failure is adjudicated; none is a batch-5 regression:

- cpu64-selftest: guest summary 951 passed, 2 failed. Both failing
  cases are lane-fd125-restage's FD-125 discharge cases from
  `a17074952`, already filed open as FD-151 ("left failing on the
  branch"). Measured composed answers: chmod null path −EFAULT
  (the case expects the −ENOENT its comment traces to a staging
  build) and the pidfd/io_uring probe −EBADF (the case expects
  ENOSYS; the emulator implements pidfd_getfd since the FD-23
  residue). The cases stay open under FD-151; neither the cases nor
  the kernel were rewritten at the gate.
- d3d9-comprehensive: FD-148's filed signature verbatim (clear
  must reach the compositor, expected >100000, got 4). The
  settlement lane proved the identical failure on the unmodified
  base.
- d3d9-32: FD-149's filed staging failure (guest never reaches
  CAPTURE_READY; explorer thread parked in
  KUnixSocketObject::lockCond).
- winsock-http, input-mouse-rel, input-fullscreen, jit64,
  jitbench: each reproduced IDENTICALLY on a scratch control build
  of the unmodified base `0c7e2f3fa` (built for this gate,
  BUILD_EXIT 0, 'Browser export verified'), same box and
  conditions — winsock-http with relay stats accepted 0 / zero
  bytes on both trees, jit64 with the same 120 s wineboot-pattern
  timeout at 128–129 s test durations on both, jitbench's latch
  assertion failing on both (both trees' attempts ran above the
  test's own one-minute load guard of 4, so its numbers are
  invalid under its own rules on both). Filed together as FD-154.
  Solo and low-load re-runs on the composed tree failed the same
  way before the control was built; the box carried load
  4.2–4.7 from other tenants through the first rounds. One
  harness note: after the chain driver was stopped mid-suite, a
  later re-run deadlocked in the probe-slot queue ("holders:
  none", position 1/1) and was killed by PID; the adjudication
  above rests on the control comparison, not on that round.

Evidence: `test-results/merge-batch5-20261103/` in this worktree
(full-suite log `browser-boot64-smoke.log`, solo and low-load
logs, and `control-base/` with the base build log and the five
control run logs).

## Still owed after this merge

- FD-89 / CL-43's in-vivo pipeline proof — owed to the batch-5
  DEPLOY on the shipped build, as is any FD-115 tally counting
  (tally unchanged at 1 wedged in 25 measured first-visit x86
  starts on shipped builds).
- Open register items carried forward: FD-139, FD-140, FD-148,
  FD-149, FD-151, FD-152, FD-153, FD-154, and the pre-existing
  FD-72/FD-74 (externally gated on the shader toolchain) and
  FD-88 (gated on Chromium honouring bind-group texture-array
  counts).
- Deployment (live site + GitHub push under the standing
  procedure) is the separate next step, not part of this merge.
