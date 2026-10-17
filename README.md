# Boxedwine64

Windows userspace in the browser: 32-bit and 64-bit CPU emulation, Wine,
OpenGL/Vulkan bridges, WebGPU/WebGL2 rendering, files, input and audio.
Upload a folder to automatically detect and start its Windows executable, or
choose a demo in one click. The launcher follows the Microapps design standard,
with light/dark themes, a local folder library and optional advanced settings.

[Live server demo](https://kn0tzer.work.gd/wine/) ·
[Test catalog](web/tests/CATALOG.md) · [Runtime map](docs/architecture.md)

## Build from a clone

Use Node 22, pnpm 9.15.0, Python 3.11+ and the pinned Emscripten SDK listed
in [the build instructions](tools/browser/README.md).

```sh
pnpm install --frozen-lockfile
pnpm bootstrap             # verified runtime inputs + npm vendor staging
./tools/bw64-build.sh       # compile emulator, export browser app, record hashes
pnpm test
pnpm typecheck
```

Generated exports live in ignored `dist/`. Runtime archives and the Slang
compiler are SHA256-pinned GitHub release assets; the bootstrap tool restores
them rather than storing copies in Git. `pnpm build:export` refreshes the page export using an already compiled
matched WASM/JavaScript pair. `pnpm build` and the full wrapper compile the
emulator too. For an offline restore, use
`python3 tools/browser/bootstrap.py --local /path/to/browser-runtime-20261013.tar`.

Serve the export with the isolation headers required by WASM threads:

```sh
node --input-type=module -e 'import { serve } from "./tools/browser/serve.mjs"; await serve("dist", 8077);'
```

Open `http://127.0.0.1:8077/`. Optional built-in game archives come from
`WINE_GAMES_DIR` or local `web/games/`. The demo catalog uses hosted archives at
`https://kn0tzer.work.gd/games/`; uploaded folders stay in browser storage.
Automatic detection selects x86/x64 Windows, known engine defaults and an entry
point. It does not make every Windows app compatible with the emulator.

## Checks

```sh
pnpm test                                        # legacy + suite unit checks
pnpm typecheck
node web/tests/launcher-ui.mjs                   # launcher, storage failure and layout checks
node web/tests/launcher-real.mjs                 # real Notepad + streamed folder auto-launch
bash scripts/build-jit-wasm.sh --selftest-only     # real native/JIT/WASM checks
python3 tools/vkd3d/tests/run-native.py            # real Vulkan bridge/shim checks
make -C project/linux test JOBS=2
project/linux/Build/Test/boxedwine 0 0 1           # full CPU32 table
node --test tools/regression/wine32-exception.test.mjs
```

The [catalog](web/tests/CATALOG.md) maps the full requested stack and both
architectures to unit, native, guest and browser checks. It names the gaps:
source/registration checks establish their assertions; correct rendering,
complete API behavior, OPFS persistence and game playability need runtime
proof. All previous suite, legacy and native tests remain. The browser
suite now runs real guest fixtures; its dependencies are described in the
[suite guide](web/tests/suite/README.md).

To run an existing game probe:

```sh
node web/tests/suite/runner.mjs --game=portal      # prerequisites before acceptance
GAME=portal node web/tests/game-probe.mjs
GAME=celeste GAME_ZIP=/path/to/celeste.zip GL32=1 MONO32_DIR=/path/to/mono32 node web/tests/game-probe.mjs
```

Use [the build/probe protocol](BUILD_PROTOCOL.md). Results must record the
build hashes; a mapped window or successful Present does not prove visible
frames. Fresh evidence goes in ignored `test-results/`.

## Recorded game status

The latest saved game measurements are dated 2026-10-13:
Portal reaches a window and presents black frames; Celeste passes graphics
initialization, then stalls in OLE marshaling. Neither is established as
playable. A GL32 triangle rendered correctly. See
[game compatibility](docs/game-compatibility.md) and
[historical reports](docs/reports/README.md) for the precise evidence and
D3D conformance failures. Maintenance checks do not refresh these game claims.

## Where to change things

| Path | Purpose |
| --- | --- |
| `source/`, `include/`, `platform/` | Emulator and retained Linux/SDL host support |
| `project/emscripten/`, `project/linux/` | Browser build and native diagnostic/test builds |
| `web/` | Browser app, renderers, protocol decoder, storage and tests |
| `tools/rootfs64/`, `tools/rootfs32/` | Guest shims, rootfs/prefix builders and staging |
| `tools/dxvk/`, `tools/vkd3d/` | Pinned vendored sources, guest probes and bridge tests |
| `tools/spirvfix/`, `tools/cpu-diff/`, `tools/regression/` | Shader transforms and reusable regression tools |
| `web/tests/diagnostics/` | Preserved former root-level probe scripts |
| `testdata/`, `web/tests/suite/fixtures/` | Committed binary/data fixtures required by tests |
| `docs/` | Architecture, protocol design and curated historical reports |
| `dist/`, `web/runtime/`, `web/vendor/`, `test-results/` | Generated local files, excluded from Git |

`master` is the maintained runtime. `archive/jit-emitter` preserves useful
advanced JIT source, generators and tests that predate the current runtime
fixes; see [the archived experiment](docs/jit-experiment.md). DXVK/vkd3d
source snapshots are ordinary files with exact provenance in `SOURCES.json`,
so a clone needs no submodule setup.

## Publication and recovery

[GitHub](https://github.com/Kn0tzer/Boxedwine64) is the sole repository remote.
The Pages workflow compiles the current browser app, runs checks and packages
same-origin rootfs chunks. Local server deployment is an explicit copy of a
verified `dist/` export into `/home/ubuntu/kn0tzer/wine/`, after a backup.

The 2026-10-13 maintenance backup is
`/home/ubuntu/backups/boxedwine64-maintenance-20261013/`: all previous Git refs
in `all-refs.bundle`, complete source/worktree contents in `workspaces.tar`,
and the archived raw probe evidence in `test-results/`. The history rewrite
map is copied there as `commit-map`; use it to resolve commit IDs cited in
older BB conversations. Git history retains
source changes and tests; generated exports, archives, dumps and old host
projects are filtered out. Future builds and probes should leave Git clean.
