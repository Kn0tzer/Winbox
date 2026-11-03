# FD-105..FD-108 — the four defects the FD-102..104 lane filed: recvfrom's silent scatter, the unwalked control chain, the recvmsg name copyback, datagram sendto never delivered

Lane `lane-fd105-108`, 2026-11-02, from `lane-fd102-104` tip
`019bca650`. Test commit `39d7761c4`; fix commit `73d2ddc2f`;
slice-surface commit `eb57da3d9`; test-guard commit `db1bd2e22`
(the browser build's BUILD_INFO git_sha). Host reference
programs and outputs in `test-results/fd105-108/` (host kernel
6.17.0-1020-oracle).

## FD-105 — recvfrom's copyback drops received bytes silently

The dispatcher's recvfrom branch received through krecvfrom
into scratch and copied back through
`KMemory64::memcpyToGuest`, which writes nothing when its
access check fails: a receive into unwritable guest memory
reported the full count while the bytes vanished. Host
reference (`hostref-name-fd107.c` N5/N6, re-checked in
`hostref-n6-fd105.c`): an unwritable buffer answers −EFAULT, a
crossing buffer faults with the writable prefix landed, a
datagram is consumed by the failed call. The fix scatters the
copyback page by page like FD-104's recvmsg scatter: each page
piece is validated with the same writability predicate before
its copy, and the first unwritable piece answers −EFAULT with
the prefix already copied.

Two consumption details are settled, not hidden. A faulted
plain receive consumes the bytes on a stream socket too: the
object receive has already taken them into scratch and has no
pushback — FD-104's settled divergence, which this lane's test
pins (a following receive answers −EWOULDBLOCK); MSG_PEEK
consumes nothing on either side and is pinned delivering the
data afterwards. And in the re-check's 46-writable-byte
crossing geometry the host left 40 bytes, not 46 — its copy
engine drops the final partial block at the boundary, where
the page-piece scatter leaves the whole writable prefix (at
the 96-byte geometry both leave 96). The answer and the
consumption match everywhere; only the sub-page prefix length
on an already-faulted call differs, accepted with FD-104's
shape. The datagram half of the host shape is exercised
through FD-104's recvmsg cases: the emulator's message queue
is reachable only through recvmsg (read/recvfrom drain only
the stream buffer), filed as FD-110.

RED on the base (`test-results/fd105-108/red-x64.log`): an
unmapped buffer returned 16, a MSG_PEEK to an unmapped buffer
returned 16, a crossing buffer returned 200. GREEN on the fix.

## FD-106 — the sendmsg control chain past the first header

`sys_sendmsg64` examined only the first control header.
Measured on the host (`hostref-chain-fd106.c`,
`hostref-chain2-fd106.c`, `hostref-order-fd106.c`), the full
contract: headers are walked at 8-byte-aligned steps while at
least 16 bytes remain, and a trailing remainder under 16 bytes
is ignored; a walked header with cmsg_len under 16 or past the
remaining room answers −EINVAL; SCM_RIGHTS headers accumulate
(their descriptors all arrive, merged) under one cumulative
253 cap; SCM_CREDENTIALS requires cmsg_len exactly 28 (20, 24
and 32 answer −EINVAL); any other SOL_SOCKET type answers
−EINVAL; other levels are skipped and the walk continues. Two
ordering facts: the whole control area is copied before any
validation, so an unreadable byte anywhere in msg_controllen
answers −EFAULT ahead of every −EINVAL (even a malformed first
header, even with controllen under the header size); and a
rights header's descriptor values resolve as the walk reaches
it, so a bad value in an earlier header answers −EBADF ahead
of a later header's malformed length. The iovlen cap still
precedes the control import (−EMSGSIZE measured against an
unreadable control area, matching the existing order).

The fix replaces FD-102's single-header block with that walk,
at the same position ahead of the gather: whole-area
readability first, then per-header validation with in-walk
descriptor resolution; translation emits every rights header's
descriptors, in chain order, into the existing compact record
run. FD-102's first-header behaviours are subsumed unchanged
(its test passes unmodified).

RED on the base: a second rights header's descriptor never
arrived (stream and datagram); rights(0)+rights(1) delivered
nothing; a trailing SOL_SOCKET type-999 header, a trailing
length-8 header and a trailing length-4096 header all sent
silently; credentials of length 16 and 32 sent silently;
rights(200)+rights(100) sent instead of answering −EINVAL
for the cumulative cap; a bad descriptor in a second header
sent silently; controllen crossing into an unreadable page
sent (valid header) or answered −EINVAL (malformed header)
instead of −EFAULT; an unreadable 8-byte control area sent.
GREEN on the fix, guards included (slack under a header's
worth ignored, credentials and foreign levels walked past,
253 across two headers delivered intact).

## FD-107 — recvmsg's source-name copyback drops the address

After a successful receive, `sys_recvmsg64` copied the bounced
source address to the guest's msg_name through the same
silent-drop accessor. Host reference (`hostref-name-fd107.c`
N1/N2): an unwritable name target answers −EFAULT, the
received data and any passed descriptor are still delivered,
and the msghdr's namelen, flags and controllen fields are
left untouched. The fix defers the copyback past the control
translation: the name range must land whole or the call
faults there, ahead of every field writeback — so the success
path writes the same fields in a different order, and the
fault path writes none.

RED on the base: an unwritable name target returned 16 (with
the flags and controllen fields written back, 0x0 and 24,
where the host leaves them untouched); a crossing name
target returned 16. GREEN on the fix.

Found while staging this fix, filed as FD-109: for a queued
message the object layer never reports a source name at all,
so the bridge initialises the reported length to the guest's
offered capacity and copies that many bounce-buffer bytes as
the "address" — the host reports the sender's real name (11
bytes for the measured named sender, 0 for an unnamed one).
The copyback fault is FD-107's claim; the fabricated content
is its own defect.

