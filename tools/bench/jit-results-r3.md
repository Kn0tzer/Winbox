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

The source split is in `146339b31`: `jit=0` disables both engines, `jit=1` enables CPU64 only, and `jit32=1` additionally enables CPU32. `jitverify` defaults off and `jitwarm` defaults to zero. The native x64 build completed, the single WASM64 build completed from `b69a939f3` at 2026-10-21 21:10 UTC, and the native CPU64 self-test passed 910/0.

### Same-build mapped results so far

The matrix uses A = both JITs off, B = CPU64 only, and C = CPU64 + CPU32. All runs used `MAPPED_TIMEOUT_S=300`.

| Demo | A: JIT0 | B: CPU64 only | C: CPU64 + CPU32 |
|---|---:|---:|---:|
| Notepad | 46.879 s | 75.715 s | 168.957 s |
| Snake | Not mapped by 300 s | 61.729 s (verifier default) | 118.546 s |
| Celeste | pending | pending | pending |
| Portal | pending | pending | pending |
| Doom | pending | pending | pending |

Notepad B is 1.62× A and C is 3.61× A, so CPU64-only has already missed the 1.3× target for this demo. The C run was delayed behind a shared build and ran while other probes were active; its timing is confounded. Snake A logged `wine64 snake.exe` exiting with status 1 in `imm32.dll` even though both engines were disabled. Snake B (CPU64 only) and C (CPU64 + CPU32) both mapped, so these runs do not attribute the A failure to either JIT engine. The Snake A retry and explicit B `jitverify=0` comparison are still pending.

The first three Notepad runs and Snake A/B were launched after load readings below 3, but some were queued behind shared builds and probes, so the actual run-time load was not consistently controlled. Snake C was launched at load 9.76 while probes were active. The measurements above are recorded as observed and should be treated with that limitation.

### Defaults and remaining checks

The candidate settings remain CPU64-only for `jit=1`, CPU32 opt-in, verifier off, and warm-up zero. The native self-test passed; the final-default browser `jit64` suite has not yet been run on this build. The A/B/C matrix, Snake verifier comparison, one Snake A retry, and Celeste/Portal/Doom mapping results remain in progress.
