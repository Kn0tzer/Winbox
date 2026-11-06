# Documentation library pass — 2026-11-05

_Branch `lane-docslib` off the integration tip `48d27952a`. Goal: the
project's documentation as one coherent library a newcomer can
navigate — accurate living documents, one owner per subject, dated
records preserved and indexed, every internal reference resolving,
and one index at the documentation's root. Documentation only: no
code or product behavior was changed, and nothing was pushed or
deployed._

## The index

- New: `docs/INDEX.md` — the single map. It defines the three genres
  (living documents, design records, dated records) and their rules,
  assigns every subject exactly one living owner, describes the
  machine-guarded known-gaps registers and their constraints, lists
  the design records with their owners, points at the reports
  sub-index, names every project-owned tool-local document by full
  path, and states which trees are vendored upstream documentation
  and out of scope.
- `docs/reports/README.md` is now a complete sub-index: its curated
  sections stay, its one rotten value (the register described as
  "FD-01…FD-38") is fixed to point at the register's own open
  section, naming notes disambiguate the two "batch 3" merge records
  and the d3d9-complex draft/final pair, and a generated "Complete
  listing" names all 227 records in the directory.
- The repo `README.md` now points at the index and the status file,
  and defers per-program verdicts to `docs/game-compatibility.md`.

## The status document

`docs/WINBOX-STATUS.md` was rewritten (7,839 → 2,645 bytes) to state
roles and sources rather than values that rot:

- Removed: the live build's commit and program hash stated inline,
  the register's entry/closure counts, a stuck-start tally, and the
  claim "the current deploy record is deploy-batch6" (batch 7's
  record had superseded it). Each subject now names its source:
  the live site's own `BUILD_INFO.json` stamp, the register's open
  section, the newest deploy record by date.
- Split: its three dated "current state" sections and its parked
  note were extracted **verbatim** (mechanical line-range copy,
  81 lines) into `docs/reports/status-snapshots-20261030-20261105.md`,
  a dated record with a provenance header. Its "Workflow speedups"
  section was extracted verbatim into the new living document
  `docs/workflow.md`, since day-to-day workflow is a different
  subject from status.

## Design records (`docs/specs/`)

The specs are the October build-out's requirements documents —
written before the work shipped, untouched since 2026-10-17/18,
with zero inbound references from code, tests, or living documents
(reference search across the tree). They are reclassified, honestly,
as design records: each now carries a header saying so and naming
its living owner. Two pairs were examined as duplicates:

- `shared-runtime-dependencies.md` vs `common-dependencies.md` —
  one subject (the shared runtime payloads), genuinely duplicated.
  **Merged**: the former's full text was appended to the latter
  under a marked section ("Same environment for demos and
  uploads"), and the file was removed. Disuse evidence: zero
  inbound references anywhere outside dated records; the only
  remaining mention of the name is the merge note itself. Nothing
  was lost — the merged text is verbatim.
- Folder access (`folder-access-modes.md` guide vs
  `specs/folder-access.md`) and the launcher redesign (spec vs
  `reports/launcher-redesign-20261017.md`) are **not** duplicates:
  one side is a behavior guide or a design record, the other a
  dated outcome record. Records stay unedited by rule, so both
  pairs stand, cross-owned in the index and in the specs' headers.

## Accuracy fixes in living documents

- `docs/architecture.md` claimed "no OPFS implementation is
  present." The code disagrees: `web/files.mjs` stages imported
  file data in OPFS (`navigator.storage.getDirectory()`). The
  paragraph now describes the IndexedDB/OPFS split with its
  sources. (Its component table was spot-checked: every named
  path exists.)
- `docs/jit-experiment.md` (last edited 2026-10-19, zero inbound
  references) carried a first-phase milestone table presented
  without a date and a deploy link to the site's old `/wine` path.
  It now carries a status header dating it and pointing at the
  register and performance records for current JIT state, and the
  link reads `/winbox`.
- Not fixed, by rule: the specs' stale tooling commands (`pnpm
  test` etc.) stand as written — they are evidence of the
  requirements as scoped; the new headers warn that specs are not
  current behavior.

## What was deliberately not touched

- `docs/known-gaps/` content: tests pin its formats and, for
  `vulkan.md`, its line numbers (the citations check). Content
  unchanged.
- `docs/defect-register.md`: the queue; unchanged.
- `docs/game-compatibility.md`: read by a unit test and is itself
  dated evidence with an honest header; unchanged.
- All 227 dated records: unedited, including the two merge records
  with colliding names (clarified in the index layer instead) and
  `d3d9-complex/REPORT-draft.md` (identified as the draft in the
  naming notes).
- Vendored documentation under `lib/`, `tools/dxvk/src/`, and
  `tools/vkd3d/src/`: upstream-owned, excluded by the index.

## Dead references found (reported, not silently absorbed)

- `web/tests/baldi-boot-probe.mjs` points readers at
  `docs/baldi-d3d9-analysis.md`, which does not exist anywhere in
  the tree; the Baldi records that do exist are the three
  `docs/reports/baldi-*.md` files. The pointer lives in a code
  comment; this pass changed documentation only, so the comment
  stands as found and is flagged here for a future code-touching
  lane. It is a documentation defect, not a product defect, so it
  is not a register entry.
- Inside records: 51 links in `q3-webgpu-luna-20261022.md` point at
  absolute `/tmp/winbox-finish/…` evidence paths, and 3 links in
  `wine32-final/report.md` point at sibling evidence files that
  were never committed. Both are dated records and stay unedited;
  `docs/reports/README.md` already records that raw evidence for
  the oldest reports no longer exists on this machine, so those
  reports stand on their own text.

## Verification (all run on this branch's tree)

- **Reference resolution.** A mechanical pass over every
  project-owned markdown file (298 files; vendored trees excluded)
  extracted every relative link and checked its target exists:
  **0 broken document references.** The 54 unresolved targets are
  exactly the record-internal evidence pointers itemized above
  (51 absolute paths + 3 uncommitted attachments), classified
  separately by the checker.
- **Index completeness.** The same pass verified every one of the
  298 project-owned markdown files is named in `docs/INDEX.md`
  (full path for anything outside `docs/`) or, for records, in the
  `docs/reports/README.md` listing: **0 uncovered.**
- **Load-bearing documents still work.** Every unit test that
  reads a document was run in this tree, after staging vendor
  modules:
  - known-gaps-audit — 1 passed, 0 failed
  - vk-register-citations — 1 passed, 0 failed
  - game-capability-matrix — 1 passed, 0 failed
  - d3d-entirety — 1 passed, 0 failed
  - runtime-completeness — 1 passed, 0 failed

## Process note

The first commit of this pass accidentally swept in the worktree
preparation script's shared-store symlinks (13 link entries under
`tools/` and `web/`), because they are untracked and not ignored.
It was immediately reset and recommitted with only the
documentation paths; the branch's history contains no trace of the
payload links. Lane trees should add paths explicitly, never
`git add -A`, until those links are covered by an ignore rule.

## State at close

Three commits on `lane-docslib` (library reorganization; index
completion and this record, with its addendum). Working tree
stripped of generated state. Nothing pushed, nothing deployed.

_Addendum (same evening): during final cleanup this lane deleted
the whole `test-results/` directory to remove its own test scratch,
not noticing the directory also holds tracked record files. The
branch was never affected — nothing was committed after the
deletion — and the tracked content was restored in full from git
in the same session; the final tree check shows zero modified or
deleted tracked files. The lesson matches the process note above:
in this tree, generated state and tracked records share directory
names, so cleanup must name exact paths, never whole directories._
