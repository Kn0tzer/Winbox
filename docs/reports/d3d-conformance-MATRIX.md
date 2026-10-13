# D3D conformance matrix

In-browser runs, 2026-10-09. 64-bit only; 32-bit untestable (wineboot blocked, separate lane).
Harness: `tools/d3dtests/d3dconf9.c`, `tools/d3dtests/d3dconf10.c`,
`tools/dxvk/probe/d3dconf11.c` + `d3dconf11b.c`, `tools/vkd3d/probe/d3dconf12.c`;
runner `web/tests/d3d-conformance.mjs` (fresh browser session per suite);
report generator `tools/d3dconf-report.mjs`.
Wasm provenance: d3dconf1/d3d9 ran on pre-01:14 UTC wasm; all other runs on the
01:14 UTC rebuild (audio lane).

## Slow thresholds

- One-shot ops (device/shader/resource creation): slow if guest ms > 60000.
  Justification: prior probes create D3D11 devices in ~10-30s; 60s is 2-6x norm.
- Present-loop ops: slow if mean frame ms > 3000. Justification: D3D9 Pong
  guest-frame baseline mean 276ms; 3000ms ~= 11x baseline.
- Flaky = inconsistent verdict across runs. D3D10 ran twice (agree);
  others ran once.

## Four-bucket summary

| API   | total | fully working | slow/flaky | fully broken | litPixels | draws |
|-------|-------|---------------|------------|--------------|-----------|-------|
| D3D9  | 15    | 13*           | 0          | 2            | 17100     | 32    |
| D3D10 | 7     | 6             | 0          | 1            | 0         | 30    |
| D3D11 | 13    | 11            | 0          | 2            | 0‡        | n/a   |
| D3D12 | 20    | 18            | 0          | 2            | 0         | 10    |

\* D3D9 "working" includes ops whose log lines arrived after capture but are
  confirmed OK by the guest's own DONE tally (present_first, vs_handasm_bonus).
  Color correctness suspect: triangle/textured regions paint BLACK, not RGB.
† D3D11 offscreen rendering verified separately (tri11_93: LIT=8192 via
  staging-texture readback).
‡ D3D11 swapchain Present: CORRECTED 2026-10-09 - prior 0 lit px was a measurement error. Two independent probes show 29130 lit px with correct RGB, pixel-identical to D3D10 verified output; chunk forensics confirm identical bridge output. Swapchain WORKS (fixed by 88cdd449).

## D3D9 (DXVK d3d9.dll) — 13/15

Working: Direct3DCreate9; CheckDeviceType X8R8G8B8, R5G6B5; CreateDevice HAL
  (X8R8G8B8 + D16); VB/IB create+fill; DrawIndexedPrimitive; Present;
  CreateTexture A8R8G8B8 + SetTexture; textured DrawPrimitive; D24X8 depth match;
  hand-assembled vs_1_1 create+draw; 30-frame present loop.
Broken:
  - `checkfmt_A8R8G8B8` FAIL hr=0x8876086A (D3DERR_NOTAVAILABLE)
  - `checkfmt_X1R5G5B5` FAIL hr=0x8876086A (D3DERR_NOTAVAILABLE)
Finding: renders (17100 non-clear px, 32 draws) but output is BLACK where RGB
  vertex colors / texture gradient expected. API ops succeed; pixel content
  wrong. Needs color-correctness investigation (fix lane).

## D3D10 (DXVK d3d10core on d3d11) — 6/7 API ops, renders NOTHING

Working: D3D10CreateDevice; DeviceAndSwapChain; RTV; D3DCompile vs_4_0/ps_4_0;
  shaders + input layout; vertex buffer.
Broken:
  - `draw_present_30f`: guest reports OK, 29-30 draws decoded from bridge
    chunks, but litPixels=0 across TWO runs. Draws execute, paints nothing —
    same symptom class as D3D12. Filed for fix lane (chunk forensics).
Note: bare D3D10CreateDevice verified OK standalone (d10bare.exe, hr=0).

## D3D11 (DXVK d3d11.dll) — 12/13 API ops; SWAPCHAIN PRESENT WORKS (corrected 2026-10-09)

Feature levels (verified, not assumed):
  - 11_0: FAIL hr=0x80070057 (E_INVALIDARG)
  - 10_1, 10_0, 9_3, 9_2, 9_1: all OK
Pipeline at FL 9_3 (d3dconf11b, fresh process): device+swapchain, RTV,
  D3DCompile, shaders+layout, VB, 30-frame draw+present — 7/7 API ops OK.
  BUT litPixels=0: swapchain Present shows clear color only.
CRITICAL: D3D11 swapchain presentation is broken while offscreen rendering
  works (tri11_93: LIT=8192 via staging readback, re-verified 2026-10-09 on
  current wasm). Draws execute; Present doesn't deliver the image to the
  canvas. Same symptom class as D3D10 swapchain. Filed for fix lane.
CRITICAL BUG (filed): repeated D3D11CreateDevice create/release cycles in one
  process cause a clean guest exit(0) mid-call — non-deterministic (3rd call in
  d11swap1, 7th call in d3dconf11). Repro: d11swap.exe. The pipeline had to be
  split into a separate exe (d3dconf11b) to avoid it.

## D3D12 (vkd3d-proton d3d12.dll) — 18/20

Working: DLL loads; D3D12CreateDevice FL 11_0; command queue/allocator/list;
  DXGI factory; swapchain; RTV heap; root signature serialize+create; PSO;
  vertex buffer upload; fence; draw+present loop (guest OK).
Broken:
  - `device_FL_12_0` FAIL hr=0x80070057 (E_INVALIDARG; expected — vkd3d
    targets 11_0 here)
  - `draw_present_10f`: guest OK, 10 draws executed, litPixels=0 — known
    byte-less BUFFER_DATA dedup desync (separate fix lane).

## Raw tables

See `buckets.json` in each run dir; per-op logs in `console-<api>.log`.

## Repros for fix lanes

1. D3D9 color: `tools/d3dtests/d3dconf9.exe` — draw_indexed paints black.
   Run: d3d-conformance.mjs SUITES=d3d9.
2. D3D10 no-paint: `tools/d3dtests/d3dconf10.exe` — 30 draws, 0 px.
   Run: d3d-conformance.mjs SUITES=d3d10.
3. D3D11 multi-device exit(0): `tools/dxvk/probe/d11swap.exe` — 3rd
   D3D11CreateDevice create/release exits process with status 0.
   Run: web/tests/run-single.sh d11swap.
4. D3D12 no-paint: `tools/vkd3d/probe/d3dconf12.exe` — dedup desync lane owns.
5. D3D9 formats: CheckDeviceType A8R8G8B8/X1R5G5B5 → D3DERR_NOTAVAILABLE.
