# KMemory64 native microbenchmark

Baseline: 2026-10-20, before MMU changes. Host is AArch64; benchmark links the native `x64` build (`BOXEDWINE_GUEST_X64`, `BOXEDWINE_MULTI_THREADED`) and precommits a 1 MiB guest range before timing. Each row is 50,000,000 operations. Pages are touched before the measured loops.

| Pattern | Baseline ns/op | After DTLB ns/op | Change |
|---|---:|---:|---:|
| Sequential 8-byte read | 10.902 | 10.712 | -1.7% |
| Sequential 8-byte write | 10.674 | 10.355 | -3.0% |
| Random 8-byte read within 1 MiB | 42.308 | 17.883 | -57.7% |
| Random 8-byte write within 1 MiB | 45.606 | 10.980 | -75.9% |
| 8-byte page-crossing read | 55.455 | 16.973 | -69.4% |
| 8-byte page-crossing write | 86.559 | 23.902 | -72.4% |

The baseline `perf stat` run over all six patterns (50M each) counted 37,029,821,269 cycles, 108,456,464,371 instructions (2.93 IPC), 22,692,242,794 branches, 26,821,086 branch misses (0.12%), and 287,292,754 cache misses in 12.782 s. A sampled `perf record` attributed 13.97% of cycles to `pthread_mutex_lock`, 13.61% and 12.81% to the const/non-const unordered-map find paths, and 10.23% to generic `memcpy`. This matched the timing gap between warm sequential accesses and direct-mapped TLB conflict/page-cross cases.

The fix grows the DTLB from 64 to 256 entries (the 256 pages in the benchmark's 1 MiB working set) and consults it for each page chunk in cross-page copies. This removes repeated page-map locks/hashes on warm random and page-cross access. Overall mean time across the six patterns fell from 41.917 to 15.134 ns/op (63.9%).
