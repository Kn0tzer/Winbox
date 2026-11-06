# Common dependencies

_Design record, October 2026 build-out: the requirements and pinned-payload rules for the shared runtime, written as the work was scoped. What actually ships is pinned in the manifests the staging tool writes (`tools/browser/stage-dependencies.py`); this page is the design record, including the same-environment-for-demos-and-uploads record merged into it on 2026-11-05. The documentation map is `docs/INDEX.md`._


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

## Same environment for demos and uploads

_The text below is the whole of the former `shared-runtime-dependencies.md`
design record, merged into this document on 2026-11-05 by the
documentation-library pass: the two records covered one subject — the
shared runtime payloads — so they now have one home. Text unedited._

The launcher must give demos and uploaded programs the same Windows environment.
Celeste currently starts without the Mono and OpenGL libraries supplied by the
direct-runtime acceptance test. Fix the environment rather than patching the game
or adding a Celeste launch condition.

Provide Wine Mono 7.4.0 (including its FNA/SDL dependencies) at a system path and
the maintained 32-bit GL bridge at the normal Linux library lookup paths. Mount
these through a shared, versioned, lazy archive in every runtime session, before
application files. Wine loads Mono only when a managed program requests it; no
engine or executable-name rule should be needed. Uploaded files take precedence
over shared dependencies. Preserve existing explicit diagnostic overlays.

Acceptance: the demo button and an uploaded Celeste folder/ZIP reach visible game
content without overlay/ldpath options. Unrelated managed executables run from
nested folders on x86 and x64 without extra files. A separate FNA program renders
and accepts input through the upload flow. Native applications still launch.
Missing dependency assets produce a startup error instead of indefinite loading.

The existing C++/Wine runtime stays unchanged. Build the supplemental archive from
the pinned Wine Mono input and checked-in GL bridge, with deterministic entries,
hashes and provenance. Fetch only archive metadata and files actually opened by
the guest. No new npm dependencies, user settings or game-specific patches.

Verification: focused browser regression before/after; `pnpm test`,
`pnpm typecheck`, `pnpm build:export`; real launcher browser tests against the
export and deployed site. Retain logs, screenshots and hashes in `test-results/`.
Commit verified changes locally; do not push.
