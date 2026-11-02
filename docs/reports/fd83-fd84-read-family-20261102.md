# FD-83 / FD-84 — readv's access event and the read family's wrong-mode answer

Branch `lane-fd83-84` (worktree `/home/ubuntu/wt-fd83-84`), off
integration `cc1d89ab3`. Commits: `0332ee82f` (tests),
`3459a7d20` (fix), `bdbac75a5` (silent-failure baseline review).
Nothing pushed, deployed, or merged; master and the live site
untouched. Evidence: `test-results/fd83-84/` (host reference
programs and outputs, RED/GREEN logs, suite logs, build logs).

Both defects were filed by the FD-79/80 lane and left as found.
The host contract for both was re-measured first on the box's
host kernel (6.17.0-1020-oracle) with dedicated reference
programs (`hostref.c`, `hostref-write.c` in the evidence dir),
extending the FD-79/80 matrices to the cases the filings did not
pin down: zero-count and zero-iovcnt wrong-mode reads, the
validation orderings among the mode answer, the offset check,
the flag check and the iovcnt range check, readv's event count
for multi-segment and all-zero-length-segment successes, and the
partial-success shape (bytes read before a later segment fails).

## FD-83 — readv generates no IN_ACCESS on a 0-byte success

Host behaviour (measured): readv generates exactly one
IN_ACCESS per successful call — one segment or three, data or
0 bytes (at EOF, with iovcnt 0, or with all-zero-length
segments) — and none on failure. A call that reads bytes before
a later segment fails returns the byte count as a success and
generates the event. Plain read/pread stay silent at 0 bytes
(FD-59, unchanged).

Emulator behaviour before the fix: `sys_readv64` walked its
segments through `sys_read64`, whose `KFile::readNative`
notifies per segment and only when bytes move — so 0-byte
successes generated nothing, and the one-event-per-call shape
for multi-segment reads held only by accident of the inotify
queue coalescing consecutive identical events. The walk also
carried no validation of its own: an out-of-range iovcnt was
never EINVAL (the walk just proceeded through the plain-read
path), and iovcnt 0 on a write-only descriptor returned a
silent 0.

Fix (`syscall64.cpp`, `kinotify.h/.cpp`, `kfile.cpp` comment):
`sys_readv64` now validates like `sys_preadv64` and in the
host's order — bad descriptor EBADF, wrong mode EBADF (FD-84),
iovcnt outside 0..IOV_MAX EINVAL, null iovec with iovcnt > 0
EFAULT — then walks its segments under a new
`KInotify::SuppressAccessNotifications` scope (a thread-local
RAII depth count checked in `notifyAccessed`), and raises
exactly one IN_ACCESS on success, 0-byte successes and
partial successes included; an outright failure raises none.
`KInotify`'s FD-59 disposition comment and `KFile::readNative`'s
comment now state the vectored exception.

Proof: `testPreadv64Syscalls` (x64 native suite) sections 10 —
RED on the base (readv at EOF / iovcnt 0 / zero-length segments
generated no event; iovcnt IOV_MAX+1 and -1 were not EINVAL;
write-only readv was not EBADF at either iovcnt), GREEN with
the fix across the multi-segment walk (data, position, one
event), the short final segment, all three 0-byte shapes, the
failure set (no events), and a plain-read-at-EOF control that
stays silent.

## FD-84 — the read family answers -EINVAL on a descriptor that cannot be read

Host behaviour (measured): read, pread, readv, preadv and
preadv2 on an O_WRONLY descriptor answer EBADF — for count 0
and iovcnt 0 too, and ahead of the buffer check. In the
positioned family a negative offset answers EINVAL *ahead of*
the mode answer; in preadv/preadv2 the mode answer precedes
the iovcnt range check and the unknown-flag EOPNOTSUPP.

Emulator behaviour before the fix: `sys_read64` and
`sys_pread64Impl` gated on `canRead()` with -EINVAL;
`sys_preadv64` reached the same answer per segment, after its
flag and iovcnt checks; `KProcess::read` answered -EINVAL; and
`KProcess::pread64` had no mode check at all — it fell through
to the backing read, whose host EBADF the backing layer
swallows into a silent 0 (visible in the RED logs as a
`BW64-FAIL host-read ... errno=9 ... outcome=call reports
failure` line followed by a 0 return to the guest).

