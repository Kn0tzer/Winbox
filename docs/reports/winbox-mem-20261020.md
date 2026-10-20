# Winbox guest memory performance, 2026-10-20

## Diagnosis

The baseline `perf record` attributed 13.97% of cycles to `pthread_mutex_lock`, 13.61% and 12.81% to the const and non-const `unordered_map::find` paths, and 10.23% to generic `memcpy`. Warm one-page accesses usually hit the direct-mapped DTLB, but its 64 entries conflicted across the benchmark's 256-page, 1 MiB working set. Cross-page `memcpyFromGuest` and `memcpyToGuest` also performed a locked page-map lookup for each page chunk instead of using the DTLB.

## Change

In `source/kernel/kmemory64.cpp`, the per-thread DTLB grew from 64 to 256 entries. Cross-page guest copies now resolve each page chunk through the DTLB before falling back to the existing slow path. Writes use `k64DTlbLookupForWrite`, preserving the slow path for registered code pages and their generation updates. The existing backing, zero-fill, lazy-commit, and mapping semantics remain in place.

## Native microbenchmark

Each row measures 50 million operations. The build is native x64 on an AArch64 host.

| Pattern | Baseline ns/op | After ns/op | Change |
| --- | ---: | ---: | ---: |
| Sequential 8-byte read | 10.902 | 10.712 | -1.7% |
| Sequential 8-byte write | 10.674 | 10.355 | -3.0% |
| Random 8-byte read within 1 MiB | 42.308 | 17.883 | -57.7% |
| Random 8-byte write within 1 MiB | 45.606 | 10.980 | -75.9% |
| 8-byte page-crossing read | 55.455 | 16.973 | -69.4% |
| 8-byte page-crossing write | 86.559 | 23.902 | -72.4% |

Mean across all six measured operations fell from 41.917 to 15.134 ns/op (63.9%). Raw baseline counters and command details are in [`tools/bench/mem64-results.md`](../../tools/bench/mem64-results.md); benchmark source and its build command are in [`tools/bench/mem64bench.cpp`](../../tools/bench/mem64bench.cpp).

## WASM and browser validation

The single `wasm64-mt-jit` build used source commit `4e4244f4c81d08ac5e01af616bec5519e938116a`, with no dirty files. Its WASM SHA-256 was `2647495e7c068036ade73ebefc326c69d54d94b3ee2eca5f7ab04d2f6441fceb`.

- Native x64 build: passed.
- Native CPU64 self-test: 905 passed, 0 failed.
- Memory-related browser unit groups: 4 passed, 0 failed.
- Browser `cpu64-selftest`: passed.
- Browser `memory-shared`: passed.
- Browser `jit64`: failed twice because JIT-on did not emit sample 3 within 120 seconds. The completed samples had the expected checksum. The gated retry waited for another lane's build lock; after it cleared, concurrent browser load rose above 11 (peaking at 14). Its JIT-off warm median was 3,303 ms, versus the 1,047 ms reference in [`winbox-jit-20261018.md`](winbox-jit-20261018.md), so this is not a valid performance comparison. No further retry was made.

## Not done

The browser JIT-on benchmark did not complete, so there is no valid WASM JIT-off before/after comparison under the required load gate. The native and memory-shared checks passed; the JIT64 timeout remains unresolved after the permitted attempt and retry.
