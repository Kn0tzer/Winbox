# FD-125 / FD-130 — test-side discharge of the settled self-test filings

Lane: lane-fd125-restage (worktree wt-fd125-restage), based on
lane-fd125-ab tip b298ed6eb. Date: 2026-11-03.

Commits on the branch:

| Commit | What |
| --- | --- |
| 027688ad0 | FD-125: cpu64SelfTest.cpp only — the JIT end block switches on the selected engine surface; chmod expects −ENOENT (null path); fchmod expects −EBADF (unopened descriptor) |
| 0d4463b53 | FD-130: cpu32-jit-growth.test.mjs only — statistics assertions gated on the cpu32 section's available surface; boot stages jit32=1; enabled asserted inside the gate |
| (this record's commit) | Register: FD-125 + FD-130 discharge notes, FD-131 filed, Section B row |

No product code changed anywhere in this lane. Diff over the base:
2 files, 76 insertions, 31 deletions.

## Setup

Fresh worktree per the sibling records: node_modules symlinked to the
main tree; browser assets bootstrapped from the retained archive this
branch pins (browser-runtime-20261101.tar, 13 assets restored and
verified); pinned-vintage graphics payloads copied in — dxvk
build.w32/build.w64 and rootfs64 gstreamer32/gstreamer64 from wt-fd38,
vkd3d build.w64 from wt-restage, build.w32 from wt-x86vkd3d (all
git-ignored paths; tree clean after the copies).

## FD-125 — the four self-test cases

The settlement (docs/reports/fd125-settlement-20261103.md) proved the
four standing failures pre-existing and recorded one disposition per
case. This lane applied them.

### The JIT pair

Before: the fixture's end block (compiled in only under
BW64_JIT_VERIFY=1, which the fixture page always sets) failed the run
whenever no engine was selected: "required JIT path did not execute
any blocks" / "no JIT engine selected (engine=off)" — counted as two
of the four failures on every standard build.

After (cpu64SelfTest.cpp, end block): the block switches on the engine
surface the run itself selected. With engine "off" it asserts the off
flavour's correct record — jitBlocks, jitInsns, wasmBlocks, wasmInsns
and wasmEntered all zero — and a nonzero counter is the failure
("no engine selected but counters report …"). The pre-existing checks
(the wasm arm demanding entered == blocks, the interp arm, the final
engine-surface else) stand verbatim for engine-selected runs.

Proof, standard build: guest console prints "JIT64: engine=off
blocks=0 insns=0" and no JIT failure line; summary clean (below).
Proof, JIT flavour (wasm64-mt-jit dist, run with
BW_TEST_EXPECT_JIT=1 so the harness also demands the emitted-execution
line): "JIT64: engine=wasm blocks=319 insns=816", "WASM64: blocks=319
insns=816 entered=319 rejected=0 instantiateFailed=0", suite PASS —
the retained wasm-arm assertions assert and pass under the flavour
that provides them.

### chmod and fchmod — the settlement's first-choice stagings are impossible in this fixture (measured)

The settlement's first choices were: re-stage fchmod on an open
descriptor (expect 0), and by the same shape stage chmod on a
resolvable path. This lane tried exactly those first (build at the
attempt staging; console preserved as
cpu64-selftest-console-openstaging-attempt.log):

- chmod staged with a stack-built "/" path and mode 0755 answered
  −ENOENT (FAIL line: R15=0xfffffffffffffffe, RDI=0x7fffe0 — the
  stack path pointer; the snippet executed exactly as encoded).
- fchmod staged openat("/") → descriptor → fchmod answered −EBADF
  with RDI=0xffffffffffffffda (−ENOSYS): the openat itself returned
  −ENOSYS.

The mechanism is in the source: the self-test's CPU runs with no
KThread/KProcess. The fixture file's header says it "validates CPU64
+ KMemory64 in isolation"; a fixture comment notes "The isolated
self-test CPU has no KThread/signal action to inspect"; sys_openat64
returns −ENOSYS when cpu->thread / cpu->thread->process is null, and
nothing in the fixture's construction ever assigns one (assignment
exists only in the loader / kthread / clone paths). KProcess::chmod
resolves through the process's Fs context and KProcess::fchmod
requires an open KFile — neither is reachable in this fixture, so no
path resolves and no descriptor can be opened inside it. A
fixture-environment fact, not a product failure; per the assignment,
the cases were not bent further — the settlement's own alternative
dispositions were applied instead:

- chmod: expectation −ENOENT, case renamed "chmod → −ENOENT (null
  path)", comment recording the disposition chain (stale pre-FD-58
  no-op expectation; settlement disposition (3): expect the layer's
  real answer; the host −EFAULT divergence is FD-126).
- fchmod: expectation −EBADF, case renamed "fchmod → −EBADF
  (unopened descriptor)", comment recording disposition (2)'s
  alternative (expect −EBADF as staged; host-measured by the
  settlement) and why the open-descriptor staging is unavailable.

