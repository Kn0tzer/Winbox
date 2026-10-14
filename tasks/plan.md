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
5. **Performance:** record Portal gameplay FPS and frame times, build and
   browser identity, GPU adapter, resolution and the measurement interval.
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

Current uncertainty: the loading-label race has a passing ASan regression
and repeated audio runs; complete browser-suite repetition remains required.
Celeste's title now renders after the framebuffer repair, but its first
interaction stalled the input pump. A targeted nonblocking-pump regression
passes; real-game verification of that repair is pending. Portal and Gang
Beasts gameplay and Portal's achievable frame rate remain unverified.
