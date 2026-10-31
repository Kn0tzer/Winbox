# Documentation and tree-organization cleanup — lane record (2026-10-30)

Branch `lane-docsclean` (worktree `wt-docsclean`), based on integration
tip `538f03db5`. One lane of the cleanup / logging / error-recovery
campaign; this lane's area was the project's documentation and overall
tree organization. Method per the clean-up skill: evidence over
inference, smallest behavior-preserving change, deletion only with
proof of disuse, one change per commit. The pathfinder lane's pattern
record (`docs/failure-logging-and-recovery.md`, branch `lane-cleanlog`)
was followed where it touches documents; nothing in this lane changes
code or behavior.

## Audit coverage

Every project-owned document was inventoried (213 tracked `.md` files;
vendored trees `lib/`, `tools/dxvk/src/`, `tools/vkd3d/src/` excluded
as upstream). Living documents were read in full; dated reports were
checked as an index problem, not re-litigated. Reference searches
(grep over the whole tree, plus `git grep` over code, tests, scripts
and workflows) established what is load-bearing before anything was
touched. A trash scan for `.bak`/`.orig`/`~`/placeholder files found
none in the tracked tree.

## Changes (one commit each)

1. `52f120c0e` — **rewrote `docs/WINBOX-STATUS.md`.** The file whose
   header says "keep updated; read this first" was a 2026-10-26
   snapshot: it named build 11 as live with the merged tree "NOT
   deployed yet" (the site has shipped several builds since, currently
   the dead-start-fix build), ended its plan at "then the DirectX 12
   campaign" (shipped 2026-10-29), quoted register counts since
   settled, and pointed at a STALENESS.md that was never committed
   and a stash backup directory that no longer exists on the box.
   The rewrite is short and states roles instead of values that rot:
   where the live build stamp, the registers, the campaign and deploy
   records, and the demo evidence live, what is current as of
   2026-10-30, and what is paused/parked by the user's orders. Its
   Workflow speedups section is kept verbatim (both scripts and the
   slot system re-verified present and as described).
2. `124056a11` — **based `docs/game-compatibility.md` on the
   2026-10-30 all-demos sweep.** The table claimed to summarize the
   latest verified evidence but was dated 2026-10-24; several rows had
   inverted against reality (Gang Beasts credited with a rendered
   title screen — the sweep shows it never produces one frame on the
   current build; Celeste "not re-verified" — its title is proven on
   the live site). All 15 demos are now covered from the sweep of live
   build `5741034f3`; the three rows later refined by lane work
   (Mindustry, Al-Khaser, Godot) are marked as branch-only fixes not
   in the live build. The verification recipe is kept; its demo list
   now names all 15 ids.
