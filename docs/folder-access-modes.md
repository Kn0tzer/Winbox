# Folder access modes: direct vs copy

The launcher offers two ways to open a game folder: **direct file system access**
and **copy to browser storage**. Both keep Wine prefixes, saves, and patches in
browser storage; they differ only in where the original program files live.

## Direct file system access

Uses the File System Access API to read the chosen folder in place. No game
files are copied into browser storage.

### Pros

- **No game-sized duplicate**: a 20 GB folder still uses 20 GB on disk, not
  another 20 GB in browser storage.
- **Edits are picked up**: changes made outside the browser are re-indexed on
  the next launch, so mods and replacement files do not need a fresh upload.
- **Bypasses browser quota for game data**: useful when the folder is much
  larger than the origin's storage allowance.
- **One authoritative copy**: backups can stay filesystem backups instead of
  browser exports.

### Cons

- **Chromium-only**: Chrome, Edge, and other Chromium browsers expose
  `showDirectoryPicker`; Firefox and Safari do not.
- **Permission can lapse**: the browser may ask you to grant read access again
  after a restart, and a saved handle can become unavailable.
- **The folder must stay put**: moving or renaming it requires **Relink folder**
  in the folder's settings.
- **First launch still indexes**: every file is read once to build ZIP metadata
  and CRCs. No bytes are copied, but a very large folder takes time to scan.
- **Slightly more overhead per read**: ranges are served from original `File`
  slices through the browser API instead of from the browser's own store.

### When to use

- Games larger than a few GB, especially when browser storage is limited.
- Folders you are actively modding or replacing files in.
- When you want the originals to remain the only copy of the game data.

## Copy to browser storage

Copies the folder into browser storage (IndexedDB metadata with OPFS-backed
file data) during import.

### Pros

- **Works in every supported browser**, including Firefox and Safari.
- **No folder permission prompts after import**: the saved installation owns
  its files.
- **Independent of the original folder**: moving or deleting the source does
  not break the saved game.
- **Predictable relaunch**: file bytes are already in browser storage, so no
  directory scan or permission check is needed.
- **Self-contained backups**: export and restore do not depend on an external
  folder remaining available.

### Cons

- **Storage duplication**: the original folder and the browser copy both use
  disk space.
- **Import time**: large folders take time to copy and may hit browser quota.
- **Stale after edits**: changing files on disk requires importing the folder
  again; there is no automatic rescan.
- **Re-import for updates**: patches applied outside the launcher need a fresh
  copy unless applied through the launcher's patch control.

### When to use

- Smaller games where the copy is quick.
- Firefox or Safari users.
- Folders on removable or network storage that may disappear.
- When you want "import once, play without prompts" convenience.
- ZIP or EXE imports, which are always copies regardless of the selected mode.

## Recommendation

**Copy to browser** is the default because it works in every supported browser
and does not depend on a permission prompt or a folder staying in place.
Choose **File system access** when the folder is large, when browser storage is
tight, or when you edit the game files outside the browser. The launcher states
these tradeoffs next to the choice and never silently substitutes one mode for
the other.

## Technical notes

- Direct mode calls `showDirectoryPicker({ mode: 'read', id: 'winbox-programs' })`
  and stores read-only directory handles plus file metadata in IndexedDB. It
  does not store game bytes.
- A virtual ZIP (STORE entries, ZIP64 when needed) serves bounded range reads
  from original `File` slices. CRCs are cached with size and `lastModified` so
  unchanged files are not rescanned on every launch.
- Original files are never written or deleted. Saves and patches go to the
  browser's writable overlay.
- Backup/restore always creates a browser copy, because a ZIP cannot recreate a
  directory handle. The folder settings say so before restore.
