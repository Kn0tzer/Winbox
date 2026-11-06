# FD-71 lane record — cold-start phase attribution, the zip declaration scans, and a rejected engine switch

- **Lane:** `lane-fd71`, worktree `/home/ubuntu/wt-fd71`, base `48d27952a` (integration tip, batch-8 register audit).
- **Date:** 2026-11-05.
- **Commits:** fix `c0397ec11`; native test `7ac6802ab`; this record + register edits in the following commit.
- **Evidence:** `test-results/fd71/` in the worktree (diag timelines, instrumented segment logs, A/B logs and per-boot `results.jsonl` under `test-results/fd71-ab-*`, `test-results/fd71-ab2-*`, `test-results/fd71-fix-x64/`).

## Scope, stated honestly

FD-71 as filed is the Godot demo's cold first-run startup, and its own text says it can only be sized and closed after FD-55's pipeline-creation defect is fixed (FD-55 is in turn blocked on FD-72). **FD-71 therefore stays open.** This lane worked the part that is measurable today: the general cold start every program pays on a first visit — time from page load to session ready on the project's standard start measurement (`web/tests/boot-rate.mjs`: fresh browser profile per boot, waits on `bwRuntime.ready()`, records machine load per boot) — and shrank it with a change proven by before/after measurements taken the same way.

## Baseline: where a cold start goes (x86 session, instrumented build)

Phase marks from a cold boot that reached ready in 47.6 s at load 4.4 (uninstrumented timeline, base wasm `9b51d601`):

- Page load, wasm fetch/compile and virtual-file-system seed: ready by ~2.5 s.
- Emulator initialisation to "Starting …": ~3 s more.
- **File-system setup: ~12.6 s** — the second-largest segment and, before this lane, unexplained.
- Guest Wine boot (service chain, explorer, deskpin ready): the remaining ~29 s.

An instrumented build (segment timers inside the file-system code, wasm `0bec17a0`) split the file-system window. Before `Fs::initFileSystem` runs, `StartUpArgs::apply` scanned every zip's central directory **twice**, looking for root-level `fullfills.txt`, then again for `depends.txt` (the zip dependency-declaration mechanism), via `FsZip::readFileFromZip` — a function that opens the archive and walks every entry one by one. Measured cost of that pre-pass, per zip (console `ms` at load ~10):

| zip | entries | fullfills.txt scan | depends.txt scan |
|---|---|---|---|
| support (lazy URL) | 3,542 | 855 ms | 1,958 ms |
| wine32 (lazy URL) | 1,926 | 1,073 ms | 884 ms |
| glibc-rootfs32 | 167 | 97 ms | 67 ms |
| prefix32 | 42 | 17 ms | 19 ms |
| **total** | **5,677** | **2,042 ms** | **2,928 ms** |

**Pre-pass total: 4,970 ms** — and it returns nothing on every shipped start: all seven archives in `dist/runtime/` were enumerated directly (support 3,542, wine32 1,926, wine64 1,257, glibc-rootfs32 167, glibc-rootfs64 100, prefix64 152, prefix32 42 entries) and **none contains a `fullfills.txt` or `depends.txt` entry**. `FsZip::init` then walked the same directories a third time to build the node index: 2,311 ms (open ~17 ms, per-entry info 1,606 ms, node building 688 ms). `Fs::initFileSystem` itself measured 7 ms.

Directions examined and not pursued, with reasons: font-cache regeneration (its writes cluster inside ~1.2 s; the guest font set is small); zip decompression volume (~123 MB inflated by 39 s is under a second of native zlib); trimming Wine's boot services (the session launcher's own history records that GUI programs need the full service stack and a bare server cascades to failure); the deskpin window poll (≤1 s, inside a shipped asset binary).

## The fix (`c0397ec11`)

