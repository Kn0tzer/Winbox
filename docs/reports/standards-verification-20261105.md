# Standards verification record — 2026-11-05

Lane: `lane-standards`, tree `/home/ubuntu/wt-standards`, based on
integration tip `48d27952a39c629f67c979b3d4b97ecc9c1d364f`
("Batch-8 register audit…"). Every standard in `docs/standards/`
was followed literally, once, in this tree; wherever reality
disagreed with the draft words, the words were fixed. Raw logs for
every run are preserved under `test-results/standards/` in this
tree and, after closeout, in the consolidated evidence store.

## Fresh working copy — verified

`/home/ubuntu/scripts/winbox-wt-prep.sh /home/ubuntu/wt-standards`
completed with the standard's success line: store pins verified
(13 files), store integrity verified, **23 links, 0 payload bytes
copied**, compiler cache prepared, verified build seed for
`48d27952a` installed. Log: `test-results/standards/prep.log`.
Prep script sha256 at verification: `7731a66de5d2…` (the script
lives only at the installed path; no repo copy exists — the
standard names the installed path as canonical).

## Checks — verified by hand

- JS unit suite, pre-build: **352 passed / 1 failed**, the one
  failure being `provenance-artifacts` (it audits `dist/`, which
  does not exist until this tree is built). In the same tree after
  preparation, `runtime-completeness` passes — the two-failure
  picture the checking queue shows for a tip is the
  *bare-checkout* picture (no prep, no build), and the standard
  now says exactly that. Log: `unit-predist.log`.
- Legacy suite: **116 tests, 115 passed, 0 failed, 1 skipped**,
  exit 0 — the standing figures in the standard are exact.
  Log: `legacy.log`.
- Citation audit: **352 rows, 0 drift** (232 MATCH /
  14 MATCH-INHERITED / 99 UNCHECKABLE / 7 EXEMPT), exit 0.
  Log: `citations.log`.
- run-native: **69 PASS, exit 0**, no aborts. Log: `run-native.log`.
- `tools/browser/bootstrap.py --check` in the prepared tree:
  **0 failures, 5 skips**, exit 0. Log: `bootstrap-check.log`.
- Post-build, end to end: `node tools/run-tests.mjs` →
  **unit 353 passed / 0 failed** (`provenance-artifacts` green
  once `dist/` exists, as §0 of the checks standard states),
  legacy as above, **`ALL TESTS PASSED`**, exit 0.
  Log: `run-tests.log`.
- Browser guest suite, both census boots run literally:
  `boot32-vkboot` **passed** (exit 0) with the census exact —
  **0 `BW64-FAIL`, exactly 5 `BW64-INFO`** in
  `test-results/suite-boot32-vkboot/console.log`.
  `boot64-smoke` first **failed without testing anything**:
  901,769 ms and `timed out waiting for a probe slot` (zero guest
  output) behind six queued sibling runs — the load artifact the
  checks standard now names, adjudicated by re-run. The re-run
  **passed** (exit 0) with the census exact — **0 `BW64-FAIL`,
  exactly 5 `BW64-INFO`** in
  `test-results/suite-boot64-smoke/console.log`.
  Logs: `browser-boot32.log`, `browser-boot64.log`,
  `browser-boot64-rerun.log`.

## Building — verified

- The first literal build attempt (log: `build.log`) queued behind
  a sibling lane's build, moved to the script's probe-drain wait,
  and then died with `FATAL: build lock busy or invalid` when a
  second sibling build took the lock first. The standard's
  sanctioned answer — relaunch with `BW64_BUILD_WAIT_SECONDS`
  raised and `BW64_BUILD_OWNER` set, the settings lanes already
  use — queued correctly, took the lock when it freed, and
  completed (log: `build2.log`).
- Verdict: the literal line **`Browser export verified`** —
  34 .mjs syntax files; dependency archive `c4dbe35986a5`
  verified — followed by `BUILD COMPLETE`.
