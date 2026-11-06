# Organization composition — 2026-11-06

Composed the four organization branches onto `integ/winbox-20261022b`
(base `465c1c2b2`, the batch-8 deploy record) and audited the register
clean. Light checks on the composed tree are green (see below). The
full browser guest suite was not run in this composition; the code
change is covered by the browser build verification, the native
suites, and the JS suites, and batch 9's full gate runs before
anything ships. NOT deployed, NOT pushed.

## Pool and order

The four branches are independent: no overlapping files between
them, and no register change on any branch (verified: empty register
diff on all four). Order was chosen to put the code change after the
docs-only branches, matching the order the lanes landed.

| Composed | Source | Content |
|---|---|---|
| `b96be41fb` | `35a53c0f6` (lane-project-root) | Project root + map, three preserved trees moved, moves M1–M8 prepared |
| `f7a8cdc5c` | `41dceffb2` (lane-project-root) | Record amendment (map citation) |
| `c91b6cac2` | `125b756e3` (lane-docslib) | Docs library: index, status rewrite, workflow split, specs reclassified + one merge |
| `a795651f4` | `a400d6e64` (lane-docslib) | Complete index coverage, reports naming notes |
| `80286ae8a` | `d44b94f99` (lane-docslib) | Record addendum on the cleanup restore |
| `d791cf025` | `295570595` (lane-codeorg) | Remove `include/proccpuinfo.h`, unused duplicate of `include/cpuinfo.h` |
| `d827964b4` | `ae3d7c274` (lane-codeorg) | Split the KProcess clone family into `kprocessclone.cpp` |
| `9c3f6d462` | `1d791f005` (lane-codeorg) | x64 dispatch guard reads `kprocessclone.cpp` |
| `9de31c5d0` | `28b2a5299` (lane-codeorg) | elf-loader-mmap guard reads `kprocessclone.cpp` |
| `6b7d636fd` | `74957e749` (lane-codeorg) | Lane record for the code organization pass |
| `d3df201af` | `0ab0103d7` (lane-codeorg-decisions) | Remove the never-compiled `ops.h` header cluster (5,215 lines); SIDT/SGDT citation repointed |
| `177099b4c` | `067094f67` (lane-codeorg-decisions) | Record amendment — all six proposals decided |
| `bec1e0d25` | `b2a765d49` (lane-standards) | Task standards for recurring work, each verified literally |
| `5ac353a19` | `8a6077e2c` (lane-standards) | Record: checking-queue verification (pipeline 12) |

14 cherry-picks, zero conflicts. The decisions branch descends from
the codeorg tip (verified: `74957e749` is an ancestor of `067094f67`),
so composing its tip carries the whole codeorg stack; `lane-codeorg`
was not picked separately.

## Register audit

No branch changed the register. Mechanical audit of the composed
register (script, re-read from disk): 162 FD entries in section A —
numbers 1..165 except the pre-existing gaps 46/56/57 — and 124
closure headers, CL-01..CL-124 each exactly once, no duplicates. No
settlement was needed: no collisions, no provisional closures in the
pool. The register diff vs the base is empty.

## Full-diff accounting

Composed diff vs base `465c1c2b2`: 40 files changed, 2752 insertions,
6122 deletions — exactly the sum of the four branches' diffs
(54 + 798 + 1154 + 746 insertions; 0 + 151 + 5971 + 0 deletions),
same file set.

## Checks (light, on the composed tree)

Evidence under `test-results/org-composition-20261106/` in the
integration tree.

- Citation audit: 352 rows, 0 drift (232 MATCH / 14 MATCH-INHERITED /
  99 UNCHECKABLE / 7 EXEMPT) — `citations.log`.
- JS unit pre-build: 351 passed, 2 failed — `provenance-artifacts`
  (no `dist/` yet, expected) and `runtime-completeness`
  (`web/runtime/` stripped by the batch-8 deploy lane, expected);
  both resolved after the build — `js-unit-prebuild.log`.
- JS legacy: 116 tests, 115 pass, 0 fail, 1 skipped —
  `js-legacy.log`.
