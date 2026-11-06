# Audit follow-ups L1 + L2 — both questions settled with evidence (2026-11-05)

Branch `lane-audit-l1l2`, off integration tip `4006ddb58`.
Test commit `64e7e3c0e`. Nothing pushed, deployed or merged;
master untouched. Evidence in this worktree under
`test-results/audit-l1l2/`, `test-results/audit-l1/`,
`test-results/audit-l1x64*/` and `test-results/suite-audit-l1*/`,
with an insurance copy in the consolidated evidence store.

## L2 — FD-19 settled: the KHR spelling IS served on the 64-bit guest path; the entry is closed (CL-119)

The register said the 64-bit Vulkan dispatcher has no
`vkGetPhysicalDeviceProperties2KHR` case while the 32-bit
dispatcher has one. Both halves are literally true of
`source/vulkan/vk64bridge.cpp`: `vk64Bridge` carries the core
case only; `vk32Bridge` pairs the core and KHR cases.

The focused test the audit asked for shows the 64-bit guest
path serves the KHR name anyway, so the implied defect does
not exist:

- The 64-bit guest shim (`tools/rootfs64/libvk64/libvk64.c`)
  exports real alias wrappers for the promoted queries' KHR
  spellings; each forwards to the core wrapper, and the
  shim's proc-address table maps the KHR names to the core
  fn ids. A 64-bit guest's KHR call therefore reaches the
  bridge under the core id, which the switch serves.
- The 32-bit shim (`tools/rootfs64/libvk32/libvk32.c`) traps
  the KHR id itself — that is why `vk32Bridge` carries the
  paired cases and `vk64Bridge` needs none. The bridge-side
  name table lists the KHR names for trap logging only.

Proof: a new GUARD section in
`tools/vkd3d/tests/vk64_native_test.cpp` drives the real
shim (compiled by `tools/vkd3d/tests/run-native.py`). For
each of the three promoted pairs (Properties2, Features2,
MemoryProperties2) it resolves the KHR name through the
shim's `vkGetInstanceProcAddr`, calls through the returned
pointer, and pins (a) the trap arriving under the core fn
id and (b) byte-identical guest-memory answers against the
core name — root struct and pNext chain included — with
spot checks that the fills really happened (driver name,
timeline-semaphore flag, heap count). run-native: **70
PASS** (69 standing + this section), exit 0
(`runnative-5.log`; an identical-steps manual compile+run,
`runnative-manual-1.log`, agrees).

One residue found on the way: the test's first run failed
on a pointer-equality assertion
(`runnative-3.log`) — the lookup returns the alias
wrapper's own address for a KHR name, a different address
than the core name's, though both are served and answer
identically. That identity gap is filed as **FD-166**
(Low) with the failing log as its measurement.

Disposition: **FD-19 CLOSED as disproven — see CL-119**
(provisional number; lane-fd162-164 claims CL-119..CL-121
on its branch; settles at the next composition). No
product code changed: the guest-visible contract already
held, and the standing shim/bridge parity unit test
already guards the table-to-case direction statically;
this section adds the dynamic call-level proof.

## L1 — the one-off benchmark trap: not re-observed verbatim; the fixture produced two other boot-killing shapes, both filed

Series design: bounded repetition of the batch-7 merge
record's fixture and settings — `jitbenchmix-x86` and
`jitbenchmix-x64`, modes alternating 0/1 (the two distinct
JIT settings), ten boots planned per fixture, on this
branch's browser build whose wasm (`c0af957a`) is
byte-identical to the batch-7 gate build where the trap
was seen. The harness load gate was raised to 24 under its
documented shared-host provision; every run's invocation
load is recorded in its `jitbench.json`.

**x86 (tag `audit-l1`):** 7 of 8 attempted boots completed
cleanly — four samples each, checksum
`b8476f000fa05ee2`, matching the gate's focused re-run.
Boot 8 (JIT mode 1) died during the benchmark's run: the
guest thread faulted at one address without recovering
(65 BW64-FAIL guest-memfault32 lines, printed count
reaching 1024, all in this boot), and the host worker then
died with a JavaScript stack-overflow RangeError whose
stack is a repeating three-frame wasm cycle. The gate
trap's exact text, "memory access out of bounds", occurs
zero times in the 9,037-line console. Filed as **FD-167**
with the full account, the gate trap cross-referenced as
a possibly-related earlier observation (identity
unproven), and FD-115's delivered-fault loop noted.

**x64 (tags `audit-l1x64`, `audit-l1x64b`,
`audit-l1x64c`):** three attempts each stopped at the
first boot. The session booted and the benchmark was
spawned, but no benchmark output appeared within the
harness's 180 s wait — zero sample lines, and no fault,
trap or page-error lines. The hang diagnostic shows the
benchmark's single thread WAITING on
`KUnixSocketObject::lockCond` in two attempts (RUNNING,
unresolved, in the first). Invocation 1-minute loads were
15.1, 19.6 and 12.2: heavy shared-box load is recorded
with every attempt and is **not** excluded as a
contributor; the same fixture passed both modes in the
batch-7 gate's focused re-run, so the stall is
intermittent, not deterministic. Filed as **FD-168** with
quiet-box closing terms.

Verdict on the audit's question: the single trap is
**not** recorded as closed-unreproduced. The repetition
series shows this fixture's boots do die abnormally — 1
of 8 x86 boots in a new, preserved signature, and 3 of 3
x64 first-boots stalled under load. The trap's own
signature remains seen-once; both new shapes are filed
by substance with their evidence (FD-167, FD-168).

## Suites on this branch

- run-native: **70 PASS, exit 0** (69 standing + the new
  FD-19 section).
- JS unit suite: **353 passed, 0 failed**, runner exit 0
  — the standing size (`js-unit-2.log`).
- Browser build: "Browser export verified", BUILD
  COMPLETE; wasm `c0af957a`, byte-identical to the
  batch-7 gate build (this branch changes no product
  code — a native test file and register/record text).
- Native serial suites: main **686 tests, 0 FAILED**
  (serial run, 395 s) and x64 **111 tests, 0 FAILED**
  — the standing sizes for this base
  (`native-main-run.log`, `native-x64-run.log`).
- Browser guest suite: only the jitbench series above —
  it is this lane's subject; no other browser tests run.

## Setup and machinery notes

- Fresh-worktree inputs per the standing recipe:
  node_modules symlink; dxvk, vkd3d and gstreamer payload
  copies from the integration tree; pinned Slang assets
  copy; runtime assets restored from the retained
  20261104 archive (`bootstrap.py --local`); stage-vendor
  clean.
- Machinery friction, all preserved in the evidence
  logs: one run-native attempt died at the runner's
  180 s compile cap under load ~28 (retried green; the
  manual identical-steps run agreed); the first browser
  build died on the shared build lock's 600 s wait bound
  behind a sibling build (restarted with a longer bound,
  completed verified); one JS-unit run was killed when
  its SSH session dropped at 193 files (re-run detached,
  353/0); the probe queue held this lane behind a
  24-minute heavy Godot capture and the FD-115 lanes'
  live start series — the queue working as designed.

## Numbers claimed on this branch (provisional; settle at the next composition)

- CL-119 — FD-19 closed as disproven (collision with
  lane-fd162-164's CL-119..CL-121 noted in both entries).
- FD-166 — proc-address pointer identity for promoted
  alias pairs (Low).
- FD-167 — delivered-fault loop kills a 32-bit benchmark
  boot; host fault path recurses to a JS stack overflow
  (Medium).
- FD-168 — 64-bit benchmark guest parks in a
  socket-condition wait and produces no output
  (Medium-Low, load confound recorded).
