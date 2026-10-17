# Recovered regression tools

These scripts formerly lived under `test-results/`. They are maintained
source; generated files and logs are disposable output.

```sh
python3 tools/regression/baldi-a3-pixel-test.py
python3 tools/regression/baldi-a3-readback-test.py
python3 tools/regression/baldi-a3-c6-test.py
node --test tools/regression/wine32-exception.test.mjs
```

The Python tests extract current helpers from
`source/opengl/gl64bridge.cpp`, compile with `g++`, and run 12 pixel, seven
readback/error and 28 format/helper vectors. Builds use temporary directories
and clean up automatically. They check native helper behavior, not an entire
Wine/OpenGL/browser session. The readback test now supplies the current
helper's explicit four-byte pixel size with the same expected results.
`fixtures/` preserves the earlier generated C++ snapshots for comparison;
the Python entrypoints use current production code.

`wine32-catch.cpp` is the original exception fixture. The Node runner checks
preserved `testdata/wine32-exception-regression/{before,after}.{mjs,wasm}`
snapshots: the negative control must fail before catching, and the positive
control must print `BW32_CATCH_OK` and exit zero. It checks historical builds.
Current build flags and the full runtime still require rebuilding and
booting Wine32; these snapshots do not certify the current build.

Two diagnostic tools are also preserved:

- `node tools/regression/glprobe-c11-scan.mjs <game-directory>` scans Unity
  graphics backend strings. Its output is static evidence.
- `node tools/regression/glprobe-c11-itch-dl.mjs '<jobs-json>'` downloads
  supplied itch.io uploads using Playwright. Jobs are arrays of
  `[gameUrl, uploadId, outputPath]`. This is an acquisition tool, not a test.

All GL helper vectors and both exception controls passed during the
2026-10-13 migration. Historical runtime reports live under `docs/reports/`.
