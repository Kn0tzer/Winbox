# Winbox CPU JIT validation, 2026-10-18–19

The combined local build provides selectable 32-bit and 64-bit CPU JITs through
`jit=1`. `jit=0` disables both. They remain opt-in: the 32-bit Chromium candidate
has a substantial cold-start cost and has not demonstrated a performance gain
over the earlier deployed optimized interpreter. These measurements preceded the
[Settings rollout](winbox-jit-ui-20261019.md).

The 32-bit startup failure came from incompatible x87 representations. The
combined runtime selects SoftFloat's 80-bit state (`KSystem::useF64=false`), while
the imported x87 emitter uses a cached double. A new guest precision witness,
`((1 + 2^-63) - 1) * 2^63`, passes under QEMU i386 and fails after the old JIT
compiles it. The fixed backend delegates x87 instructions to the interpreter in
80-bit mode. Integer and SSE instructions still execute in generated modules;
double-cache builds retain native x87 emission.

CPU64's original 512-entry direct-mapped module cache repeatedly evicted hot
code, and its 4096-slot generation hash let unrelated data writes invalidate
code. The cache now holds 8192 entries with four-way replacement. Exact page
registration gives code pages stable generation slots, including inline checks
after generated stores. Registry exhaustion falls back to a conservative global
generation. Concurrent registration completes data-TLB invalidation before a
compiler uses its slot. The block interpreter retains those slots too, avoiding
a registry lookup after every interpreted instruction. Shared 32-bit blocks also
initialize each entering CPU's flag helpers before compiled/pending early returns.
That initialization applies to already compiled blocks; cold blocks retain their
normal hotness threshold. A regression test catches premature compilation and
repeated compiler entry when JIT is disabled.

The validation record includes:

| Check | Result |
| --- | --- |
| Native decoder/cache/emitter assertions | 606 passed |
| Real emitted-module tests under Node | 259 passed |
| Native code invalidation, registry exhaustion and concurrent registration | 54 passed in each of two separately compiled binaries: instruction interpreter and block executor |
| Browser CPU32 branches, termination and SSE3 | Passed with JIT requested |
| Browser CPU32 growth and 80-bit precision regression | Failed before the fix; passed afterward with nonzero generated-code entries and retirements |
| Browser CPU64 self-tests | 332 passed; generated code reported 121 entries and 332 retired instructions |
| Real guest Chromium109 | Startup, mouse, keyboard, arithmetic checksum and Canvas scene passed with ordinary `jit=1` and with `jit=0` |
| Real Wine64 PE64 workload | Matching checksums with JIT off/on; nonzero generated entries and retirements with JIT on |
| Repository checks | 132 unit groups and 104 legacy tests passed; one optional shader browser E2E was skipped by the fast runner |
| TypeScript | `npm run typecheck` passed |
| CPU64 lifetime | Destruction passed with block execution enabled and disabled |

The CPU64 self-test fixture explicitly sets hotness to zero because its cases
execute once. Production keeps the threshold of eight. Without that test setting,
the suite stayed in the interpreter and correctly failed its execution-evidence
assertions. A separate Wine64 benchmark uses production hotness.

The final Chromium run used the existing archive and smoke fixture, with added
keyboard-event logging. Its arithmetic checksum was `1783293664`; the 30-frame
Canvas scene hash was `1470154170`.
It recorded 475,069,032 generated-module entries and no installation failures.
These counters cover the whole guest; they do not attribute the JavaScript loop
itself to generated code. Detailed retirement counting was enabled separately in
the precision regression test, rather than adding that cost to this timing run.

| Chromium measurement | Combined build, JIT off | Combined build, JIT on |
| --- | ---: | ---: |
| JavaScript readiness | 43.467 s | 84.070 s |
| Warm arithmetic, median of final four samples | 187 ms | 256.5 ms |
| Startup renderer CPU | 64.69 s | 145.54 s |

These are individual observations from the same artifact, not a repeated
alternating campaign. Both modes use the combined build's safe-memory policy.
The disabled run reported zero generated entries and no compiled modules.
The earlier deployed-interpreter reference was 28.705 s readiness, 98 ms warm
arithmetic and 43.42 s startup CPU; see the three-pair report
[`winbox-performance-20261018.md`](winbox-performance-20261018.md). The combined
build also needs optimization with JIT disabled. These observations support
keeping this build experimental and JIT opt-in.

Earlier candidate runs missed keyboard input or stalled before rendering; a
JIT-disabled control also stalled before rendering. After the threshold repair,
the final disabled-JIT run completed with 89,297 disabled-compiler entries,
compared with 315,745,637 in that failed control. Independent Win32 keyboard
tests passed on both architectures, and the Wine32 font-metrics test passed with
JIT enabled. The final Chromium run observed every key-down, keypress,
before-input, text update and key-up for `boxedwine works`, using the original
60 ms host typing interval. No text was seeded or inserted by script. The
threshold regression above is fixed, but these results do not isolate the cause
of every earlier stall or establish long-run application stability.

The Wine64 paired diagnostic on the final application build produced the following
samples; every sample matched the independent ELF64 checksum `8b527b4986bdaccf`.

| Wine64 PE64 benchmark | First sample | Three warm samples | Warm median |
| --- | ---: | --- | ---: |
| JIT off | 1037 ms | 1002, 1086, 1047 ms | 1047 ms |
| JIT on | 813 ms | 819, 883, 826 ms | 826 ms |

This final pair supports functional execution and an observed gain on this
workload; it does not establish a general speedup. It reported 77,356
installed modules, 107,308,501 generated entries, 473,897,983 generated retirements,
and zero installation failures. Emit time was 2.144 s and installation time
19.605 s, accumulated across Wine startup and the workload. The original stalled
run had compiled over one million modules before Wine finished startup.

An intermediate exact-registry version slowed the interpreter warm median to
2538 ms by looking up page identities after each instruction. Keeping stable
slots in interpreter blocks restored it to 956 ms; the earlier apparent 3×
same-build speedup was largely that temporary interpreter regression.

The final application artifact was built from source commit `07cf22eef`:

- WASM: `7b4ab076519bc0e152d057f58815953fc6496b23883f164ee1a3824cb290153e` (5,624,505 bytes).
- Runtime JS: `6592b2b5c0b8e9c81c4471b0038680e8dd1b13bdecf892f7f972d33df7779931`.

All executable changes were committed before testing. The three pending files
reported in the test build's provenance were documentation. The final export
is identified by the executable hashes above, independently of documentation
commit IDs.

Raw artifacts live under `test-results/suite-jit32-hotness-fixed`,
`test-results/suite-jit32-hotness-control`,
`test-results/suite-cpu32-jit-growth`, `test-results/suite-cpu64-selftest`, and
`test-results/suite-jit64-release-validation`. Failed attempts and native logs are in
`/tmp/winbox-jit-continue-20261018`.

Build with `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2 bash tools/bw64-build.sh`.
Read execution counters through `bw64_jit_stats_json_text()`; the field meanings
and sampling limits are in [`winbox-jit64-metrics.md`](winbox-jit64-metrics.md).
