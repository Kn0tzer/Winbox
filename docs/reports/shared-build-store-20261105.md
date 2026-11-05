# Shared build store — fresh worktrees link their generated payloads instead of copying them

Lane record, 2026-11-05. Branch `lane-sharedbuild` on integ `48d27952a`.
Register: FD-172 (filed and fixed by this lane), CL-125.
Evidence: `test-results/sharedbuild/` in this tree (prep, build, suite and
audit logs for both proof trees).

## The problem, measured

Every new working copy of the project needed the same set of generated
payload inputs, and the setup ritual copied them into each tree:

- the six pinned runtime zips and the pinned Slang assets (~515 MB),
  restored per tree from the retained runtime-assets archive;
- the DXVK build outputs (`tools/dxvk/build.w64`, `build.w32`, `install`);
- the vkd3d build outputs (`tools/vkd3d/build.w64`, `build.w32`);
- the `tools/rootfs64` payload extractions (gecko, mono32, gstreamer32,
  gstreamer64, openal, cjk-font);
- the `tools/browser` tool payloads (zig, disasm).

A freshly prepared tree held **1,981,059,339 bytes** of real copies of
these paths, and the 14 trees on the box held **12,255,216,359 bytes**
between them (measured per path, real files only, tracked sources and
build outputs excluded).

The shipped helper script made this worse than heavy: it fails partway
on a fresh tree today (exit 1 — its donor checkout no longer holds
several payload directories; see `prep-old-script.log`), it never
listed the gstreamer payloads or vkd3d `build.w32`, it sourced DXVK
from the main checkout — whose DXVK build outputs differ from the
pinned donor set, so a tree assembled exactly as the script says
**fails the build's own manifest gate** (first proof build:
`build-old.log` — every check passed except "manifest matches shipped
zips"; the support pack it produced was `support-4239f15c6dd3…`
instead of the pinned `support-c4dbe35986a5…`) — and it overwrote the
worktree's `runtime-assets.json` with a stale manifest pinned to the
20261013 release, a commit trap the status doc warned about in prose
instead of the script not doing it.

## What was built

**One shared store**: `/home/ubuntu/boxedwine64-shared/` —
**1,980,622,511 bytes, 18,718 files**, with a `MANIFEST.json`
(provenance per section) and a `SHA256SUMS` covering every file
(full `sha256sum -c` self-check passes).

- `web-runtime/` + `slang/`: extracted from the retained archive
  `/home/ubuntu/backups/winbox-runtime-assets-20261104/browser-runtime-20261104.tar`
  after the archive's sha256 was verified against the release pin
  (`3c80c8a14e77…`). All 13 pinned files in the store match the
  project's committed `tools/browser/runtime-assets.json` pins under
  bootstrap's own size+sha256 rule.
- `dxvk/`: build outputs copied read-only from the integration tree;
  their full-tree digests are byte-identical to the pinned copies in
  the preserved donor tree (`build.w64` `227b417585fb…`,
  `build.w32` `4414320ff8ff…`). `dxvk/install` from the main checkout
  (the one payload the main checkout still holds correctly).
- `vkd3d/`, `rootfs64/`, `browser/` (zig + disasm): the integration
  tree's gate-consumed payload sets, read-only copies. The preserved
  full vkd3d build trees in the restage worktrees were not used and
  not touched — they carry a different, larger file set by design.

**One setup step**: `tools/winbox-wt-prep.sh` (installed over
`/home/ubuntu/scripts/winbox-wt-prep.sh`; the old script is kept
beside it as `winbox-wt-prep.sh.pre-shared-20261105`). Run once
against a fresh worktree it:

1. verifies the store against the worktree's committed
   `runtime-assets.json` pins (all 13 files, size + sha256);
2. verifies the store's full `SHA256SUMS`;
3. aborts before linking on any mismatch; otherwise creates 23
   payload links — per file for the runtime zips and Slang assets
   (`web/runtime` and `tools/browser/slang` must stay real
   directories: the build writes into the former, and bootstrap's
   restore replaces individual files in both), whole-directory for
   the payload trees — plus the `node_modules` and `web/vendor`
   links the old script also made;
4. never overwrites real content (an existing real file or directory
   is kept, with a warning) and never touches `runtime-assets.json`.

The store is read-only for consumers; the script prints the escape
hatch (replace a link with a real copy) for the rare lane that must
modify a payload. All payload paths are git-ignored, so the links do
not dirty a worktree. Setup documentation
(`docs/WINBOX-STATUS.md`, "Workflow speedups") now describes exactly
this one step; the per-copy ritual and the manifest trap warning are
gone.

