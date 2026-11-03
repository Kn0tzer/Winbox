# Batch-4 merge record — 2026-11-03

Composed onto the batch-3 deploy record tip `ff31d5bfe` on
`integ/winbox-20261022b`. NOT deployed, NOT pushed: this tree is
deploy-ready for the batch-4 deploy lane. Gate evidence lives in
`test-results/merge-batch4-20261103/` plus the per-test
`test-results/suite-*/` directories.

## Branches composed (pool order)

| Branch | Tip composed | What landed |
| --- | --- | --- |
| `lane-fd87loss` | `d4059ee5e` | The x87 stale-cache fix (`dd6a6855d`, composed as `d5b024272`), its probe fixtures (`36aa29c12` -> `90f1def81`), and its closure + lane record (`d4059ee5e` -> `1ea5ec2a2`). The carry commit `82941a2a1` was skipped: its fixtures and register entry were already composed in batch 3. Its parallel-worker filing was NOT taken as a new entry — see the FD-32 reconciliation below. |
| `lane-wedge-incidence` | `6f9b53e1e` | Register + record only: the wedged first-visit 32-bit start filing with measured incidence, settled as FD-115 (`adcbd2475`). |
| `lane-fd105-108` | `20ab45fc0` | The message quartet fixes (tests `bfd0c8e0c`, fix `dc6293848`, guard `ed11d4ba1`, register `d68559d46`). Its allowance commit `eb57da3d9` (117 -> 119 on its tree) was skipped whole: the composed baseline is set by rescan, below. |
| `lane-fd109-112` | `963272707` | The datagram-delivery four, stacked on the quartet (tests `a3247fbe0`, fix `99e5f8f6b`, follow-ups `a6b447533`, register `d2f2b3549`, numbering note `5e79828a6`). |
| `lane-fd113-115` | `4322652a7` | Pathname lifetime, stream byte order, gathered write (tests `50b9888c5`, fix `95ead1d37`, fixture corrections `ca5a18c5c`, register `bc024eb75`). Its allowance bump was likewise not carried; the socket64-message slice anchor fix in the same commit WAS kept. |
| `lane-fd116-117` | `bf3588622` | Empty-write routing + the JS harness repair (tests/harness `541228eef`, test-width flag `daaf281f5`, fix `9faebf3de`, register `14d756fa9`). |

26 commits sit on `ff31d5bfe` at the record's writing: the 22
pool picks above, two composition adjustments and one composition
fix (below), the silent-failure baseline commit, and the two
audit commits.

## Renumber ledger (settled at this composition)

Composed register before the audit: 111 FD entries, closures
through CL-66.

Fresh FD numbers, in filing order:

