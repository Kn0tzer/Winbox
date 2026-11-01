# FD-73 — the pinned runtime-assets release archive disagreed with the manifest's file pins

Lane `lane-fd73` (worktree `wt-fd73`), off integration `ae8f2325d`.
Fix in `cc2e3a220`; this record and the register closure (CL-26) are
the following commit. Evidence in `test-results/fd73-20261101/`.

## End state reached

A fresh checkout can bootstrap again: `bootstrap.py --local` from
the retained archive restores and verifies all 13 pinned assets
into a tree that had none of them, and `bootstrap.py --check` then
passes with the completeness checker at 0 failures. The retained
archive and `tools/browser/runtime-assets.json` now agree member by
member, and the completeness checker verifies that agreement on
every run, so a future re-pin that leaves the archive stale fails
the standard verification instead of the next fresh checkout. The
GitHub release upload remains user-gated (as with every previous
generation); nothing was published, pushed, or deployed by this
lane.

## What was wrong (verified against the retained archive)

The retained archive
`/home/ubuntu/backups/winbox-runtime-assets-20261026/browser-runtime-20261026.tar`
(513,013,760 B, sha256 `9978166f0d548dd2...`) matches the manifest's
archive pin exactly, and its member set is exactly the 13 pinned
paths — but two members disagree with their file pins
(`archive-audit-old.txt`): `web/runtime/wine64.zip` hashes to
`b74765411050e214...` (209,697,213 B) against a pin of
`53d77f337544e713...` (209,611,866 B), and `web/runtime/wine32.zip`
to `955cc41cf6d92a0d...` (239,760,722 B) against
`7f2209cff5ffe799...` (239,672,753 B). The other eleven members
match. Reproduced from a genuinely clean worktree (none of the 13
files present): `bootstrap.py --local` fails with `asset
size/SHA256 mismatch: web/runtime/wine64.zip` and installs nothing,
while `bootstrap.py --check` passes on any tree that already
carries the pinned files without ever reading the archive — the
masking that let this survive every merge gate while each gate
restaged the wine zips by hand from a verified tree.

## Which side was stale (member census)

The pins are the shipped bytes: the live tree
(`/home/ubuntu/kn0tzer/winbox/runtime/`) and the rollback tree
(`winbox-prev-20261101-1225`) both carry wine64.zip/wine32.zip
bit-identical to the pins. The archive's copies are the pre-fix
builds (`wine-census-diff.txt`; name sets identical — 1,257 and
1,926 members):

- wine64.zip differs in exactly five members: `win32u.so`
  `5dac58217e6b...` (stock) vs pinned `bffd9036f5c6...` (FD-01
  desktop-wait patch); `win32u.dll` `643b762302d5...` vs pinned
  `49b874381063...` (FD-40 stamp); and the Vulkan shim
  `ae96105b161f...` vs pinned `03edc142351a...` under all three of
  its names (`bw64vulkan.dll`, `vulkan-1.dll`, `winevulkan.dll` —
  the pre-restage bridge build).
- wine32.zip differs in exactly the four win32u copies:
  `ca5dd9c5532f...` vs pinned `b2d9d135150b...` (the FD-02
  desktop-wait patch).

The archive was built 2026-10-30, before FD-40/FD-01/FD-02 landed;
their compositions re-pinned the files but never rebuilt the
archive. Re-pinning the manifest to the archive instead would have
rolled every fresh checkout back to the unpatched desktop-init and
unstamped win32u builds — so the archive was regenerated from the
pinned files, per the register entry's stated preference.

## The fix (`cc2e3a220`)

- The retained archive was rebuilt: all 13 files staged (eleven
  extracted from the old archive, the two wine zips taken from the
  live tree's pinned copies), **every staged file verified against
  its file pin before packing**, then packed deterministically
  (manifest member order; zeroed mtime/uid/gid/names) as
  `/home/ubuntu/backups/winbox-runtime-assets-20261101/browser-runtime-20261101.tar`
  (512,829,440 B, sha256
  `23e8c8614f20e3ae3531e4dd795222bdb6e0c7ddac7858f0eda44946dfdf6416`).
  The 20261026 archive is untouched, alongside the previous
  generations.
- `tools/browser/runtime-assets.json` now describes release
  `runtime-assets-20261101` (archive name/URL/bytes/SHA256 updated;
  the 13 file pins are byte-for-byte unchanged), per the README's
  rule that a runtime update takes a new release tag and manifest.
  The README's own release references — stale at 20261025 through
  two generations — name the new release.
- `tools/rootfs64/check_runtime_completeness.py` gains the check
  `retained runtime-assets archive agrees with the file pins
  (FD-73)`: it resolves the retained archive via
  `BW64_ASSETS_ARCHIVE`, else by the manifest's own archive name
  under `/home/ubuntu/backups/winbox-runtime-assets-*/` (loud skip
  when no retained copy exists, e.g. off-box), and verifies the
  archive's own pin, its member set, and every member's
  size/SHA256 against the file pins. It runs wherever the
  completeness checker runs — inside `bootstrap.py --check` and the
  `build.mjs` hard gate — i.e. at every merge/deploy gate.
- `docs/known-gaps/runtime.md`: the re-pin disposition row now
  records this guard, and an FD-73 addendum records the census,
  the new archive, and the proofs.

## Proof

- **Defect reproduced** from a clean worktree: `--local` against
  the 20261026 archive fails on `web/runtime/wine64.zip`, nothing
  installed (all-or-nothing held).
- **The new check can fail** (`negative-check-old-manifest.txt`):
  run under the *previous* manifest (whose archive pin matches the
  old archive, so the pin stage passes), it fails naming the member
  exactly: `web/runtime/wine64.zip: archive carries size/SHA256
  209697213/b74765411050... but the file pin is
  209611866/53d77f337544...`, exit 1.
- **The new check passes** on the regenerated archive: default
  resolution finds `browser-runtime-20261101.tar` by the manifest's
  name; archive pin, member set, and all 13 members verify.
- **Clean room** (`cleanroom-local.log`, `cleanroom-check.log`):
  the lane worktree was created fresh and carried none of the 13
  files (`--check` listed all 13 missing). `--local` from the
  regenerated archive: `Restored and verified 13 runtime assets.`
  Then `--check`: `Runtime assets verified (13 files)`, exit 0,
  completeness **0 failures** with the FD-73 line PASS (the 6 skips
  are the bare-worktree ones — no dist/support pack exists in a
  fresh checkout — each printed loudly as designed).
- `test-bootstrap.py`: 4 tests OK (restore mechanics untouched).
  The JS wrappers `runtime-assets.test.mjs` and
  `runtime-completeness.test.mjs` both pass in this tree.
- The full dist build was not run: no product code changed (the
  wasm is untouched by this lane), and the assets path is proven
  end to end above; the merge gate's build will re-verify as usual.

## Housekeeping notes

- Nothing pushed, deployed, or published; master and the live site
  untouched; the old retained archive was read and extracted from
  but never modified or removed.
- The worktree's restored `web/runtime` + `tools/browser/slang`
  payloads (untracked bootstrap products) were stripped again
  after the proof — they are restorable in seconds via the fixed
  bootstrap, which is the point of the fix. The lane's
  `test-results/fd73-20261101/` evidence stays in the tree
  (gitignored, as with prior lanes). `/tmp` scratch from the audit
  (extracted stale wine zips, the pin-verified staging dir, the
  negative-test tree) was removed.
- No further defects were found along the way; nothing else was
  added to the register.
