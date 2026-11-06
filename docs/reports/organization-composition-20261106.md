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
