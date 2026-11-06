# Build cache — FD-173 (lane-buildcache, 2026-11-05)

Compiling the project now reuses remembered results. When any working
copy compiles a source file that has already been compiled anywhere on
this machine with identical inputs and settings, it gets the remembered
result instead of recompiling. Filed as FD-173, closed as CL-126 (both
provisional until the merge audit). Base: integ tip `48d27952a`.

## What was installed and wired

- **ccache 4.9.1** (the box's distribution package) does the remembering.
  No custom code. It keys each compile on the preprocessed source plus
  the exact command line, so a result is only ever served when the
  inputs and flags are identical.
- **One guarded block, added identically to both build entry points**
  (`project/emscripten/makefile`, `project/linux/makefile`): when
  ccache is on PATH and `BW64_CCACHE` is not `0`, the makefile prefixes
  `CC`/`CXX` with `ccache`, exports `CCACHE_BASEDIR` as the tree's own
  root (unless already set), and points `CCACHE_DIR` at the shared
  directory below when it exists (unless already set). CC/CXX given on
  the make command line still win over the block (make semantics), so
  an explicit compiler choice is never silently cached. Both the
  engaged and the `BW64_CCACHE=0` forms were verified by dry run in
  both makefiles before any timed run.
- **Shared cache directory:** `/home/ubuntu/boxedwine64-cache/ccache`
  (mode 0777), with its own `ccache.conf` asserting `max_size = 2.0G`
  and `compression = true`. Measured footprint is far under the cap:
  the full browser build's 270 results take ~12 MB, and after every
  build in this record (browser + all native configurations) the cache
  holds ~49 MB (2.39% of the cap). The cap is sized for the box's
  currently tight disk and can be raised in one command when there is
  room (the prep script's comment says how).
- **Prep fold-in:** the installed `winbox-wt-prep.sh` gained a
  compiler-cache section (previous version kept beside it as
  `winbox-wt-prep.sh.pre-buildcache-20261105`): it creates the shared
  directory if missing, asserts the cap + compression, and prints the
  cache state. Verified end to end on a fresh tree, which printed
  `compiler cache: /home/ubuntu/boxedwine64-cache/ccache (max_size=2.0 GB, compression=true)`.
  COMPOSITION NOTE: `lane-sharedbuild` carries a repo copy
  (`tools/winbox-wt-prep.sh`) that needs the same section when the two
  lanes compose; the installed script already carries it.

## Why CCACHE_BASEDIR is load-bearing (measured, not assumed)

The build rules pass ABSOLUTE source paths, so a naive cache can never
hit across working copies. A single real translation unit
(`vk_host_marshal.cpp`, compiled with the exact flags from the build's
own dry run) was compiled in two different trees:

- plain ccache: the second tree MISSES (2/2 misses);
- `CCACHE_BASEDIR=<each tree's own root>`: the second tree HITS, the
  returned object is byte-identical to the first tree's, and the
  `-MMD` dependency file is restored with the hit (so incremental
  builds keep their header dependencies);
- `CCACHE_DEPEND=1` + basedir also hits with identical objects, but
  was NOT chosen: depend mode checks header freshness by file stat,
  while the default preprocessor mode hashes the preprocessed source
  itself — the stronger guarantee, and fast enough (see warm times).

## Proof — browser build (`tools/bw64-build.sh`, default -j2)

All four runs are the same tree content at `48d27952a` (Runs 2-3 with
the makefile change; Run 0 before it). Times are lock-acquired →
BUILD COMPLETE; queue waits behind other lanes' builds are listed
separately and excluded. Loads are the 1/5/15-minute averages at the
run's ends; every run shared the box with other lanes.

| Run | Condition | Queue wait | Build time | Cache | Product wasm |
|-----|-----------|-----------|-----------|-------|--------------|
| 0 | no cache (baseline) | 0 s | **1,434 s** | — | `9b51d60101ab` |
| 1 | cold cache (emptied first) | 1,016 s | **1,084 s** | 270 misses / 0 hits | `9b51d60101ab` |
| 2 | warm cache, same tree rebuilt | 433 s | **182 s** | 270/270 hits | `9b51d60101ab` |
| 3 | warm cache, FRESH tree, its first build | 65 s | **175 s** | 270/270 hits | `9b51d60101ab` |

- The product file is **byte-identical in all four runs**:
  sha256 `9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241`,
  5,688,949 bytes — the same figure the shared-build-store lane
  measured independently for this tip (cross-lane reproducibility).
- Run 1 is the empty-memory first build: it works, prints BUILD
  COMPLETE, and produces the identical product. Its time is BELOW the
  uncached baseline only because the box's load fell during it
  (15 → 3); a cold cache can only add overhead, and the honest reading
  is that the overhead is smaller than this box's load noise.
- The warm runs' remaining ~3 minutes are the parts a compile cache
  cannot remember: the link (emcc -O3 + wasm-opt), bootstrap, and the
  export/verify steps. The compiling itself went from ~24 minutes of
  Run 0 to seconds.

## Proof — native build (`make -C project/linux release`, default -j2)

Three runs back to back in the same tree (01:24-01:45Z, loads 3-8):

| Run | Condition | Time | Cache | `boxedwine` sha256 |
|-----|-----------|------|-------|--------------------|
| baseline | `BW64_CCACHE=0` | **747 s** | — | `1be499b76dd1…` |
| cold | cache on, native entries new | **543 s** | 377 misses | `594c4aea2a72…` |
| warm | objects deleted, rebuilt | **3 s** | 377/377 hits | `594c4aea2a72…` |

(Cold beating baseline is again load variance, not a cache effect.)
The other native configurations were then built through the same
wiring for the suites: `make test` exit 0 in 1,096 s (146 of its
compiles were already remembered from the other configurations),
`make x64-test` exit 0 in 1,869 s at load ~14 (144 already
remembered).

## One byte-level difference, diagnosed and bounded

The cached native binary (`594c4aea…`, cold and warm identical)
differs from the uncached baseline binary (`1be499b7…`). An
object-level diff of one translation unit compiled both ways isolates
the entire difference to ONE embedded string: the source file's path
as recorded by the compiler — absolute
(`/home/ubuntu/wt-buildcache/lib/asmjit/…`) in an uncached compile,
relativised (`../../lib/asmjit/…`) in a cached one, because the
cache's base-dir rewriting hands the compiler a relative path. The
16-byte length difference shifts later section offsets; section sizes
and all code are otherwise identical (evidence: `strings` diff shows
exactly one differing string). Two consequences, both stated plainly:
(a) the difference is a diagnostic annotation, not code — and it is
arguably an improvement, since cached objects from DIFFERENT trees
agree byte-for-byte, while uncached objects from different trees
embed their different absolute paths; (b) the byte-identity claim of
this lane therefore covers the shipped product (the wasm, identical
in every run) exactly, and the native verification binary up to
source-path annotation strings.

## Suites on the cache-built tree

- Native main, serial (`Build/Test/boxedwine 0 0 1`): **689 tests,
  0 FAILED** (617 s).
- Native x64, serial (`Build/X64Test/boxedwine 0 0 1`): **0 FAILED**
  (28 s).
- JS unit (`node web/tests/suite/runner.mjs --unit`, against the
  warm-cache build's dist): **353 passed, 0 failed**, exit 0.

## Coverage and gaps, stated plainly

- Covered: the browser (Emscripten) toolchain and the native
  (gcc/g++) toolchain — every configuration of both project
  makefiles, including the JIT browser target, whose distinct flags
  key separate cache entries by construction.
- Not covered: link steps (nothing to remember — they consume the
  objects; ccache passes them through untouched); the `.s` assembly
  rule (not wrapped; the shipped targets compile zero `.s` files —
  dry-run count 0 — so nothing is lost today); ad-hoc compiles
  outside the two makefiles (e.g. Wine backport builds under
  `tools/`); anything off this machine (the cache is per-machine,
  as specified).
- Escape hatches: `BW64_CCACHE=0` disables; a missing ccache binary
  disables silently (the makefiles behave exactly as before); an
  explicit `CCACHE_DIR`/`CCACHE_BASEDIR` in the environment is
  respected.

Evidence: `test-results/buildcache/` (run logs with epochs, loads,
hashes and cache statistics for every run above; suite logs).
