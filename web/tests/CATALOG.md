# Tests and diagnostics

Run commands from the repository root. `npm test` runs the legacy Node tests
and the discovered suite unit files. Browser, native CPU and guest-program
checks have separate entrypoints. A green fast suite proves its assertions;
it does not prove that every supported API or a game works.

## Commands

```sh
npm test
DEMO=notepad JIT=0 node web/tests/demo-matrix.mjs
node web/tests/suite/runner.mjs --unit --filter=spirvfix
bash scripts/build-jit-wasm.sh --selftest-only
make -C project/linux test
project/linux/Build/Test/boxedwine 0 0 1
bash tools/cpu-diff/run.sh
python3 tools/vkd3d/tests/run-native.py
python3 tools/regression/baldi-a3-pixel-test.py
python3 tools/regression/baldi-a3-readback-test.py
python3 tools/regression/baldi-a3-c6-test.py
node --test tools/regression/wine32-exception.test.mjs
```

The Linux binary takes `[start-index] [count] [workers]`; zero count means
the remaining entries. CPU differential tests require the cross compiler
and QEMU described in `tools/cpu-diff/METHOD.md`. The full
`scripts/build-jit-wasm.sh` also requires an activated Emscripten SDK and
builds an isolated interpreter/JIT WASM comparison.

## Coverage and limits

The architecture columns name existing checks, not complete coverage of
each API. **Static** checks inspect source, packaging or recorded reports.
**Behavioral** checks execute functions with fixtures or fake devices.
**Native** checks execute compiled CPU/helper harnesses. **Browser** probes
run guest programs and capture runtime evidence.

| Stack | 32-bit checks | 64-bit checks | Evidence and remaining gaps |
| --- | --- | --- | --- |
| Boxedwine CPU, decoder, MMU | Linux `source/test/` binary; `cpu32-*`, `elf-loader-mmap` | `tools/cpu-diff/run.sh`; `scripts/build-jit-wasm.sh`; `tools/x64test/run-static-elf-suite.sh <binary>`; `cpu64-*` | Native instruction and emitted-WASM execution; opcode/entirety guards mostly inspect source. Differential scope excludes documented control-flow/string/SIMD forms. |
| Wine and Win32 | `wine32-boot-regression.mjs`, `win32min32-probe.mjs`; `tools/win32test/`; `wine32-stack` | `game-probe.mjs`, `diagnostics/b64-probe.mjs`; `cpu64-win32-opfs-wine` | Browser boot/PE probes, native saved-stack checks and static configuration guards; recovered exception fixture controls check historical WASM builds. |
| Vulkan bridges, vkwebgpu ICD | Native `testVk32Trap` witness and `tools/vkd3d/tests/run-native.py`; `vk32-*` | `vkcov-probe.mjs`, `vkcov-probe-matrix.mjs`; `tools/vkcov/vkcov-analyze.py`; `vk-bridge-coverage`, `vulkan-*` | Real shared bridge/native shim checks, trap/layout/source guards and guest API probes. The native test also checks Vulkan32 allocation/map outputs and failed-allocation cleanup; it has no GPU. |
| OpenGL and GL bridge | Native GL32 trap witness; `GAME=gl32 node web/tests/game-probe.mjs` with its fixture archive; `gl32-shared-renderer` | `glcov-probe.mjs`; `tools/glcov/glcov-analyze.py`; native helpers in `tools/regression/` | Guest browser probes and native pixel/readback execution; shim/entirety guards are static. Shared renderer IDs do not prove every GL32 call. |
| D3D9 and DXVK | `d3d9-32bit-smoke.mjs`, `d3d9-32-verify.mjs`, `tri9-32-probe.mjs` | `SUITES=d3d9 node web/tests/d3d-conformance.mjs`; `dxvk-pixels.mjs`; `tools/d3dtests/` | Guest browser/API/pixel evidence; recorded matrix includes failures. Both architecture DLLs and guest fixtures must be available. |
| D3D10 and DXVK | Shared CPU/Vulkan32 guards; no complete D3D10 32-bit runner | `SUITES=d3d10 node web/tests/d3d-conformance.mjs`; `tri10-probe.mjs`, `tri10swap93-probe.mjs` | 64-bit browser probes and static `d3d10-contract`; full 32-bit behavior remains a gap. |
| D3D11 and DXVK | Shared CPU/Vulkan32 guards; no complete D3D11 32-bit runner | `SUITES=d3d11 node web/tests/d3d-conformance.mjs`; `tri11-93-stats.mjs` | Guest/API/pixel checks plus static contract guards; feature levels and presentation require separate measurements. |
| D3D12 and vkd3d | Shared CPU/Vulkan32 guards; no dedicated D3D12 32-bit runner | `SUITES=d3d12 node web/tests/d3d-conformance.mjs`; `d3d12-pixels.mjs`; `tools/vkd3d/probe/` | Guest vkd3d/DXVK/bridge probes; `d3d12-vkd3d-dxvk` is static. Full 32-bit behavior remains a gap. |
| SPIR-V, WGSL, Naga | Shared host translator; separate guest layout checks | `spirvfix.test.mjs`, `dxvk-pixel-shaders.test.mjs`, `naga-rt.test.mjs`; `spirvfix-*`, `naga-f32-max`, `wgsl-uniform-arrays` | Behavioral fixtures and real Naga execution alongside static toolchain guards. `testdata/spirv/` and suite fixtures are required inputs. |
| WebGPU, WebGL2, canvas | Shared host renderer through GL32/Vulkan32 probes | `vkwebgpu*.test.mjs`, `graphics.test.mjs`; `vkwebgpu-*`, `v2-*`; `dxvk-pixel-replay.mjs` | Behavioral codecs/fake GPU and selected shader validation. Real canvas/WebGL2 presentation needs browser evidence. |
| Input | `input32-probe.mjs` | `input3-probe.mjs`, `input3-setupapi-verify.mjs`, `tri9pong-probe.mjs`, `d3d9-fpsinput-probe.mjs`; `input-keyboard-full`, `xwire-*` | Guest events plus static/compiled XWire checks; actual game mouse-look and every key/button path remain separate claims. |
| Audio | `tools/winmm32/winmm.c`; shared device guards | `audio-dsptone.mjs`, `audio-verify.mjs`; `audio-audible-output` | Historical browser tone/capture probes; many require missing `/tmp/audioprobe*` fixtures. Queue calls do not prove audible bytes or waveOut completion. |
| OPFS, VFS, packaging | `files.test.mjs`; `sparse-memfs`, `packaging-prefix`, `vfs-socket-isolation`, `wine32-stack` | Same host filesystem; `cpu64-win32-opfs-wine`, `zip-*`, `provenance-artifacts` | Behavioral sparse/ZIP checks and static/package guards. Durable OPFS persistence, quota errors and full guest access need real browser checks. |

