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

## Focused lock-free hit follow-up, 2026-10-21

The sequential-hit path now lives inline in `include/kmemory64.h`. For 1/2/4/8-byte accesses wholly within a page, the code checks the same-page bound, indexes the calling thread's 256-entry direct-mapped DTLB, compares the memory-owner and guest-page tags, loads and compares one acquire generation value, tests the cached permission and scalar-fast bit, then performs a fixed-size `memcpy`. Native AArch64 disassembly shows the compiler lowers the final copy to one `LDR` for a read or one `STR` for a write. There is no mutex, atomic RMW, hash/tree lookup, block-page registry call, or out-of-line KMemory64 call on a successful hit. Misses and cross-page accesses fall back to the existing helpers; they may lock and consult the sparse page map. Generation invalidation is moved to mapping/protection changes, while registered code-page writes stay on the slow path.

`perf stat` for one 50M sequential loop counted 1,618,442,903 instructions and 419,345,260 cycles for reads (32.37 instructions, 8.39 cycles, 6.08 branches/op); writes counted 1,718,417,224 instructions and 467,463,595 cycles (34.37 instructions, 9.35 cycles, 6.08 branches/op). Branch misses were 100,104 reads and 98,675 writes, about 0.03% of branches. Totals include loop address generation and harness setup. `perf annotate` confirms the instruction sequence above; `perf report` places 96.34% of sampled read cycles in the benchmark's inlined `main`.

| Pattern | After DTLB ns/op | After inline hit ns/op | Change this round |
| --- | ---: | ---: | ---: |
| Sequential 8-byte read | 10.712 | 2.998 | -72.0% |
| Sequential 8-byte write | 10.355 | 3.204 | -69.1% |
| Random 8-byte read within 1 MiB | 17.883 | 7.342 | -58.9% |
| Random 8-byte write within 1 MiB | 10.980 | 3.397 | -69.1% |
| 8-byte page-crossing read | 16.973 | 18.267 | +7.6% |
| 8-byte page-crossing write | 23.902 | 24.634 | +3.1% |

The six-pattern mean improved 34.1%, from 15.134 to 9.974 ns/op. The page-crossing changes remain within the 10% keep threshold. The memory unit groups passed 4/4, and the native CPU64 self-test passed 905/905. The test fixture now extracts the actual header DTLB declaration and insertion function to check generation-stale insertion.

The page map remains a sparse `std::unordered_map`; I skipped replacing it in this round. That change would touch mapping, cloning, shared-file aliasing, protections, and backing lifetime. Warm scalar hits no longer consult the page map; misses still do.

## WASM follow-up, 2026-10-21

The single build for this round completed successfully. `BUILD_INFO.json` records source `4397d28a84f8e39ab1dce79743f6ba3298a43f1b`, five dirty files, and WASM SHA-256 `d0a0e83cfda83eea17ff0e053966e7697aea2a3b72b6f1f2c5346c791f08c8bb` (6,210,807 bytes). Browser `cpu64-selftest` and `memory-shared` each passed.

The `jit64` probe ran with `BW_TEST_JIT64_MODES=0` and `BW_TEST_URL_PARAMS='jit=0'`. A separate build delayed its start; once the guest ran, load rose above the required 4 threshold (observed at 12.00), so I stopped it after two samples (4,262 ms and 5,744 ms). Those values are invalid and are not used as the after result. The reference warm median remains 1,047 ms; there is no valid after measurement. After stopping that run, load stayed above 4 while other lanes held the available probe slots and ran timed work, so I did not start another attempt. Since `jit64.test.mjs` supports JIT-off mode, the `jitbench` fallback is unnecessary.

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
