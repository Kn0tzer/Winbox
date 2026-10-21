# JIT fix R2 results

Date: 2026-10-21 UTC  
Worktree: lane/winbox-jitfix-20261020b

## Regression

The Wine64 startup regression was the gamepad merge 59985d9c1. It was reverted in 39200a84c; the parent thread reported that notepad JIT1 then mapped in 144 s and JIT0 in 50 s. The requested mapped-only Doom and Snake checks also passed: Doom 153.194 s, Snake 86.768 s.

## B2: CPU64 hotness

Startup samples use demo-matrix mapped-only mode. JIT0 is unaffected by the hotness value. The startup stats identify CPU64 as the engine compiling during Wine64 startup: CPU32 emitted zero blocks.

| CPU64 hotness | notepad JIT0 | notepad JIT1 | CPU64 blocks compiled at map | Result |
|---:|---:|---:|---:|---|
| 8 baseline (parent measurement) | 50 s | 144 s | — | 2.88× |
| 24 (3×) | 46.477 s | 130.953 s | 39,859 | 2.82× |
| 80 (10×, selected) | 44.354 s | 84.435 s | 20,082 | 1.90× |

The 10× candidate cut measured JIT1 startup by 41% against the parent baseline and compiled about half as many blocks as the 3× candidate. It does not meet the requested 10% startup target: JIT1 remains about 1.90× JIT0.

Standalone jit64.test.mjs passed with checksum 8b527b4986bdaccf in all modes:

| CPU64 hotness | JIT0 warm ms | JIT1 warm ms | JIT speedup |
|---:|---:|---:|---:|
| 8 baseline | 1,646 | 892 | 1.85× |
| 24 | 2,417 | 806 | 3.00× |
| 80 selected | 1,697 | 894 | 1.90× |

The 10× JIT1 result is 0.2% slower than the baseline JIT1 result, within the 3% limit, and remains faster than JIT0. The 24× pair had higher shared load during the test, so its JIT0 wall time is not comparable to the other rows.

## B1: delegated CPU32 blocks

jitbench32.test.mjs passed with stable checksums. Baseline first low-load pair: JIT0 591 ms, JIT1 324 ms. With B1 enabled, two JIT1 warm medians were 237 ms and 337 ms; the test started each mode below load 4. The stats reported rejectDelegated 289 and 294 respectively. JIT0 repeats were 1,382 ms (load 3.45→5.11) and 618 ms (load 3.95→3.59), showing substantial host-load variation; B1 does not affect JIT0.

## Remaining

B3 was not attempted because the three-build limit was used by the 3× threshold, 10× threshold, and B1 evaluation. The final JIT1 notepad startup target of within 10% of JIT0 remains unmet.

## C1–C3: post-merge startup cost

Merged `fix/winbox-chromium-20261018` at `3f4b8650d` and performed one post-merge build. The mapped-only notepad runs used the same merged build and began below load 4.

| Post-merge startup | clickToMapped |
|---|---:|
| JIT0 | 39.931 s |
| JIT1 | 76.034 s |
| Ratio / gap | 1.90× / 36.103 s |

At JIT1 map, CPU64 had hot threshold 80 and compiled, emitted, and instantiated 21,226 blocks. It recorded 94,145,798 block entries and 16,531,225 cold skips. `emitUs` was 590,815 (0.591 s); `instantiateUs` was 5,711,369 (5.711 s); combined compile and instantiate time was 6,302,184 µs (6.302 s), 17.5% of the startup gap. CPU64 recorded 6,444 emit rejects, zero instantiation failures, and top rejected opcodes `0F 28` (3,816), `FF` (3,686), `0F 11` (3,382), `0F 29` (3,129), and `89` (2,513).

CPU32 JIT was enabled but compiled, entered, and instantiated zero blocks. Its `rejectDelegated` count was zero and all other reported fallback counters were zero. CPU64 compile and instantiate time therefore does not explain most of the extra startup time. Per the conditional scope, the further hotness sweep and startup grace gate were skipped; the startup result also remained above 1.3×, so C3 was not eligible.

The post-merge jit64 suite passed with matching checksums. Warm medians were JIT0 1,537 ms and JIT1 831 ms, a 1.850× speedup at hot threshold 80. This remains above the 1.8× floor. The 1.3× notepad startup goal remains unmet.
