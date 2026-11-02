# FD-81 - the temp-file-to-lock-name link pattern, served

Lane `lane-fd81-linktmp` (worktree `/home/ubuntu/wt-fd81link`),
off integ `bc989b010`. Fix commit `b0b3a70d3`. Evidence
`test-results/fd81/` (on this tree). Register: FD-81 filed in
Section A, an update block appended to FD-60, and a Section B
row for this branch. FD-60 itself stays closed by CL-31; FD-81
closes when this branch is composed and the composed gate
re-measures the full-session boot count.

## The defect, measured

The FD-58..60 deploy record observed 285 BW64-FAIL lines
across its eight live boots, all one kind: FD-60's loud
hard-link refusal on fontconfig cache temp-to-lock links
(`/root/tmp/fontconfig/*.TMP-*` -> `*.LCK`), exactly 57 per
32-bit boot, 0 on 64-bit. FD-60 (CL-31) had specified that
refusal for sources that are not host files. These sources
ARE ordinary files - what refuses them is one layer down:

- In the browser build the "host" filesystem is Emscripten's.
  Its MEMFS and IDBFS backings (the shell mounts IDBFS at the
  root in IndexedDB mode, MEMFS otherwise) provide no `link`
  node operation, and Emscripten's `FS.link` answers every
  attempt on such a backing with EMLINK (upstream source,
  libfs.js: "Hardlinks are only supported by filesystem
  backends that provide a `link` node op"; neither libmemfs.js
  nor libidbfs.js defines one).
- EMLINK is 34 in this build's numbering (the shipped glue's
  errno table: `EMLINK:34`; the sysroot header defines the C
  macro as `__WASI_ERRNO_MLINK`) - exactly the errno=34 on
  all 285 deploy lines.

So in the browser build the FD-60 host-link path can never
succeed: every link() of a regular file is refused. On the
host, the same pattern (create a temp file in a directory,
link it to a second name in the same directory) succeeds.
The refusal is therefore not the host-correct answer for
this pattern, and the pattern is a mainstream one - it is
fontconfig's cache-lock mechanism (`FcDirCacheLock`-style
TMP->LCK linking), hit on every 32-bit boot.

## The fix (b0b3a70d3)

When the host link fails with EMLINK in the browser build,
`KProcess::link` serves the link at the emulator layer via
the new `KProcess::linkSharedBacking` (the fallback is
compiled only under `__EMSCRIPTEN__`; native builds keep
every refusal, because there EMLINK is the host's real
link-count limit):

- The new name is a second node with the source's node id
  in the same FD-60 link group, but pointing at the SAME
  backing path as the source. Content identity is real, not
  copied: every open of either name opens the one backing
  file, so writes through one name are visible through the
  other, and the pair survives exactly as a host hard link
  does at the layer the guest can observe.