- FD-115 — the wedge filing (lane-wedge-incidence; its lineage
  number FD-103 was already taken by batch 3's readv settlement).
- FD-116 — recvmsg fabricates the source name for queued
  messages (lineage FD-109). CLOSED here (CL-73).
- FD-117 — the message queue is invisible to read/recvfrom
  (lineage FD-110). CLOSED (CL-74).
- FD-118 — named datagram delivery has no route (lineage
  FD-111). CLOSED (CL-75).
- FD-119 — a zero-length sendmsg datagram queues nothing
  (lineage FD-112). CLOSED (CL-76).
- FD-120 — a closed pathname socket answers ENOENT where the
  host answers ECONNREFUSED (lineage FD-113). CLOSED (CL-77).
- FD-121 — stream byte order across the two receive stores
  (lineage FD-114). CLOSED (CL-78).
- FD-122 — writev queues one datagram per segment (lineage
  FD-115). CLOSED (CL-79).
- FD-123 — a zero-length plain 64-bit write queues nothing
  (lineage FD-116). CLOSED (CL-80).
- FD-124 — the JS slice test unix-connect-handoff no longer
  compiles (lineage FD-117). CLOSED (CL-81).
- FD-125 — SOCK_SEQPACKET unix sockets unmodelled (lineage
  FD-118). Stays OPEN; the sibling lane carrying it is still
  running in parallel.
- FD-126 — the first datagram send after a peer close answers
  -ENOTCONN where the host answers -ECONNREFUSED once (lineage
  FD-119). Stays OPEN, same sibling lane.

One restoration: lane-fd109-112 had closed FD-116..119's
lineage entries by removing their Section A bullets and writing
only CL records. The four entries are restored to Section A in
their filed form with FIXED markers, per the register
convention (closed entries stay, marked, pointing at their CL
record).

Closures, in composition order after CL-66:

- CL-67 — FD-108 (the x87 stale-cache fix; the lane wrote the
  closure under provisional CL-55).
- CL-68 — FD-85, written by this merge on the composed gate
  evidence (see Gate).
- CL-69..CL-72 — the quartet: FD-111, FD-112, FD-113, FD-114
  (lane claims CL-56..59).
- CL-73..CL-76 — the delivery four: FD-116..FD-119 (lane claims
  CL-60..63).
- CL-77..CL-79 — the follow-up three: FD-120..FD-122 (lane
  claims CL-64..66).
- CL-80, CL-81 — the pair: FD-123, FD-124 (lane claims
  CL-67/68).

Register after the audit: 123 FD entries, no duplicate numbers;
81 closure records, CL-01..CL-81 each exactly once. Section B
gained settled-number notes on the four stack rows and new rows
for lane-fd87loss and lane-wedge-incidence. FD-82's inline
pointer to FD-79's deferral closure was corrected CL-36 ->
CL-38 (its entry already carried the correction note).

## Reconciliation: the parallel-worker filing folds into FD-32

lane-fd87loss also filed, as its own entry, that the native
test binaries crash under their default parallel workers
(exit 139, death point varying), A/B-proven pre-existing on the
unmodified base. FD-32 already covers this exact shape:
multi-worker native suite runs die on the loaded box, exit 139,
serial runs are the trustworthy mode — and FD-32's own text
records that it was "not A/B-tested against a pristine-tip
binary by the lane that documented it most recently." The
batch-3 merge record likewise attributed its own early
parallel-run failure to FD-32. Decision: FOLD. The filing is
appended to FD-32 as a dated addendum (it supplies exactly the
A/B proof FD-32 lacked: base and fixed binaries die at the same
test under the same conditions; one-worker runs complete at
0 FAILED). No fresh FD number.

## Composition adjustments (merge-lane changes to lane material)

1. FD-116's guest test pinned -EINVAL for a wrong-mode
   zero-length write — its own lineage's standing write-family
   answer. Its record's host measurement says -EBADF, and the
   composed tree carries FD-93's settled host-measured EBADF
   contract for the family. The expectation was aligned to
   -EBADF (commit `e64f206aa`, test only).
2. sys_writev64: FD-93's validation block (batch 3) and
   FD-115's datagram gather (this pool) were composed in host
   order — FD-93's gates first, then the gather — and the
   gather's inner wrong-mode answer was aligned -EINVAL ->
   -EBADF to the same settled contract.
3. The unix-connect-handoff test stub was unioned: the lane's
   S32 addition kept, the composed tree's K_EMFILE (FD-05) kept.

## Composition fix caught by the gate (194876c51)

sys_write64 on the composed tree failed FD-97's guest test
(x64 suite: 1 FAILED, "write/pwrite64 from unreadable guest
buffers"). The two lanes never shared a tree: FD-97 (batch 3)
reassigns `count` to the readable prefix inside sys_write64;
FD-116 (this pool) tests `count == 0` at the write point to
recognise a guest zero-length write. Composed naively, every
nonzero write from a fully unreadable buffer looked like a
zero-length write and answered 0 (or queued an empty datagram)
instead of -EFAULT. Fix: capture `zeroCount` before the
reassignment; FD-116's user-range check keys on it, FD-97's
badBuffer answer is unchanged. Both suites re-run green after
the fix (below).

## Silent-failure baseline (measured, not carried)

The pool's lineages moved the allowance differently (119, 120,
unchanged in places), so no lineage total was carried. The
scanner run against the composed tree reports
`source/kernel/syscall64.cpp` unloggedErrno at **134**; the
allowance in `web/tests/suite/known-silent.json` was set from
the batch-3 baseline of 132 to the measured 134 (commit
`4d2ebbe8f`). Arithmetic: the quartet's lineage delta (+2)
accounts for the whole rise; FD-115's lineage delta (+1) does
not add a counted site on the composed tree, where FD-93's
validation already answers ahead of its gate. No other file
moved. After the update the scanner test passes.

## Gate (all on the composed tree; evidence in test-results/merge-batch4-20261103/)

- Bootstrap ONLY from the retained runtime-assets-20261103
  archive: 13 assets restored and verified; `--check` exit 0.
- Build: `tools/bw64-build.sh` — "Browser export verified",
  BUILD COMPLETE. Final build (after the composition fix) from
  tip `194876c51`: wasm `c274128e`, 5,664,763 bytes; exactly
  one support archive, `c4dbe35986a5`, as pinned.
- Completeness: 0 failures, 1 standing skip (post-build run).
- Citations: audit exit 0 — 352 rows with citations: 232 MATCH,
  14 MATCH-INHERITED, 99 UNCHECKABLE, 7 EXEMPT; zero drift.
- Native suites, run serially, single worker:
  main 675 tests, 0 FAILED (run twice: before and after the
  composition fix; 253s / 256s); x64 95 tests, 0 FAILED after
  the composition fix (first composed run: 1 FAILED — the
  FD-97/FD-116 interaction above). 95 = batch-3's 83 + the
  pool's 12 x64 tests.
- JS unit: 352 passed, 0 failed. (Two earlier runs each showed
  a single environment-shaped failure — the scanner before its
  baseline update, and probe-slots under cross-lane contention;
  probe-slots passes standalone and in the final full run.)
- JS legacy: 115 passed, 0 failed (116 subtests, 1 skip).
- run-native: 62 PASS, 0 FAIL.
- Boots via the probe slot: boot64-smoke PASS, 0 BW64-FAIL;
  boot32-vkboot PASS, 0 BW64-FAIL and exactly 5 served-link
  lines (the hard-link serves, count=1..5, in its console).
- common-dependencies PASS unloosened (352s), OPENAL: RESULT 0,
  0 BW64-FAIL — FD-85's closing term, discharged on this tree;
  FD-85 is closed as CL-68 on this evidence.

## Still owed, deliberately not in this gate

FD-89 / CL-43's in-vivo pipeline proof (the two specification
modules' second-wave pipeline creations in a live session)
stays owed to the carrying deploy, as in batch 3 — the deploy
lane retries it in a quieter window with an extended launch
wait. FD-125 and FD-126 stay open for the sibling FD-118/119
lane still running in parallel. FD-88, FD-72 and FD-74 are
unchanged (browser/spec-gated); FD-55 stays open on that stack.