The two pre-init scans are deleted. `FsZip::init` now captures the two declarations during the central-directory walk it already performs: the first entry whose raw name matches exactly (the same comparison `readFileFromZip` makes) is read out with the same full-read + CRC checks, and a failed or corrupt entry counts as absent, exactly as a failed scan left its result empty. `StartUpArgs` resolves dependencies from the captured contents **after** the listed zips are initialized, with the old predicates and ordering unchanged: `fullfilled` collects every listed zip's declaration in list order; a dependency is queued only when not already listed, fulfilled or queued; dependency zips are initialized after the listed ones, in discovery order; a dependency zip's own declarations are not followed, as before. `readFileFromZip` itself is unchanged for its other callers (UI filesystem scans). On every shipped start the resolution is a no-op by the enumeration above: same archives, same order, same contents served.

Equivalence is additionally pinned by a new native test (`7ac6802ab`, `testFsZipDeclCapture`): crafted archives check the capture against `readFileFromZip` on the same bytes — a subdirectory namesake (`sub/fullfills.txt`) is ignored, the first root-level `depends.txt` wins over a later duplicate, an archive without declarations captures nothing, and a CRC-corrupt declaration counts as absent in both readers.

## Proof

- **Segment level:** on the fix build the pre-pass lines are gone entirely; deskpin spawn moved from 14.6 s to 8.4 s in comparable diag boots; the boot reached ready in 31.7 s at load 4.17 (instrumented base: 38.0 s at load 10.33 — loads differ, so the segment figures, not this pair, are the load-independent evidence).
- **Standard measurement, interleaved A/B** (final fix wasm `c70cc5b7` vs base wasm `9b51d601`, x86, fresh profile per boot, loads recorded per boot):

  | round | base ready | fix ready | loads (start) |
  |---|---|---|---|
  | 1 | 30,875 ms | 28,635 ms | base 4.75, fix 5.03 |
  | 2 | 26,543 ms | 24,840 ms | base 8.05, fix 8.07 |
  | 3 | 28,054 ms | 24,863 ms | base 7.95, fix 10.21 |

  Medians **28,054 ms → 24,863 ms (−3,191 ms, −11.4%)**; every pair faster, including round 3 where the fix boot started at the higher load. All 6 boots started; no failures, no restarts, no rescues on either arm. The wall-clock gain (~3 s) is smaller than the ~5 s of guest-side scan work removed because the standard harness measures wall time on a shared, loaded machine where later boot stages overlap differently; the direction is consistent across all pairs.
- **Suites on the fixed tree:** JS unit suite **353 passed, 0 failed**; native main suite **690 tests, 0 FAILED** (689 at the base gate + the new capture test), run 232 s.
- **No start that worked before fails now:** x86 3/3 fix-arm boots above; **x64** session on the fix build started successfully via boot-rate in 419.1 s at load 17.1→21.6 (the baseline x64 diag boot took 597.6 s at load 16.7 — different harness and load, cited only as a successful start, not a controlled comparison).

## Rejected, with its measurement: switching the default engine build to JIT for startup

A `wasm64-mt-jit` build (wasm `489d72c7`) was measured on the same harness, interleaved with the base: the base arm started in 53.5 s; the JIT boot **exhausted the 420 s budget** still making slow progress (normal console progress, no failure signature, deskpin never ready) and was classified "other" (`test-results/fd71-ab-jit-1/`). Compile-on-first-execution makes cold starts far slower even though the same engine can win once warm. The shipped default stays the interpreter build for cold starts; the A/B was stopped after one round because the margin was decisive.

## What remains

- FD-71 stays open for its Godot portion (above).
- Of the cold start that remains, the guest Wine boot is now the dominant segment (interpreter execution of the standard service chain); the file-system window is down to `FsZip::init`'s own walk.
- That walk's per-entry cost through the lazy fetch layer (1,606 ms across 5,677 entries on the standard x86 start) is filed separately as **FD-166**: the central directory is contiguous at the end of each archive, so a bulk fetch parsed in memory could remove most of it.
