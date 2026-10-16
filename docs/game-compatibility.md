# Game compatibility (verified only — inferred is not coverage)

| Game | Path | Verified status |
| --- | --- | --- |
| Portal 1 (`hl2.exe`, 32-bit D3D9 via DXVK→Vulkan32) | `GAME=portal` | The resolve2 shim repair restores visible 1024×768 frames, but textures are corrupt and the menu is missing (`portal-resolve2-oct15`). Gameplay remains unverified. CPU32 SSE3 repair is under verification. |
| Celeste DRM-free (`Celeste.exe`, 32-bit OpenGL/FNA/Mono) | `GAME=celeste`, `GL32=1`, `MONO32_DIR` | Functional prologue acceptance passed on WASM `a97994c0725f`: menus, level loading, movement with D, jump with Space and landing (`celeste-playability-oct15/playability.json`). Frame rate and game audio were not verified. |
| 32-bit GL fixture (`trigl32.exe`) | shim in `/home/username/apps` | Colored triangle, `trigl: RESULT 0`. Verified 2026-10-13 (`gl32-real-shim-01`, `pass:true`). |
| D3D11 offscreen (`tri11_93`) | staging-texture readback | `LIT=8192`. Verified 2026-10-09. Swapchain Present shows clear color only (open). |
| Gang Beasts (x64 Unity) | `GAME=gangbeasts` | Earlier boot produced no frames. Gameplay on the repaired runtime awaits verification after Portal. |

D3D conformance (in-browser, 2026-10-09): D3D9 13/15, D3D10 6/7,
D3D11 11/13, D3D12 18/20. Full buckets:
`docs/reports/d3d-conformance-MATRIX.md`.

Rules: a game is verified only by its own probe evidence
(`test-results/<tag>/summary.json` + screenshots). Adjacent green (e.g.
mouse works, triangle renders) never verifies a game. Missing archives
(Borderlands 2, KSP, Subnautica 2, TF2) are untested, never claimed.
