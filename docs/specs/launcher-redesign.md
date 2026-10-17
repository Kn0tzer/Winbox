# Launcher replacement

The launcher replaces all existing page markup, styles and application UI code.
The emulator, graphics bridges and local library data remain reusable engines.

## Objective and assumptions

One Upload folder action opens the directory picker. Selection validates the
folder, identifies the main Windows executable and its PE architecture, applies
known program defaults, saves it locally and starts it without a setup form.
Each demo tile starts that demo in one action. Native file-picker confirmation
is browser UI. Automatic selection covers supported x86/x64 Windows programs;
unsupported formats receive an actionable error rather than a false success.
Automatic configuration does not establish game compatibility or fix existing
Portal/Celeste/Gang Beasts runtime defects.

## Capabilities and order

1. `folder-launch`: bounded, deterministic PE/entry-point and engine detection;
   large folders use a stored ZIP through the existing lazy runtime mount.
2. `demo-launch`: catalog for DOOM, Chromium, Portal, Celeste, Gang Beasts,
   File explorer, Notepad, OpenGL cube and Snake. Server-owned game archives
   stay outside git; local builds use an explicit external archive base.
3. `launcher-ui`: entirely new semantic HTML/CSS/ES modules, Microapp defaults,
   local library, loading/error/retry states, runtime stop/restart/fullscreen,
   optional settings and diagnostics. Preserve IndexedDB keys and save data.
4. `publish-launcher`: tests, browser verification, local commit, authorized
   GitHub push and deployment of the generated export to the existing /wine/.

Dependencies: folder-launch and demo-launch precede launcher-ui integration;
publish-launcher follows all three. No Microapps homepage edits or container
restarts. No password/auth changes in this task.

## Design

A single launcher screen, native buttons, system fonts, neutral gray tokens and
one muted blue accent. System light/dark preference. A prominent upload action,
a compact game library, and saved folders below. Settings disclose overrides;
shader tooling and the previous multi-panel lab layout are removed. The running
program expands into a dedicated player with visible Stop and Fullscreen.
Mobile-first CSS Grid, 44px controls, visible focus, reduced motion, no clipping
at 320px or 200% zoom. Example token: `--accent: #355fa3`.

## Contracts

Validate package paths, collisions, PE header bounds and argument strings.
Saved files must remain readable after the original folder is removed. Retain
owned OPFS backing for large files and extracted ZIPs until installation deletion.
Never interpolate imported names as HTML. Prefer a folder-name/known-engine main
EXE over crash reporters, uninstallers, redist installers and auxiliary tools.
Detection never runs uploaded code. Graphics selects the working WebGL path by
default; existing Vulkan frame forwarding remains available to the engine.
Archives are checked with HEAD before starting, and use stored ZIP/range loading
rather than decompressing the full demo into browser memory. Browser storage
errors preserve existing installations and expose a retry. Runtime messages
must come from the active same-origin iframe; stale async work cannot replace a
new run or append frames after Stop.

## Verification

Node tests for executable ranking, PE architecture, helper exclusion, Chromium
and Source/Unity/FNA profiles, malformed input and catalog argument construction.
Existing path/ZIP/storage and complete fast tests plus `pnpm typecheck`.
Actual browser checks: upload auto-start with a real fixture, all demo tile
routes with controlled runtime navigation, a real Notepad or Chromium launch,
failed upload/download recovery, local library reload, settings, stop/restart,
keyboard focus, narrow/wide/light/dark layouts and screenshots. Test new streaming
ZIP behavior through the actual guest mount. Review removed UI against preserved
save/graphics/message contracts. Build only through tools/bw64-build.sh.

Live acceptance checks the deployed page, assets, isolation headers and at least
one one-click demo. GitHub push is explicitly authorized by the user; report
remote commit and deployment URL, without claiming unverified game playability.

## Acceptance — 2026-10-17

Folder and demo routing, saved library, backing-file ownership, backup/restore,
error recovery, layouts and real Notepad/streamed x86 guest restart passed.
The deployed page and one-click Chromium window passed live verification.
See [the dated report](../reports/launcher-redesign-20261017.md) for provenance,
rollback location and the failed optional WebGPU capture check. Existing game
compatibility limits remain outside this UI change.
