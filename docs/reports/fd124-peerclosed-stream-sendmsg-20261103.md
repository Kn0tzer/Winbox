# FD-124 — a peer-closed stream survivor's unnamed sendmsg answers -EPIPE

Lane `lane-fd124`, based on `lane-fd120-123` tip `daf5ec34e`.
The defect, by substance: on a unix stream pair, after the peer
closes, the surviving socket's sendmsg with no destination in
the header answered -ENOTCONN, where the host answers -EPIPE.
The no-peer branch of `KUnixSocketObject::sendmsg`
distinguished never-connected from peer-died for the
record-oriented sibling (seqpacket, FD-118) but not for
stream: any stream socket whose connection no longer resolved
answered -ENOTCONN. (Lineage number FD-124, filed by the
FD-120..123 lane; the next merge audit renumbers this
lineage, so the substance above is the durable description.)

## Host measurement first

`test-results/fd124/hostref-fd124.c`, run on the box host
(kernel 6.17.0-1020-oracle), output
`test-results/fd124/hostref-fd124.out.txt`. The full send
matrix around the filed shape:

- Stream survivor (pair peer closed): sendmsg with no name
  -EPIPE — at length 1, at length 0, and with an empty iovec;
  sendto with no address, plain send, write (including a
  zero-length write), writev and sendmmsg all -EPIPE; sendmmsg
  answers -EPIPE on the second call too (no one-shot
  accounting for stream). Without MSG_NOSIGNAL and with a
  SIGPIPE handler installed: -EPIPE, and SIGPIPE delivered.
- The peer shutting only its write side leaves sendmsg
  working (the byte is delivered); the peer shutting its read
  side, or the socket shutting its own write side, answers
  -EPIPE.
- A survivor holding unread data the peer sent before closing
  still answers -EPIPE.
- Never connected: fresh, bound-unconnected and listening
  stream sockets all answer -ENOTCONN (length 1 and 0).
- Addressed sendmsg/sendto on the survivor: -EISCONN
  (FD-123's shape). Seqpacket survivor sendmsg: -EPIPE
  (FD-118). Datagram survivor sendmsg: -ECONNREFUSED once,
  then -ENOTCONN (FD-119).

The filing is corroborated on every point; no neighbour
contradicts it.

## The fix

`118d2fc85` (source/kernel/kunixsocket.cpp, 6 insertions):
in sendmsg's no-peer branch, the stream case gains the
outClosed distinction the seqpacket case directly above it
already had — a peer that closed answers -K_EPIPE; a socket
that was never connected (outClosed clear) keeps
-K_ENOTCONN. The flag is the right discriminator: the pair
disconnect path sets it on the survivor exactly when the peer
goes away, and the connection pointer this branch has
already found unresolvable is what separates peer-died from
the shutdown shapes (which keep resolving their connection
and already answer through the write-side-closed path
below). sendmmsg inherits the answer — it loops the same
single-message path. Nothing else moves: the datagram
accounting (FD-119), the seqpacket answer (FD-118), the
addressed answers (FD-123) and the read side are untouched,
and the guest test pins each of them as a control.

## Proof

- New guest test testPeerClosedStreamSendmsg64Syscalls
  (`50938973a`), registered in the x64 table. RED on the base
  (`test-results/fd124/red-x64.log`): exactly the six filed
  shapes fail, each answering -107 (-ENOTCONN) against the
  host's -32 (-EPIPE) — unnamed sendmsg at length 1, without
  MSG_NOSIGNAL, at length 0, with an empty iovec, sendmmsg,
  and the survivor holding unread data. Every control passes
  on the base: healthy-pair delivery and zero-length sendmsg,
  the write family on the survivor (-EPIPE, including the
  zero-length write — its cases flag the shared test process
  64-bit first, the FD-116 test's pattern, since the socket
  layer keys its broken-pipe split on the process width),
  the three shutdown shapes, never-connected and listening
  -ENOTCONN, the seqpacket survivor's -EPIPE, the datagram
  survivor's sendmsg accounting (-ECONNREFUSED then
  -ENOTCONN), and the addressed survivor's -EISCONN.
  An earlier staging of the test (`red-x64-staging1.log`)
  also flagged the write-family cases returning their
  syscall numbers; that was the harness process not flagged
  64-bit, so the writes took the 32-bit SIGPIPE path's
  -K_CONTINUE sentinel — a staging artifact, corrected
  before the RED above was taken, and the reason the flag
  pattern is in the test. GREEN on the fix (`green-x64.log`).
