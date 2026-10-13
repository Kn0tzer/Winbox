# Bridge API Coverage — Summary (2026-10-09)

Systematic in-browser coverage of the Vulkan (vk64), 32-bit Vulkan (vk32), and
OpenGL (GL64) bridges. See `tools/vkcov/METHOD.md` for methodology.

## vk64 (Vulkan, 64-bit)

Probe: `test-results/bridge-coverage-vkcov-run2/` via `tools/vkcov/vkcov.exe`
(269 DLL exports; 3 `vk_icd*` loader-negotiation functions excluded as out of scope).

| Bucket | Count |
|---|---|
| 1. Total handlers tested | 266 |
| 2. Fully working | 252 (170 functional + 82 by-design -8 stubs) |
| 3. Slow or flaky (>=10ms/call, or inconsistent across 2 runs) | 0 |
| 4. Broken, stub, or guarded | 14 (12 guarded by d1713623, 2 stub-contract violations) |

Raw table: `matrix-vk64.md`. See MATRIX.md for full details.

## GL64 (OpenGL, 64-bit)

Probe: `test-results/bridge-coverage-glcov-run*/` via `tools/glcov/glcov.exe`
(306 bridge cases; run with `BW64_GLVERSION=33` for the full surface).

| Bucket | Count |
|---|---|
| 1. Total handlers tested | 306 |
| 2. Fully working | 5 (<10ms, GL_NO_ERROR) |
| 3. Slow or flaky | 191 (190 slow ≥10ms, 1 flaky) |
| 4. Broken or stub | 110 (14 GL errors, 96 stubs) |

Raw table: `matrix-gl64.md`. glGetAttachedShaders fix committed as 2e02a285 (306/306 zero errors).

## vk32 (Vulkan, 32-bit) — static record

In-browser testing blocked on 32-bit wineboot (Wine 8.0 bug: find_env_var bad params;
CPU emulator verified correct; workaround pending 5-boot verification).
Static: 157 shim wrappers, 83 with bridge implementations in `vk32Bridge`,
74 explicit `-8` stubs. All 157 bucket 4 (unimplemented/unreachable in practice).

Raw table: `matrix-vk32.md`.

## Notable findings for fix lanes

- 12 vk64 copy/blit/resolve crashers → guarded by d1713623 (no longer crash).
- glGetAttachedShaders → fixed by 2e02a285.
- GL64 bridge ~47x slower per call than vk64 (34ms vs 0.72ms avg) — systematic overhead.
- vk32 untestable (32-bit Wine 8.0 bug; workaround pending verification).
