# Common dependencies

User-authorized capability: extend the same system runtime used by demos and
uploads with common redistributables. Audit existing Wine implementations first:
CRT 2005–2013/v14/UCRT, D3DX/compilers, XInput/XAudio/XACT and media APIs already
exist. Prove representative calls rather than duplicating native DLLs blindly.
Add missing compatible audio, embedded HTML and managed-runtime payloads using
pinned upstream inputs, hashes, provenance and lazy reads. Preserve app-local
libraries and the existing Mono/FNA behavior.

Do not advertise an installed payload as verified compatibility. Modern .NET,
WebView2 and codec backends require functional guest tests and may expose missing
Wine/emulator support; record concrete limits and errors. Never add per-game
patches. Test actual guest API work on x86/x64 and native/Mono regressions. Keep
downloaded archives/build products out of Git. Commit sources and reproducible
preparation steps; deploy tested export; do not push.

## Pinned payloads

- Wine Mono 7.4.0 (tree hash and archive hash in `dependencies.json`), staged at
  `/opt/winbox/mono` with normal Wine `mono.link` files.
- Wine Gecko 2.47.3 x86_64 for the Wine 8 x64 runtime and 2.47.4 x86 for the
  Wine 9 x86 runtime, under `/usr/share/wine/gecko`.
- OpenAL Soft 1.25.2 (`soft_oal.dll` renamed to `OpenAL32.dll`) in each Wine
  loader's per-architecture `*-windows` directory.
- Mesa `libGL.so.1` for i386 and x86_64 under both `lib/` and `usr/lib/`.
- `winbox-runtime.json` inside the archive records every upstream URL and hash;
  `dependencies.json` exposes the content-addressed archive and its size.

`tools/browser/stage-dependencies.py` downloads only pinned inputs, verifies
hashes before use, and emits one lazy ZIP. The launcher passes that archive as
the first `-zip`, so per-prefix files still win and no application data is
copied into it.

## Verification and limits

- x64: embedded HTML reaches `MSHTML: RESULT 0` with parsed title and body;
  OpenAL Soft opens a device, plays a tone, and non-zero PCM reaches the browser
  audio capture.
- x86: Wine Mono, FNA and OpenGL guest tests still pass. The Wine 9 x86 console
  path blocks inside mshtml's `CoCreateInstance` while Gecko starts and stops in
  OpenAL device enumeration, so the x86 Gecko/OpenAL payloads are staged and
  hash-checked but not end-to-end proven in this harness. This is recorded rather
  than advertised as support; a GUI-message-loop probe is the next step.
- Gecko exposed two interpreter gaps on the x64 path: x87 `D8` register
  arithmetic and `D9 E5` (`FXAM`), plus SSE2 saturated byte subtraction
  (`PSUBUSB`). The CPU64 self-test now covers those encodings.
