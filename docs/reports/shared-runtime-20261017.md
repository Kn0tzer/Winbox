# Shared runtime libraries

The launcher started Celeste without the Mono and GL32 libraries that its old
acceptance test supplied in a separate archive. The same omission affected other
managed uploads. Dependencies now belong to the shared Windows environment:
Wine Mono 7.4.0, its bundled FNA libraries, and both graphics bridge architectures.
Wine and the dynamic loader select the required libraries. No executable-name or
engine condition installs them.

`pnpm build:export` creates a deterministic 86,612,478-byte archive and records its
SHA256 and input provenance in `runtime/dependencies.json`. The complete Mono
payload is checked against the pinned release. The runtime validates availability,
size and byte-range support before booting, then reads library files lazily.
Application folders are not modified. Mono uses Wine's normal Windows-directory
lookup ([Wine 8.0 runtime discovery](https://github.com/wine-mirror/wine/blob/wine-8.0/dlls/mscoree/metahost.c#L734)).

Browser evidence under `test-results/`:

- `launcher-dependencies-before`: an unrelated managed upload fails to create its
  result file before the repair.
- `launcher-dependencies-after`: renamed executables in nested uploaded folders
  run on x86 and x64; reflection, threads and file output produce the expected
  results and both display a window.
- `launcher-dependencies-games`: a renamed FNA upload renders 16,384 red pixels;
  keyboard input moves their center by 120 pixels. The Celeste demo completes
  loading and displays colored content (`GAME DISPLAYED`, 233,504 ms).
- `launcher-dependencies-upload-reopen`: the original ZIP's saved installation
  completes loading and renders colored game content (`GAME DISPLAYED`,
  231,740 ms). Native Notepad draws while fetching only 1,153,534 bytes of
  dependency archive metadata/blocks. Missing dependencies show a startup error.
  No unexpected JavaScript errors occur.
- `launcher-dependencies-live`: the deployed site passes the FNA render/input,
  native Notepad and unavailable-dependency checks. All library reads use HTTP
  206 ranges; Notepad again reads 1,153,534 bytes rather than the whole archive.

The first large-upload attempt hit the private browser's storage quota. A normal
isolated profile imported the original 2.5 GiB ZIP and prepared its streamed
archive. That test process received SIGTERM after asset loading. Reopening the
same saved installation in a fresh test process passes the complete render check.

Fast checks pass: 98 legacy tests, 128 unit groups, type checking and export build.
The pre-existing opt-in legacy skip remains. Existing Mono, FNA and Celeste suite
tests now use the shared runtime instead of supplying dependency overlays.
Native WASM remains `46eb53ee977739bb0f765dc52387e6774ad1aee5252ae31dafbdd07bf49cd30a`.

The tested export is deployed at `https://kn0tzer.work.gd/winbox/`. HTTP checks
confirm matching runtime files, isolation headers, dependency size and range
responses. The previous export is retained at
`/home/ubuntu/backups/winbox-shared-runtime-20261017T215931Z/previous`.

This repairs dependency setup. Celeste's own cold asset loading still takes about
four minutes in this server's headless browser. Frame rate and audio were not
measured in these checks.
