# Portal boot-time profiling — 2026-10-23

## Summary

Portal's menu-visible boot is dominated by lazy ZIP range reads, not by guest
JIT execution or frame submission. On the probe path the game archive is the
deflated `portal.zip`; Portal reads it through 512 KiB `fszipurl` blocks with a
256 MiB C++ LRU. The probe fetched 8,034 MB, evicted 15,552 blocks, and created
1,562 VkImages on the way to the menu at 1126.6 s. The launcher/app path uses
the stored `portal-stored.zip`; the same boot read 608-880 MB and reached the
menu much earlier.

A launcher-side read-ahead cache now keeps a 384-block (192 MiB) JS LRU of
512 KiB game ranges and fetches four blocks ahead. It does not change the WASM
runtime. On the stored archive with the same instrumented probe, boot-to-menu
fell from 555.15 s to 435.76 s (21.5%). On the original deflated probe it fell
from 1126.59 s to 927.59 s (17.7%), with the same 8,034 MB of reads and zero
probe errors. The remaining gap is the still-serial 512 KiB guest request path;
the read-ahead hides network latency but cannot reduce the number of block
requests.

## Reproduction

- Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-portal-20261022`
- Branch: `lane/winbox-portal-20261022` (base `e00a6c105`)
- Deflated probe: `GAME=portal TAG=portal-bc2-20261022 TIMEOUT_MS=1800000 MEASURE_MS=120000 node web/tests/game-probe.mjs`
- Stored probe: `GAME=portal GAME_ZIP=/home/ubuntu/kn0tzer/games/portal-stored.zip TAG=portal-prof-stored-20261023 TIMEOUT_MS=900000 MEASURE_MS=15000 node web/tests/game-probe.mjs`
- Stored probe after the read-ahead: `GAME=portal GAME_ZIP=/home/ubuntu/kn0tzer/games/portal-stored.zip TAG=portal-prefetch2-stored-20261023 TIMEOUT_MS=900000 MEASURE_MS=15000 node web/tests/game-probe.mjs`
- App path: `web/tests/demo-matrix.mjs` (Portal catalog entry uses
  `portal-stored.zip`, 5,186,497,979 bytes; JIT=0).

## Measurements

| Path | Boot to menu | fszip reads | LRU evictions | Notes |
|---|---:|---:|---:|---|
| Probe, deflated `portal.zip`, before | 1126.59 s | 8,034 MB | 15,552 | Baseline `portal-bc2-20261022`, pass, 108 frames/127.7 s |
| Probe, deflated `portal.zip`, after | 927.59 s | 8,034 MB | 15,552 | `portal-prefetch-deflated-20261023`, pass, 15 frames/20.2 s, no errors |
| Probe, stored `portal-stored.zip`, profiler | 555.15 s | 608 MB | 704 | `portal-prof-stored-20261023`, menu verified; killed before 15 s measurement |
| Probe, stored + launcher read-ahead | 435.76 s | 608 MB | 704 | `portal-prefetch2-stored-20261023`, pass, 15 frames/22.4 s, no errors; 1,562 images |
| App, stored `portal-stored.zip`, before | 360 s post-map (~397 s page) | ~880 MB | 55 fszip lines | `demo-portal-bc4-20261022`, menu at `boot-360s.png` |
| App, stored `portal-stored.zip`, after | menu present at 360 s sample (~396 s page) | 608 MB | 704 | `demo-portal-prefetch-20261023`, click-to-mapped 37.4 -> 29.2 s; menu visible at `boot-360s.png` |

The app and stored-probe numbers are not an apples-to-apples comparison:
`game-probe.mjs` additionally runs a 15 s OCR/frame measurement, wraps
`onSubmittedWorkDone`, and samples a 1024x768 canvas. The 1126.6 s deflated
number is the one the probe originally reported.

## Root causes

1. **Deflated-archive read amplification.** The probe's `portal.zip` is mixed
   stored/deflated. `fszipurl` reads 512 KiB compressed blocks, so reaching
   Portal's textures touched 8,034 MB and 15,552 LRU evictions. The app's
   stored archive read only ~880 MB. Switching the measurement archive to
   `portal-stored.zip` removes most of the 2x difference before any code
   change.
2. **Synchronous block misses on a busy main thread.** The guest asks the page
   for one 512 KiB Range through a synchronous emscripten proxy. Each miss
   waits for the main thread, which is also building pipelines and presenting
   WebGPU work. The launcher now fetches four blocks ahead into a JS LRU so the
   next guest request is a cache hit instead of another network round trip.
   The deflated probe still read the same 8,034 MB and evicted the same 15,552
   C++ blocks, but reached the menu 199 s earlier; the win is latency hiding,
   not reduced transfer.
3. **Not guest JIT.** JIT is off by default in `web/app.mjs`. The app's JIT
   stats show `cpu32.enabled:false`, `blocksCompiled:0`, and
   `instructionsRetired:0`, so this boot used the interpreter. Prior evidence
   shows CPU32 JIT is slower than the interpreter and caused regressions on
   other demos. A Portal JIT=1 probe was started but canceled while waiting for
   a slot, so JIT is not part of this change.
4. **Not frame submission or pipeline creation.** The deflated probe had only
   four presents between 300 and 800 s. Post-menu page-tier metrics show
   pipeline ~5.6 s/89, translate ~0.87 s/67, submit ~0.23 s/273, render
   ~33.5 s/273 over a 45 s window; those costs are real but are not the
   menu-visible boot bottleneck.

## Changes

- `project/emscripten/wine64-launcher.js`: installs a lazy-zip read-ahead cache
  before the runtime starts. It intercepts only 512 KiB `Range` requests,
  keeps a 384-block JS LRU, fetches four blocks ahead on a miss, parses the
  `Content-Range` total to stop at EOF, and skips rootfs `wine32.zip`/
  `wine64.zip`/`support-*.zip` prefetches so small lazy roots do not generate
  416s. Cache hits return a synthetic 206 `Response`; non-Range fetches pass
  through unchanged.
- No WASM rebuild is required.

## Validation

- `node --check project/emscripten/wine64-launcher.js`: pass.
- Node harness of the read-ahead closure with a mock range server: first
  request fetches the requested block plus four ahead; the second and next
  block requests are cache hits; EOF-bounded prefetch emits no 416.
- `portal-prefetch2-stored-20261023`: `pass:true`, `menuVerified:true`,
  `bootToMenuSeconds:435.758`, `errors:[]`.
- `portal-prefetch-deflated-20261023`: `pass:true`, `menuVerified:true`,
  `bootToMenuSeconds:927.592`, `errors:[]`; baseline
  `portal-bc2-20261022` was 1126.588 s.
- `portal-prof-stored-20261023` pre-change: `menuVerified:true`,
  `bootToMenuSeconds:555.153`.

## Remaining

- Run a clean stored-archive probe without the profiler/OCR wrapper to remove
  the instrumentation overhead before comparing against the app's 360 s.
- The app's fixed 5/30/60/120/240/360 s sampling shows the menu at 360 s both
  before and after; the read-ahead did not move the app's first sampled menu
  (click-to-mapped improved 37.4 -> 29.2 s, and reads fell ~880 -> 608 MB).
- The deflated read-ahead measurement is 927.59 s (17.7% better); further
  improvement needs fewer synchronous block requests or a larger cache.
- If the deflated path remains above ~900 s, the next lever is increasing
  `fszipurl` block size (512 KiB -> 1-2 MiB) and/or the 256 MiB C++ cache in a
  WASM build; the launcher read-ahead cannot reduce the number of synchronous
  guest block requests.
- JIT remains off. A Portal-specific JIT measurement is still outstanding and
  would need to beat the interpreter without the known regressions.
- The 0.67 fps post-menu measurement is a separate rendering-throughput issue;
  it does not affect time to first visible menu.

## Build-5 C++ lever (not measured here)

The 2 MiB / 512 MiB change in this follow-up commit is not built or measured in
this worktree; it is the candidate for integration build 5.

- `source/io/fszipurl.cpp`: `BLOCK_SIZE` 512 KiB -> 2 MiB and
  `MAX_CACHED_BLOCKS` 512 -> 256, keeping the C++ LRU at 512 MiB.
- `project/emscripten/wine64-launcher.js`: the read-ahead now accepts both
  512 KiB and 2 MiB Range requests. For 2 MiB blocks it prefetches two blocks
  (4 MiB) ahead and uses a byte-capped 192 MiB JS LRU, so the larger C++ block
  does not lose the read-ahead behavior measured above.

Reasoning for the expected win: the deflated probe made about 16,068 512 KiB
block fetches for 8,034 MB. The same data at 2 MiB is about 4,017 synchronous
proxy waits, a 4x reduction in the main-thread scheduling stalls that the JS
read-ahead only partially hides. The 512 MiB C++ cache holds 256 blocks, enough
to cover most of the app's 608 MB stored working set and to reduce the 704
evictions seen at 256 MiB. The trade-off is up to 2 MiB of overfetch per miss;
Portal's texture reads are large and sequential, and the 2 MiB read-ahead
window is the same one the measured JS prefetch already used (four 512 KiB
blocks).

Unit tests, without a full build:

- `web/tests/suite/unit/fszipurl-block-cache.test.mjs`: asserts the 2 MiB/512 MiB
  constants and eviction log arithmetic, extracts the launcher read-ahead with
  a mock server, and checks a 2 MiB request fetches two blocks ahead and the
  next 2 MiB block is a cache hit.
- `node --test` on the new test plus `vfs-socket-isolation` and
  `packaging-prefix`: 17 passed, 0 failed.
- `node --check project/emscripten/wine64-launcher.js`: pass.