3. `f8bd7d927` — **corrected `docs/reports/README.md`.** It still
   described the 2026-10-13 preservation batch ("these 25 Markdown
   reports" — there are now 70+ files), never mentioned the campaign
   or deploy records, and told readers to restore raw captures from
   `/home/ubuntu/backups/boxedwine64-maintenance-20261013/`, which no
   longer exists on the box (verified). The index now leads with the
   campaign/deploy records and states plainly that the old capture
   archive is gone, so those reports stand on their own text.
4. `a421ac2d5` — **rewrote `tools/rootfs64/README.md` as a directory
   map.** The old text was the pre-Wine milestone journal (Docker
   Desktop on macOS, dynamic glibc "cannot load libc.so.6", an
   unconfirmed pthread_join) and said nothing about what the directory
   is for now. The rewrite maps every tracked entry group from its own
   header or README — assembly/verification path, the two-vintage Wine
   staging, guest shims, support payloads — and keeps an accurate
   section for the `work/` + `root/` standalone-runner fixtures.
5. `1cc5b5396` — **deleted `tasks/plan.md` and `tasks/todo.md`.**
   Evidence of disuse: no reference from any script, test, workflow,
   or document of record (only a dated spec and a dated report mention
   `tasks/plan.md`, as history); both present October 17–19 work
   orders as current direction, since superseded at least twice over.
   Content remains in git history.
6. `df919ba83` — **README.md:** added WinMerge and Tetris to the
   program-support list (both verified working in the 2026-10-30
   sweep; the list predated their staging as demos). The user's own
   "How it was made" text was not touched.
7. `b9426683c` — **`project/emscripten/buildInstructions.md`:**
   header now points at the canonical build (`tools/bw64-build.sh`,
   `BUILD_PROTOCOL.md`); fixed a make target named with the wrong
   case (`Release` → `release`; make is case-sensitive, verified
   against `project/emscripten/makefile`) and noted the `wasm64-mt` /
   `wasm64-mt-jit` targets the shipped runtime is built from.

## Verification

- `web/tests/suite/unit/game-capability-matrix.test.mjs` — the one
  test that reads a file changed here — passes 8/8 against the new
  `docs/game-compatibility.md`. (An initial 1-failure run was A/B'd:
  the same failure occurs with the old text stashed; it was the
  fresh worktree's missing `node_modules`, fixed by the standard
  symlink, after which 8/8.)
- Link integrity: a scripted check of every relative markdown link in
  all project-owned `.md` files (211 files on this branch, 213 on the
  base) finds **54 broken links before and after — the identical
  set**, all pre-existing inside two dated reports (51 links from
  `docs/reports/q3-webgpu-luna-20261022.md` to `/tmp/winbox-finish/…`
  scratch evidence, long gone; 3 sibling-file links in
  `docs/reports/wine32-final/report.md`). No link in any file this
  lane touched is broken, and the `tasks/` deletion broke none (its
  only mentions are backticked history in a dated spec and report).
- No code, test, build, or deploy file was changed; the JS/native
  suites are unaffected by construction (the only test reading any
  changed file is the one run above).

## Deliberately left alone

- `docs/known-gaps/*` — the live registers; test-guarded (citation
  audit), actively maintained, and
  `runtime-artifact-manifest.md` is machine-generated by
  `tools/rootfs64/gen_runtime_manifest.py`.
- `docs/reports/*` contents — dated records of what was measured;
  several are consumed by tests (e.g. the D3D conformance matrix by
  `web/tests/suite/unit/d3d-entirety.test.mjs`). Only the index was
  stale; only the index changed.
- `tools/vkd3d/CURRENT_STATUS.md`, `FEASIBILITY.md`, `PROBE_PLAN.md`,
  `ICD_WORK_ITEMS.md` — despite their names, each already carries an
  honest "historical investigation" banner naming its successor, and
  tests reference this area. Correctly labelled history is not rot.
- `docs/architecture.md` (checked against the current networking and
  bridge state — accurate), `docs/vulkan-protocol.md` (honest status
  line, names live sources), `docs/jit-experiment.md` (a living JIT
  document despite its name), `BUILD_PROTOCOL.md` (matches the slot
  system it describes), `web/tests/*` docs, `tools/vkd3d/BUILD.md`,
  `tools/conformance/README.md` — current as written.
- `docs/specs/*` and the `docs/folder-access-modes.md` /
  `docs/specs/folder-access.md` pair — design contracts of record and
  a spec/explainer pair, not duplicates; nothing in them claims a
  false current state.
- `testdata/`, `tools/bench/*.md`, `test-results/site-chromium-20261030/`
  — evidence data and a committed evidence record, not prose.
- Vendored trees (`lib/`, `tools/dxvk/src/`, `tools/vkd3d/src/`) —
  upstream content, tracked as pinned by `SOURCES.json` files.

## Flags for the coordinator

- **The stash drop-safe proof has rotted.** `docs/WINBOX-STATUS.md`
  recorded `stash@{0}` in the main tree as "proven drop-safe, backup
  in `/home/ubuntu/backups/winbox-stash0-20261025/`". The stash still
  exists (`On fix/winbox-render-20261020: item 8 triangle fans in
  progress`), but that backup directory no longer exists on the box
  (checked 2026-10-30 — presumably removed in the 2026-10-29 storage
  purge). Do not drop the stash on the old proof.
- The 54 pre-existing broken links (census above) are all evidence
  paths inside two dated reports whose captures are gone; the reports
  index now tells readers those reports stand on their own text.
  Rewriting the reports themselves was judged worse than the broken
  links.
