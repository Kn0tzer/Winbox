# One-click launcher — 2026-10-17

The previous launcher markup, stylesheet and UI controller have been replaced.
The new Microapps-style interface uses system fonts, neutral colors, one blue
accent, local light/dark preferences, native dialogs and 44px controls. It has
one folder-picker action, a nine-program demo catalog, saved folders and a
separate player with Stop, Restart, Fullscreen and expandable diagnostics.

Folder selection validates paths and complete Windows PE headers, chooses the
main executable and x86/x64 runtime, applies known Chromium/Source/Unity/FNA
settings and starts automatically. Large folders use an OPFS stored ZIP and
Blob URL byte ranges through the existing lazy mount. Optional EXE/arguments,
patching and backup/restore live in folder settings. Original library and save
database names remain readable; built-in demos retain their shared `demo` saves.
Patch/restore stage a new save database before atomically replacing the library
entry, preserving the original installation if storage fails. Incomplete save
restoration holds boot and exposes retry. Runtime shell/loader URLs share a
content-derived version to avoid mixing cached and current code after deploy.

## Validation

- `pnpm test`: 87 legacy checks passed, one existing opt-in naga browser check
  skipped; all 127 suite unit groups passed. `pnpm typecheck` passed.
- Browser UI checks cover all nine one-click routes, automatic folder selection,
  reload/rerun, settings, retry, keyboard focus, touch targets, 320/640/1920px,
  200% CSS zoom, startup log/first-paint feedback, OPFS ZIP ranges/cache, storage-failure preservation, extraction
  cleanup and backup round trips. Controlled runtime responses test routing;
  they do not establish game playability.
- Three static adversarial review cycles informed storage, lifecycle and import
  fixes. The final cycle found no remaining concrete P1/P2 issues. No separate
  cross-model review was run. A subsequent browser backup round trip exposed
  file-backed Blobs becoming unreadable after temporary-file deletion. Imported
  files now have owned backing storage: small picker files are copied, large
  files and ZIP entries retain OPFS backing until installation deletion. The
  regression checks restore/reload and removal of the original picked folder.

- Live acceptance at `https://kn0tzer.work.gd/wine/` passed: exported assets
  match byte-for-byte, isolation headers are present, and one click opens a
  real Chromium 109 window (655,340 nonblack pixels), then stops without errors.
- The optional legacy WebGPU cube acceptance timed out with zero captured
  WebGPU frames and no GPU validation errors. The actual cube is visible via
  the WebGL path. Experimental WebGPU frame capture is not established by
  this release; its existing acceptance assertion remains intact.

## Deployment

The tested export replaced `/home/ubuntu/kn0tzer/wine` with an atomic directory
exchange. The previous export is at
`/home/ubuntu/backups/boxedwine64-ui-20261017-1929/wine`. No container restarted.
The GitHub branch is `ui/one-click-launcher-20261017`; remote `master` and this
checkout have unrelated histories. The publication snapshot is based on remote
`master` and keeps its workflow definitions unchanged; the local implementation
branch retains its original history. The token cannot publish workflow changes.

## Runtime and compatibility

The compiled emulator is unchanged: WASM SHA256
`46eb53ee977739bb0f765dc52387e6774ad1aee5252ae31dafbdd07bf49cd30a`, JavaScript
`01bdda9520b8750edcf4a7044f2b88b7cab028eb11ac18979f7be84204113961`.
Only the runtime bootstrap and browser export changed; export uses
`tools/bw64-build.sh --export-only` with the existing build/probe coordination.
Raw summaries and screenshots live under ignored `test-results/launcher-*`.
The real guest acceptance opens Notepad, imports a 129 MiB folder, verifies the
x86 program paints 286,296 nonblack pixels, and checks that its saved counter
increments from 1 to 2 after Restart without creating another installation.

Automatic configuration covers Windows x86/x64 applications; it does not fix
emulator compatibility. Portal, Celeste and Gang Beasts remain unverified as
playable. Chromium's normal demo opens offline; the earlier restricted HTTP
fixture is not a deployed general-purpose Internet proxy. Demo archives stay
outside Git at the existing `/games/` host. No homepage, password or container
restart changes are part of this deployment.

## Startup feedback follow-up

While the runtime says Running but has not painted visible pixels, recent logs
show “Not frozen, still loading”. The message expires after 2.5 seconds without
logs and clears once a visible frame is detected. A 32×20 canvas sample runs
only twice per second during startup; it stops after first paint, Stop or error.
Log events do not trigger canvas readback. Unit and browser regressions cover
active logs, quiet logs, errors/late logs, retry, subsequent logs after painting,
and first-paint cleanup. Live Chromium also showed and cleared the message.
The preceding deployed redesign is backed up at
`/home/ubuntu/backups/boxedwine64-ui-20261017-1935/wine`.