## FD-108 — datagram sendto is reported sent, never delivered

`KUnixSocketObject::sendto` routed every send to the stream
write path, whose datagram branch discards the payload and
returns the length. The fix gives sendto a datagram branch
that queues a real datagram on a connected pair, mirroring
sendmsg's queueing tail exactly: the peer-closed answer
(writePipeClosed with MSG_NOSIGNAL honoured), the socket
filter applied to the payload (a filtered packet still
reports the submitted length), one length-prefixed segment,
the peer's queue, signal and async-ready notification. A
zero-length datagram queues as a real empty message (the
host delivers it; sendmsg's tail reads the filter's 0 as
"filtered" and queues nothing — filed as FD-112). A datagram
socket whose peer was never linked keeps the old fall-through:
connect() on a datagram socket sets the connected flag
without linking a peer object, and addressed sends answer
−EDESTADDRREQ — named-datagram delivery has no route at all,
filed as FD-111.

RED on the base: every delivery case received nothing
(recvmsg −EWOULDBLOCK after a sendto reporting 16, poll not
readable for the zero-length case). GREEN on the fix: one
datagram whole and exactly once, two in order with their
boundaries, a 100-byte datagram truncating at a 16-byte
receive buffer with the remainder discarded, the empty
datagram readable and received as 0, MSG_DONTWAIT accepted;
the stream-pair sendto guard unchanged.

## Verification

New tests testRecvfromBadBuffer64Syscalls,
testSendmsgControlChain64Syscalls,
testRecvmsgNameCopyback64Syscalls,
testSendtoDatagram64Syscalls (x64 suite), RED on the base for
the filed reasons and GREEN on the fix; the sibling tests
(FD-101..104) pass unmodified beside them. Full native suites
serial: main 668/668, x64 77/77, 0 FAILED. JS unit 348/0 with
a reviewed silent-failure allowance for syscall64.cpp
unloggedErrno 117 → 119 (`eb57da3d9`) — the net counted
returns are the guest-validation errno answers the fixes add
in the FD-101/FD-102 shape. Browser build BUILD COMPLETE,
"Browser export verified"; BUILD_INFO git_sha `db1bd2e22`,
wasm sha256 `77b9c090`; the staged dependency archive matches
the pinned `c4dbe35986a5`.

Build-input note for the merge lane: the build's generated
inputs were restored from the retained runtime-assets archive
(bootstrap --local) plus two payload trees. The integration
tree's own tools/dxvk build outputs do NOT match the pinned
support pack (their d3d9/d3d11/dxgi CRCs differ from the
pinned members on both architectures); the pinned-vintage
dxvk builds and the gstreamer payloads were copied from
wt-fd38, whose member CRCs match the pinned pack exactly, and
with them the staged archive verifies as `c4dbe35986a5`. The
pinned gstreamer generator no longer runs end-to-end: one
pinned deb (libass9 deb12u1) has aged out of the Debian
security pool.

Filed from this lane: FD-109 (recvmsg fabricates the source
name for queued messages), FD-110 (read/recvfrom cannot see
message-queued data), FD-111 (named datagram delivery has no
route), FD-112 (a zero-length datagram via sendmsg queues
nothing). Evidence: `test-results/fd105-108/`.
