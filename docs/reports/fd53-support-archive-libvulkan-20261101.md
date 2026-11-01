# FD-53 — support archive carried two different Vulkan libraries under one name

Lane `lane-fd53` (worktree `/home/ubuntu/wt-fd53`, off integ
`a1e8928ba`), 2026-11-01. Fix commit `1e228a7ed`. Register: FD-53
closed as CL-24. Nothing pushed, nothing deployed; the live site
still serves the previous pack — this lane changes what the NEXT
build ships.

## End state reached

The support archive the standard build produces now contains
exactly one copy of the guest Vulkan library at each staged path,
that copy is the project shim the pins intend, and two independent
guards fail the build loudly if a duplicate or substituted copy
ever returns.

## What was wrong (all verified against the shipped pack)

The shipped pack (`support-de592964f772...`, 517,859,771 B, 3,544
entries / 3,543 unique names) carried
`usr/lib/x86_64-linux-gnu/libvulkan.so.1` twice — the only
duplicated member name in the archive:

- entry 2959: 185,464 B, sha256 `60b89e5afc7c...` — the project's
  own Vulkan shim (`tools/rootfs64/libvk64/libvulkan.so.1`,
  bit-exact): ELF with SONAME `libvulkan.so.1`, zero DT_NEEDED,
  312 exported symbols, no loader strings. Staged deliberately by
  `tools/browser/stage-dependencies.py` at both `lib/` and
  `usr/lib/` prefixes for both architectures, with its digest
  recorded as the `vulkan` provenance in `dependencies.json`.
- entry 3272: 457,224 B, sha256 `91dd2cb10ee4...` — the genuine
  Khronos Vulkan loader 1.3.239 (loader strings, DT_NEEDED
  libm/libc). It arrived as a transitive passenger: the amd64
  GStreamer closure's `libvulkan1_1.3.239.0-1` deb
  (`tools/rootfs64/prepare-gstreamer.py`), whose tree preserves
  the deb's `libvulkan.so.1 -> libvulkan.so.1.3.239` symlink;
  the staging walk then added every closure file at its relative
  path, re-adding the loader under the soname (entry 3272) and
  its versioned name (entry 3273). `add()` never deduplicated;
  zipfile only emitted the `UserWarning` FD-01's lane saw.

Which bytes a reader got depended on that reader's duplicate
handling (Python zipfile returns the last entry — the loader).

## Why the shim is the intended copy (duplication not load-bearing)

- The shim is staged by an explicit, provenance-recorded act; the
  loader's presence traces to an unreviewed closure walk.
- The pack contains no Vulkan ICD manifests anywhere, so a real
  loader in the guest would find no driver; the shim is the only
  workable Vulkan library at that path.
- `wine64.zip` carries no `libvulkan.so.1` at all — the support
  pack is the sole source of the path for the 64-bit guest.
- The closure's DT_NEEDED soname reference is satisfied by the
  shim (same SONAME), so removing the loader breaks no link.

## The fix (`1e228a7ed`)

- `tools/browser/stage-dependencies.py`: the closure walk skips
  the loader under both names (`GSTREAMER_ARCHIVE_EXCLUDES`, with
  the reasoning in a comment), and `add()` now refuses any
  duplicate member name with `SystemExit` — the register's
  explicit ask.
- `tools/rootfs64/check_runtime_completeness.py`: new gate check
  (docstring item 2c) — member names unique archive-wide; the
  four `libvulkan.so.1` members occur exactly once and hash to
  the `dependencies.json` `vulkan` provenance digests. The
  GStreamer split check now accounts for the deliberate
  exclusion. Like its sibling support checks it skips loudly
  when no pack has been built yet, and runs for real inside
  `build.mjs` (hard gate) and `bootstrap.py --check`.
- `docs/known-gaps/runtime-artifact-manifest.md` regenerated:
  support pack re-pinned to `support-c4dbe35986a5...`
  (517,564,189 B, 3,542 members); the placement accounting now
  shows the versioned loader as deliberately not shipped.

## Proof

Red (each guard can fail):

- New completeness check against the previously shipped pack:
  FAIL — `support pack has duplicate member names:
  ['usr/lib/x86_64-linux-gnu/libvulkan.so.1']`.
- Same check against a sabotage pack (loader bytes once under
  the soname, no duplicate): FAIL — hash-pin mismatch
  (`91dd2cb10ee4... is not the pinned project shim
  60b89e5afc7c...`).
- Staging with the exclusion disabled: exit 1,
  `duplicate member name in support archive:
  usr/lib/x86_64-linux-gnu/libvulkan.so.1`.

Green:

- Standalone staging and two runs of the standard build
  (`tools/bw64-build.sh`) all produce the byte-identical pack
  `support-c4dbe35986a5...` — staging is deterministic with the
  fix. Build 2 ends `Browser export verified` / `BUILD
  COMPLETE`; its gate reports the FD-53 check and the GStreamer
  split check PASS, 0 failures overall (build 1 had failed only
  on the expected manifest drift, discharged by regeneration).
- Member-level audit of the built pack vs the shipped one
  (`test-results/fd53-20261101/archive-audit.txt`): 3,542
  entries, zero duplicates; the only name removed is
  `libvulkan.so.1.3.239`; no names added; every common member
  byte-identical except the formerly duplicated soname, whose
  effective bytes are now the pinned shim; all four
  `libvulkan.so.1` members hash to the recorded provenance.
- `web/tests/suite/unit/runtime-completeness.test.mjs` passes
  against the built dist (1/1).

Not run: the JS unit suite beyond the completeness wrapper and
the native suites — the change touches only Python packaging /
checker code and the generated manifest; the built wasm is
byte-identical to the live build's (`11d2e651...`).

## Housekeeping notes

- Tree bootstrapped from pin-verified copies (payload trees from
  the finished `wt-fd55`; wine zips from the live runtime dir;
  prefix/glibc32 zips from the retained runtime-assets archive —
  each verified against `runtime-assets.json` before use;
  `bootstrap.py --check`: "Runtime assets verified (13 files)").
- Disk: 5.1-5.6G free through the lane. Removed ~1.4G of
  abandoned 2026-10-31 lane scratch from /tmp
  (`fd02-depstest2`, `fd40tar`, `bw32-wine32-cache`, `rtassets`)
  to keep the build above the floor; no other lane's tree or
  tenant file touched. My tree's generated state stripped
  before handoff.
- One SSH drop mid-build-1: the build survived on the box and
  was monitored to completion by exact pid; nothing restarted.
