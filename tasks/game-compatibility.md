# Game compatibility and Portal performance

Goal: playable rendering, keyboard/mouse/controller input, audio, save/reload,
and measured startup/frame rate for the named games. A window or nonzero alpha
does not count as gameplay. Record failures and the tested build hashes.

| Target | Requested path | Current archive |
| --- | --- | --- |
| Portal 2 | 32-bit Vulkan | Missing; portal.zip contains Portal 1 (hl2.exe, x86) |
| Gang Beasts | 32-bit Unity | gangbeasts.zip contains x64 Unity |
| Borderlands 2 | 32-bit Unreal / D3D9 | Missing |
| Celeste | 32-bit OpenGL / .NET | Original uses FNA; new DRM-free copy references XNA 4, forwarded to FNA by Wine Mono |
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
python3 tools/rootfs64/build_vulkan_shims.py
GAME=celeste GL32=1 GLTRACE=1 node web/tests/game-probe.mjs
GAME=celeste GAME_ZIP=/home/ubuntu/kn0tzer/games/celeste-drmfree.zip GL32=1 MONO32_DIR=/home/ubuntu/scripts/mono32 node web/tests/game-probe.mjs
GAME=gangbeasts node web/tests/game-probe.mjs
GAME=portal node web/tests/game-probe.mjs
```

Builds and probes serialize through BUILD_PROTOCOL.md. No GitHub push or
unrelated service restart is needed. Commit each verified increment locally.

Verified October 13:

- Fresh Vulkan shim binaries match their source hashes; stale source/binary
  replacement is rejected by the probe and Wine64 rootfs packaging.
- Portal passed its former query-results crash point with the rebuilt 32-bit
  library. Complete presentations remain black. Captured first-pass replay
  shows scene geometry with corrupted textures; subsequent passes are under
  investigation. No new boot-to-menu or gameplay FPS claim is justified.
- The Wine base archive's Mesa library beat the later OpenGL overlay because
  ZIP mounts keep their first matching file. The probe now puts the shim in
  `/home/username/apps`, the first library search path. The real 32-bit Wine
  fixture displayed its colored triangle and returned `trigl: RESULT 0` in
  `test-results/gl32-real-shim-01`.
- The supplied DRM-free Celeste is packaged separately as `celeste-drmfree.zip`.
  With the selected shim it reaches `CELESTE : 1.4.0.0` / `BEGIN LOAD`; earlier
  tests blocked at Wine's pixel-format query. Gameplay still requires proof.