## Side-by-side proof (both trees detached at integ `48d27952a`)

- **Old way** (`/home/ubuntu/wt-proof-old`): the shipped script failed
  partway (above); the tree was completed manually the way lanes
  actually do it — payload copies from the integration tree plus
  `bootstrap.py --local` ("Restored and verified 13 runtime assets").
  Its first build failed the manifest gate on the script's stale DXVK
  donor (above); with DXVK taken from the pinned donor, the rebuild
  reached **BUILD COMPLETE** with "dependency archive c4dbe35986a5
  verified" (`build-old2.log`). Real payload bytes: **1,981,059,339**.
- **New way** (`/home/ubuntu/wt-proof-new`): one run of the new script
  — pins OK, store integrity OK, 23 links, exit 0 (`prep-new.log`).
  Build reached **BUILD COMPLETE** with the same pinned archive
  verified (`build-new.log`). Real payload bytes: **557**.
- The two builds produced **byte-identical product wasm**:
  sha256 `9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241`,
  5,688,949 bytes, with zero error/FAILED lines in either build log.
- JS suites (`node tools/run-tests.mjs`, run solo, one tree at a
  time): old-way tree **ALL TESTS PASSED** — legacy suite exit 0,
  unit suite **353 passed, 0 failed** (`js-old.log`). New-way tree:
  legacy suite exit 0; unit suite **349 passed, 4 failed** in its
  full run (`js-new.log`), which ran under measurably heavier box
  load (legacy phase 92 s vs 37 s; unit phase 1,204 s vs 1,120 s).
  The four failures — `boot-progress-snapshot`,
  `x11-drawing-handlers`, `x11-ewmh-state`, `x11-freecolormap` —
  are timing/ownership assertions in tests that read no payload
  path, and **all four pass on filtered re-runs in the same
  new-way tree** (`js-new-rerun-x11.log`: 36 passed, 0 failed;
  `js-new-rerun-boot.log`: 1 passed, 0 failed). This is the
  campaign's documented load-flake family, not a store effect:
  the trees' product wasm and payload bytes are identical.
- Known-gaps audit (`node tools/known-gaps-audit.mjs`, the disasm
  payload's consumer): exit 0 in both trees. Its scan counts differ
  slightly (old: 1,765 files / 4,939 hits; new: 1,760 / 4,936)
  because the audit by design does not follow symlinks, so in a
  linked tree it does not descend into the payload extractions —
  whose bytes are the store's, and which every real-copy tree
  (including the integration tree) still scans. Recorded here so
  the delta is never mistaken for drift.
- Browser capstone not run: a D3D11/D3D12 conformance run in the
  new-way tree (the vkd3d/DXVK payloads' end-to-end consumer) stayed
  first in the heavy probe queue for ~45 minutes, held back by other
  lanes' builds waiting for probes to drain and by a long-running
  browser benchmark from another lane, and was stood down unstarted
  rather than hold this lane open (its log, `d3dconf-new.log`, shows
  the queue wait only). Payload consumption by the new-way tree is
  instead proven by the build itself: staging consumed the linked
  rootfs64/DXVK/vkd3d payloads and passed the prefix checks
  (including the CJK font pin and the built-artifact sections), the
  manifest gate verified the shipped archives built from those
  payloads against the committed pins, and the JS unit suite's
  vk/d3d tests passed in the same tree. A gate-run browser
  conformance pass in a linked tree remains a worthwhile future
  confirmation; nothing in this record claims it happened.

## Disk effect

Per fresh tree: 1,981,059,339 real payload bytes → 557 (a saving of
~1.98 GB per new tree, plus the copy time). Fleet-wide, the 14
existing trees still hold their 12,255,216,359 bytes — adoption is
by new trees, and existing trees, the preserved vkd3d source trees
and the retained release archives were left exactly as they are, so
~10.3 GB becomes recoverable only as old trees retire through the
normal cleanup. The store itself is one 1.98 GB copy, verified
against the project's pins, replacing one copy per tree from now on.

## Notes for the merge lane

- FD/CL numbers (FD-172, CL-125) are provisional, per the standing
  collision rule: FD-166..171 were deliberately left free for the
  in-flight census/perf/fd115 filings.
- `tools/winbox-wt-prep.sh` is the repo copy of the box script;
  the two are byte-identical (sha256 `4eff9b353ccf…`).
- The two proof worktrees were removed after the proof; their logs
  are preserved in this tree's `test-results/sharedbuild/`.