Flat filenames live in `web/tests/`; short unit names live in
`web/tests/suite/unit/` and end in `.test.mjs`. Guest/browser commands need
matched runtime assets, guest executables, bridge libraries and the relevant
DXVK/vkd3d DLLs. Files available on disk and generated files tracked by Git
are distinct questions.

## Browser suite limitations

`node web/tests/suite/runner.mjs --browser` discovers every suite probe.
All classes now use actual runtime/guest fixtures. Input checks compile a
Win32 witness for both CPU modes and require guest key-down/key-up and
RAWINPUT deltas. Audio checks actual DSP output PCM. GL32 and D3D12 inspect
visible pixels; Celeste's boot check requires its `GAME DISPLAYED` log and
colored canvas pixels. Interactive game playability needs separate evidence.
The GL32 framebuffer fixture checks offscreen rendering, independent read/draw
bindings, guest object names and presentation of the window's backbuffer.

The comprehensive D3D classes use the real runtime API and stage guest
executables. Their names still cover more than their current assertions:
D3D11 primarily runs `tri11_93.exe`, and D3D12 runs `tri12.exe` plus
`tri12draw.exe`, including a compositor screenshot pixel assertion. Use
`d3d-conformance.mjs` and its guest conformance programs for the broader
per-operation matrix. Record actual runtime, GPU backend and fixture hashes
with browser results. Passing `game-capability-matrix` cannot guarantee that
a game is playable.

## Preservation and evidence

All original suite, legacy, native and diagnostic tests are retained. The
25 former repository-root `.mjs` scripts now live in
[diagnostics](diagnostics/README.md), preserving names and environment controls.
Flat probes stay in place because tests/build tools reference them.
[Regression tools](../../tools/regression/README.md) were recovered from the
former output directory.

Historical Markdown evidence lives in [docs/reports](../../docs/reports/README.md).
The static D3D guard reads its matrix there. Fresh logs, screenshots,
generated programs and captured chunks belong in ignored `test-results/`.
Raw captures remain useful inputs to `REPLAY_DIR=... node
web/tests/dxvk-pixel-replay.mjs`, `dxvk-pixel-shaders.mjs`,
`replay-game-frame.mjs`, `decode-chunks.mjs` and coverage/timing analyzers.
Retain or archive them with their provenance.

The maintenance backup is under
`/home/ubuntu/backups/boxedwine64-maintenance-20261013/`. Reports describe
measurements at their stated dates, including failures; they are not fresh
certification of the current checkout.