- Bootstrap: restored 13 runtime assets from the retained archive
  `browser-runtime-20261104.tar` (sha256 re-verified
  `3c80c8a14e77e88a93fe5b9ce80d50077a457370aa7546070a211d95172062ba`);
  `bootstrap.py --check` 0 failures / 5 skips pre-build, 0 failures /
  1 skip post-build — `bootstrap.log`, `bootstrap-check-pre.log`,
  `bootstrap-check-post.log`.
- Browser build: "Browser export verified" + "BUILD COMPLETE"
  (`BW64_BUILD_OWNER=org-composition`). BUILD_INFO: git_sha
  `5ac353a19240e3a92a601ecd0eba59a576ab418c` (the composed tip),
  dirty 0, wasm sha256
  `79a948262120f838e072137b13e8be8cac2fa15e41868489ab814da9905cbd74`
  (5,688,985 bytes), js_sha256 `9fe55076…` (unchanged from batch 8).
  The wasm matches the codeorg-decisions lane's measured
  byte-identical build on its branch tip, as expected: the composed
  tree's code equals that tip's code and the other branches are
  docs-only — `browser-build.log`.
- Native builds: all five targets exit 0 (`release`, `test`,
  `x64-test`, `x64`, `x64-cache`) — `native-build-*.log`,
  `native-build-status.txt`.
- Native serial suites: main 689 tests, 0 FAILED (248 s); x64 113
  tests, 0 FAILED (25 s) — `native-main.log`, `native-x64.log`.