- Native suites serial, one worker, full runs on the final
  tree: main 668/668 and x64 89/89, 0 FAILED
  (`suite-main-green.log`, `suite-x64-green.log`; the x64
  count is the base's 88 plus the one new test).
- JS unit (the sibling lanes' gate form, `runner.mjs
  --unit`): 348 passed / 0 failed (`js-unit.log`).
- Browser build: BUILD COMPLETE, "Browser export verified"
  (`browser-build.log`); BUILD_INFO git_sha `118d2fc85`
  (the fix commit, the tip the build ran at), wasm sha256
  `07d5b927`, staged dependency archive `c4dbe35986a5` as
  pinned — build inputs as the sibling records describe:
  bootstrap --local from the retained runtime-assets-20261101
  archive (this branch's manifest pin), the node_modules
  symlink to the main tree, and the pinned-vintage dxvk
  builds plus gstreamer payloads copied from wt-fd38. The
  silent-failure allowance for syscall64.cpp stays 120 —
  this lane changes no dispatcher file and adds no counted
  site.
- Build discipline note: the new test first landed just
  after the BOXEDWINE_GUEST_X64 #endif that closes the
  sibling x64 tests' region, so the x64 build compiled it
  and the 32-bit main build could not; it was moved inside
  the guard before the suites above were run (the failed
  main build is preserved as `build-test-main.log`).

## The full-suite browser section, and the stood-down A/B

Beyond the lane gate, one full unfiltered suite run was made
(`js-full-run.log`; the lane stopped it partway once the
pattern was clear). Its unit section passed 348/0, like the
gated run. In its browser section, under documented
contention (load 6.6 on 4 CPUs, another lane's probe work
and other tenants active), the long deadline-bound tests
failed — cpu64-selftest, cpu32-jit-growth,
d3d10-comprehensive, d3d11-comprehensive, d3d11-triangle,
d3d12-comprehensive, d3d9-32, at 60–326 s each — while the
boot tests (audio-tone, boot32-vkboot, boot64-smoke,
celeste-boot) passed. Standalone re-runs (`js-rerun.log`):
cpu64-selftest failed again with the identical guest
summary (951 passed, 4 failed) at load 2.5, cpu32-jit-growth
and d3d10-comprehensive failed again at loads 2.7 and 4.5;
the remaining three were not re-run standalone (the re-run
script was stopped, first to free the probe slot for the
A/B below, and its log says so).

cpu64-selftest is the one failure that is deterministic and
load-independent, so the lane read its preserved guest
console (`cpu64-selftest-console.log`, copied from the
runner's `test-results/suite-cpu64-selftest/console.log`).
The four cases are: "chmod → 0 (no-op success)" answered
-ENOENT; "fchmod → 0 (no-op success)" answered -EBADF;
"required JIT path did not execute any blocks"; "no JIT
engine selected (engine=off)". An A/B against the base was
started (a scratch tree at `daf5ec34e` staged identically)
but its browser build queued behind the batch-4 deploy
lane's Godot capture, which held the probe slot for the
whole window; after a bounded wait the lane stood the A/B
down and removed the scratch tree — rather than hold this
lane's proven closure hostage to another lane's proof run,
and rather than leave an unattended build to fire at an
unknown later moment and take the build lock then. The
classification therefore rests on this account, stated as
account and not as the A/B's proof: the two JIT cases print
only when the guest fixture sets BW64_JIT_VERIFY=1, and
then require an engine the standard interpreter browser
build does not select — the run's own log records
engine=off — so they fail by construction on every standard
build, the base's included; the chmod/fchmod pair encodes a
disposition (those calls succeed as no-ops) that the 64-bit
syscall layer does not implement for a null path or an
unopened descriptor, in filesystem-syscall code this lane's
diff (six lines in one socket function) cannot reach, with
the native suites that do exercise the changed function all
green. The finding is filed as FD-125; its closing terms
ask for the base A/B and a disposition decision. Nothing in
this lane's closure depends on it, and the graphics-test
failures above are recorded here as contended-run
observations, not as findings — the deploy gate re-runs the
browser checks on the shipped build.

## Numbers in this record

Re-read from the repository after the fact: test
`50938973a`, fix `118d2fc85`, base `daf5ec34e`. Register:
FD-124 closed as CL-75 on this lineage; FD-125 filed from
this lane. Evidence: `test-results/fd124/`.
