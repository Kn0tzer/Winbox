# Game compatibility and Portal performance

Goal: playable rendering, keyboard/mouse/controller input, audio, save/reload,
and measured startup/frame rate for the named games. A window or nonzero alpha
does not count as gameplay. Record failures and the tested build hashes.

| Target | Requested path | Current archive |
| --- | --- | --- |
| Portal 2 | 32-bit Vulkan | Missing; portal.zip contains Portal 1 (hl2.exe, x86) |
| Gang Beasts | 32-bit Unity | gangbeasts.zip contains x64 Unity |
| Borderlands 2 | 32-bit Unreal / D3D9 | Missing |
| Celeste | 32-bit OpenGL / .NET | Present; wine-mono overlay packaged |
| Kerbal Space Program | 64-bit Unity Vulkan | Missing |
| Team Fortress 2 | 64-bit D3D9 | Missing |

Ordered slices:

1. **Shared 32/64-bit OpenGL (M)**: replace the 32-bit no-op bridge with the
   existing renderer; decode pointer arrays by guest width. Verify memory
   adapter tests, WASM build, GL fixtures and actual Celeste. Stage the shim
   only after runtime verification; preserve the original archive.
2. **Repeatable game probes (M)**: one isolated harness with build provenance,
   progressive logs, screenshots, explicit renderer overlays, and clean
   cancellation. Portal's gate is recognizable menu text followed by a timed
   sample of WebGPU-completed frames. Gameplay is tracked separately.
3. **DXVK Unity path (M)**: launch Gang Beasts with native d3d11/dxgi and the
   Vulkan shim instead of Wine's failed GL fallback. Diagnose device, shader,
   memory and presentation failures; verify real menu/gameplay.
4. **Portal performance (M)**: retain historical measurements, then compare
   complete frame replay under identical settings. Profile CPU, range loading,
   GPU encoding, and readback. Keep measured wins and record rendering defects.
5. **Future-game ingestion (M)**: detect PE bitness and supported renderer from
   archive contents; keep 32/64-bit Vulkan/OpenGL/D3D9 fixtures passing. Test
   named missing games as their archives arrive; never claim them verified.

Commands:

```sh
npm test
npm run typecheck
bw64-build
bash tools/rootfs64/build-libgl32.sh
GAME=celeste GL32=1 GLTRACE=1 node web/tests/game-probe.mjs
GAME=gangbeasts node web/tests/game-probe.mjs
GAME=portal node web/tests/game-probe.mjs
```

Builds and probes serialize through BUILD_PROTOCOL.md. No GitHub push or
unrelated service restart is needed. Commit each verified increment locally.