- Identity, from `dist/runtime/BUILD_INFO.json`: `git_sha`
  `48d27952a39c629f67c979b3d4b97ecc9c1d364f` = this tree's HEAD;
  wasm sha256 `9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241`,
  5,688,949 bytes; `builder` recorded as `lane-standards` (the
  owner label); `git_dirty_files` 15 — the untracked
  entries the tree carried at build time (the prep link
  directories and this lane's then-uncommitted documents); the
  build's own outputs are ignored files and modify no tracked
  path, which `git status` at commit time confirmed (15 untracked
  lines, 0 modified). Exactly the "tree as the build found it"
  the standard now describes; a gate build on the clean composed
  tree must show 0. The draft standard had named the identity file
  `dist/BUILD_INFO.json`; reality puts it in `dist/runtime/` —
  corrected (correction 5 below).
- Native builds: **all five targets exit 0** (`make release`,
  `make test`, `make x64-test`, `make x64`, `make x64-cache`;
  logs `native-build-*.log`). Serial suites: main **689 tests,
  0 FAILED** (287 s, exit 0); x64 **113 tests, 0 FAILED**
  (29 s, exit 0). Logs: `native-main.log`, `native-x64.log`.
  The draft standard's standing totals (686 / 111) had drifted
  behind tests added since; updated (correction 7 below).

## Combining — mechanics verified on a scratch composition

A real composition was deliberately not performed: the batch-8
merge lane owns the integration tree and was running. The
standard's mechanics were followed on a throwaway tree
(`wt-stdcompose`, branch `scratch-standards-compose`, same base):

- prep: 23 links, as the fresh-copy standard describes;
- cherry-pick of a real pool commit (`73e0232cd`, the
  precompiled-header lane's first commit) applied cleanly →
  composed commit `72f191e8a`, diff accounted: 2 files,
  +66 / −2;
- register audit on the composed register: **0 duplicate
  numbers** (the same audit on the live integration register:
  162 FD headings in §A, 82 CL headings in §C, 79 rows in §B,
  0 duplicates);
- citation audit on the composed tree: 352 rows, 0 drift;
- unit suite on the composed tree: **352 passed / 1 failed**, the
  failure again only `provenance-artifacts` (no build in that
  tree). Log preserved as `test-results/standards/scratch-unit.log`.
- the scratch tree and branch were then discarded following the
  closeout standard — which is where correction 4 below was found.

## Corrections reality forced into the standards

1. **Checks §0** — the draft's pre-build unit picture was wrong.
   Reality: in a prepared tree exactly one unit test fails before
   a build (`provenance-artifacts`); the second failure
   (`runtime-completeness`) belongs to unprepared checkouts, which
   is what the queue runs. Rewritten from the two logs.
2. **Building** — the draft treated the 600 s lock wait as the
   whole story. Reality adds the sanctioned
   `BW64_BUILD_WAIT_SECONDS` / `BW64_BUILD_OWNER` settings and
   the probe-drain wait after the lock. Both observed live, both
   now written.
3. **Combining** — gate archives are date-named
   (`winbox-runtime-assets-20261104` is the canonical gate
   archive); lane-suffixed archives are lane artifacts. The
   standard now forbids using a lane archive as the gate source.
4. **Closeout** — `git worktree remove` refuses any prepared tree:
   the payload link directories are untracked. The strip step now
   removes those link directories (the store is untouched), after
   which removal succeeds without `--force` — proven on the
   scratch tree above (status clean, removal clean, branch
   deleted).
5. **Building** — the build identity file is
   `dist/runtime/BUILD_INFO.json`, not `dist/BUILD_INFO.json`, and
   its dirty count describes the tree as found. Corrected from
   the verified build's own stamp.
6. **Checks §6** — three facts the draft missed, all observed in
   the literal runs: the light probe kind has 2 slots, not a
   single box-wide probe; the slot wait itself times out
   (900 s), producing a failure that tested nothing and must be
   adjudicated as a load artifact, never loosened away; and the
   boot census is counted in the per-test console
   (`test-results/suite-<test>/console.log`), not the runner's
   output. All three now written.
7. **Checks §4** — standing native totals corrected to the
   measured 689 (main) and 113 (x64); the verdict rule
   (0 FAILED) was and remains the standard.

## Defects filed during verification

None. No literal run surfaced a product defect; every disagreement
was documentation against reality and was fixed in the standards,
as listed above. No standard is recorded as blocked.
