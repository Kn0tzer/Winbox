# D3D conformance matrix — test-results/d3d-conformance-d3dconf2

Generated from in-browser runs. 32-bit: untestable (wineboot blocked, separate lane).

## Slow thresholds

- One-shot ops (device/shader/resource creation): slow if guest-measured ms > 60000. Justification: prior probes create D3D11 devices in ~10-30s; 60s is 2-6x the observed norm.
- Present-loop ops: slow if mean frame ms > 3000. Justification: D3D9 Pong guest-frame baseline mean 276ms (test-results/frametime-pong2/); 3000ms ~= 11x baseline.
- Flaky = inconsistent verdict across runs. All suites ran once; flakiness not assessed unless a rerun disagrees.
- Render ops (draw/present) with draws executed but litPixels=0 count as broken ("paints nothing").

## Four-bucket summary

| API | total tested | fully working | slow/flaky | fully broken | skipped | litPixels | draws |
|-----|--------------|----------------|------------|--------------|---------|-----------|-------|
| d3d9 | 12 | 10 | 0 | 2 | 0 | 17100 | 32 |
| d3d10 | 7 | 6 | 0 | 1 | 0 | 0 | 29 |
| d3d11 | 6 | 5 | 0 | 1 | 0 | n/a | 0 |
| d3d12 | 17 | 15 | 0 | 2 | 0 | 0 | 10 |

## Per-operation table

| API | op | result | hr | ms | bucket | note |
|-----|----|--------|----|----|--------|------|
| d3d9 | d3d9_create | OK | 0x00000000 | 0 | working |  |
| d3d9 | checkfmt_X8R8G8B8 | OK | 0x00000000 | 0 | working |  |
| d3d9 | checkfmt_A8R8G8B8 | FAIL | 0x8876086A | 0 | broken | hr=0x8876086A |
| d3d9 | checkfmt_R5G6B5 | OK | 0x00000000 | 0 | working |  |
| d3d9 | checkfmt_X1R5G5B5 | FAIL | 0x8876086A | 0 | broken | hr=0x8876086A |
| d3d9 | vb_create_fill | OK | 0x00000000 | 0 | working |  |
| d3d9 | ib_create_fill | OK | 0x00000000 | 0 | working |  |
| d3d9 | draw_indexed | OK | 0x00000000 | 0 | working |  |
| d3d9 | tex_create_set | OK | 0x00000000 | 0 | working |  |
| d3d9 | draw_textured | OK | 0x00000000 | 0 | working |  |
| d3d9 | depth_D24X8_match | OK | 0x00000000 | 0 | working |  |
| d3d9 | present_30f | OK | 0x00000000 | 0 | working |  |
| d3d10 | device_10_0 | OK | 0x00000000 | 0 | working |  |
| d3d10 | device_swapchain | OK | 0x00000000 | 0 | working |  |
| d3d10 | create_rtv | OK | 0x00000000 | 0 | working |  |
| d3d10 | compile_vs_ps_4_0 | OK | 0x00000000 | 0 | working |  |
| d3d10 | shaders_inputlayout | OK | 0x00000000 | 0 | working |  |
| d3d10 | vb_create | OK | 0x00000000 | 0 | working |  |
| d3d10 | draw_present_30f | OK | 0x00000000 | 0 | broken | draws executed (29) but litPixels=0 — paints nothing |
| d3d11 | fl_11_0 | FAIL | 0x80070057 | 0 | broken | hr=0x80070057 |
| d3d11 | fl_10_1 | OK | 0x00000000 | 0 | working |  |
| d3d11 | fl_10_0 | OK | 0x00000000 | 0 | working |  |
| d3d11 | fl_9_3 | OK | 0x00000000 | 0 | working |  |
| d3d11 | fl_9_2 | OK | 0x00000000 | 0 | working |  |
| d3d11 | fl_9_1 | OK | 0x00000000 | 0 | working |  |
| d3d12 | load_d3d12 | OK | 0x00000000 | 0 | working |  |
| d3d12 | load_dxgi | OK | 0x00000000 | 0 | working |  |
| d3d12 | getprocaddrs | OK | 0x00000000 | 0 | working |  |
| d3d12 | device_FL_12_0 | FAIL | 0x80070057 | 0 | broken | hr=0x80070057 |
| d3d12 | create_window | OK | 0x00000000 | 0 | working |  |
| d3d12 | dxgi_factory2 | OK | 0x00000000 | 0 | working |  |
| d3d12 | swapchain_hwnd | OK | 0x00000000 | 0 | working |  |
| d3d12 | qi_swapchain3 | OK | 0x00000000 | 0 | working |  |
| d3d12 | rtv_heap | OK | 0x00000000 | 0 | working |  |
| d3d12 | getbuffer_rtvs | OK | 0x00000000 | 0 | working |  |
| d3d12 | serialize_rootsig | OK | 0x00000000 | 0 | working |  |
| d3d12 | create_rootsig | OK | 0x00000000 | 0 | working |  |
| d3d12 | create_cmdalloc | OK | 0x00000000 | 0 | working |  |
| d3d12 | create_cmdlist | OK | 0x00000000 | 0 | working |  |
| d3d12 | vb_upload | OK | 0x00000000 | 0 | working |  |
| d3d12 | create_fence | OK | 0x00000000 | 0 | working |  |
| d3d12 | draw_present_10f | OK | 0x00000000 | 0 | broken | draws executed (10) but litPixels=0 — paints nothing |
