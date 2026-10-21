# JIT correctness and startup results R3

Date: 2026-10-21 UTC  
Worktree: `lane/winbox-jitpar-20261021`  
Runtime: `4894444b2348eceb331b27699e6ea559af91d80aa05477d79c61c8dcf9bb30b9` (built from `6f225a295` plus six dirty source files)

## Correctness fallback

The Snake failure was not localized to a repeatable compiled block. The `jitmax` bisect appeared to isolate block 2048 once, but repeated runs passed, and candidate byte dumps did not reproduce the exit. The user-requested fallback is implemented: each new CPU64 block is checked against the interpreter on isolated guest-memory writes for its first three executions; divergent blocks are restored, rejected, and negative-cached. The default policy uses a 2,000,000,000-instruction CPU64 warm-up gate and hotness 500.

The native CPU64 self-test passed: 910 passed, 0 failed, including the isolated-memory overlay check.

Snake mapped on the final runtime in 29.977 s under the default policy. At mapping, its stats showed 2,000,000,000 warm-up instructions and zero CPU64 blocks compiled or verified, so this result confirms startup but does not exercise the verifier. Doom mapped in 30.355 s after checking 6,145 CPU64 blocks; the verifier rejected 6,144. Notepad did not map within 300 s and its log shows `notepad.exe` exiting with status 1 in `imm32.dll` startup. The fault remains unresolved.

## Mapped time

The “before” values are the supplied current-main measurements. JIT0 after-values were not rerun. JIT1 after-values use the final built runtime and default policy (`jithot=500`, `jitwarm=2000000000`, verification enabled).

| Demo | JIT0 before | JIT1 before | JIT0 after | JIT1 after |
|---|---:|---:|---:|---:|
| Notepad | 40.4 s | 54.3 s | not rerun | not mapped by 300 s |
| Snake | 29.3 s | never mapped | not rerun | 29.977 s |
| Celeste | 22.4 s | 52.7 s | not rerun | 67.365 s |
| Portal | 24.5 s | 47.0 s | not rerun | 54.924 s |
| Doom | not supplied | 45.1 s | not rerun | 30.355 s |

Against the supplied JIT0 baselines, Snake is 1.02×, Celeste is 3.01×, and Portal is 2.24×. Only Snake meets the 1.3× startup target among the comparable mapped runs. Portal mapped, but its final screenshot was blank.

## Celeste hotness sweep

All runs used JIT1. CPU64 emitted no blocks in the hotness sweep; the stats show CPU32 JIT active, so these results do not isolate CPU64 startup policy. Raising hotness did not improve mapping time.

| Policy | Click to mapped |
|---|---:|
| `jithot=80` | 51.900 s |
| `jithot=500` | 52.117 s |
| `jithot=2000` | 53.509 s |
| `jithot=8000` | 51.766 s |
| default 2B-instruction warm-up | 67.365 s |

## JIT64 throughput

The required default-verifier run of `node web/tests/suite/runner.mjs --browser --filter=jit64` did not complete. The test forces `jitwarm=0`; during JIT-on Wine startup the probe timed out waiting 120 s for `wineboot.exe`, while verification generated 104,321 block-reject log entries. The runner then hit a Playwright write-after-end error during teardown.

One diagnostic retry with `BW_TEST_URL_PARAMS='jitverify=0'` passed the suite: JIT0 warm median 2,262 ms, JIT1 warm median 752 ms, 3.008× speedup, 52,293,712 compiled-block entries, and matching checksums. This is the unverified throughput result and does not establish the required speedup with default verification enabled.

## NOT_DONE

- The faulty Snake block and emitter/decoder root cause remain unidentified; the differential verifier is the fallback, not a root-cause fix.
- Default-verifier `jit64` suite speedup is unmeasured because its JIT-on startup timed out. The 3.008× result disables verification.
- The 1.3× startup target is unmet for Notepad, Celeste, and Portal. The Celeste `jithot` sweep did not affect CPU32 JIT startup.
- Post-change JIT0 demo controls were not rerun; JIT0 values in the comparison are the supplied baselines.
## Round 4: CPU32/CPU64 switch split

The requested Round 4 matrix was not run. The single authorized WASM build attempt stopped during compilation at `source/emulation/cpu/jit64.cpp:1449` because the warm-up lambda returned inconsistent integer types. The source fix (`return 0ULL`) was committed as `bc918d412`, but Round 4 allows only one build, so there is no fresh runtime containing the CPU32/CPU64 switch split. The existing runtime predates this change and cannot answer the A/B/C comparison.

No Round 4 mapped timings, Snake verifier comparison, jit64 browser-suite result with final defaults, or fresh-build native self-test result are available. Current source candidates are `jit=1` CPU64 only, CPU32 JIT opt-in via `jit32=1`, verifier default off, and warm-up default zero; these choices are unvalidated against the requested timing matrix. The Snake engine attribution remains unconfirmed. Earlier evidence points toward CPU32 JIT because Snake is a 32-bit guest and CPU64 emitted no blocks during the hotness sweep, but that does not replace the requested same-build comparison.
