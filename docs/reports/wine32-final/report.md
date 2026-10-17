Both success criteria pass in Chromium with the rebuilt runtime.

- Three fresh browser launches print `wine-10.0`.
- Two fresh browser launches run a PE i386 Windows program and print `BW32_PROGRAM_OK pages=32 stack_sum=3696 pointer_bits=32`. The program checks zero-filled, independent anonymous pages, writes and reads each page, checks recursive stack state, frees the allocation, and returns zero.
- All 601 native tests pass. The new demand-page regression fails against the initial faulty RAM implementation and passes with the fix.

The RAM fast path now requires backing RAM. Unbacked pages previously bypassed allocation and could access host address zero, corrupting shared offsets. Interpreter diagnostics that inspect guest memory now run only in instruction-trace builds; the old PEB watcher retained a guest address across processes.

The Emscripten compile step now enables Wasm exceptions consistently with the linker, and object files depend on the makefile so flag changes trigger recompilation. Browser ZIP mounts retain their owners when installing the main loop unwinds startup. The exception regression demonstrates that an identical catch block escapes before the compile flag change and succeeds afterward. See [Emscripten exception documentation](https://emscripten.org/docs/porting/exceptions.html).

The 32-bit launcher and prefix now use Wine 10's `/opt/wine-stable` paths. Fontconfig, Expat, a DejaVu font, their copyright files, and Fontconfig configuration are staged in the 32-bit rootfs. A captured first null call matched `FcPatternCreate` in Wine's font backend; the initial rootfs omitted Fontconfig. See [Wine 10 font backend](https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/win32u/freetype.c). The staging helper is idempotent and backs up archives before replacing them.

The final Wasm SHA-256 is `c722a28b93e5d2bede3c074e78a50d9d3d0a6623f8b0850b4a6c9142db23e71a`. Temporary fault and instruction tracing has been removed. The Wasm, JS, build provenance, prefix32 archive, and rootfs32 archive match between `dist/runtime` and `web/runtime`.

[Source diff](changes.patch) is relative to the uncommitted workspace at task start. It excludes unrelated existing edits and passes `git apply --check --whitespace=error` against the initial backups. [Artifact inventory](artifact-manifest.json) records before/after hashes and ZIP entry changes; rebuilt JS/Wasm and runtime archives are listed there rather than included in the text patch. [Acceptance results](outcome.json) link each probe's console log, summary, and screenshot through its result directory.

Backups are in `/home/ubuntu/backups/bw64-wine32-20261010`; the font staging helper also saves timestamped archive backups under `/home/ubuntu/backups/bw64-fonts32-*`. No commits or pushes were made. Branch and HEAD are unchanged.

To repeat the checks from the repository:

```sh
PROBE_SLOTS=1 PROBE_TAG=wine32-check-version node web/tests/wine32-boot-regression.mjs
PROBE_SLOTS=1 PROBE_TAG=wine32-check-program PROBE_PROGRAM=wine32-boot-regression.exe PROBE_GAMEZIP=test-results/wine32-program-assets/program.zip PROBE_EXPECT='BW32_PROGRAM_OK pages=32 stack_sum=3696 pointer_bits=32' PROBE_TIMEOUT=180000 node web/tests/wine32-boot-regression.mjs
```

The test PE is built from `tools/win32test/wine32-boot-regression.c` with `i686-w64-mingw32-gcc -O2 -Wall -Wextra`. Its program ZIP mounts it at `/home/username/apps/wine32-boot-regression.exe`.
