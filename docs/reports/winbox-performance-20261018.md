# Winbox Chromium performance, 2026-10-18

The deployed runtime reduces the warmed integer benchmark from **113 ms to
98 ms (13.3% less time)** in the final comparison against the saved live export.
Startup renderer CPU falls **5.7%**. Observed startup wall time and scene CPU
medians also fall, but their ranges overlap. Animation callback cadence does
not improve. These are controlled Chromium workloads, not a claim that every
website is 13.3% faster.

The selected change is bounded chaining of cached direct branches in the
32-bit guest interpreter. It avoids returning through the outer virtual CPU
loop after each basic block. Each run chains at most 64 blocks before returning
to the existing termination and cleanup checks. Indirect calls, returns,
syscalls, native builds and JIT builds keep their existing behavior.

## Progress over time

All times below are medians across accepted runs. Integer time is the median
of the final four samples within each run, then the median across runs. UTC
windows show the first and last trial starts; some control trials were
interleaved with later experiments.

| Stage | UTC window | Runs | Ready, s | Startup CPU, s | Integer, ms | Scene CPU, s | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| Live pilot | 02:42 | 1 | 29.191 | 44.57 | 103 | 30.60 | Setup pilot; excluded from final comparison |
| Rebuilt source control | 02:42–02:49 | 3 | 30.246 | 46.47 | 107.5 | 31.89 | Development baseline |
| Direct branch chaining | 02:45–02:50 | 3 | 28.089 | 44.88 | 95.5 | 29.51 | Promising; replaced by bounded chaining |
| Bounded 64-block chaining | 02:53–03:03 | 4 | 28.441 | 43.57 | 98 | 30.32 | Selected |
| Also chain indirect calls/returns | 02:59–03:04 | 3 | 28.233 | 43.02 | 97.5 | 29.88 | No convincing additional gain; reverted |
| Saved live export, final comparison | 03:23–03:27 | 3 | 29.557 | 46.06 | 113 | 32.08 | Deployment baseline |
| Selected runtime in original live shell | 03:24–03:28 | 3 | 28.705 | 43.42 | 98 | 30.84 | Deployed |

The rebuilt source control and saved live export are different runtimes. The
development rows show the optimization decisions; the last two rows establish
the change relative to what visitors previously received. Their trials ran in
alternating order: old 1, new 1, old 2, new 2, old 3, new 3.

## Final saved-live comparison

Ranges are the minimum and maximum of the three run-level measurements.

| Metric | Before: median [range] | After: median [range] | Median change |
| --- | --- | --- | --- |
| Warm integer time | 113 ms [111.5–113.5] | 98 ms [95–101.5] | 13.3% less time |
| Startup renderer CPU | 46.06 s [43.98–46.18] | 43.42 s [43.09–44.12] | 5.7% less CPU |
| Guest-ready wall time | 29.557 s [29.349–30.274] | 28.705 s [27.658–30.364] | 2.9% lower observed median |
| Scene renderer CPU | 32.08 s [30.33–32.61] | 30.84 s [30.73–31.65] | 3.9% lower observed median |
| Animation callback interval | 49.998 ms | 49.998 ms | Unchanged |
| Canvas draw time | 3 ms | 3 ms | Unchanged |

Integer time and startup CPU improve in every pair. Startup wall time is
essentially tied in the second pair; scene CPU is worse in the third pair.
Three trials per variant support the integer improvement more clearly than
the smaller, variable wall-time and scene CPU changes. Callback intervals
measure guest callbacks, not final display frame rate.

## Method and evidence

The host was the four-logical-CPU aarch64 server with about 23 GiB RAM. The
outer headless browser was Chrome 145.0.7632.0. The guest was Windows Chromium
109, x86, under Boxedwine WebAssembly. Each trial used a new outer browser and
guest profile, the same guest arguments, and a local HTTP server with range
support. Startup timing begins at runtime page navigation and ends at the
guest-ready console event. It excludes the outer library UI and WAN download
costs. OCR, screenshot processing and input acceptance happen outside timed
benchmark work.

The guest archive is byte-identical to `/home/ubuntu/kn0tzer/games/chromium.zip`:
468,348,892 bytes, SHA-256
`6420689eb3793a78d8f1a4b4e785b527fe27e5f657db845f973c29949cd28e71`.
The fixture is `tools/guest-fixtures/chromium-smoke.html`.

