# KMemory64 native microbenchmark

Baseline: 2026-10-20, before MMU changes. Host is AArch64; benchmark links the native `x64` build (`BOXEDWINE_GUEST_X64`, `BOXEDWINE_MULTI_THREADED`) and precommits a 1 MiB guest range before timing. Each row is 50,000,000 operations. Pages are warm/touched before the measured loops.

| Pattern | Baseline ns/op |
|---|---:|
| Sequential 8-byte read | 10.902 |
| Sequential 8-byte write | 10.674 |
| Random 8-byte read within 1 MiB | 42.308 |
| Random 8-byte write within 1 MiB | 45.606 |
| 8-byte page-crossing read | 55.455 |
| 8-byte page-crossing write | 86.559 |

`perf stat` over all six patterns (50M each): 37,029,821,269 cycles; 108,456,464,371 instructions (2.93 IPC); 22,692,242,794 branches; 26,821,086 branch misses (0.12%); 287,292,754 cache misses; 12.782 s elapsed. A second unprofiled run was within 2% on every pattern.

No implementation changes are included in these baseline numbers.