- `FsNodeLinkGroup` gains a `sharedBackingPath` flag, and
  `FsFileNode` consults it in the three places the host
  filesystem used to answer for free:
  - `getHardLinkCount` returns the group's live membership
    (the backing's own host count is 1);
  - `remove()` detaches only while another group name is
    still attached - the last name's remove deletes the
    backing through the normal path (open handles on a
    detached name keep working on the still-existing
    backing, as on Linux);
  - `rename()` of one name while siblings exist moves only
    the guest tree entry and keeps the shared native path
    (POSIX rename moves an entry, not the file).
- Each served link logs a capped BW64-INFO line
  (`outcome=served by shared backing path`, first 64 then
  every 1024th, with count=). A served link is a success,
  not a BW64-FAIL; the refusal line still fires for every
  case that stays refused.
- Scope kept exactly: symlink sources keep FD-60's
  behaviour (sidecar host-linked or loudly refused) in
  every build; non-file sources keep the EPERM refusal;
  directories, EEXIST and ENOENT answers are unchanged -
  `linkSharedBacking` runs the same prechecks as `link()`.

## Proof

- New test `testTempFileLinkPattern`, registered in both
  native suites (`green-window-main.log`,
  `green-window-x64.log`): the fontconfig pattern end to
  end - create `/CACHEDIR.TAG.TMP-*`, link to
  `/CACHEDIR.TAG.LCK`, shared inode, nlink 2 on both
  names, a host-side census of the fixture directory
  showing exactly ONE backing file throughout (a copy
  would show two), a write through the lock name read back
  through the temp name, unlink of the temp name leaving
  content and identity intact under the lock name at
  nlink 1, and the backing deleted when the last name is
  unlinked; plus link()'s error shapes (-ENOENT, -EPERM)
  and a rename of one name of a pair keeping content,
  identity and nlink 2.
- Sabotage-checked both ways (`sabotageA-window-main.log`,
  `sabotageB-window-main.log`): giving the new name its
  own backing path fails the test ("write through the
  lock name failed"); disabling the unlink guard fails it
  ("content did not survive unlinking the temp name",
  "backing must survive while the lock name exists,
  found 0"). Restored code re-verified green
  (`green-window-main2.log`).
- Full native suites, serial (`full-main-serial.log`,
  `full-x64-serial.log`): main 666/666, x64 65/65,
  0 FAILED - the integ baselines 665/64 + the new test.
- Browser build of the branch (`browser-build5.log`):
  'Browser export verified' / BUILD COMPLETE; the dist's
  BUILD_INFO reads git_sha `b0b3a70d3` (this lane's fix
  commit), 0 dirty files, wasm `8303eff4`; the support
  archive is the deterministic `c4dbe35986a5` pack.
- In vivo (`boot32-runner.log`, `boot32-console.log`,
  `boot32-BUILD_INFO.json`): boot32-vkboot PASS (59.6 s)
  against that dist. The preserved console carries 0
  BW64-FAIL lines of any kind and 5 BW64-INFO hard-link
  lines, on exactly the fontconfig files the deploy
  record lists: `CACHEDIR.TAG.TMP-*` -> `CACHEDIR.TAG.LCK`
  and the four `*-le32d4.cache-8.TMP-*` -> `*.LCK` links
  (identical cache hashes to the pre-fix refusal log).
  This boot shape runs one fontconfig pass; the deploy's
  57-per-boot figure came from full live sessions and is
  the merge/deploy gates' number to re-measure.

## Incidents, on the record

- The first browser-build attempt compiled and linked the
  wasm cleanly, then failed at packaging three times on
  missing lane-tree payload state, never on code: no
  `node_modules` (the project uses pnpm; `pnpm install`
  from the warm store fixed it), then the generated
  payload set a fresh worktree does not carry (dxvk
  build.w32/w64 and the rootfs64 payloads - mono, libgl,
  libvk, gstreamer - copied from the integration tree,
  which is their standing home; all copies are ignored
  generated state and are stripped with the tree's other
  generated state). The fourth and fifth attempts are
  `browser-build4.log` / `browser-build5.log`; the fifth
  is the green one above.
- During sabotage work, a `git checkout` meant to revert
  sabotage A also reverted the uncommitted fix in the
  same file. The fix was re-applied from the recorded
  patch and the tree verified back to the identical
  386-insertion diff before sabotage B proceeded; the
  sabotage-B revert was done by edit, not checkout.
- One tool result delivered into this session contained a
  complete 32-bit boot log that matches no command this
  lane issued (this lane ran no browser probe before its
  own build existed). It was treated as foreign output -
  the campaign's standing garbled-delivery hazard - and
  used for nothing; the in-vivo evidence above is from
  this lane's own runner invocation against its own dist,
  with the artifact preserved.

## Residuals, stated

- The group-liveness link count can lag while an unlinked
  name's node is still open (FD-60's stated class, now
  applying to file nodes on this path too).
- After a cold rescan of the backing tree, the two names
  load as separate nodes (FD-60's stated class - the
  backing holds one file, under the first name).
- A served link exists in the guest tree only; the
  backing store never gains a second name for the file.
- 64-bit boots showed no fontconfig link activity in the
  deploy measurement (0 lines); nothing in this fix is
  32-bit-specific, but the in-vivo proof is a 32-bit boot,
  matching where the defect was measured.
