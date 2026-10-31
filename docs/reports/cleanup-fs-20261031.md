# Filesystem layer cleanup — lane-cleanfs (2026-10-31)

Area: the emulator's filesystem layer — `source/io/*` plus the platform
directory lister it depends on. Base: integration tip `538f03db5`.
Standard: the pathfinder pattern record
(`docs/failure-logging-and-recovery.md`) — one searchable
`BW64-FAIL key=value` line per failure with values and outcome,
reporting never gated behind a flag, graceful honest answers where the
guest can survive, loud immediate failure only where recovery is
impossible, rate limits as counting cap (first 64, then every 1024th,
always `count=`) or once-per-case.

## Commits (branch lane-cleanfs, 10 total)

1. `860b1ef7e` — cherry-pick of lane-cleanlog `4f483f6c0`:
   bw64RecoverLogOnce reports unconditionally. The integration tip
   still gates the *reporting*, so without this every LogOnce report
   below would vanish by default on this tree.
2. `2622e3a43` — FsMemNode rename/remove/open/removeDir/setTimes
   refused with EPERM + trace instead of `kpanic`; `length()` /
   `lastModified()` null-guarded. Test: testMemfdNodeOps.
3. `787d91b06` — read errors can no longer pose as huge successful
   reads: `Fs::readNativeFile` (a caller writes `buffer[result] = 0`
   with the value — a live stack smash on any host read error),
   FsZip::init symlink read (same cast + a one-byte terminator
   overflow), `FsZip::readFileFromZip` (same cast, silent short
   reads, and the `unzCloseCurrentFile` CRC verdict discarded).
   Tests: testFsReadNativeFile, testFsZipReadBadCrc.
4. `d424d752c` — zip extraction integrity: both bulk loops
   (`extractFileFromZip`, `unzip`) handed negative error codes to
   `fwrite` as lengths and left partial files that were then served
   as real; `unzip` additionally reported success. `FsZipNode::
   moveToFileSystem` never checked its symlink-branch open, leaked
   the destination handle on write failure, and left truncated files
   at nativePath permanently. All now log, verify byte counts, and
   remove partial output. Test: testFsZipExtractCorrupt.
5. `3a72fc608` — FsZipOpenNode stream failures (read error, CRC on
   close, failed backward-seek reopen, released archive) are
   reported instead of being faked as EOF. Logging only.
6. `cb7ccb669` — host file layer: `FsFileOpenNode::readNative` /
   `writeNative` had the same -1→U32 cast; `reopen` never checked
   its open; `FsFileNode::open` was silent and could clobber the
   errno its caller converts for the guest; the remove-fallback
   rename failure line had no errno or token. Test:
   testFsFileOpenNodeIoErrors.
7. `93227d56b` — directory/mkdir/times honesty: `Platform::
   listNodes` returns bool; `FsNode::loadChildren` and
   `Fs::iterateAllNativeFiles` log when an existing host directory
   cannot be listed instead of presenting it as empty;
   makeLocalDirs/makeNativeDirs log MKDIR failures; makeNativeDirs
   refuses loudly on an empty path or uninitialized separator
   (splitting on an empty separator never terminates in
   `BString::split`); `FsFileNode::setTimes` returns the translated
   errno and logs instead of the designed-in silent success;
   `Fs::setDosAttrib` reports an unopenable attribute file;
   `FsFileNode::getTmpPath` no longer dereferences a null /tmp/del
   node and no longer hands back a live file's path on tmp-id
   exhaustion.
8. `52bebaf3c` — FsDynamicLinkNode rename/removeDir: bare `-1`
   became `-K_EPERM` with a once-per-op trace.
9. `102433f4c` — testMemfdCreate was defined but registered in
   neither test list; registered (it passes).
10. This record.

## Verification

- Baseline, pristine integration code, native suite serial:
  647 passed / 1 failed — "CPU32 UD2 with SIG_DFL in a subprocess",
  a CPU-layer subprocess test, run while the box was at load ~27
  with every cleanup lane building. It passes in both full runs
  below at ordinary load; recorded as contention flakiness, not a
  code signal.
- After commits 1–7 (above numbering 1–6 at the time): full serial
  run 653/653.
- Final tree: full serial run **654/654** (648 baseline entries +
  5 new tests + the newly registered testMemfdCreate).
- Every added test is sabotage-proven, per commit message: with the
  guarded code reverted, testMemfdNodeOps dies in the old `kpanic`,
  the two commit-3 tests fail, testFsZipExtractCorrupt fails, and
  testFsFileOpenNodeIoErrors fails; all pass with the fixes, and the
  new BW64-FAIL lines were observed in the test output
  (e.g. `zerr=-105` CRC, `zerr=-3` data error, EBADF counts).
- The JS suite is unaffected by these C++ changes (pathfinder
  precedent noted; not re-run).

## Deliberately left alone

- **kinotify.cpp** — already the layer's model: loud ENOSYS stance,
  bounded 16384-entry queue with an IN_Q_OVERFLOW record, blocking
  reads that check thread termination. (The kernel-subsystems lane
  additionally corrected its seek result and added value logging on
  its own branch.)
- **fszipurl.cpp** — exemplary already: stall watchdogs, bounded
  retries, deadline-bounded waits, loud failure lines.
- **FsZip::openEntryStream** — retries 3× with a log per attempt;
  meets the standard. FsZipOpenNode deliberately does not duplicate
  its lines.
- **FsNode::addLockAndWait** unbounded condition wait — matches
  kernel blocking-lock semantics (Linux F_SETLKW is likewise
  unbounded); not a silent-failure path.
- **FsZip boolean probes** (doesFileExist, detectGuestIs64,
  iterateFiles) — false is a legitimate answer; archive corruption
  is reported at init/open/read sites.
- **FsVirtualNode kdebug notes** for O_EXCL/O_TRUNC/O_APPEND on
  virtual files — no-ops by design.
- **Fs::deleteNativeDirAndAllFilesInDir** swallowed
  `std::error_code` — cleanup-only caller; noted, not widened.
- **FsDynamicLinkNode::setTimes** returning 0 without storing —
  dynamic links are synthetic /proc-style entries whose timestamps
  are generated (`lastModified()` returns "now"); nothing
  guest-visible is lost. `remove()` returning false matches the
  virtual-node disposition.
- **translateOpenError** (kernel layer) maps only a few errnos and
  defaults to EINVAL — the fs layer now preserves errno exactly;
  widening the kernel's map is that layer's decision.

## Found outside this layer (flagged, not fixed here)

- **BString::split with an empty delimiter loops forever**
  (source/util/bstring.cpp): `strstr(s, "")` always matches and the
  cursor advances by the delimiter length 0. In production
  `Fs::init` sets the separator before any split, so the trigger is
  an uninitialized separator; the fs layer now guards its own entry
  point (makeNativeDirs). Found via a gdb stack of a hung test
  process: the test harness never runs Fs::init. Owner: util layer.

## Reachability note (commit 2)

FsMemNode is created only by `KProcess::memfd_create` and is never
linked into the VFS tree, and every setTimes caller is path-based —
so the five panics were unreachable by construction on the day they
were removed. They were still guest-facing vtable slots one
registration change away from killing the emulator on an ordinary
rename/unlink, which is why they were fixed rather than annotated.

## Lane incidents

- The lane's first baseline build was killed by the lane's own broad
  `pkill` (the pattern matched the build's own command lines — the
  self-match trap recorded in AGENTS.md; it bit another lane the
  same night). Relaunched cleanly; the baseline result above is
  from the clean run.
