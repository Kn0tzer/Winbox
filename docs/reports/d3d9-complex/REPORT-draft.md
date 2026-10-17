# D3D9 Complex Verification — report (DRAFT, pending probe results)

Date: 2026-10-09. Branch: integrate/schema-v2 @ 5b0cdb74 (dirty tree; dist wasm built 2026-10-09 01:14:38 UTC incl. uncommitted emitImageData klog fix).

## Scope
Verify the D3D9 path with real, complex content — not just Pong — in-browser:
rendered gameplay AND working input, plus frame-time measurement.

## Programs under test
- `tools/d3dtests/scene9.exe` — 28 draws/frame, 3 textures, alpha blending, 120 frames. Renders; no gameplay input (WM_CLOSE only). Probe: `web/tests/d3d9-scene9-probe.mjs`, results in `test-results/dxvk-d3d9-complex-scene9-1/`.
- `tools/dxvk/tri9/fpsinput.exe` — FPS-style input test: WASD movement, relative-mouse FPS aim, left-click shoot, Escape quit. Probe: `web/tests/d3d9-fpsinput-probe.mjs`, results in `test-results/dxvk-d3d9-complex-fpsinput-1/`.

## Baseline for comparison (Pong, test-results/frametime-pong2/, 2026-10-08)
- guest frame: mean 276.1 ms, median 281.3 ms, p95 384.5 ms (n=301) — 3.6 fps
- page render: mean 12.8 ms, median 12.3 ms
- present latency: mean 27.4 ms
- litPixels: 2324
NOTE: Pong baseline was measured on the 21:31 UTC dist; scene9/fpsinput run on the 01:14 UTC dist (emitImageData klog fix included). Comparison is directional; exact build recorded per probe.

## Results (four-bucket format: total / passing / slow-or-flaky / broken)

### scene9.exe — rendering
PENDING

### scene9.exe — frame time
PENDING

### fpsinput.exe — rendering
PENDING

### fpsinput.exe — input (per device)
PENDING

### fpsinput.exe — frame time
PENDING

## New bugs found
(none yet)

## Assumptions
- Single-probe slot discipline observed; probes may have queued behind other lanes' probes.
- Headless SwiftShader: relative numbers, not absolute.