Fix: the mode answer is -EBADF at all five sites
(`sys_read64`, `sys_pread64Impl`, `sys_preadv64`,
`KProcess::read`, and a new check in `KProcess::pread64`),
placed in the measured order: `sys_pread64Impl` validates a
negative offset (EINVAL) before the mode, which also gives
plain pread64 the host's order; `sys_preadv64` now checks
offset, then mode, then flags, then iovcnt; the zero-count
returns stay behind the mode check everywhere.

Proof: `testPreadv64Syscalls` section 9 (the write-only case
the FD-80 lane pinned at -EINVAL, rewritten to the host
matrix: eleven assertions covering both orderings, the
zero-count cases, and no-event-on-failure) and
`testChmodAccessHardLink`'s new FD-84 section through the
KProcess API (plain read, zero-length read, pread64). RED on
the base in both native suites — the x64 run failed 20
assertions across sections 9-10 plus the three KProcess
assertions, the main-suite run failed the same three — GREEN
with the fix in both.

## Suites and builds on the fixed tree

- Native suites, serial (fork-per-test): main 668/668 and
  x64 69/69, 0 FAILED — the integration baseline counts (this
  lane extends two existing tests, adds none).
- Browser build on the fixed tree (clean-bootstrap staging
  from the retained archive this base pins,
  `runtime-assets-20261101`, `--check` green): BUILD COMPLETE,
  "Browser export verified", wasm `2601ef69a323`,
  BUILD_INFO git_sha `3459a7d20`, support archive
  `c4dbe35986a5` unchanged.
- JS unit suite: 348 passed / 0 failed. One reviewed harness
  adjustment: the silent-failure baseline for `syscall64.cpp`
  moved 115 -> 116 (`bdbac75a5`) — the scanner's net count over
  the reworked read paths; the new returns are guest-validation
  answers in the same unlogged shape as `sys_preadv64`'s.
- JS legacy suite: 110 passed / 4 failed — the four failures
  are FD-64's standing descriptor-set fixture set on this base,
  unchanged (that fix lives on `lane-fd64`).

## Filed by this lane, left as found

- **FD-92** — the 32-bit dispatcher's readv (145) has a null
  dispatch slot and answers -ENOSYS; only the 64-bit readv
  exists. Found while scoping FD-83 (which covers the
  implemented readv). Numbered past FD-85..FD-91, which live
  on unmerged lane branches off this same base, so the
  composition audit does not have to renumber it.
- **FD-93** — the write family's mirror of FD-84, host
  matrix measured by this lane (`hostref-write.c`):
  write/pwrite/writev on a read-only descriptor answer EBADF
  on the host (zero counts included; pwrite's negative offset
  again precedes the mode answer), while `sys_pwrite64` and
  `KProcess::write` answer -EINVAL, `KProcess::pwrite64` has
  no mode check (the backing layer swallows the host EBADF
  into a silent 0), and `sys_write64`/`sys_writev64` carry no
  mode gate at all. FD-84's filing covers the read family;
  this lane did not widen its fix to match.

Observed, not filed: a readv whose segment lengths sum past
the host's ssize_t range answers EINVAL on the host; the
emulator's per-segment 256 MB clamp in `sys_read64` (a
deliberate loader-driven bound) makes the guest-visible
divergence a >2 GB single-readv length sum, and the 64-bit
accumulator itself cannot overflow at IOV_MAX segments. A
non-null but unmapped iovec pointer faults through the guest
memory model rather than answering EFAULT; the null-iovec
case answers EFAULT as the host does.

## Incidents

- One native build attempt lost an assembler temp file under
  the shared /tmp (`can't open /tmp/cc*.s`); the incremental
  rerun completed. No tree state was affected.
- The first two browser-export attempts failed on fresh-tree
  staging: `node_modules` is a symlink to the main tree's in
  lane worktrees, and the DXVK/vkd3d/rootfs64 payload
  directories are generated state the integ tree carries and
  fresh trees do not. Both were staged from the integ tree
  (one copy initially nested `tools/rootfs64/mono32` inside
  itself and failed the payload's pinned tree hash; replaced
  with a clean copy, which verifies). All staged payloads are
  stripped again with the rest of the generated state.
- A `runner.mjs --legacy` mis-invocation ran the unit suite
  (348/0, confirming the baseline fix) and then wandered into
  the browser-probe section, where it wedged in the probe-slot
  queue; the process (this lane's own) was stopped and the
  legacy suite run with its own command (`node --test
  web/tests/*.test.mjs`) for the 110/4 result above.