- run-native: 69 PASS, exit 0 — `run-native.log`.
- One incident during the checks: the first native-build loop died
  when its SSH connection dropped (only `release` had finished; the
  loop died writing to the dead session's stdout). The loop was
  re-run detached for the remaining four targets; `make` is
  incremental and nothing was rebuilt twice. No result was lost.

## Not done / caveats

- The full browser guest suite (32 tests) was not run in this
  composition. Batch 9's full gate runs it on the composed tree
  before anything ships.
- The browser build's wasm differs from batch 8's (`9b51d601` →
  `79a94826`) because the code changed (the KProcess clone split
  changes translation-unit layout); the function-level equivalence
  was established by the codeorg lane on its branch.
- The `ops.h` removal was proven byte-identical by the adjudication
  lane (wasm `79a9482621…` before and after the removal).

## Moves (executed after this commit)

Moves M1–M6, M8, and the integration tree's M7, per
`/home/ubuntu/winbox-project/MAP.md`, in the same quiet window.
Their outcomes are recorded in the follow-up commit amending this
record. Two items stay as they are, awaiting the user's own
decisions: the ~0.7 GB of older `/tmp` items with unclear owners,
and `wt-x86vkd3d`'s deleted `tools/dxvk` files.

## Moves — outcomes (follow-up, 2026-11-06)

All moves executed in the same quiet window as the composition, in
map order. MAP.md was amended per move as each landed. The two items
awaiting the user's own decisions were left exactly as they are: the
~0.7 GB of older /tmp items with unclear owners, and wt-x86vkd3d's
deleted tools/dxvk files.

- **M1 (main repo → repo/boxedwine64):** moved by plain mv.
  `git worktree repair` from the new home fixed all 15 worktree .git
  pointers; `git worktree list` correct from the new home; main
  checkout HEAD 69c800476, 0 status entries, old path gone. Proof:
  the M2 scratch build below (one tree builds and its build
  verifies, at the composed tip).
- **Integ M7 (same window as M1):** moved by `git worktree move`;
  HEAD fdc0a5d1a, 0 status entries, old path gone.
- **M2 (shared store → store/):** moved by plain mv (the first
  attempt nested one level deep because store/ pre-existed empty;
  corrected by moving the contents up). Contents identical: 19,195
  files, 2.0G; SHA256SUMS verifies with 0 failures. The preparation
  script's store path updated (sha256 7731a66d → ab991f66) and
  winbox-publish-build-seed.sh likewise (44803a8e → 59b715f5).
  Re-prep: six trees (wt-buildcache, wt-codeorg,
  wt-codeorg-decisions, wt-docslib, wt-incbuild, wt-pch) re-prepped
  through the script, each rc=0, "prepared ... (linked=23
  kept-existing=0; copied 0 payload bytes)"; the integration tree
  re-prepped rc=0 (linked=1 kept-existing=22 —
  tools/dxvk/install was missing and linked fresh, the rest is real
  content left untouched). wt-fd170 (22 links) and wt-fd171 (17
  links) were repointed by hand to the identical files at the new
  home because the script aborts on them by design: their
  runtime-assets.json pins their own lane-cut archives' wine zips,
  not the store's 20261104 files. Their node_modules/web/vendor
  links, and wt-fd169's and wt-gamepad's node_modules links (those
  two hold no store links), were repointed to the new repo home.
  One trap, recorded for the script's owner: the preparation script
  cannot replace a DANGLING node_modules/web/vendor link (M1 moved
  the repository they point at) — the re-prep removed the dangling
  links first and let the script link them fresh. Final sweep: zero
  links anywhere under worktrees/ point at the old store or repo
  paths. Proof: fresh scratch tree wt-orgproof at the composed tip,
  prepared end-to-end (pins OK, 13 files; SHA256SUMS OK; "prepared
  ... (linked=23 kept-existing=0; copied 0 payload bytes)") and
  built — "Browser export verified", "BUILD COMPLETE", BUILD_INFO
  git_sha fdc0a5d1a, wasm sha256
  79a948262120f838e072137b13e8be8cac2fa15e41868489ab814da9905cbd74
  (5,688,985 B), byte-identical to the composition build's wasm.
  BUILD_INFO records git_dirty_files 13 — the pre-fix behaviour
  (the incbuild lane's dirt-counting fix is not in this tip; it
  rides with batch 9). The scratch tree was removed per the
  closeout standard afterwards (links stripped, worktree removed,
  temp branch deleted). Evidence:
  test-results/org-composition-20261106/m2-proof.log,
  m2-reprep.log, m2-reprep2.log.
- **M3 (checking setup → checks/):** `docker compose down` first
  (all four containers up and healthy, no pipeline running), then
  moved and `docker compose up -d` from the new home:
  checks-woodpecker-server, checks-woodpecker-agent (both healthy),
  checks-gitea, checks-minio all up; Woodpecker API, Gitea, and
  MinIO health endpoints all answer 200. Absolute paths updated in
  submit-check (CI and MAIN),
  laptop-overflow/request-laptop-check (BASE and REPO; mode restored
  to 444, change recorded in the overflow RECORD.md), and
  canonical/.woodpecker.yml (9 references); docker-compose.yml
  carries only relative volume paths and needed no edit. One repair
  the move exposed: the checks scratch clone's origin still pointed
  at the old repo path, so the first proof run failed at fetch; the
  origin was repointed to the new repo home and the run repeated.
  Proof: submit-check lane-standards, pipeline #13 (commit
  148903f2, lane tip 8a6077e2c plus the definition commit) — the
  SAME verdict shape as the Stage-0 record (pipelines #11/#12):
  overall failure, carried entirely by js-unit exit 1 on the two
  known bare-checkout artifacts (351 passed / 2 failed:
  provenance-artifacts, runtime-completeness); citations exit 0
  (352 rows: 232 MATCH / 14 MATCH-INHERITED / 99 UNCHECKABLE / 7
  EXEMPT); js-legacy exit 0 (116 tests: 115 pass, 0 fail, 1
  skipped); native-main-build and native-main exit 0 (689 tests, 0
  FAILED, 251 s); native-x64-build and native-x64 exit 0 (113
  tests, 0 FAILED, 25 s); run-native exit 0 (69 PASS); verify-source
  proves the mounted commit equals the submitted commit and the
  source mount is read-only. Evidence:
  test-results/org-composition-20261106/m3-proof.log and
  checks/evidence/pipeline-13/ (step logs + MinIO objects).
  Follow-up (not done here): the hygiene-audit cron body mentions
  /home/ubuntu/checks-ci descriptively; that one line needs a
  follow-up edit with user agreement.
- **M4 (project scripts → scripts/):** exactly the seven named
  files moved (winbox-wt-prep.sh, its three .pre-* versions,
  winbox-publish-build-seed.sh, winbox-wait-load.sh,
  winbox-runtime-assets.local.json). Everything else in
  /home/ubuntu/scripts/ stays, including laptop-ssh (a named live
  exception). One transitional symlink left at the old
  preparation-script path for one release cycle. Proof: the scratch
  tree was prepared through the old path (via the symlink) and
  through the new path; both print pins OK and "prepared ...
  (linked=23 kept-existing=0; copied 0 payload bytes)".
- **M5 (archives and evidence → backups/ and evidence/):** nine
  winbox-runtime-assets-* directories to backups/; fifteen snapshot
  directories (winbox-evidence-consolidated-20261101, winbox-font*,
  winbox-wine64-payloads-*, winbox-wine8-backports-*) to evidence/.
  Nothing winbox-* remains in /home/ubuntu/backups/; the rest of
  that directory (other tenants' and services' backups) was not
  touched. Two items not named in the split were left in place and
  noted: wine32-stage-20261020 (a dated snapshot inside backups/,
  not the M8 staging dir) and wine-runtime-20261013. Live path
  updates in the integration tree (this commit): the standards docs
  (fresh-working-copy, closeout, combining, checks), workflow.md,
  game-compatibility.md, the five stage-*.sh snapshot paths plus
  stage-font-alias64.py and stage-wine64-payloads.sh (now writing
  under evidence/), and check_runtime_completeness.py (archive
  comments, the archive lookup code, and its message now read under
  backups/). The one-off probe scripts under web/tests/ and dated
  records keep their old paths as written. Proof: bootstrap.py
  --local from the new home restores and verifies all 13 runtime
  assets (rc=0, "Runtime assets verified (13 files)"), and --check
  reports 0 failures / 1 skip (the prefix64 built-artifact skip, as
  before); the 20261104 archive sha256 still matches the release
  pin 3c80c8a1...
- **M6 (compiler cache → cache/):** moved by plain mv (the empty
  placeholder cache/ directory was removed first so the move did
  not nest). Contents intact: 48M, and ccache -s at the new home
  shows the moved cache's accumulated stats (2,452 calls, 1,208
  hits / 1,234 misses, 2.0G cap, compression on). The preparation
  script's cache section updated (CACHE_DIR line 136 and its comment
  line 134; script sha256 fc24cf5f...). Proof: the moved cache
  stores and serves real project objects at the new home — a
  source file compiled twice through ccache with the native
  makefile flags missed once, then hit (1,208 → 1,209 hits) — and
  both preps report the cache at the new home. Deviation from the
  map's literal two-build form, recorded: a second build of an
  unchanged tree compiles nothing, so it reports no cache activity;
  and the end-to-end "builds report cache hits" form cannot run on
  this tip at all, because the buildcache lane's makefile wiring is
  not composed yet (it rides with batch 9) — the scratch build
  compiled without ccache, and ccache -s was unchanged across it.
  The full two-build proof belongs to the batch-9 gate, after that
  branch composes.
- **M8 (payload staging → staging/wine32-stage):** moved (599M);
  old path gone. The live defaults in tools/rootfs32/closure32.sh,
  build-img386.sh, stage-fonts.py, and tools/build-prefix32.sh now
  point at the new home (this commit). Proof: DEFERRED, as the map
  allows — no payload build is scheduled in this window; the
  repointed defaults are the enabling step, and the owning work's
  next payload build runs against the new path.

## Register re-verification (after the moves)

The register was re-read from disk on the composed tree after all
moves: 162 FD entries (numbers 1..165 except gaps 46/56/57, no
duplicate entry headers), 124 distinct closures CL-01..CL-124 with
no gaps, FD-69's "(provisional number)" parenthetical present. The
register file itself is unchanged from the composed tip — none of
the moves touched it. The composition tip before this commit is fdc0a5d1a; this commit adds only this record addendum and the path updates listed above.
