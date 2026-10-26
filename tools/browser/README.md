# Browser build and runtime assets

The browser app is built from `web/` and the Emscripten emulator. `dist/` is the
generated export. Wine/rootfs archives and Slang are restored from the immutable
`runtime-assets-20261013` GitHub release; `runtime-assets.json` pins their sizes
and SHA256 hashes. Naga and zip.js come from `pnpm-lock.yaml`.

## Fresh checkout

Use Node 22, pnpm 9.15.0, Python 3.11 or newer, GNU Make, and Emscripten 6.0.9.
Activate the SDK first or set `EMSDK` to its directory; the build wrapper also
checks `/opt/emsdk`.

```sh
pnpm install --frozen-lockfile
pnpm bootstrap
pnpm build
pnpm typecheck
pnpm test
```

`pnpm bootstrap` verifies cached assets before downloading anything, then stages
`web/vendor/` from the npm packages and the pinned Slang files. To restore from
an archived local copy:

```sh
python3 tools/browser/bootstrap.py --local /path/to/browser-runtime-20261013.tar
node tools/browser/stage-vendor.mjs
```

`python3 tools/browser/bootstrap.py --check` only verifies local files. A corrupt
download or archive entry fails before any cached asset is replaced. Keep the
versioned release asset unchanged; a runtime update needs a new release tag and
manifest, even when the ZIP filenames stay the same.

## Build and inspect

`tools/bw64-build.sh` owns the build lock, verifies the WASM magic, records hashes
and the source commit, and exports the current app. It also maintains the
generated `web/runtime/` mirror required by the packaging and provenance tests.
`JOBS=2` is the default; set it explicitly for another build host.

```sh
tools/bw64-build.sh --check
pnpm build:export  # reuse a compiled pair with matching recorded provenance
```

`--export-only` preserves the source provenance of the existing binary pair.
It never attributes an old binary to the current source commit. Live builders
retain their locks regardless of age; incomplete locks and malformed probe slots
stop a competing build. Investigate the owner before removing a lock manually.

Built-in game archives are optional. Set `WINE_GAMES_DIR` to their directory or
place them in `web/games/`. `WINE_GAMES_COPY=1` copies them into the export;
otherwise the local export uses a symlink. Game archives are excluded from Pages.

For a local server with the required cross-origin isolation headers:

```sh
node --input-type=module -e 'import { serve } from "./tools/browser/serve.mjs"; await serve("dist", 8077);'
```

Open `http://127.0.0.1:8077/`. `WINE_BUILD_OUT` selects another generated output
directory. The runtime export includes the renderer protocol, WGSL helpers,
SPIR-V pre-pass, and shader compilers; vendor copies are generated from their
canonical inputs rather than maintained twice.

### Static host headers

The exported files need cross-origin isolation for threaded WASM. The
production `/winbox/*` Caddyfile matcher must send
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` on both the launcher and runtime
paths. If deploying to a static host that reads a `_headers` file, include
equivalent rules for `/*` and `/runtime/*`; the runtime iframe must receive the
same isolation policy as the launcher. These response headers are configured
by the host and are not emitted as files by `build.mjs`.

## Native verifiers

```sh
bash scripts/build-jit-wasm.sh --selftest-only
python3 tools/vkd3d/tests/run-native.py
make -C project/linux test JOBS=2
project/linux/Build/Test/boxedwine 0 0 1
make -C project/linux x64 JOBS=2
tools/run_x64_smoke.sh
make -C project/linux x64-cache JOBS=2
BW64_JIT=1 BW64_JIT_VERIFY=1 project/linux/Build/X64HeadlessCache/boxedwine --x64-selftest
```

`make x64` creates a fresh `Build/X64Headless/` interpreter build for CPU64,
KMemory64, syscall64 and ELF64 diagnostics. It compiles with
`BOXEDWINE_DISABLE_UI` and excludes native OpenGL/UI backends; the browser
WASM build and Vulkan bridge harness keep their graphics checks. The CPU32
`make test` target retains its existing host JIT coverage. `make x64-cache`
uses a separate `Build/X64HeadlessCache/` directory for the existing decoded
block executor and checks real execution, including IMUL overflow flags.

The Linux builds use the system SDL2, OpenGL, curl, OpenSSL, and zlib development
packages, including GLU. On Ubuntu:

```sh
sudo apt-get install build-essential libsdl2-dev libgl1-mesa-dev libglu1-mesa-dev libcurl4-openssl-dev libssl-dev zlib1g-dev
```

Native runner scripts use `BW64_BIN` when set, then the Linux headless X64
diagnostic binary, then the Release binary. Pass a binary path explicitly when
testing a particular build. `tools/x64test/run-static-elf-suite.sh` additionally
requires Zig; it cross-compiles real x86_64 static ELF programs.

## Pages

The workflow builds this app, runs the complete fast suite and native JIT/Vulkan
checks, then runs the Notepad window/canvas smoke gate. `prepare-pages.py dist`
splits large Wine ZIPs into 90 MiB same-origin parts; the launcher already falls
back to their manifests when a whole ZIP returns 404. A service worker supplies
the isolation headers on Pages. The local export retains whole ZIPs.

The published export must remain below the [GitHub Pages site size limit](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits).
The workflow uploads `dist/` through the [official Pages artifact/deployment flow](https://github.com/actions/deploy-pages#usage).
