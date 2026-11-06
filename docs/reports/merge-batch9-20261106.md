# Merge record: batch 9

Base: integ `fb5a39e15` (organization composition; batch 8 live and pushed).
Composed tip (git control): `71b1336ba` (54 commits: 57 own cherry-picks
minus 3 skipped strip-only commits).
Composed tip (Jujutsu trial): tree hash `36d44ae330a2073c58646a35150cab76c87d02c7`,
IDENTICAL to the git-control tree hash. See "Jujutsu trial" below.

Pool, in composition order (19 lanes; tips from
test-results/merge-batch9-20261106/branch-commits.txt):
lane-fd115, lane-machine-definition, lane-dll-census, lane-fd167,
lane-fd165, lane-perf2, lane-fd71, lane-sharedbuild, lane-audit-l1l2,
lane-disk-cleanup, lane-server-inventory, lane-fd168, lane-fd169,
lane-fd170, lane-fd171, lane-incremental-build, lane-pch,
lane-buildcache, lane-gamepad.

Method (git control): cherry-pick each lane's own commits in pool order
with /tmp/pick-lane.sh (v4). Commits touching the strip set
(docs/defect-register.md, tools/conformance/dll_export_baseline.json,
tools/browser/runtime-assets.json,
docs/known-gaps/runtime-artifact-manifest.md, backport binaries under
tools/rootfs64/wine8-backports-x86_64/*.dll|*.so and
tools/rootfs32/wine10-*-i386/*.dll) were picked with -n and those files
restored from HEAD; commits touching ONLY strip files were skipped
(3: fd167's pin commit 81b45ae65, fd167's baseline commit c8bf5ca89,
fd169's baseline commit 8a531b200 — settled in the audit). Conflicts
only in strip files resolved from HEAD. Log:
test-results/merge-batch9-20261106/git-control.log.

Hand resolutions (all union-type; substance disjoint):
- sharedbuild: docs/WINBOX-STATUS.md conflicted with the docs-library
  rewrite; resolved by taking HEAD's status doc and carrying the lane's
  substantive "New worktree" text into docs/workflow.md line 11 with
  paths updated to the new homes.
- fd168: build-wine8-backports.sh (PATCHES fd167+fd168; DLLS + msvcp140
  setupapi), check_runtime_completeness.py (lane's win32u.so pin kept,
  fd167's msvcp140/setupapi pins kept), README (lane's six shared-DLL
  rows + fd167's two rows).
- fd169: build-wine32-base.sh (FD-167 + FD-169 substitution blocks),
  regen-wine32-zip.sh (FD-167 args + FD-169 pins, both dicts, both
  checks, combined print), build-wine8-backports.sh (PATCHES + fd169;
  DLLS + 9 CRT names), stage-wine8-backports.sh (DLLS + 9 CRT names),
  check_runtime_completeness.py (lane's six shared hashes + fd167's two
  pins + nine CRT pins; FD-167 and FD-169 32-bit dicts + test fns both
  kept), README (lane's six rows + fd167's two + nine CRT rows).
  guest-fixtures.mjs: fd167 four + fd169 two entries.
- fd170: build-wine64-zip.sh (both loops gain bluetoothapis/winusb/
  netutils; FD-169 CRT loop kept), build-wine8-backports.sh (PATCHES +
  fd170; DLLS + three), stage-wine8-backports.sh (DLLS + three),
  check_runtime_completeness.py (HEAD pins kept; FD-170 comment + three
  pins appended), README (fd168 patch row + fd170 patch row; lane's six
  rows + HEAD's msvcp140/setupapi + nine CRT + three new rows).
- fd171: build-wine32-base.sh (FD-167 + FD-171 blocks; FD-169 block
  untouched), build-wine64-zip.sh (both loops gain the 24 new DLLs;
  FD-169 CRT loop kept), build-wine8-backports.sh (PATCHES + fd171;
  DLLS + 24 names), stage-wine8-backports.sh (DLLS + 24 names),
  check_runtime_completeness.py (per-DLL newest-touching-lane pins:
  lane wins except win32u.so, where HEAD's fd169-era hash is newer
  than the lane's pre-fd168 value; setupapi pinned once, in the FD-171
  block; FD-167 + FD-169 + FD-171 32-bit dicts + test fns all kept),
  README (lane rows win where present; HEAD rows kept otherwise;
  lane-only rows appended). guest-fixtures.mjs auto-merged.
- incbuild: tools/winbox-wt-prep.sh add/add — took the lane's version
  (all three sections: store + compiler cache + build seed) with the
  four organization-move path updates; the repo copy is now
  byte-identical to the installed
  /home/ubuntu/winbox-project/scripts/winbox-wt-prep.sh (verified by
  cmp). This reconciles the three compiler lanes' sections in the repo
  copy deliberately, as the brief required.
- gamepad: source/kernel/syscall64.cpp — insertion-point collision
  (HEAD's FD-165 xattr helpers + lane's FD-172 statfs helper); kept
  both blocks.
- pch, buildcache makefiles: auto-merged with each other (PCH's
  precompile blocks + buildcache's ccache blocks coexist).

Pins convention (provisional until the rebuild re-measures them): for
each shared DLL, the hash from the branch whose patch most recently
touched that DLL's source; where two lanes agree, their value. The
rebuilt DLLs are committed by the rebuild; the completeness checker
and the census baseline are regenerated from the rebuilt tree.

## Jujutsu trial (verdict: KEEP GIT)

Trial: compose batch 9 with Jujutsu 0.47.1 in the composing seat
alongside the git method (wt-jj-batch9, colocated on the integration
tree; lanes kept plain git). Method: `jj rebase -b <lane> -d <tip>`
per lane in pool order, `jj restore` for strip files per change
(/tmp/jj-strip-lane.sh, saved as
test-results/merge-batch9-20261106/jj-strip-lane.sh), conflicts
resolved by copying the already-worked-out resolution from the
matching git-control commit (the trial tests the tool mechanics, not
the resolution content — re-deriving the unions would test nothing new).
Log: test-results/merge-batch9-20261106/jj-trial.log.

Result: CORRECT — the jj tip's tree hash is
36d44ae330a2073c58646a35150cab76c87d02c7, identical to git-control's,
with the same 54 changes (the 3 strip-only commits became abandoned
empty changes in jj, exactly as git skipped them). Zero conflicts
remain; working copy clean (empty change on top of the gamepad tip).

Effort, counted:
- git: 57 picks across 19 script invocations; 10 real conflicted picks
  (9 hand-resolved, 1 strip-only auto-resolved; 2 further log lines
  were artifacts — an unmerged-files sequencing error and one bad hash
  the coordinator typed — not conflicts).
- jj: 18 rebase commands; the census rebase pulled all 22 stacked
  commits at once (auto-follow); 16 "new conflicts" events, most of
  them transient strip-ripple or inherited conflicts that self-resolved
  when the root was fixed (e.g. resolving fd170's two real conflicts
  cleared its three timeout/tip descendants untouched; the census strip
  rippled into 10 descendant commits and the strip restores cleared
  them); 24 strip restores; 3 abandoned empty changes.
- Root resolutions needing content: 8 in both methods, the SAME eight
  (sharedbuild, fd168, fd169 main + fixture, fd170 fixture + main,
  fd171 main, incbuild prep, gamepad syscall64) — the dominant cost in
  both methods is working out the union content, which is
  method-independent.

jj's wins (real, but modest): no cherry-pick --continue/abort state
machine — a conflict resolves by editing the file in place and the
change updates; descendants auto-follow a rebase; `jj restore` settles
a strip file (and its conflict) in one command; inherited conflicts
self-heal when the parent is fixed.

jj's costs (real): `jj rebase -b` on a lane stacked on an
already-composed branch pulls the already-composed stack in too
(fd165 pulled lane-fd162-164's 4 commits, already in batch 8 — fixed by
abandoning those 4 changes, which rebased fd165's own 4 cleanly onto
the tip; the jj-native form is `jj rebase -s <oldest own commit>`);
strip operations ripple through descendants, producing noisy transient
conflicts; the colocated git bookmarks move when jj rebases (the lane
bookmarks now point at the jj-rebased commits, not the original lane
tips — the originals survive in the object store and are recorded in
branch-commits.txt).

Verdict: jj does NOT clearly win. Correctness is identical, effort is
roughly at parity with different failure modes, and the dominant cost
(the resolution content) is the same either way. Per the trial rule
(keep jj only if it clearly wins), the composing seat STAYS GIT. jj
remains available as a tool (its log/diff/restore are useful for
inspection), and the trial trees are kept for reference.

## Rebuild (verified from disk)

Rebuild commit `579b0b0b6` (parent `71b1336ba`, detached HEAD in
wt-merge-batch9; 94 files, 47,123 insertions). The backport DLLs were
rebuilt from the composed sources, wine64.zip + wine32.zip re-cut, the
runtime archive re-cut as `runtime-assets-20261106`
(browser-runtime-20261106.tar, 491,827,200 B, sha256
`ea90231ffcde0076406d4fa6d665cbbb795c41bed51d7a930c644da4aa8be037`),
pins regenerated in tools/browser/runtime-assets.json (wine64.zip
195,094,569 B sha256 `4bdd429ccef59c1bfa745f590734454e0f3835a58d948bd224fbc67d86574467`;
wine32.zip 231,609,651 B sha256
`f100d5a1606e3d432958327e486423837467b7bfa499f56db735530c56f95333`),
manifest + census baseline regenerated. Census: 1,171 DLL tables,
206,103 export slots, 161,677 named, 33,072 stub slots (31,838 named
stubs) — exactly the expected 31,838: 33,756 -> 31,838 (-1,918), with
per-lane attribution matching every claim exactly (fd167 = 30, fd168 =
14, fd169 = 1,634, fd170 = 37, fd171 = 203), 0 newly-appearing stubs.
Side effects explained: forwarders +57 (cfgmgr32 CM_* became
forwarders to setupapi +46; kernel32 +6; vcomp90 +2; bthprops.cpl +3);
named exports +1 (x64 msvcrt.dll gained one real export from the
fd169 patch). Census --check against the committed baseline: "census
matches the baseline exactly", exit 0.

Patch conflicts: none materialized. fd167 + fd171 apply cleanly in
sequence on both Wine 8 and Wine 10 (setupapi hunks disjoint);
propsys.dll + mf.dll carry both lanes' functions (VariantCompare +
PropVariantGetStringElem; MFCreateDeviceSource +
MFTranscodeGetAudioOutputAvailableTypes) via the new combined build
tools/rootfs32/build-wine10-fd167-fd171.sh. Composition gap the rebuild
closed: the old shipped wine64.zip carried the Debian msvcp140.dll,
not the fd167 backport the pins pointed at (same for the fd169 CRT
DLLs); the new zips carry the backports.

Findings carried forward: the 64-bit build needs the fd168 sysroot
plus LDFLAGS=-Wl,-rpath-link,<sysroot>/usr/lib/x86_64-linux-gnu
(recorded in build-wine8-backports.sh's header); build-wine64-zip.sh
cannot run on this host (docker image boxedwine64/wine64-debian:bookworm
absent) — wine64.zip was rebuilt via stage-wine8-backports.sh's merge
path; PE DLLs are not byte-reproducible (mingw wall-clock timestamps);
the fd167/fd169/fd171 32-bit completeness tests were defined but never
registered in __main__ — now registered (all four 32-bit tests pass).

Store refresh (2026-11-06, batch-9 gate): the first gate run found
winbox-wt-prep.sh exiting 1 (PIN MISMATCH) — the shared store still held
the 20261104 wine zips while the tree pins the 20261106 rebuilds. Per
the store MANIFEST's own refresh procedure, wine64.zip + wine32.zip
were replaced in the store from the retained runtime-assets-20261106
archive (hashes verified against the tree pins; the other four zips
are byte-identical to the 20261104 versions), SHA256SUMS regenerated
(19,193 entries, integrity OK), MANIFEST.json updated (pin_source ->
20261106, total_bytes 1,988,083,462, refresh recorded). The seven
finished trees that symlink the old zips (wt-buildcache, wt-codeorg,
wt-codeorg-decisions, wt-docslib, wt-incbuild, wt-pch, wt-fd170) were
verified idle at their lane tips; their work is committed and the
20261104 zips remain in the retained archive. Prep now exits 0 on the
merge tree (pins OK, 13 files). This unblocks the M6 two-build proof
(a fresh tree on the new pins can now prep cleanly).

Completeness checker (re-run by the coordinator after the commit):
0 failures, 1 skip, exit 0 — all 24 checks PASS, including the new
combined fd167+fd171 test, the retained-archive (FD-73) test, and the
manifest test. The single skip is "prefix64 built artifact:
build-prefix64.sh output not present", unrelated to batch 9. Disk 31G
free after the rebuild lane stripped ~3.5 GB of its scratch.

## Audit (verified from disk)

Audit commit `7567aa2c8` (parent `579b0b0b6`, detached HEAD in
wt-merge-batch9; register only, 1,814 insertions). The 19 lanes'
register additions were merged in pool order with `git apply --3way`
per lane (own-base diffs; disk-cleanup and server-inventory made no
register changes; fd71, fd169, fd171 wrote no Section B row). Three
of the four diffs first generated used the wrong base (4006ddb58
instead of the lanes' true base 48d27952a) — caught because the
disk-cleanup and server-inventory diffs came out byte-identical and
full of batch-8 content, and regenerated from the true bases (the
lane bookmarks moved by the jj trial, so bases were taken from each
lane tip's own first commit's parent, not from the bookmarks).

Settlement applied (test-results/merge-batch9-20261106/
collision-settlement.md): 15 fresh FD numbers FD-175..FD-189 and 7
fresh CL numbers CL-127..CL-133, first-composed keeps the number,
text preserved, Batch-9 settled-notes added to the 10 affected
Section B rows. Keepers: FD-165 xattr write-side (closed CL-129),
FD-166 no-progress stall (closed CL-128), FD-167..171 census families,
FD-172 _Throw_Cpp_error, FD-173 seeds (closed CL-126), FD-174
prep-symlink dirt, CL-125 shared-build store, CL-126 build seeds.
Two corrections to the earlier ledger: lane-fd167 claims no closure
numbers — FD-167 stays open (39 of 40 pairs meet a closing term, the
40th refiled as FD-172), and lane-fd170's "FD-167's discharge"
references are corrected in both the FD-170 entry and the fd170
closure; the audit lane's FD-166/167/168 filings (proc-address lookup,
32-bit benchmark trap, 64-bit benchmark trap) were missed by the ledger
and are losers (FD-179/180/181). The gamepad statfs filing had no
Section A entry on its lane — the audit created it as FD-187 from the
lane's own closure/row text. The buildcache CL-132 closure had landed
in the Appendix (a 3-way merge misplacement) — moved into Section C
between the incbuild CL-126 and gamepad CL-133 closures. The pch
entries' hyphen headers were normalized to the register's em dash.

Register verification (script /tmp/audit-verify.py, re-run after the
commit): AUDIT CLEAN — 186 FD entries (1..189 except the known gaps
46/56/57), CL-01..CL-133 each exactly once, no duplicates, no missing,
no extra, 0 CL headers in the Appendix, all 27 settled-entry
spot-checks pass, no stale provisional references, keeper FD-165
xattr entry intact.

## Gate — STOPPED at the browser build: the composed tree does not compile (gate run 2026-11-06, tip 7567aa2c8)

Verdict: the gate does NOT pass. The browser build fails on a compile
error in the composed tree itself (below), so no dist exists and every
dist-dependent item (browser suite, boot census, fd167/fd170/fd171
fixtures + RED, D3D11/D3D12 conformance, buildcache two-build proof,
JS unit post-build re-run) is BLOCKED, not failed. Nothing was fixed,
edited, or worked around in the tree: `git status` is clean at tip
`7567aa2c8f8132d63a9386490279990c92aa8e30`. Nothing was deployed or
pushed. All gate logs:
`wt-merge-batch9/test-results/merge-batch9-20261106/`.

### The blocker (verified three ways)

- Browser build (`tools/bw64-build.sh`, `CCACHE_DIR=/home/ubuntu/winbox-project/cache/ccache`,
  lock acquired, "Runtime assets verified (13 files)"): emcc fails on
  `source/kernel/syscall64.cpp` — "function definition is not allowed
  here" at lines 3475, 3485, 3545, ... (20 errors), make Error 2, no
  BUILD COMPLETE, no wasm, no BUILD_INFO. Log: `browser-build.log`.
- Cause: the file carries a duplicate definition header. Line 3468
  opens `static U64 sys_statfs64_common(CPU64* cpu, U64 bufPtr) {`;
  lines 3469-3483 then define the FD-172 helper
  `bw64StatfsMagicForPath64` inside it; line 3485 opens the real
  definition `static U64 sys_statfs64_common(CPU64* cpu, U64 bufPtr,
  U32 fType = X64_TMPFS_MAGIC) {` — still inside the first header's
  braces, so every following definition nests illegally. The region
  sits inside `#ifdef BOXEDWINE_GUEST_X64` (opened line 85, closed
  line 4741), which is why only guest-x64 builds fail.
- Provenance: the gamepad pick `6b6186d93` conflicted in
  `syscall64.cpp` (`git-control.log` lines 461-472) and the hand
  resolution kept the old header line together with the lane's new
  helper + new header. The lane itself is clean: at its own code
  commit `6b6186d93` the helper stands at line 3323 and the single
  definition at 3333, with no duplicate header. The jj trial carries
  the same broken text (identical tree hash `36d44ae3...`), so both
  composition methods produced it.
- Cross-checks: the native x64-test build (defines
  `BOXEDWINE_GUEST_X64=1`) fails identically — g++ "a
  function-definition is not allowed here before '{' token" at
  3475/3485/3545, make Error 2, no binary (`native-x64-build.log`,
  32 error lines). The native main build (no `GUEST_X64`) compiles
  the region out and succeeds — which is how the main suite below
  could run.
- Diagnosis for the fix-forward (NOT applied by this gate): deleting
  the stray header at line 3468 restores the lane's own arrangement
  (helper, then the single 3-arg definition). That is the merge
  lane's fix to make and re-verify with a fresh browser build; this
  gate made no tree edits.

### Prep (failed on stale store pins; tree prepared in substance)

- `winbox-wt-prep.sh` exits 1: PIN MISMATCH on
  `web/runtime/wine64.zip` and `web/runtime/wine32.zip`
  ('missing/size'). The shared store still holds the 20261104 wine
  zips (wine64 sha `be4d4956...`, 209,637,447 B); the tree's pins are
  the rebuilt 20261106 zips (wine64 `4bdd429c...`, 195,094,569 B;
  wine32 `f100d5a1...`, 231,609,651 B). The store was not updated by
  the rebuild. The gate proceeded because the tree is prepared in
  substance: every other prep link is in place (node_modules,
  web/vendor, dxvk/vkd3d build outputs, rootfs64 payloads, slang,
  zig, disasm — verified link by link), the tree's `web/runtime`
  zips are real files matching the new pins, and `bootstrap.py
  --check` verifies all 13 runtime assets (`bootstrap-check.log`).
  The store must NOT be updated in place: seven other trees
  (wt-buildcache, wt-codeorg, wt-codeorg-decisions, wt-docslib,
  wt-incbuild, wt-pch, wt-fd170) symlink the store's old wine zips
  and would silently change runtime. The store needs a
  generation/versioned-entry mechanism before the next tree on new
  pins can prep cleanly. Log: `prep.log`.
- Bootstrap: archive `runtime-assets-20261106` re-verified (sha256
  `ea90231f...`; `browser-runtime-20261106.tar`, 491,827,200 B,
  matches the rebuild record). `bootstrap.py --check`: "Runtime
  assets verified (13 files)". Pre-build completeness: 0 failures,
  1 skip (standing prefix64 skip). Logs: `bootstrap-check.log`,
  `completeness-prebuild.log`.

### Passed on this tip

- Citation audit: 352 rows, 0 drift (232 MATCH / 14
  MATCH-INHERITED / 99 UNCHECKABLE / 7 EXEMPT), exit 0. Log:
  `citations.log`.
- Silent-failure baseline: unchanged — returned=16, diffs=0;
  `syscall64.cpp` {0,7,134} exactly at allowance;
  `package-archive.mjs` and `shader.mjs` NOT-RETURNED (standing).
  Log: `silent-failure.log`.
- JS legacy suite (`node --test web/tests/*.test.mjs`): 116 tests,
  115 pass, 0 fail, 1 skipped — exactly standing. Log:
  `js-legacy.log`.
- run-native: 71 PASS, exit 0 — 69 standing + 2 net new (FD-19
  KHR-spelling guard; 4 GiB device-local heap check). Log:
  `run-native.log`.
- Native main suite, serial (`Build/Test/boxedwine 0 0 1`): 692
  tests, 0 FAILED in 253 s — 689 standing + 3 net new, all passing
  (FD-172 32-bit statfs magics; FD-165 xattr set/remove forms;
  FsZip declaration capture). Log: `native-main.log` (build:
  `native-main-build.log`).
- JS unit suite: 353 passed, 1 failed — the single failure is
  `provenance-artifacts`, the documented pre-build-only failure (no
  dist exists); total is 354 tests, one net new vs batch 8. The
  post-build re-run is BLOCKED (no dist). Log: `js-unit.log`.
- Payload-staging proof (M8): `tools/rootfs32/closure32.sh` ran
  against the new staging home
  `/home/ubuntu/winbox-project/staging/wine32-stage` — staged 30
  files (11M) under `rootfs32/` and produced `glibc-rootfs32.zip`
  (1.8M) there; the 27-item BFS missing list names the
  dlopen/transitive tails, exactly the shape recorded in
  `docs/reports/sysroot32-regeneration-20261028.md` ("stages 30
  files"). PASS. Log: `staging-m8.log`.

### Dark (recorded, not passed)

- JIT block-invalidation gate (FD-176): DARK on this tip, verified
  firsthand. `scripts/build-jit-wasm.sh` passes its early phases
  (jit64 tests, generated emitters byte-identical) then FAILs at the
  section-14.3 interp invalidate-test build:
  `inv_build_interp.log` shows undefined references to
  `bwCurrentThreadRegistryLookup()`,
  `FsOpenNode::noteMappedPagesReleased(unsigned int)`, and
  `KSystem::guestCpuCount()` — the FD-176 link failure, unchanged.
  Logs: `jit-dark.log`, `jit-dark-inv-build-interp.log` (preserved
  copy of the link log).

### Blocked (no dist — not failures)

- Browser build + wasm hash / BUILD_INFO: no dist produced (see
  blocker).
- Browser suite (32 tests), boot census (both widths),
  fd167-contracts both widths + RED run, D3D11/D3D12 conformance in
  a store-linked tree, fd170-contracts64, fd171-contracts both
  widths, buildcache two-build proof (M6): all BLOCKED on the
  browser build. None was attempted against a substitute dist.
- Native x64 suite: build fails (see blocker); the 113-test serial
  run was not attempted. Its FD-172 64-bit statfs test is among the
  unrun.

### Follow-ups for the coordinator

1. Fix-forward the `syscall64.cpp` duplicate header (line 3468) on
   the merge lane, then re-run the full gate from the browser build
   onward; the items listed as passed above do not need re-running
   except the JS unit post-build re-run.
2. Store generation: the shared store cannot serve the 20261106 pins
   (see prep note); decide the versioned-store mechanism before the
   next tree preps on new pins.
3. Nothing was deployed or pushed; the integration tree and defect
   register were not touched except this gate section.


## Gate re-run — STOPPED at the guest fixtures: two deterministic 64-bit fixture crashes + the 32-bit boot wedged (gate re-run 2026-11-06, tip 5b2ea224d)

Verdict: the gate does NOT pass. The fix-forward commit (5b2ea224d,
deleting the stray duplicate `sys_statfs64_common` header) verified —
the composed tree compiles and the browser build completes — and the
native x64 suite, the JS unit suite, and the FD-170 whole-DLL fixture
all pass on this tip. But the guest-fixture gate stops here: the
FD-167 64-bit fixture crashes with a guest access violation and the
FD-171 64-bit fixture crashes with a guest null-pointer write, both
reproduced byte-identically on re-run; and the 32-bit guest boot wedges
before the desktop is ready in four attempts across three tests, on a
quiet box. Per the gate rule the run STOPPED at the failing proofs
rather than working around them: the full browser suite, the boot
census, the D3D11/D3D12 conformance pass, the M6 two-build proof, and
the FD-167 RED run were NOT attempted. Nothing was deployed or pushed.
The integration tree and defect register were not touched except this
gate section. All re-run logs:
`wt-merge-batch9/test-results/merge-batch9-20261106/` (new files carry
`-rerun` / `-rerun1` names; the first run's files are untouched).
Per-test guest consoles: `wt-merge-batch9/test-results/suite-<test>/console.log`
(note: each focused re-run overwrote its test's console dir with the
re-run's console, so the suite-* consoles now hold the re-run evidence;
the first-run consoles for these tests are superseded by the identical
re-run signatures quoted below).

### The fix verification (passed)

- Browser build A, uncached (`BW64_CCACHE=0`,
  `CCACHE_DIR=/home/ubuntu/winbox-project/cache/ccache`):
  `[bw64-build] BUILD COMPLETE`, `BUILD-A EXIT: 0 2026-11-06T11:22:12Z`.
  The compile error is gone: 0 errors in the log. Export verified
  (`Browser export verified: 34 .mjs syntax files ...`).
  wasm sha256 `206f1652b15de9878a9b4ea00261a3266afd11d267d87485da64b6ff175ba279`,
  5,722,692 bytes; js sha256
  `94669195ae2041379071bb46b9b6ba8951b788494a01688f1f93051784692c5e`;
  BUILD_INFO `git_sha 5b2ea224d9a7b3b74bb30c48274bed82679bb3cc`,
  `git_dirty_files 0` (preserved as `BUILD_INFO-buildAB.json` in the
  gate dir; the dist itself was stripped with the other generated
  state at the end of the run). ccache stats identical before and
  after the build (3,294 cacheable calls, 1,494 hits, 1,800 misses) —
  proof the build used no cache.
- Browser build B' (cached, build dir removed first so make really
  recompiles): `[bw64-build] BUILD COMPLETE`,
  `BUILD-B-PRIME EXIT: 0 2026-11-06T11:29:10Z`, 0 errors. wasm
  byte-identical to the uncached build (same sha256 `206f1652...`).
  Cache delta for the rebuild: +81 cacheable calls, all 81 hits,
  0 new misses; +193 uncacheable calls. Verbose stats explain the
  shape: `Could not use precompiled header: 236 / 254 (92.91%)` of
  uncacheable calls — the batch-9 PCH lane's precompiled header makes
  ccache decline those compiles, so on this tip only the ~81
  non-PCH objects are cacheable at all (272 objects were rebuilt).
  Consequence for the M6 proof (not run — gate stopped): a fresh
  tree's first build on this tip can show at most ~81 cache hits,
  not the lane's 270/270; the PCH and build-cache lanes interact and
  the 270/270 standard cannot be reproduced as written.

### Passed on this tip (re-run)

- Native x64 suite, serial (`Build/X64Test/boxedwine 0 0 1`):
  `Running 116 Test tests with 1 threads`, `0 tests FAILED in 25s`,
  exit 0. Standing was 113; the 3 net new all pass (FD-172 64-bit
  statfs magics; x64 xattr set/remove forms; the x64 non-trailing
  `..` case). The FD-172 64-bit statfs test that was unrun in the
  first gate run now runs and passes. Log: `native-x64-rerun.log`
  (build: `native-x64-build-rerun.log`, exit 0, 0 errors).
- JS unit suite re-run (`node web/tests/suite/runner.mjs --unit`):
  `SUITE RESULT: 354 passed, 0 failed`, exit 0. The first run's
  single failure (`provenance-artifacts`, the documented pre-build-only
  failure) is gone now that a dist exists. Log: `js-unit-rerun.log`.
- FD-170 whole-DLL fixture (64-bit): `PASS fd170-contracts64
  (85167ms)`. Console `suite-fd170-contracts64/console.log`: all
  four groups ran (bluetoothapis GATT, bluetoothapis radio,
  winusb, netutils) and printed `fd170-64: RESULT 0`; 0 BW64-FAIL
  lines. Log: `browser-fixtures-contracts-rerun.log`.

### The stoppers (all reproduced, none worked around)

Contracts run (`node web/tests/suite/runner.mjs --browser
--filter=contracts`, 5 tests): `SUITE RESULT: 1 passed, 4 failed`,
exit 1. Log: `browser-fixtures-contracts-rerun.log`.

1. FD-167 64-bit fixture crashes — deterministic. First run:
   `FAIL fd167-contracts64 (201132ms)`, `Guest output
   /fd167-64: RESULT \d+/ not seen within 120000ms`. Console
   `suite-fd167-contracts64/console.log`: the fixture prints
   `target DLLs load ok` and `msvcp140 exports resolve ok`
   (`bad=...05ce getlast=...0661 throwc=...06e8`), then immediately
   `BW64-FAIL guest-memfault pid=48 addr=0x33f8ebef3
   rip=0x14000166d err=4 si_code=1 delivered=1 count=1` and wine
   `Unhandled page fault on read access to 000000033F8EBEF3 at
   address 000000014000166D (thread 008c), starting debugger...`;
   the fixture exits with status 3221225477 (0xC0000005). No RESULT
   line. The fault is in the fixture's own code at 0x14000166D,
   immediately after calling `std::_Xbad_function_call()` under its
   vectored handler: the handler walks the MSVC exception record's
   ThrowInfo/CatchableTypeArray/TypeDescriptor chain
   (`base + ti[3]`, `base + cta[1]`, `base + ct[1]`) and reads through
   the wild pointer 0x33F8EBEF3. Re-run (`fd167-contracts64-rerun1.log`,
   200765ms): FAIL with the byte-identical signature — same rip
   0x14000166D, same fault address 0x33F8EBEF3, same exit status.
   The FD-167 lane never completed a 64-bit fixture run (its report
   records "no verdict"), so this is the first verdict on the 64-bit
   throw helpers, and it is a crash, not a pass. Whether the corrupt
   chain comes from the backport's throw implementation or from
   exception dispatch is for the fix lane to determine; this gate
   makes no change and no claim beyond the preserved consoles.
2. FD-171 64-bit fixture crashes — deterministic. First run:
   `FAIL fd171-contracts64 (204469ms)`, same RESULT-wait failure.
   Console: `FD-171 contract fixture start`, `group propsys`, then
   `BW64-FAIL guest-memfault pid=48 addr=0x0 rip=0x14000295a err=6
   si_code=1 delivered=1 count=1` and wine `Unhandled page fault
   on write access to 0000000000000000 at address 000000014000295A`;
   exit status 3221225477; no report line printed. The fault is a
   null-pointer write at the very start of the propsys group, before
   its first check (`SafeArrayCreateVector` / `SafeArrayAccessData` /
   filling the string vector in `test_propsys`). Re-run
   (`fd171-contracts64-rerun1.log`, 200776ms): FAIL with the
   byte-identical signature — same rip 0x14000295A, same null write,
   same exit status (pid 49 instead of 48).
3. 32-bit guest boot wedged — four attempts, three tests, quiet box.
   - `fd167-contracts32`: `FAIL (360072ms)`, `browser test timed out
     after 360000ms (hung probe killed)`; re-run `FAIL (360083ms)`,
     identical.
   - `fd171-contracts32`: `FAIL (602064ms)`, `page.waitForFunction:
     Timeout 600000ms exceeded` in `bootGuest`.
   - `boot32-vkboot` (the census test itself): `FAIL (602135ms)`,
     same 600 s readiness timeout in `bootGuest`.
   All three consoles share one signature: `session: spawned
   persistent wineserver pin`, `session: spawned desktop pin`,
   `session: persistent pins spawned; Wine owns service
   initialization` — then step-ring diagnostics (ending at seq 2423)
   and a thread dump, no desktop-ready, no fixture spawn, 0
   BW64-FAIL lines. Box load during these runs was 0.04–2.4, so this
   is not the batch-8 contention shape (loads 13–25); the 32-bit
   boot wedges after service initialization starts on this tip, three
   times out of three. The 64-bit boots in the same run worked
   (fd170 passed in 85 s; both 64-bit fixtures reached their code).
   Logs: `fd167-contracts32-rerun1.log`, `boot32-vkboot-rerun1.log`,
   `browser-fixtures-contracts-rerun.log`; consoles
   `suite-fd167-contracts32/`, `suite-fd171-contracts32/`,
   `suite-boot32-vkboot/`.

### Not run (gate stopped — not failures)

- Full browser suite (44 test files), boot census (boot64-smoke not
  run; boot32 fails — see above), D3D11/D3D12 conformance, M6
  two-build proof, FD-167 RED run. The M6 fresh tree is ready for
  the re-gate: `wt-merge-batch9-m6` at tip `5b2ea224d`, prep exits 0
  (linked=23, no build seed for this tip), no build run yet, no
  generated state to strip; the D3D conformance prerequisites
  (`tools/dxvk/probe/d3dconf11.exe`, `tools/vkd3d/probe/d3dconf12.exe`,
  dxvk d3d11/dxgi, `tools/rootfs64/libvk64/libvulkan.so.1`) are
  present there. The pre-change archive for the RED run is retained
  at `/home/ubuntu/winbox-project/backups/winbox-runtime-assets-20261104/browser-runtime-20261104.tar`.

### Dark (unchanged from the first run)

- JIT block-invalidation gate (FD-176): still DARK on this tip; not
  re-run (the first run's `jit-dark.log` /
  `jit-dark-inv-build-interp.log` stand).

### Items from the first run, still standing (not re-run)

- Citation audit 352 rows 0 drift; silent-failure baseline unchanged;
  JS legacy 116 tests 115 pass 0 fail 1 skipped; run-native 71 PASS;
  native main serial 692 tests 0 FAILED; payload-staging proof M8 PASS.

### Housekeeping (this re-run)

- Generated state stripped from the merge tree at the end of the run:
  `dist/` (958M), `project/emscripten/Build` (55M),
  `project/linux/Build` (164M) removed; `git status` clean at tip
  `5b2ea224d`; disk 30G free. `web/runtime/` (the rebuilt 20261106
  zips) left in place — it predates this run and belongs to the
  rebuild, not the gate. Build lock idle; no stray probe processes.

### Follow-ups for the coordinator (re-run)

1. Two deterministic 64-bit fixture crashes need fix lanes before
   any re-gate: the FD-167 msvcp140 throw-helper exception chain
   (wild pointer 0x33F8EBEF3 read at fixture 0x14000166D) and the
   FD-171 propsys-group null write (fixture 0x14000295A). Both
   crash inside the fixtures' own code immediately after calling
   into the newly implemented DLL surface, so the defect is in the
   implementation the fixture exercises (or, for FD-171, possibly in
   the fixture's unchecked `SafeArrayCreateVector`/`SafeArrayAccessData`
   use — the fix lane should determine which).
2. The 32-bit boot wedge needs a fix lane: three of three 32-bit
   boots wedged after service initialization on a quiet box. Until
   it is fixed, no 32-bit guest proof can run at all — the
   FD-167/FD-171 32-bit fixtures, the FD-167 RED run, and the boot
   census are all blocked on it, not on their own content.
3. M6 caveat for the re-gate: the PCH lane caps the cacheable
   surface at ~81 of 272 browser-build objects (see above); the
   two-build proof should be judged on hits/misses for the cacheable
   set plus wasm byte-identity, not on 270/270.
4. Nothing was deployed or pushed; the integration tree and defect
   register were not touched except this gate section.

## Fixes composed after the re-run gate (tip 0874d1f3f)

### 32-bit boot wedge — FIXED and composed (lane/boot32-batch9)

Cause, in plain terms: batch 9 swapped 35 of the 32-bit Windows
libraries for versions built by a different compiler (mingw), and
those files were missing the 32-byte "Wine builtin DLL" marker Wine's
loader requires. Wine refused to load the core library `ntdll.dll`
(error `c000007a`), the startup and desktop programs died at launch,
and the boot timed out at 10 minutes. The 64-bit libraries were never
affected (Wine's own tool writes the marker).

Fix commit `0ffd786e1` (branch `lane/boot32-batch9`, parent
`5b2ea224d`, 47 files), cherry-picked cleanly onto the merge tree as
`0874d1f3f`. It stamps the marker onto all 35 files (new script
`tools/rootfs32/stamp-wine-builtin.sh`), re-cuts their pins, adds the
stamp step to all four lane build scripts, and adds a fail-closed
check so the base build aborts if any of the 35 lacks the marker.
`wine32.zip` re-cut (231,609,042 bytes, sha256
`cdba91dd24d626004364bce5b05dcd8b34ee7cd2da4ebcd5113492611c8cc2a9`);
manifest regenerated (only the wine32 row changed).

Proof (re-read from the lane's logs): `boot32-vkboot` PASS
(81,227 ms; was a 602 s timeout); `fd167-contracts32` PASS
(28,658 ms) — 32-bit guest fixtures run, not just the boot.

The new `wine32.zip` was restored into the merge tree's
`web/runtime/` from the lane's re-cut archive
(`/tmp/b9cmp/browser-runtime-20261106-boot32fix.tar`, 491,827,200
bytes, sha256 `7b6e82867b2494419eb1463f4d7f89d21a318c7c0148c441a0ccf031a80f1a5a`);
its sha256 matches the new pin exactly.

Completeness/bootstrap check on the merge tip: all content checks
PASS (stamped DLLs, backports, fonts, DirectX/VC++ sets). The single
failure is the FD-73 retained-archive check — expected: the retained
archive `backups/winbox-runtime-assets-20261106/browser-runtime-20261106.tar`
and the shared store still hold the old `wine32.zip`. With
`BW64_ASSETS_ARCHIVE` pointed at the re-cut tar, FD-73 PASSES and the
check reads **0 failures, 5 skips**. The store/archive refresh is
DEFERRED until all three fixes are composed (the two sibling fix lanes
still build against the old pins; refreshing the store now would
change their symlinked `wine32.zip` under them). At publish: copy the
re-cut tar over the retained archive, refresh the store's `wine32.zip`
from it, regenerate the store SHA256SUMS, update the store MANIFEST,
and re-verify FD-73 without the override.

Still open: the fd167-contracts64 and fd171-contracts64 crashes —
their fix lanes are running.

### FD-167 64-bit fixture — FIXED and composed (lane-fd167-64fix)

Cause, in plain terms: the test calls a helper that should throw a
real C++ `bad_function_call` exception. The FD-167 patch had given
`msvcp140.dll` the tables describing the exception types but never
switched them on — three start-up calls were missing from
`init_exception()`. On 64-bit only, those tables start out filled
with the placeholder `0xDEADBEEF`, so the test's exception handler
read the placeholder as an address: the fault `0x33f8ebef3` is the
DLL's base plus sign-extended `0xDEADBEEF` plus 4.

Fix commit `7281b27a` (branch `lane-fd167-64fix`, parent `5b2ea224d`,
8 files), cherry-picked onto the merge tree as `f71798346` with a
hand resolution of the pin files (see below). It adds the three
missing start-up calls (`init_bad_function_call_rtti`,
`init_system_error_cxx`, `init_bad_function_call_cxx`) to the patch
and rebuilds `msvcp140.dll` (sha256 `8525ebf0…`, 1,153,111 bytes). A
second fix in the same commit corrects one wrong number in the test
itself: it expected error code `0xE00002C7` for a missing device
interface, but Wine 8.0, Wine master, and mingw-w64 all define
`ERROR_NO_SUCH_DEVICE_INTERFACE` as `0xE0000225` — the test was
corrected and the DLL left alone.

Proof (re-read from the lane's logs): `fd167-contracts64` PASS
(144,534 ms), `SUITE RESULT: 1 passed, 0 failed`, `RESULT 0`
(15 checks), zero `BW64-FAIL` lines. RED run against the
pre-change 20261104 archive: `FAIL fd167-contracts64`, 0 passed,
1 failed — the five msvcp140 checks fail with the Wine stub
exception `code=80000100`, then the stub alias call crashes the
guest, so no `RESULT` line appears. The fixed `wine64.zip` was
restored afterwards and re-verified green.

Composition notes: the cherry-pick conflicted in
`tools/browser/runtime-assets.json` and
`docs/known-gaps/runtime-artifact-manifest.md` (both the boot32 fix
and this fix re-pinned). Resolved as a union: wine32 keeps the
boot32 pin (`cdba91dd…`, 231,609,042 B), wine64 takes this fix's pin
(`a52e6577…`, 195,094,685 B), the archive pin stays the boot32
re-cut (`7b6e8286…`) as a placeholder, and the release name returns
to the canonical `runtime-assets-20261106`. The new `wine64.zip`
was restored into the merge tree's `web/runtime/` from the lane's
re-cut archive (sha verified `a52e6577…`). The completeness checker
auto-merged correctly (msvcp140 pin `8525ebf0…`). Bootstrap check on
the merge tip: all content checks PASS; the single failure is FD-73
(the retained archive still holds the old zips — the combined
re-cut is pending the fd171 fix, same deferred state as the boot32
fix).

New filing from this lane's work, verified by the coordinator
against the census baseline and filed on the register as **FD-190**
(commit `64800e8b5`): the 32-bit `setupapi.dll` still ships
`SetupDiGetDeviceInterfaceAlias` as an unimplemented stub
(x86/setupapi.dll: 242 stubs, the alias among them; the 64-bit
table no longer lists it), and the 32-bit fixture contains no alias
checks at all — the 32-bit side was never validated. Register
re-verified after the filing: **AUDIT CLEAN** — 187 FD entries
(1..190 except gaps 46/56/57), CL-01..133 each exactly once, no
duplicates, no missing, no extra. (The lane's report said the
32-bit twin fixture was still blocked on the boot investigation —
that was stale: the boot32 fix had already landed and the 32-bit
fixture passed on it.)

Still open: the fd171-contracts64 crash — its fix lane is running.

### FD-171 64-bit fixture — FIXED and composed (fd171-contracts-fix)

Cause, in plain terms: the test program itself crashed at the start
of its first group with a write through a null pointer. It had built
a string-list value the wrong way (a SafeArray out of a list of
string pointers, which Windows refuses), and it looked up a cleanup
function (`PropVariantClear`) in the wrong DLL — propsys.dll does not
export it; it lives in combase.dll and is reached through ole32.dll.
On top of the test-program bugs there was one genuine Wine defect:
asking the graphics library to create a device context with no
surface crashed instead of returning "invalid argument", because
upstream Wine never checks for a null surface.

Fix commit `bea5e0a5c` (branch `fd171-contracts-fix`, parent
`5b2ea224d`, 8 files), cherry-picked onto the merge tree as
`47ae3368f`. The fixture was corrected (SafeArray built properly,
`PropVariantClear` resolved from the right DLL) and the Wine patch
now returns `E_INVALIDARG` from `D2D1CreateDeviceContext` on a null
surface; the rebuilt `d2d1.dll` was re-pinned.

Proof (re-read from the lane's logs): `fd171-contracts64` PASS,
`SUITE RESULT: 1 passed, 0 failed`, `RESULT 0` (49 checks), zero
`FAIL` lines — including `D2D1CreateDeviceContext(NULL)`
(hr=80070057), `PropVariantToUInt32WithDefault(empty)`,
`CM_Get_Device_ID_Size_Ex` (idlen=19), and the Sspi/networking
checks. Verification trio on the lane's final state, all green:
completeness 0 failures / 1 skip, bootstrap "Runtime assets
verified (13 files)", census "matches the baseline exactly".

Composition notes — a real trap, caught before it bit: the fd171
lane's rebuilt `wine64.zip` (sha `c8608a1a…`) was built from the
pre-fix base, so it carries the OLD `msvcp140.dll` (the fd167
fix) — restoring it would have regressed fd167-contracts64. The
composed `wine64.zip` was therefore REBUILT from the tree with
`tools/rootfs64/stage-wine8-backports.sh`, which overlays both
fixed DLLs (`msvcp140.dll` `8525ebf0…`, `d2d1.dll` `1afe8f05…`).
Rebuilt zip: 195,094,788 bytes, sha256
`431d61cb9c7689da6d587a3ed21e2829386dcbe86ca168d6ac135c8fced4d8f2`
(commit `f6072448e`, which also re-pinned it in the manifest,
runtime-assets.json, and the census baseline). The census
regeneration was proven clean: totals and DLL tables identical,
only the recorded wine32 vintage changed (the boot32 zip);
`--check` then exits 0, "census matches the baseline exactly".
The cherry-pick conflicted in the pins and manifest (third
re-pin); resolved as a union each time (wine32 keeps the boot32
pin, wine64 takes the newest lane value, archive pin held as a
placeholder until the combined re-cut below).

New filing from this lane's work, verified by the coordinator and
filed on the register as **FD-191** (commit `9f3fed228`):
`CM_Locate_DevNodeW` is an unimplemented stub returning
`CR_FAILURE` unconditionally in Wine 8.0's `setupapi/stubs.c`,
and the stub census never covered it (it appears in neither
setupapi stub list, though the name is exported by the shipped
DLL). The fixture documents the gap and works around it through
the SetupDi path. Register re-verified: **AUDIT CLEAN** — 188 FD
entries (1..191 except gaps 46/56/57), CL-01..133 each exactly
once. The lane's other follow-up — the identical D2D1 NULL crash
possibly existing in the 32-bit d2d1.dll — is NOT filed: the
32-bit fixture tests the same NULL case, so the final gate's
fd171-contracts32 run settles it directly. The
windows.networking placeholder (`DllGetActivationFactory` always
answering CLASS_E_CLASSNOTAVAILABLE) is a documented boundary of
the FD-171 implementation, not filed.

### Combined archive re-cut and store refresh (final pins)

All three fixes composed, the runtime archive was re-cut once from
the merge tree (deterministic: the 13 pinned files in manifest
order, tar mtimes normalized to epoch, symlinks dereferenced —
the first cut silently archived the three slang files as 0-byte
symlinks and was caught by the member-size check and re-cut with
`tar -h`). Combined archive:
`/tmp/b9cmp/browser-runtime-20261106-combined.tar`, 491,827,200
bytes, sha256
`d910c9662ff9066b8feeb3f0d94304224b110a8a2c5a315ddabd849d1f982ccd`;
all 13 members verified against the tree's pins. Archive pin
committed as `4d603252e`. The retained archive was refreshed to
the combined tar and the store's `wine64.zip` + `wine32.zip`
replaced from it (SHA256SUMS regenerated, 19,193 entries,
integrity OK; store MANIFEST updated). One incident in the refresh:
the FD-73 check still failed after the refresh because its glob
picks the last-sorted retained copy, and the fd171 lane's archive
(`winbox-runtime-assets-20261106-fd171/browser-runtime-20261106.tar`,
sha `cbf974c2…`) sorted after the canonical one. The superseded
lane archive was moved to the evidence store
(`evidence/winbox-runtime-assets-20261106-fd171/`), after which
FD-73 PASSES without any override: **0 failures, 5 skips**,
"Runtime assets verified (13 files)". Prep on the merge tree
exits 0 (pins OK, 13 files). Lane archives that must NOT match
the FD-73 glob in future: use a lane-distinct tar filename, as
the fd167 lane did.

Merge tip after all fixes: `4d603252e`.
## Final gate — STOPPED at fd171-contracts32: the 32-bit D2D1 NULL crash is real (final gate 2026-11-06, tip 4d603252e)

Verdict: the gate does NOT pass. The three composed fixes all
reproduce on the composed tip except one: the 32-bit side of the
FD-171 D2D1 fix does not exist. The 32-bit fd171 fixture crashes
with a null-pointer read at the exact point it calls
`D2D1CreateDeviceContext(NULL)`, deterministically, twice with a
byte-identical signature. Per the gate rule the run STOPPED there
rather than working around it: the FD-167 RED run, the boot census,
the full browser suite, the D3D11/D3D12 conformance pass, and the
M6 two-build proof were NOT attempted. Nothing was deployed or
pushed. The integration tree and defect register were not touched
except this gate section. All final-gate logs:
`wt-merge-batch9/test-results/merge-batch9-20261106/` (files carry
`-final` names; earlier runs' files are untouched). Per-test guest
consoles: `wt-merge-batch9/test-results/suite-<test>/console.log`
(note: the fd171-contracts32 re-run overwrote that test's console
dir with the re-run's console; the signature is byte-identical to
the first run's, quoted below).

### Passed on this tip (final gate)

- Browser build (`tools/bw64-build.sh`): `[bw64-build] BUILD
  COMPLETE`, `BUILD EXIT: 0 2026-11-06T14:38:32Z`, 0 failures, 1
  skip (standing prefix64 skip). wasm sha256
  `206f1652b15de9878a9b4ea00261a3266afd11d267d87485da64b6ff175ba279`,
  5,722,692 bytes; js sha256
  `94669195ae2041379071bb46b9b6ba8951b788494a01688f1f93051784692c5e`;
  BUILD_INFO `git_sha 4d603252ead12fbe242592298976800aacdad77d`,
  `git_dirty_files 0`, built 2026-11-06T14:37:02Z. The wasm is
  byte-identical to the previous tip 5b2ea224d's build (same sha256,
  same byte count, same js sha) — the three composed fixes touch
  only guest DLLs, fixtures, and pins, as designed. Log:
  `browser-build-final.log`.
- JS unit suite re-run (`node web/tests/suite/runner.mjs --unit`):
  `SUITE RESULT: 354 passed, 0 failed`, exit 0.
  Log: `js-unit-final.log`.
- Native x64 suite, serial (`Build/X64Test/boxedwine 0 0 1` from
  `project/linux`, per `docs/standards/checks.md`): `Running 116
  Test tests with 1 threads`, `0 tests FAILED in 25s`, exit 0.
  Log: `native-x64-final.log` (build:
  `native-x64-build-final.log`, exit 0). One procedural note: the
  first attempt ran from the worktree root and failed 3 tests
  (`syscall fallback guard`, `guest CPU topology consistency`,
  `futex dispatch guard`) because those guards read sources
  relative to the working directory; re-run from `project/linux`
  passes all 116. The wrong-CWD attempt is preserved as
  `native-x64-final-wrongcwd.log`; it is a harness-invocation
  artifact, not a tip failure.
- Guest contracts (`node web/tests/suite/runner.mjs --browser
  --filter=contracts`, 5 tests): `SUITE RESULT: 4 passed, 1
  failed`, exit 1. Log: `browser-fixtures-contracts-final.log`.
  - `fd167-contracts32`: PASS (26,646 ms), 0 BW64-FAIL lines —
    the 32-bit boot fix holds and the 32-bit fixture runs.
  - `fd167-contracts64`: PASS (82,893 ms), console
    `fd167-64: RESULT 0 (15 checks)`, 0 BW64-FAIL lines — the
    msvcp140 exception-table fix reproduces.
  - `fd170-contracts64`: PASS (82,476 ms), console
    `fd170-64: RESULT 0`, 0 BW64-FAIL lines.
  - `fd171-contracts64`: PASS (115,832 ms), console
    `fd171-64: RESULT 0 (49 checks)`, 0 BW64-FAIL lines,
    including `D2D1CreateDeviceContext(NULL)` returning
    `hr=80070057` (E_INVALIDARG) — the 64-bit D2D1 NULL fix
    reproduces.

### The stopper: fd171-contracts32 crashes at D2D1CreateDeviceContext(NULL)

- First run: `FAIL fd171-contracts32 (139459ms)`, `Guest output
  /fd171-32: RESULT \d+/ not seen within 120000ms`. Console
  `suite-fd171-contracts32/console.log`: the fixture boots, prints
  `FD-171 contract fixture start`, and completes six groups
  (propsys, ntdll, cfgmgr32, pdh, combase,
  kernelbase/kernel32), enters `small-dlls`, passes
  `CryptExtOpenCERW(export)`, and then crashes. The next
  statement in the fixture (`tools/guest-fixtures/fd171-contracts.c`,
  `test_small()`) is the d2d1 block:
  `D2D1CreateDeviceContext(NULL, NULL, NULL)`. The crash:
  `BW64-FAIL guest-memfault32 pid=111 tid=112 addr=0x0
  eip=0x78692441 err=0 delivered=1 count=1` and wine
  `Unhandled page fault on read access to 00000000 at address
  78692441 (thread 0148), starting debugger...`; the fixture
  process then terminates; no RESULT line is ever printed.
  Exactly 1 BW64-FAIL line in the console.
- Re-run (determinism check, same tip, no changes):
  `FAIL fd171-contracts32 (139117ms)`, exit 1 — byte-identical
  signature: same pid 111, tid 112, same fault address 0x0,
  same eip 0x78692441, same `Unhandled page fault on read
  access to 00000000`. Log:
  `fd171-contracts32-final-rerun1.log`.
- What this settles: the open question from the fd171 fix lane
  ("the identical D2D1 NULL crash possibly existing in the 32-bit
  d2d1.dll") is answered — YES, it exists on the 32-bit side.
  The 64-bit `D2D1CreateDeviceContext` NULL check was never
  applied to the 32-bit Wine 10 d2d1 build, so the 32-bit call
  still reads through a null surface instead of returning
  E_INVALIDARG. The 32-bit fixture is otherwise healthy: it
  reaches the D2D1 check having passed every earlier group, so
  this is a single missing fix, not a broken fixture or boot.
- Fix lane needed before any re-gate: port the D2D1 NULL-surface
  check to the 32-bit d2d1.dll (Wine 10 build), rebuild
  `wine32.zip`, re-pin, and re-run fd171-contracts32. This is a
  new filing for the coordinator/register; no register entry was
  made by this gate (the gate does not touch the register).

### Not run (gate stopped — not failures)

- FD-167 RED run (pre-change 20261104 archive), boot census
  (both widths), full browser suite, D3D11/D3D12 conformance in a
  store-linked tree, M6 two-build proof. The M6 fresh tree was
  not created: the existing `wt-merge-batch9-m6` is at the stale
  tip `5b2ea224d` and was not used. The M6 caveat from the re-run
  still stands (PCH caps the cacheable surface at ~81 of 272
  objects; judge on the cacheable set plus wasm byte-identity,
  not 270/270) — and this gate adds one data point for it: the
  fresh tip build at 4d603252e produced a wasm byte-identical to
  the previous tip's build.

### Dark (unchanged, not re-run)

- JIT block-invalidation gate (FD-176): still DARK on this tip;
  not re-run (the first run's `jit-dark.log` /
  `jit-dark-inv-build-interp.log` stand — build script fails with
  undefined symbols).

### Items from earlier runs, still standing (not re-run)

- Citation audit 352 rows 0 drift; silent-failure baseline
  unchanged; JS legacy 116 tests 115 pass 0 fail 1 skipped;
  run-native 71 PASS; native main serial 692 tests 0 FAILED;
  payload-staging proof M8 PASS.

### Housekeeping (this final gate)

- Generated state stripped from the merge tree at the end of the
  run: `dist/` (958M), `project/emscripten/Build` (55M),
  `project/linux/Build` (164M) removed; `git status` clean at tip
  `4d603252e`; disk 26G free. `web/runtime/` (the rebuilt 20261106
  zips) left in place — it predates this run and belongs to the
  rebuild, not the gate. Build lock idle; no probe slots held; no
  stray probe processes.

### Follow-ups for the coordinator (final gate)

1. One fix lane before any re-gate: the 32-bit D2D1 NULL-surface
   check (see stopper above). File it on the register; the
   re-gate then needs only fd171-contracts32 plus the items
   listed as not run (RED run, boot census, full browser suite,
   D3D11/D3D12 conformance, M6).
2. Nothing was deployed or pushed; the integration tree and defect
   register were not touched except this gate section.

### FD-192 32-bit D2D1 crash — FIXED and composed (lane-fd192)

Cause, in plain terms: the test calls a graphics function with no
drawing surface at all (NULL) and expects a polite "invalid
argument" error back. The 64-bit version had been fixed to do
exactly that; the 32-bit version never got the fix — it handed the
empty pointer straight to the next step, which tried to read
through it, and the guest crashed with a page fault on address 0.

Fix commits `7b3b241b1` + `dec0275e4` (branch `lane-fd192`, parent
`eccdd06c1`), cherry-picked cleanly onto the merge tree as
`00873996e` + `2b9058ce7`. The identical six-line NULL check was
ported to the Wine 10 patch
(`tools/buildWine/patches/wine10-fd171-contracts.patch`); the
32-bit `d2d1.dll` was rebuilt from the patched Wine 10.0 tree and
re-pinned (`c7f43ca2…` → `642621d6…`). Only `d2d1.dll` changed
functionally — the other 20 rebuilt DLLs differed solely in PE
timestamp/checksum bytes and were restored to their committed
bytes. New `wine32.zip`: 231,609,052 bytes, sha256
`128952652c5c7e6a47044e33d168ff703464b889652d52ab5c2f76f223108e47`.

Proof (re-read from the lane's logs, with the console escape codes
stripped — the raw 32-bit console interleaves terminal escapes
between characters, which defeats a plain grep): `fd171-contracts32`
PASS (27,422 ms), `fd171-contracts64` PASS (113,933 ms), `SUITE
RESULT: 2 passed, 0 failed`; 32-bit console
`D2D1CreateDeviceContext(NULL) ok hr=80070057`, `RESULT 0
(38 checks)`, 0 BW64-FAIL; 64-bit console `RESULT 0 (49 checks)`,
0 BW64-FAIL. Filed on the register as **FD-192** (commit
`eccdd06c1`, before the fix lane started); register re-verified
**AUDIT CLEAN** — 189 FD entries (1..192 except gaps 46/56/57),
CL-01..133 each exactly once.

The lane's re-cut archive is preserved lane-named at
`backups/winbox-runtime-assets-20261106-fd192/browser-runtime-fd192-20261106.tar`
(sha `3c72c339…`). The coordinator re-cut the combined archive
from the merge tree (same deterministic procedure; all 13 members
verified against the pins): `browser-runtime-20261106.tar`, sha
`6006959cb7efd14409e1a654ac9ab0fe07ac958a7c273003cdb3bde4f6e82984`
(committed as `3a574c6bd`); the retained archive and the store's
`wine32.zip` were refreshed to it (SHA256SUMS regenerated,
integrity OK; store MANIFEST updated). Bootstrap check on the
merge tip: **0 failures, 5 skips**, "Runtime assets verified
(13 files)" — FD-73 passes without override. Census baseline
regenerated for the new wine32 vintage (totals and DLL tables
identical; `--check` exits 0, "census matches the baseline
exactly"; committed as `e213779fb`). Prep exits 0.

Verification gap carried from the final gate (noted, not hidden):
the final gate's wasm-hash claim (206f1652, byte-identical) could
not be verified from the preserved files — no fresh BUILD_INFO was
kept and the build log carries no hash lines. The re-gate must
preserve `dist/runtime/BUILD_INFO.json` into the gate dir before
stripping. The build itself was proven (BUILD COMPLETE, JS unit
354/0 against the dist, fixtures ran).

Merge tip after all fixes: `e213779fb`.

## Re-gate — full suite 41/44: the FD-192 fix reproduces, but three suite tests fail (re-gate 2026-11-06, tip e213779fb)

Verdict: the gate does NOT pass. The FD-192 fix reproduces on the
composed tip (all five guest contracts green, including
fd171-contracts32), the boot census passes on both widths, the M6
two-build proof passes on its adapted standard, and the D3D11/D3D12
conformance run reproduces the documented October baseline exactly.
But the full browser suite finishes 41 passed, 3 failed, so the gate
as a whole does not pass. The three failures are analysed below from
their preserved consoles — none is a product regression in the
composed fixes; one is a stale test left behind by the intentional
FD-03 machine change, one is a missing unavailable-engine branch in
the test, and one is the documented gamepad blocker (FD-173/FD-174).
Nothing was fixed, edited, or worked around in either tree: `git
status` is clean at tip `e213779fb` in both. Nothing was deployed or
pushed. The integration tree and defect register were not touched
except this gate section. All re-gate logs:
`wt-merge-batch9/test-results/merge-batch9-20261106/` (files carry
`-regate` names; earlier runs' files are untouched). Per-test guest
consoles: `wt-merge-batch9/test-results/suite-<test>/console.log`
(one exception noted under the RED run: the fd167-contracts64
console dir now holds the RED console).

Prep: `winbox-wt-prep.sh` exits 0 on the merge tree (linked=17,
kept-existing=6) and on the fresh M6 tree (linked=23). Log:
`prep-regate.log`, `m6-prep-regate.log`.

### Passed on this tip (re-gate)

- Browser build (`tools/bw64-build.sh`, owner `regate-batch9`):
  `[bw64-build] BUILD COMPLETE`, `BUILD EXIT: 0 2026-11-06T16:14:47Z`
  (started 16:10:14Z), 0 errors in the log, `Browser export
  verified`. wasm sha256
  `206f1652b15de9878a9b4ea00261a3266afd11d267d87485da64b6ff175ba279`,
  5,722,692 bytes; js sha256
  `94669195ae2041379071bb46b9b6ba8951b788494a01688f1f93051784692c5e`;
  BUILD_INFO `git_sha e213779fb65f1056fe5700b251c2b8ff5daa5165`,
  `git_dirty_files 0`, built 2026-11-06T16:13:11Z. BUILD_INFO is
  preserved as `BUILD_INFO-regate.json` in the gate dir (this closes
  the final gate's verification gap — the wasm hash is now quoted
  from a preserved BUILD_INFO, not just the log). The wasm is
  byte-identical to the previous tips 5b2ea224d and 4d603252e (same
  sha256, same byte count, same js sha) — the FD-192 change touches
  only guest DLLs, pins, and the census baseline, as designed. ccache
  delta for the build: +81 cacheable calls, all 81 hits, 0 new
  misses; +193 uncacheable calls (the PCH-declined recompiles). Log:
  `browser-build-regate.log`.
- JS unit suite re-run (`node web/tests/suite/runner.mjs --unit`):
  `SUITE RESULT: 354 passed, 0 failed`, exit 0. Log:
  `js-unit-regate.log`.
- Native x64 suite, serial (`Build/X64Test/boxedwine 0 0 1` from
  `project/linux`, per `docs/standards/checks.md`): `Running 116
  Test tests with 1 threads`, `0 tests FAILED in 25s`, exit 0. Build
  (`make x64-test`): exit 0, 0 `error:` lines; the binary was freshly
  linked at 16:19 (ccache hits supplied the objects — no
  `source/` or `project/` file changed between 4d603252e and
  e213779fb). Log: `native-x64-regate.log` (build:
  `native-x64-build-regate.log`).
- Guest contracts (`node web/tests/suite/runner.mjs --browser
  --filter=contracts`, 5 tests): `SUITE RESULT: 5 passed, 0 failed`,
  exit 0. Log: `browser-fixtures-contracts-regate.log`.
  - `fd167-contracts32`: PASS (25,944 ms), 0 BW64-FAIL lines;
    console `fd167-32: RESULT 0 (51 checks)`.
  - `fd167-contracts64`: PASS (85,887 ms), console `fd167-64:
    RESULT 0 (15 checks)`, 0 BW64-FAIL lines.
  - `fd170-contracts64`: PASS (86,578 ms), console `fd170-64:
    RESULT 0`, 0 BW64-FAIL lines.
  - `fd171-contracts32`: PASS (26,070 ms), console `fd171-32:
    RESULT 0 (38 checks)`, `D2D1CreateDeviceContext(NULL) ok
    hr=80070057`, 0 BW64-FAIL lines — the FD-192 fix reproduces on
    the composed tip.
  - `fd171-contracts64`: PASS (112,368 ms), console `fd171-64:
    RESULT 0 (49 checks)`, `D2D1CreateDeviceContext(NULL) ok
    hr=80070057`, 0 BW64-FAIL lines.
  (The 32-bit consoles interleave terminal escapes between
  characters; the RESULT lines above were read with the escapes
  stripped.)
- FD-167 RED run (pre-change 20261104 archive): FAIL as designed —
  see the dedicated note below.
- Boot census: `boot64-smoke` PASS (146,245 ms), `boot32-vkboot`
  PASS (45,288 ms), both exit 0. Each console shows 0 `BW64-FAIL`
  lines and exactly 5 `BW64-INFO` served lines (count=1..5, the
  fontconfig hard-link lines) — the standing expectation, met
  exactly. Logs: `boot-census64-regate.log`,
  `boot-census32-regate.log`.
- M6 two-build proof (fresh tree `wt-merge-batch9-regate-m6` at tip
  `e213779fb`, prep exit 0): build A (first build)
  `BUILD COMPLETE`, exit 0, 18:14:01Z–18:18:44Z; build B (build dir
  removed first so make really recompiles) `BUILD COMPLETE`, exit 0,
  18:23:24Z. Cacheable-set delta per build: +81 calls, +81 hits, +0
  misses; uncacheable +193 per build (the PCH-declined recompiles).
  Wasm byte-identical across build A, build B, and the gate build
  (`206f1652...`, 5,722,692 B; js `94669195...`); both BUILD_INFO
  files record `git_sha e213779fb...`, `git_dirty_files 0`
  (preserved as `m6-BUILD_INFO-A-regate.json` /
  `m6-BUILD_INFO-B-regate.json`). M6 PASSES on the adapted standard
  the task set (cacheable set plus wasm byte-identity). The lane's
  270/270 standard cannot be reproduced as written: the PCH lane
  makes ~193 of ~272 compiles per build uncacheable, so the
  cacheable surface on this tip is ~81. Logs:
  `m6-buildA-regate.log`, `m6-buildB-regate.log`.
- D3D11/D3D12 browser conformance (`web/tests/d3d-conformance.mjs`,
  `SUITES=d3d11,d3d12`, in the store-linked M6 tree on its build-B
  dist, wasm `206f1652...`): both suites completed and wrote
  `results.json`. d3d11: 11/12 working (only `fl_11_0` FAIL,
  hr=0x80070057); d3d12: 19/20 working (only `device_FL_12_0` FAIL,
  hr=0x80070057). Both failures are exactly the documented October
  baseline ones (`docs/reports/d3d-conformance-MATRIX.md`: D3D11
  11_0 FAIL 0x80070057; D3D12 device_FL_12_0 the expected
  E_INVALIDARG, implementation targets FL 11_0). Lit pixels: d3d11
  208,582 (60 draws), d3d12 208,554 (11 draws — exactly October's
  count). The runner exits 1 because its exit code is "any FAIL
  op", not because anything new failed. One improvement vs October:
  the D3D11 swapchain now paints (208,582 lit px; October's pipeline
  showed litPixels=0). Report-generator buckets:
  `d3d11: total=12 working=11 slow=0 broken=1`,
  `d3d12: total=20 working=19 slow=0 broken=1`. Logs and artifacts:
  `d3dconformance-regate.log`,
  `d3dconformance-results-regate.json`,
  `d3dconformance-buckets-regate.json`,
  `d3dconformance-MATRIX-regate.md`,
  `d3dconformance-console-d3d11-regate.log`,
  `d3dconformance-console-d3d12-regate.log`; canvases stay in the M6
  tree at
  `wt-merge-batch9-regate-m6/test-results/d3d-conformance-d3dconf-regate-20261106/`.

### The FD-167 RED run — first attempt invalid (my error), corrected run fails as designed

The first attempt (`fd167-red-regate.log`) swapped
`web/runtime/wine64.zip` for the 20261104 version and the fixture
PASSED (83 s) — but that proves nothing: the runner serves `dist/`
(`probe-base.mjs` symlinks `dist` into a temp root and serves that),
so the guest loaded `dist/runtime/wine64.zip`, and the RED console
shows it mounted `bw64url:195094788` — the current zip's size. The
web/runtime copy is never served to the guest. That log is preserved
as the record of the mistake, not as a RED result.

The corrected run (`fd167-red-regate2.log`) swapped
`dist/runtime/wine64.zip` for the 20261104 version (sha256
`be4d4956b9cc93f0a1da0c2086f490d1dec9bb749ba48382d10ea9e633963cee`,
209,637,447 bytes): `FAIL fd167-contracts64 (201362ms)`,
`SUITE RESULT: 0 passed, 1 failed`, exit 1. The guest mounted
`bw64url:209637447` (the 20261104 zip — the size comes from a live
HEAD request, so the swap took effect), the five msvcp140 checks
fail with the Wine stub exception `code=80000100`
(`_Xbad_function_call throws bad_function_call FAIL rc=2
code=80000100`, `_XGetLastError ...`, `_Throw_C_error(3) ...`), then
the stub alias call crashes the guest (`BW64-FAIL guest-memfault
pid=48 addr=0x0`, `Unhandled page fault on read access to
0000000000000000`); no RESULT line. Exactly the lane's designed RED
signature. The current zip was then restored into `dist/runtime/`
and verified against its pin (`431d61cb...`, match); `bootstrap.py
--check` after the restore: 0 failures, 1 skip, `Runtime assets
verified (13 files)`, exit 0. The RED console is preserved as
`fd167-contracts64-console-RED-regate.log`. Note: the RED run
overwrote `test-results/suite-fd167-contracts64/console.log`, so
that dir now holds the RED console; the green console was read and
quoted above (RESULT 0, 15 checks, 0 BW64-FAIL) before the RED run.

### The full suite: 41 passed, 3 failed

`node web/tests/suite/runner.mjs --browser` (44 test files):
`SUITE RESULT: 41 passed, 3 failed`, exit 1, 16:42:02Z–18:11:35Z.
Log: `browser-suite-regate.log`. The three failures, each read from
its own preserved console:

1. `cpu64-selftest` — FAIL (3,059 ms). Guest summary: `950 passed,
   3 failed`. The three failing checks, all in
   `source/emulation/cpu/cpu64SelfTest.cpp`:
   - `CPUID leaf 1 topology and feature bits`
   - `sched_getaffinity returns 8, mask=1`
   - `sched_getaffinity returns the virtual CPU mask`
   Cause: batch 9's FD-03 fix (`265a158d6`, "raise the machine
   definition to 4 GiB RAM, 2 vCPUs, 4 GiB Vulkan device-local
   heap") raised `KSystem::guestCpuCount` 1 -> 2: CPUID leaf 1 EBX
   logical-processor field is now `guestCpuCount()`, the 64-bit
   default affinity mask is `0x3`, and `sched_getaffinity` writes
   the online mask (3). The three self-test checks still hardcode
   the 1-vCPU expectations (EBX `(8u << 8) | (1u << 16)`, mask `==
   1`). The FD-03 lane updated the native topology tests — which
   pass on this tip (native x64 `guest CPU topology consistency`
   OK) — but never touched these three browser checks (only the
   perf2 commit touched `cpu64SelfTest.cpp` in batch 9, for the
   INC/DEC fast path). This is test staleness against an intentional
   machine change, not a product regression: on 2026-11-03 the same
   test was 953/0 on the standard build (register FD-125 row), and
   the product now behaves exactly as FD-03 specified. Fix lane:
   update the three expectations to the 2-vCPU machine (or derive
   them from `guestCpuCount()`).
2. `incprobe-bench` — FAIL (103,817 ms). The jit=0 mode completes
   its four samples (checksum `593ca6f88c175dae`, passes=100000);
   the jit=1 mode then fails
   `assert.equal(Boolean(stats.cpu64.enabled), jit === '1')`
   (`false !== true`). This build has no JIT compiled in (METRICS:
   cpu64 `available:false, engine:"off", enabled:false`), so
   `enabled` can never be true in a jit=1 mode. The sibling bench
   `jitbench` handles exactly this case with an explicit
   unavailable-engine branch (assert only `blocksEntered == 0`)
   and PASSES on this tip (1,893,650 ms, all 20 fixture/mode
   results); `incprobe-bench` has no such branch. Test bug, not a
   product regression. Fix lane: add the unavailable-engine branch
   `jitbench` has (or restrict the jit=1 modes to JIT builds).
   Note `jit64` also PASSED (123,634 ms) on this tip.
3. `input-gamepad` — FAIL (140,858 ms). Documented BLOCKED in the
   test file's own header: "this test currently fails at the slot-0
   CONNECTED wait for reasons filed as FD-173 (the WineBus kernel
   driver fails to initialise in the emulator, like every kernel
   driver) and FD-174 (the 64-bit prefix has no PlugPlay
   registration)". The console matches that shape exactly: the
   witness spawns, prints `gamepad-witness: READY xinput=1
   dinput=0` and `DINPUT devices=0`, and the 120 s wait for
   `XINPUT slot=0 CONNECTED` times out. Known-blocked, filed
   defects; the emulator-side feed chain (simulated pad -> SDL ->
   kernel evdev) is in place and the test is the proof vehicle for
   the moment either blocker lands.

Everything else in the suite passes, including all five D3D
comprehensive tests (`d3d10-comprehensive`, `d3d11-comprehensive`,
`d3d11-triangle`, `d3d12-comprehensive`, `d3d9-32`), `jitbench`,
`jit64`, `celeste-boot`, both boots, and all contract fixtures
(re-run inside the suite: `fd167-contracts64`, `fd171-contracts32`,
and the rest PASS again).

### Dark (unchanged, not re-run)

- JIT block-invalidation gate (FD-176): still DARK on this tip;
  not re-run (the first run's `jit-dark.log` /
  `jit-dark-inv-build-interp.log` stand — build script fails with
  undefined symbols).

### Items from earlier runs, still standing (not re-run)

- Citation audit 352 rows 0 drift; silent-failure baseline
  unchanged; JS legacy 116 tests 115 pass 0 fail 1 skipped;
  run-native 71 PASS; native main serial 692 tests 0 FAILED;
  payload-staging proof M8 PASS.

### Housekeeping (this re-gate)

- Generated state stripped from the merge tree at the end of the
  run: `dist/` (958M), `project/emscripten/Build` (55M),
  `project/linux/Build` (164M) removed; from the M6 tree: `dist/`
  (958M), `project/emscripten/Build` (55M) removed. `web/runtime/`
  (the rebuilt 20261106 zips) left in place — it predates this run
  and belongs to the rebuild, not the gate. `git status` clean at
  tip `e213779fb` in both trees; disk 24G free. Build lock idle; no
  probe slots held; no stray probe processes. /tmp scratch removed.
- The fresh M6 tree `wt-merge-batch9-regate-m6` (detached HEAD
  `e213779fb`) is kept: it holds the D3D conformance results dir
  (canvases + full consoles) and is the store-linked tree the
  conformance pass ran in.

### Follow-ups for the coordinator (re-gate)

1. Three fix lanes before the gate can pass: (a) the three stale
   1-vCPU expectations in `cpu64-selftest` (FD-03 follow-up);
   (b) the missing unavailable-engine branch in `incprobe-bench`;
   (c) the gamepad blockers FD-173/FD-174. None is a defect in the
   composed FD-192 fix or the other batch-9 fixes — the contracts,
   census, M6, and D3D evidence all reproduce.
2. The full suite had never been run to completion on any batch-9
   tip before this re-gate (earlier gates stopped at the build or
   the fixtures), so these three failures have no earlier
   full-suite baseline to compare against; the comparisons above
   are against the lanes' own records and the October conformance
   matrix.
3. Nothing was deployed or pushed; the integration tree and defect
   register were not touched except this gate section.

## Focused re-gate — full suite 42/44 on tip d2c08c524; jitbench passes on focused re-run (2026-11-06)

### The two fixes, verified and composed

- cpu64-selftest fix: lane commit `4246efd9e` (branch `lane-cpu64-selftest-2cpu`, parent `e213779fb`; 1 file, `source/emulation/cpu/cpu64SelfTest.cpp`, 6 insertions / 6 deletions). Proof re-read from the lane's logs: `PASS cpu64-selftest (2737ms)`, `SUITE RESULT: 1 passed, 0 failed`; console `953 passed, 0 failed`; all three stale 1-vCPU expectations now pass. Cherry-picked cleanly as `10af8df40`. (The lane's `__pycache__` sweep crossed into the shared store; store integrity re-verified afterwards: `sha256sum -c SHA256SUMS` exit 0.)
- incprobe-bench fix: lane commit `9865e1eaf` (branch `incprobe-bench-unavail`, parent `e213779fb`; 1 file, `web/tests/suite/browser/incprobe-bench.test.mjs`, +22/−2). Proof re-read: `PASS incprobe-bench (294644ms)`, `SUITE RESULT: 1 passed, 0 failed`; all four modes record `engineUnavailable`, `available:false`, `blocksEntered:0`; wasm byte-identical to the re-gate build (`206f1652…`). Cherry-picked cleanly as `d2c08c524` — the new merge tip.
- Spawning the focused re-gate as a child failed twice (the runtime refused both attempts: `subagent reservation owner is terminal or structurally invalid`); the gate was run directly as a backgrounded job on the box instead, same procedure (build under the build lock, full suite under the probe slot, BUILD_INFO preserved before stripping).

### The focused re-gate run (tip d2c08c524)

- Build: `BUILD COMPLETE`, `Browser export verified`. BUILD_INFO preserved as `BUILD_INFO-suite2.json` before stripping: git_sha `d2c08c524`, dirty 0, wasm `92b36180…`, 5,722,692 bytes, js `94669195…` unchanged. The wasm differs from the re-gate build (`206f1652…`) because the cpu64-selftest fix changes test code compiled into the wasm (the lane documented this); js hash and byte size identical.
- The SSH link dropped twice mid-run (exit 255, `Connection closed by remote host`); the remote script survived both times (ignores SIGHUP) and ran to completion. Its status-file logging broke after the first drop (writes went through the dead channel), but every artifact landed; two background waiters watching it were killed by the runtime (SIGTERM, 47 min and 23 min in) and had to be relaunched — the run itself was unaffected.
- `SUITE RESULT: 42 passed, 2 failed` (`browser-suite-suite2.log`). Both fixes pass inside the full suite (`PASS cpu64-selftest`; `PASS incprobe-bench (277905ms)`).

### The two failures

- input-gamepad — KNOWN-BLOCKED, as adjudicated in the re-gate (FD-173/FD-174 provisional; FD-188/FD-189 settled). Verified again from its own console: `XINPUT slot=0 CONNECTED/ not seen within 120000ms`, with the driver-start blocker signatures verbatim (`ZwLoadDriver failed … MountMgr: c0000142`; `Auto-start service "MountMgr" failed to start: 1114`; same for nsiproxy, NDIS, wineusb). No fix attempted; the test remains the proof vehicle for the moment either blocker lands.
- jitbench — TIMEOUT, not an assertion: `browser test timed out after 3660000ms (hung probe killed — never stall forever)`; the guest was alive and emitting METRICS to the end; no `jitbench.json` produced. Re-gate baseline: `PASS jitbench (1893650ms)` — 31.5 min. Between the re-gate tip and this tip only the two test-only commits changed, and neither is loaded by jitbench. The whole suite ran slow (jit64 252s vs 124s in the re-gate; jitbench hit its 61-minute load-gated timeout at ~2x its passing duration) — the same shape as the batch-7 jitbench timeout, which a focused re-run passed.
- Focused re-run (same tip, fresh build, jitbench only): `PASS jitbench (2169025ms)` — 36.1 min — `SUITE RESULT: 1 passed, 0 failed` (`browser-jitbench-rerun.log`; `BUILD_INFO-jitbench-rerun.json`: git_sha `d2c08c524`, wasm `92b36180…` identical to the suite-2 build). Console shows `BW_JITBENCHMIX` samples with identical checksum `b8476f000fa05ee2`. Adjudication: the suite-2 jitbench failure was a timeout flake under a slow run, not a regression; the test passes on this tip.

### Final tally

- 43 passed, 1 known-blocked (input-gamepad, FD-188/FD-189), jitbench passing on focused re-run. The gate passes on this reading.
- Standing items still not re-run (unchanged from the re-gate): citations, silent-failure baseline, JS legacy, run-native, native main serial, staging M8. The JIT block-invalidation gate stays DARK (FD-176).
- Nothing deployed, nothing pushed. Gate tip `d2c08c524`; tree clean; `dist/` stripped. The record commit rides on top of the gate tip; the integration branch fast-forwards to it.

### Housekeeping (this focused re-gate)

- The D3D conformance canvases + consoles from the re-gate M6 tree preserved into the merge tree's gate evidence dir (`test-results/merge-batch9-20261106/d3d-conformance-d3dconf-regate-20261106/`, 2.0M) before the M6 trees were stripped.
- Generated state stripped from `wt-merge-batch9` (`dist/`, Build dirs, `web/runtime/` real files, `__pycache__`), `wt-jj-batch9`, `wt-merge-batch9-m6`, and `wt-merge-batch9-regate-m6`.