`integer-sum-v2` sums one million integers eight times. The final four trials
form each run's warmed measurement; checksum **1783293664** must match.
`canvas-scene-v1` uses 30 warmup and 300 measured animation callbacks, drawing
12 colored rectangles and text on a 400×160 canvas. The required final pixel
hash is **800777293**, with **7200** colorful pixels. Acceptance also includes
readable rendered text, a real mouse click, focus and exact typed text
`boxedwine works`.

Renderer CPU is measured with CDP `SystemInfo.getProcessInfo`, including the
renderer threads owned by the trial. It excludes separate OCR, server,
browser and GPU processes. Scene CPU is the difference between snapshots
around the fixed scene. All six final comparison trials pass, with identical
pixel hashes and zero page errors.

There are 23 recorded optimization-tagged trials, of which 20 are accepted.
The first three failed OCR acceptance on the old fixture's placeholder and
are retained as failures, not included in timing aggregates. An explicit
“Type here” fixture label fixed OCR targeting. All accepted trials use the
same fixture hash. This changes only the test page.

The machine-readable [run history](winbox-performance-20261018.json) preserves
every trial's timestamp, integer samples, runtime hashes, results and failure
reason, plus group medians and ranges. Raw trial records are under
`test-results/suite-opt-20261018-*`; retained runtime pairs and deployment
evidence are under `test-results/winbox-performance-20261018/`.

## Rejected changes and limits

The indirect-call/return extension passed correctness checks but did not show
a reliable additional gain over the bounded direct-branch control. It was
removed. A separate runtime asset versioning experiment had an HTTP readiness
timeout and was removed; the original loader is deployed. Later retries do
not establish the timeout's cause. Prior SIMD, LTO, stored-Wine and global
indirect-dispatch experiments were already documented as unsuccessful and
were not repeated here.

The final change does not add a guest CPU JIT. It improves the 32-bit
interpreter path used by Chromium. CPU64 compatibility was checked, but no
CPU64 speedup is claimed. Existing GL emulation warnings remain. The fixed
scene still has roughly 50 ms callback intervals.

## Validation and deployment

Completed checks: all fast repository tests (including 128 suite unit
groups), TypeScript checking, the real-browser CPU32 branch and SSE3 fixtures,
and all 327 CPU64 selftest checks. The branch fixture covers 512 conditional
branch/flag cases, a million-iteration loop, indirect calls and returns, and
termination of a guest thread busy in a loop without syscalls. Its reference
execution also passes under QEMU i386. Guest HTTP navigation, rendering,
clicking and typing passed with the selected runtime and original loader.

The clean release build records source commit
`bf256b316bb1a54cff0249224de11ecb7643e43b` and zero dirty source files. Local
implementation and tooling commits are `5e096f860` and `bf256b316`. Nothing
was pushed.

| Release artifact | SHA-256 |
| --- | --- |
| `boxedwine64.wasm` (3,269,375 bytes) | `35e230d206ef0dfd06f2766794e8187a9a1a662b2e08d3a47574a81bdcd3bd18` |
| `boxedwine64.js` | `7dcfefeda318ff904cec153530db27cb7e868a939f043f9a3f1816020810f1d6` |

At **03:31 UTC**, the runtime JS, WASM and `BUILD_INFO.json` were deployed to
`/home/ubuntu/kn0tzer/winbox/runtime/`. The shell, archives and remaining
export files were preserved. Public HTTPS asset hashes match the release.
The live browser smoke reached Chromium `about:blank` with readable output
and zero page errors. No containers were restarted. The existing one-hour
asset cache policy remains; a hard refresh fetches the new runtime.

The complete original export is preserved at
`/home/ubuntu/backups/winbox-performance-20261018/live/`. Rollback consists of
restoring `boxedwine64.js`, `boxedwine64.wasm` and `BUILD_INFO.json` from its
`runtime/` directory to the deployed `runtime/` directory. Deployment proof,
live smoke metadata and screenshots are retained in the evidence directory
listed above.

## Reproduction

These commands document the benchmark procedure; they are not a request to
repeat completed checks. Run probes serially and follow the build/probe lock
rules enforced by the repository tooling.

```sh
# Build the selected source with the pinned Emscripten 6.0.9 toolchain.
JOBS=2 tools/bw64-build.sh

# Fresh-browser and guest-profile workload, including the 300-callback scene.
CHROMIUM_ZIP=/path/to/chromium.zip LONG_SCENE=1 TAG=example-final-1 \
  node web/tests/chromium-probe.mjs

# Aggregate explicitly selected saved runs; retains rejected trials.
node tools/chromium-benchmark-summary.mjs \
  test-results/suite-opt-20261018-*/summary.json
```
