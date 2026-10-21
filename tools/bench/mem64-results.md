# KMemory64 native microbenchmark

Baseline: 2026-10-20, before MMU changes. Host is AArch64; benchmark links the native `x64` build (`BOXEDWINE_GUEST_X64`, `BOXEDWINE_MULTI_THREADED`) and precommits a 1 MiB guest range before timing. Each row is 50,000,000 operations. Pages are touched before the measured loops.

| Pattern | Baseline ns/op | After DTLB ns/op | After inline hit ns/op | Change this round |
|---|---:|---:|---:|---:|
| Sequential 8-byte read | 10.902 | 10.712 | 2.998 | -72.0% |
| Sequential 8-byte write | 10.674 | 10.355 | 3.204 | -69.1% |
| Random 8-byte read within 1 MiB | 42.308 | 17.883 | 7.342 | -58.9% |
| Random 8-byte write within 1 MiB | 45.606 | 10.980 | 3.397 | -69.1% |
| 8-byte page-crossing read | 55.455 | 16.973 | 18.267 | +7.6% |
| 8-byte page-crossing write | 86.559 | 23.902 | 24.634 | +3.1% |

The six-pattern mean fell from 15.134 to 9.974 ns/op this round (-34.1%). Page-crossing cases regressed by less than the 10% keep threshold.

The baseline `perf stat` run over all six patterns (50M each) counted 37,029,821,269 cycles, 108,456,464,371 instructions (2.93 IPC), 22,692,242,794 branches, 26,821,086 branch misses (0.12%), and 287,292,754 cache misses in 12.782 s. A sampled `perf record` attributed 13.97% of cycles to `pthread_mutex_lock`, 13.61% and 12.81% to the const/non-const unordered-map find paths, and 10.23% to generic `memcpy`. This matched the timing gap between warm sequential accesses and direct-mapped TLB conflict/page-cross cases.

## Inline-hit counters and annotation, 2026-10-21

`perf stat` on the isolated 50M sequential loops counted 1,618,442,903 instructions and 419,345,260 cycles for reads (32.37 instructions, 8.39 cycles, 6.08 branches per op); writes counted 1,718,417,224 instructions and 467,463,595 cycles (34.37 instructions, 9.35 cycles, 6.08 branches per op). Branch misses were 100,104 reads and 98,675 writes (0.03% of branches). The process totals include benchmark-loop address arithmetic and setup.

`perf annotate` shows the read hit checks the same-page bound, indexes the thread-local DTLB, compares owner and page tags, performs one acquire `LDAR` of the generation and compares it, tests `READ | FAST_SCALAR_READ` (`0x10001`), loads the page pointer, then performs one qword `LDR`. The write hit tests `WRITE | FAST_SCALAR_WRITE` (`0x40002`) and performs one qword `STR`. The calls to `memcpyFromGuest`/`memcpyToGuest` are miss/bounds fallbacks and are not taken on a hit. There is no mutex acquisition, hash/tree lookup, code-page registry lookup, or `memcpy@plt` on the hit path. `perf report` attributed 96.34% of sampled read cycles to the benchmark's inlined `main`; the hit contains no out-of-line KMemory64 call.

The fix grows the DTLB from 64 to 256 entries (the 256 pages in the benchmark's 1 MiB working set) and consults it for each page chunk in cross-page copies. This removes repeated page-map locks/hashes on warm random and page-cross access. Overall mean time across the six patterns fell from 41.917 to 15.134 ns/op (63.9%).
