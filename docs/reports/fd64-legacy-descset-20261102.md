# FD-64 — the four remaining legacy DESC_SET cases (2026-11-02)

Lane: `lane-fd64`, worktree `/home/ubuntu/wt-fd64`, off integ
`cc1d89ab3`. Fix commit `9d132afc7` (test file only). Nothing
pushed, deployed, or merged; no other lane's tree touched.

## End state asked

FD-64's remaining four legacy test cases (the standing failing set
every merge gate reproduced byte-identically — legacy 110/4, all in
`web/tests/vkwebgpu-v2.test.mjs`, all throwing
`vk64: v2 DESC_SET has 132 trailing bytes`) each end genuinely
passing for the right reason, or proven to encode behaviour the
product no longer has and corrected to the current contract with
the justification on the register. No case made green by weakening
what it asserts about served behaviour.

## Result

The legacy failing set is now **empty**: full legacy group
**115 passed / 0 failed** (baseline 110/4; 116 tests, 1 skipped),
and with the browser build run in this tree the suite-unit group
passes **348/0** and `tools/run-tests.mjs` reports ALL TESTS
PASSED (see the Correction section for the first run's story).

## Baseline

Fresh worktree staged by the clean path only
(`bootstrap.py --local` from the retained archive, 13 assets
verified; `node_modules` symlinked from the main tree;
`stage-vendor.mjs`). Baseline run of the one file: 15 pass / 4
fail — tests 6, 7, 8, 9 (`two sampled views...`, `a DXVK-shaped
frame decodes...`, `legacy sampler records...`, `malformed frames
are rejected...`), each with the filed error
`vk64: v2 DESC_SET has 132 trailing bytes`
(`test-results/fd64/vkwebgpu-v2-baseline.log`).

## Determination — the fixture was the stale side

The mismatch is exactly in the DESC_SET header. The fixture wrote
`u8 setIndex, u8 count, u16 pad, u64 layoutId`; the reader parses
`u8 setIndex, u8 pad, u16 nBind, u64 layoutId`, so it read the
count from the u16 the fixture left zero, decoded zero bindings,
and rightly reported the 3 x 44 = 132 binding bytes as trailing.

Four independent sources carry the reader's layout:

- `docs/vulkan-protocol.md` section 0x08: `u8 setIndex, u8 pad,
  u16 nBind, u64 layoutId`, "one entry per (binding, element)",
  count always 1.
- The bridge writer, `emitDescSet` in
  `source/vulkan/vk64bridge.cpp`:
  `p.u8(setIndex); p.u8(0); p.u16(nWire); p.u64(s->layout)`, and
  `applyDescWrite` stores one `Binding` per element with
  `b.count = 1`, `dstArrayElement = elem + i` — the bridge never
  emits a single entry with count 3.
- The reader in `web/vk-protocol.mjs` (same layout, trailing-byte
  strictness via `r.done('DESC_SET')`).
- The gate-standard suite's `vk-desc-set-wire.test.mjs`, whose
  header comment records why the count widened to u16: the old
  u8-count decoder "broke every DXVK/vkd3d layout with more than
  8 written descriptors (and every descriptor array)" — the
  FD-16/FD-49-era growth FD-64's entry suspected.

The per-entry layout (44 bytes: binding, type, dstArrayElement,
count, kind, pads, obj, range, bufOff) already agreed on both
sides, and the fixture's inline-uniform entry (count 16) matches
the bridge too (`updateInlineUniform` sets count to the byte
size). Only the header and the array's single-entry form were
superseded. The reader, bridge and docs were not touched.

FD-64's fifth subtest (the spirvfix corpus conversion) was no
longer failing on this base: the FD-62 lane's measured corpus
re-baseline (`ea7283041`) resolved it and the FD-70
composition's fixture enrollment (`bf2ce6bbf`) kept it green.

## The masked second staleness

With the header corrected, the decode test reached assertions
the throw had masked since FD-64 was filed, and one was stale
in the same way: the decoded sampler object's expected shape.
The fixture's raw modes are U/V = CLAMP_TO_BORDER (3), W =
MIRROR_CLAMP_TO_EDGE (4); the current ADDR table maps them to
clamp-to-edge / clamp-to-edge / mirror-repeat, and the decoded
object has also carried the raw `addrModeU/V/W` fields since the
border-emulation feature (`26ffedefa`), pinned by the suite's
`vk-border-emulation.test.mjs` (which asserts the raw modes on a
decoded record). The expectation now asserts exactly the current
object — all previously asserted fields kept, the three raw-mode
fields added; nothing dropped.

## Changes (test file only, `9d132afc7`)

`web/tests/vkwebgpu-v2.test.mjs`:

1. Set-0 DESC_SET fixture: header now `u8(0) u8(0) u16(5)`;
   the sampled-image array is three per-element entries
   (dstArrayElement 0/1/2, count 1, kind 2, same view 0x9e9), the
   shape the bridge actually emits.
2. Set-1 DESC_SET fixture: header now `u8(1) u8(0) u16(1)`.
3. Decode assertions updated to the same contract:
   `ds0.bindings.length` 3 -> 5; the array check now asserts
   three binding-1 entries with elements [0,1,2], each
   count 1 and kind 2 — the "explicit, not collapsed to
   element 0" check in current form.
4. Sampler expectation updated per above.

## Proof

- `web/tests/vkwebgpu-v2.test.mjs`: 20/20
  (`test-results/fd64/vkwebgpu-v2-fixed2.log`).
- Full `tools/run-tests.mjs` against the built tree: ALL TESTS
  PASSED — legacy group 115/0, suite-unit group 348/0
  (`test-results/fd64/run-tests-built.log`). Browser build:
  BUILD COMPLETE with 'Browser export verified', wasm
  `d631d254` — byte-identical to the shipped build's wasm, as
  expected for a test-only change (`browser-build3.log`).
- Sabotage: a scratch copy with only the stale u8-count header
  restored returns the same four failures (now "220 trailing
  bytes" over the five current entries), so the corrected cases
  still discriminate; the scratch copy was deleted
  (`test-results/fd64/sabotage-stale-header.log`).
- No product file changed (`git diff` vs base: the one test file,
  then register + this record). The changed file is a test, not
  shipped code; the browser build was nevertheless run in-tree
  because the suite's provenance-artifacts test requires a
  built tree (see Correction).

## Correction to the first completion figures

The lane's first completion message quoted full-run figures
(legacy 114/0, suite 348/0, rc 0) taken from a background-delivery
summary that proved to be garbled. The preserved log of that run
(`run-tests-fixed.log`) actually shows legacy 115/0 and the
suite-unit group at 347 passed / 1 failed — the failure being
`provenance-artifacts` in a then-unbuilt tree (it asserts
BUILD_INFO.json and wasm/js pairs in dist/runtime and
web/runtime, which exist only after a browser build; the failure
is environmental and pre-existing for any unbuilt tree). The
browser build was then run in this tree (first attempt named the
fresh-tree payload staging the established recipe requires —
dxvk/vkd3d build dirs and the rootfs64 payload dirs copied from
integ; log `browser-build3.log`) and the full runner re-run
against the built tree gives the figures quoted in this record:
legacy 115/0, suite 348/0, ALL TESTS PASSED
(`run-tests-built.log`).

## Also found

The comment above the ADDR table (`web/vk-protocol.mjs:89`)
says border addressing and mirror-clamp "both use
clamp-to-edge", but the table maps MIRROR_CLAMP_TO_EDGE (4) to
'mirror-repeat'. The table is the served behaviour; the comment
is the stale side. Filed on the register as FD-91 (provisional),
left unedited here as outside this lane's mandate.

## Register

FD-64 marked closed on branch with an update block; CL-43 filed
provisionally (CL-40/41/42 are provisionally taken by in-flight
lanes' uncomposed records; the composing merge verifies and
renumbers by audit). FD-91 filed provisionally in Section A
(FD-85..FD-90 likewise taken in flight).

Housekeeping: the browser build ran in this tree for the
provenance evidence above; generated state (dist, web/runtime,
project/linux/Build, web/vendor, the copied payload dirs) was
stripped again at lane end. Evidence logs under
test-results/fd64/ stay in the worktree; disk 63G free at lane
start.
