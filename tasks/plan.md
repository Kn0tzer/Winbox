# Runtime repair and game acceptance

The user authorized this order in BB thread `thr_7k42yen7hk`, continuing
`thr_euc85c2hcr`: fix failing tests, verify Celeste and Portal gameplay,
verify Gang Beasts gameplay, then optimize the stack toward Portal above
30 FPS. These are the accepted requirements and phase boundaries.

## Acceptance

1. **Runtime tests:** fast tests, type checking, native CPU32/CPU64 and
   Vulkan bridge checks, and the full browser suite pass. Preserve and
   explain intermittent failures; a successful retry does not close them.
2. **Celeste:** the real archive loads and draws visible game content;
   enter a level and verify movement and jumping with before/after captures.
3. **Portal:** the real archive draws its menu and a loaded map; verify
   movement, camera control and an interactive game action with captures.
4. **Gang Beasts:** the real archive draws its menu and a match; verify
   character movement and grabbing with captures.
5. **Performance:** use the current server at 1024×768 as the baseline.
   Record Portal gameplay FPS and frame times, build and browser identity,
   GPU adapter, resolution and the measurement interval.
   Compare changes under identical conditions and retain measured wins.
   Above 30 FPS is the target; a menu or submission count cannot prove it.

## Method and boundaries

Use small subsystem regressions to explain failures before expensive game
boots. Follow the existing kernel, guest-fixture and browser-probe layouts;
keep fixes local to their owner, for example
`web/tests/suite/unit/<subsystem>-<case>.test.mjs`. Test each fix and commit
verified increments locally. Do not push or restart unrelated containers.
Do not remove failing tests or weaken their asserted behavior.

The stack is C++ guest CPU/kernel emulation compiled with Emscripten,
Wine/DXVK guest libraries, and JavaScript WebGPU/WebGL presentation. Runtime
builds must obey `BUILD_PROTOCOL.md`; browser evidence must include hashes.
An isolated sanitizer export is diagnostic evidence, not a release build.

Commands: `pnpm test`, `pnpm typecheck`, `make -C project/linux test -j2`,
`project/linux/Build/Test/boxedwine 0 0 1`,
`project/linux/Build/X64Headless/boxedwine --x64-selftest`,
`python3 tools/vkd3d/tests/run-native.py`, `pnpm build`, and
`node web/tests/suite/runner.mjs --browser`. Game preflights use
`runner.mjs --game=<game>` and game acceptance retains an isolated browser
through the existing probes. Save raw evidence in ignored `test-results/`
and failure backups in `/home/ubuntu/backups/`.

Runtime checkpoint (2026-10-15): the fast suite passes 120 unit files
and 73 legacy checks, with the existing skip unchanged. Native CPU32 and
CPU64 pass 602 and 327 checks after the executor-exit repair. Type checking
and seven Vulkan harness groups pass.

The first complete browser run on WASM `bd004303a645` passed 18 of 19
files. The mouse test consumed late duplicate Wine log lines; sequence-aware
waits now pass in both architectures (`5e978310`). A subsequent full run
reproduced allocator corruption, so the browser gate remains open.

Further regressions exposed cleanup outside the serialized teardown in
ordinary exit and exit_group. MT cleanup now waits until the executor has
left its run loop, and the last executor publishes process termination
(`33edab1e`). The extended fork fixture checks final ordinary-thread exits
and parent waitpid under QEMU and in the real browser; both pass on WASM
`68956b49a713`. Browser repetition then exposed an independent Wine graphics
startup failure. The launcher now waits for a successful
window acknowledgement from the desktop helper before accepting later apps
(`30f8e18f`). Three consecutive D3D11/audio pairs pass. All fast checks and
packaging/provenance checks pass. The next full browser run stopped at a
Wine32 server connection failure in GL32 startup. Two forced filesystem
regressions then exposed incomplete directory publication and duplicate
inodes during concurrent file creation. Both repairs pass their focused
regressions (`be66e10b`). Native CPU32/CPU64, the 118-file fast suite, type
checking and three untraced GL32 repeats pass on `a97994c0725f`. The fresh full
browser run passes all 19 files. Evidence and derived architecture-specific
game prerequisites are in `test-results/acceptance-oct15/`.

Celeste functional acceptance passes in its prologue: the real archive,
menus, level, horizontal movement with D, jumping with Space, and landing
are captured under `test-results/celeste-playability-oct15/`. The jump rises
70 pixels relative to the same stationary terrain. Game audio and frame
rate were not measured. Portal is now booting for map/input acceptance;
Gang Beasts and Portal performance remain outstanding. Preserve the
requested phase order and the recorded scope of each result.

Portal's first real run submitted frames but remained black. A captured
frame (`portal-playability-oct15/black-frame.json.gz`) reproduces this;
replaying its early scene pass draws geometry, and inserting the missing
framebuffer resolve restores visible final pixels. The live log records
`gipa MISS: vkCmdResolveImage2`. Both guest shims now translate core/KHR
resolve2 regions into the existing ordered resolve ABI, without changing
the runtime WASM. Cross-compiled 32/64-bit tests use the official Vulkan
structs and check offsets, layers, full-width image handles and every
region; both fail before the fix and pass afterward. The 120-file fast
suite and all seven native Vulkan harness groups pass. A fresh Portal
run uses the repaired shim and a content-validated archive with stored
VPK entries to avoid repeated outer-ZIP decompression. Rendering, menu,
map and input acceptance remain pending; replay pixels alone do not pass
the game.
