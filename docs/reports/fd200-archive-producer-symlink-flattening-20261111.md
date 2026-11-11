# FD-200 — The archive producer flattened symlinks on re-cut

Filed by the batch-10 merge lane, 2026-11-07. Fixed and closed 2026-11-11
by the FD-200 lane (resume of the lane that stopped on the disk floor).
Fix commit `7bede8831` on branch `lane-fd200`. No closure number claimed;
composition settles it.

## What was wrong, in plain words

The script that re-cuts the 64-bit runtime archive to add the drivers/
mirror (`tools/rootfs64/stage-wine64-drivers.sh`) rebuilt the archive by
rewriting every carried entry from its filename alone. That builds a
fresh entry with mode `0o600`, so every symlink was flattened to a
regular file holding the link-target text. The one symlink in the
archive — `usr/lib/wine/wineserver` (`0o120755`, target
`/usr/lib/wine/wineserver64`) — lost its type bit on every re-cut. This
is FD-199's flattening, still live in this script: the FD-199 entry
itself was repaired at the byte level for batch 10, but the producer
path that made the defective archive was never changed.

## Diagnosis (verified from disk and git history)

- The merge step wrote `out.writestr(info.filename,
  zdst.read(info.filename))` for carried entries and `out.writestr(name,
  data)` for fresh entries. Passing a filename string makes Python's
  zipfile build a fresh `ZipInfo` with mode `0o600` and the current
  time, dropping symlink bits, dates, and modes.
- The sibling script `tools/rootfs64/stage-wine8-backports.sh` had the
  identical line and was already fixed the same way in commit
  `cc1aecbaa` (carried entries via their `ZipInfo`, with the comment
  "keep ZipInfo: symlink bits (external_attr), dates, modes"). The
  drivers script was simply never given that fix — it has had exactly
  one commit, FD-188's own `89d9b8c87`.
- The full-build path `tools/rootfs64/build-wine64-zip.sh` is clean: it
  zips with `zip -qry9`, and `-y` preserves symlinks.
- The current store `wine64.zip` (sha256
  `13f254e3c3a51449abfec2a3e4aba385e085f41ffee9ab53e9eb77260f2a271e`):
  1,281 members, exactly one symlink (`usr/lib/wine/wineserver`,
  `0o120755`). All 1,194 regular files already sit at `0o600` — the
  FD-199 byte-level repair restored only the symlink's type bits, so a
  string-form re-cut is invisible on regular files and only the
  symlink's type change shows.

## The fix

`tools/rootfs64/stage-wine64-drivers.sh`, one merge block (+21/−2):

1. Carried entries are written with their own `ZipInfo`
   (`out.writestr(info, ...)`), mirroring `cc1aecbaa`.
2. Entries the payload replaces keep their old `ZipInfo` with only the
   content changing (`out.writestr(old[name], data)`). This also keeps
   the payload zip's five intermediate directory entries (`usr/`,
   `usr/lib/`, ...) on their old dates — the first fixed run showed
   those five as the only date changes, and carrying the old `ZipInfo`
   removes them.
3. A post-merge check inside the script snapshots every pre-existing
   entry's (`external_attr`, `date_time`) before the rewrite and fails
   loudly (`SystemExit`, naming FD-200) if any pre-existing entry
   changed type, mode, or date. The pipeline can never silently
   produce a flattened archive again.

One deliberate deviation from the first lane's recorded plan: fresh
entries that do NOT already exist in the archive are still written with
`writestr(name, data)`, not via their payload `ZipInfo`. The payload
zip's entries carry filesystem modes (`0o644` from extraction), while
every regular file in this archive — including the 17 `.sys` siblings
the new `drivers/` entries mirror — sits at `0o600`. Writing the fresh
entries by name keeps them at `0o600`, matching their siblings, exactly
as the sibling fix did.

## Proof — fail before, pass after

Staged stale archive: a real file (prep symlink replaced with a copy
per the prep script's own rule), 1,263 members, 0 `drivers/` members,
wineserver still a symlink, sha256
`0d5a1515110f6ff3e60cf40f3b49feccf9af5224c874b279ec3c39a2dc258969`.
Central-directory dumps in `test-results/fd200/` (`centraldir-precut.tsv`,
`centraldir-unfixed.tsv`, `centraldir-fixed.tsv`, `centraldir-fixed2.tsv`;
helpers in `/tmp/fd200-helpers/`).

- **Unfixed script** (reproduction): 18 additions (17 `.sys` +
  `drivers/` dir), 0 removed, **1 type change —
  `usr/lib/wine/wineserver: 0o120755 -> 0o600`**, 1 mode change (same
  entry), **1,263 date_time changes** (every carried entry reset),
  0 data changes. The flattening is reproduced exactly as filed.
- **Fixed script, first pass**: 18 additions, 0 removed, **0 type
  changes, 0 mode changes, 0 data changes**, 5 date_time changes —
  the payload zip's five intermediate directory entries.
- **Fixed script, final** (with the old-`ZipInfo` rule for replaced
  entries): 18 additions, 0 removed, **0 type changes, 0 mode
  changes, 0 date_time changes, 0 data changes** across all 1,263
  carried entries. Wineserver `0o120755`, date `2026-11-07 02:44:36`
  (preserved). `testzip` clean. The script's own mirror verification
  passes (17 members) and its post-merge check prints "carried entries
  preserved (1263 members)".

The staged archive was restored to the prep symlink afterwards and the
three script-run evidence backups (~187 MB each) were removed; the
central-directory TSVs above are the preserved proof. The store
`wine64.zip` still hashes to `13f254e3…` — it was never written through.

## Light checks

Pipeline #90 on the fix commit — see the pipeline-verdict commit on
branch `lane-fd200`.

## Notes

- No new defects found during this work; nothing further filed.
- Disk was 4.7G free at lane start (below the 5G floor; the drop was
  another project's growth, not this lane's) and 4.9G at close. The
  coordinator cleared this lane to proceed: the remaining work peaked
  at ~400 MB of writes and involved no build.
