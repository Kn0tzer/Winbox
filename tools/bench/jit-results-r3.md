# JIT correctness and startup results R3/R4

Date: 2026-10-22 UTC

Worktree: lane/winbox-jitpar-20261021

WASM build: b69a939f3ae83149174eac0639cdac55257b4f2a
WASM SHA-256: a9f0e2b88e0825f052a37ad1e31f4c42d0f31d8ad945308f64b02d4c2ef1666a

## Switches and defaults

The public switches now split the engines: **jit=0** disables CPU32 and CPU64; **jit=1** enables CPU64 and disables CPU32; **jit32=1** enables CPU32 as well (or by itself when jit is absent); **jit32=0** forces CPU32 off. **jitverify** is off by default and jitverify=1 enables three differential checks for each new CPU64 block. **jitwarm** defaults to zero. The hotness threshold remains 500.

The native CPU64 self-test passed: 910 passed, 0 failed.

## Snake correctness

The earlier Snake startup failure was not reproducible: the first JIT0 attempt failed in imm32.dll, but a JIT0 retry mapped in 31.222 s; CPU64-only mapped with the default verifier off in 61.729 s and with explicit jitverify=0 in 154.941 s; CPU64+CPU32 mapped in 118.546 s. The explicit verifier-off run reported zero verification checks. These runs identify neither engine as the correctness cause; the measured difference is startup time, and the explicit retry ran after the load wait timed out at 8.12.

The earlier jitmax bisect candidate at block 2048 did not reproduce consistently, and no guest-byte dump or interpreter self-test isolated a faulty block. The differential verifier remains available as an opt-in fallback; it is not the default.

## Mapped time

“Before” values are the supplied current-main measurements. A/B/C are same-build Round 4 results: A = both engines off, B = CPU64 only, C = CPU64 plus CPU32. Every summary reported the same WASM SHA above.

| Demo | JIT0 before | JIT1 before | A: both off | B: CPU64 only | C: CPU64 + CPU32 |
|---|---:|---:|---:|---:|---:|
| Notepad | 40.4 s | 54.3 s | 46.879 s | 75.715 s | 168.957 s |
| Snake | 29.3 s | never mapped | 31.222 s* | 61.729 s | 118.546 s |
| Celeste | 22.4 s | 52.7 s | 38.259 s | 52.462 s | 86.620 s |
| Portal | 24.5 s | 47.0 s | 53.854 s | 65.971 s | 123.773 s |
| Doom | not supplied | 45.1 s | 66.381 s | 77.627 s | 86.568 s |

*Snake's first A attempt did not map within 300 s; the retry shown here mapped. The explicit B run with jitverify=0 also mapped in 154.941 s.

CPU64-only B/A ratios were 1.62× for Notepad, 1.98× for Snake, 1.37× for Celeste, 1.22× for Portal, and 1.17× for Doom. The observed timings miss the 1.3× target for Notepad, Snake, and Celeste. Celeste and Portal mapped but their final canvas screenshots were blank. Doom, Snake, and Notepad ended with nonblank screenshots.

Load readings varied across the matrix. The wrapper logs load before and after each command; those samples include time queued behind builds and probe slots and are not readings at browser launch. The explicit Snake verifier-off retry followed the one-shot load wait, which ended at 8.12 after 9 minutes (threshold 3). Treat cross-run timing ratios as indicative rather than controlled.

The earlier Celeste jithot=80/500/2000/8000 sweep ran while CPU32 JIT was active and CPU64 emitted no blocks, so it did not select a CPU64 startup threshold. No post-split hotness sweep was run; the default remains 500.

## CPU64 throughput

node web/tests/suite/runner.mjs --browser --filter=jit64 passed: 1 passed, 0 failed. Final-default warm medians were 2,245 ms with JIT0 and 881 ms with JIT1, a 2.548× speedup. All samples matched checksum 8b527b4986bdaccf; JIT1 executed 3,334 compiled blocks. This clears the 1.8× speedup target.

## NOT_DONE

- The old Snake failure's faulty block and emitter/decoder root cause remain unidentified; the failure did not reproduce in the Round 4 retries.
- CPU64-only startup did not meet the 1.3× target on three demos. Queue and load variation prevent a clean attribution, and a controlled low-load rerun plus a post-split hotness sweep remain undone.
- Celeste and Portal mapped but ended on blank canvases; their visual/gameplay state was not repaired here.
