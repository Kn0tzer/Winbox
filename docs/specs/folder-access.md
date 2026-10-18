# Folder access

User-authorized capability: offer a persistent choice between copying a folder
into browser storage and reading a user-selected folder through File System
Access. Explain both choices before selection; never silently substitute one.
ZIP/EXE imports remain copies and say so. Unsupported browsers explain why the
direct option is unavailable.

Direct access stores read-only directory handles and a file index in IndexedDB,
not game bytes. A virtual ZIP serves range reads from original File slices to
the existing lazy mount. Build valid ZIP CRCs during an initial scan, cache them
with size/mtime, and refresh changed files on reopening. Keep existing path,
file-count and runtime limits. No service worker or native runtime change.
Original files are never written or deleted. Saves and patches remain in the
browser's writable overlay. Permission loss and moved folders produce actionable
errors and a relink control. Preserve existing copied installations and backups.

Acceptance: real uploaded program renders and reads assets through both choices;
direct mode adds no game-sized OPFS/IDB payload, can reopen saved handles, detects
changed files, and leaves originals unchanged. Unit tests verify ZIP64 ranges,
CRCs, unsafe paths and permissions; browser tests verify UI, persistence and the
actual lazy mount. Commands: pnpm test, pnpm typecheck, pnpm build:export.