Proof, standard build (final tip): guest summary **953 passed,
0 failed**, with "PASS: chmod → −ENOENT (null path)" and
"PASS: fchmod → −EBADF (unopened descriptor)" in the console
(cpu64-selftest-console-standard-final.log). The count arithmetic:
955 staged lines before = 953 staged cases + the 2 engine=off
pseudo-failures; after, the 953 cases all pass and the pseudo-failures
no longer count. Same 953/0 on the JIT build.

## FD-130 — cpu32-jit-growth

Before: the guest reported RESULT 0 on every build, then the harness
asserted stats.cpu32.retiredCounting === true (and blocksEntered /
instructionsRetired > 0) unconditionally — undefined on the standard
build, whose stats JSON carries the cpu32 section only as the
unavailable stub {"available":0,"enabled":0,"engine":"unavailable"}
(the writer appends the real section, from bw32_jit_stats_json, only
under BOXEDWINE_WASM_JIT).

Two staging defects were in the test:

1. The assertions ran on builds that cannot carry the fields. They
   now run only when stats.cpu32.available is true — the surface the
   JIT flavour emits — the same gate shape jit64.test.mjs uses for
   the cpu64 section. The guest RESULT 0 asserts on every build.
2. The boot staged the wrong engine parameter. It passed jit: '1',
   which the launcher maps to CPU64 only ("?jit=1 enables CPU64 only;
   ?jit32=1 also enables CPU32" — project/emscripten/
   wine64-launcher.js); the CPU32 engine is enabled by ENV BW32_JIT,
   set from jit32. Measured on the JIT build in the test's original
   form: the section reported available:true but enabled:false with
   blocksEntered 0 and instructionsRetired 0 (scratch probe run,
   run-statsprobe-jit.log). The boot now stages jit32: '1', and the
   gated block also asserts stats.cpu32.enabled === true.

Proof: standard build PASS (gate closed over the stub, guest RESULT
0). JIT build PASS with the gate open: with jit32 staged the section
measures enabled:true, retiredCounting:true, blocksEntered
105,339,927, instructionsRetired 452,013,423, and the original
ungated assertions pass verbatim (scratch probe carrying the
pre-gate form plus a section print, run-statsprobe-jit2.log; the
scratch file was deleted after the run and is not committed).

## Builds and runs

| Build | Identity | Result |
| --- | --- | --- |
| Standard dist, first-attempt staging (open-state chmod/fchmod) | git_sha 08e6e203b (transient tip, see disclosure), wasm fc77d68a | cpu64-selftest 951 passed / 2 failed — the two open-state stagings, analysed above |
| Standard dist, settled staging | git_sha 272a6dafc | cpu64-selftest PASS 953/0 (one earlier run at this build died waiting for a probe slot behind another lane's build — non-verdict, disclosed in run-cpu64-selftest-standard2.log); cpu32-jit-growth PASS |
| JIT dist (BW64_BUILD_TARGET=wasm64-mt-jit) | git_sha 272a6dafc, wasm 9bb606a0 (7,103,463 B) | cpu64-selftest with BW_TEST_EXPECT_JIT=1 PASS 953/0; cpu32-jit-growth (final form) PASS; scratch probes as above. The tip difference to 0d4463b53 is the .mjs test file only — no product or fixture code differs |
| Standard dist, final tip | git_sha 0d4463b53, wasm 5245c810 (5,643,139 B), dirty 0 | cpu64-selftest PASS 953/0; cpu32-jit-growth (final form, jit32 staged) PASS |

Every dist build: BUILD COMPLETE, "Browser export verified",
completeness 0 failures / 1 skip, support archive c4dbe35986a5.
JS unit suite at the final tip: **348 passed, 0 failed** (js-unit.log).

## By-finding: FD-131 filed

The Node self-test target (make wasm64-selftest in
project/emscripten) does not compile — faultstage.h's staged mutex
lock/unlock reads members through BOXEDWINE_MUTEX, which is void* in
that build (soft_rw_page.cpp:153, soft_copy_on_write_page.cpp:166).
A/B-proven pre-existing: the identical failure reproduces on the
unmodified base b298ed6eb in a throwaway worktree
(basecheck-node-target-build.log). The browser builds compile the
same units clean; the target is the milestone-1 standalone harness,
not a gate vehicle. Filed as FD-131 with closing terms; not fixed
here (outside this lane's test-side scope).

## Disclosure

The first FD-125 commit attempt was folded into the FD-130 commit by
a git commit --amend aimed at the wrong HEAD (transient tips
082dca5a5 / 9a19ba31b / 08e6e203b; the first standard build above ran
at the last of those, whose tree content was the first-attempt
staging plus the FD-130 gate). The stack was rebuilt with
git reset --soft onto the base and recommitted by path; the two
commits cited throughout (027688ad0, 0d4463b53) are the real,
re-read stack, each touching exactly its one file.

## Evidence

test-results/fd125-restage/ in this worktree: build logs for all four
dist builds plus the Node-target A/B, run logs for every test run
(including the slot-timeout non-verdict), and the guest consoles
(open-staging attempt, standard settled, standard final, JIT).
