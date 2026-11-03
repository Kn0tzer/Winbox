# FD-113..115 — socket follow-ups from the message-delivery lane (2026-11-03)

Branch `lane-fd113-115`, based on `lane-fd109-112` tip
`963272707`. Commits: `8550a9823` (tests), `1fcfe3018`
(fix), `b80adf804` (fixture corrections), plus the
register/record commit carrying this file (which also
carries the two JS harness touches under Verification).

Numbering note: this branch's entry numbers are its own
lineage's. On the composed integration register the same
numbers belong to other entries (FD-109/110 are the
fd23res pair there, FD-111..114 the message quartet); the
batch-4 merge audit renumbers in filing order — the wedge
filing first, then the fd105-108 lane's four filings, then
this lane's FD-113, FD-114, FD-115 and its new FD-116.
Closures below are CL-64, CL-65, CL-66 provisionally, this
lineage's next after CL-63.

## FD-113 — a closed pathname socket answers like the host

Substance: on the host, closing a bound pathname socket
leaves its file node behind. Sends and connects to that
path answer -ECONNREFUSED while the node remains, a bind
over it answers -EADDRINUSE, and only an explicit unlink
turns the answer into -ENOENT. The emulator's binding
socket unlinked its node in its destructor, so a stale
path answered -ENOENT and a re-bind silently succeeded.

Host measurement (`test-results/fd113-115/hostref-fd113.c`,
box kernel): sendto, datagram connect and stream connect
to the stale path all -ECONNREFUSED (111); bind over the
stale node -EADDRINUSE (98); after unlink, sends and
connects -ENOENT (2); bind after unlink succeeds and
delivers.

Fix (kunixsocket.cpp): the destructor no longer removes
the bound node. Nothing else had to change — the FD-111
resolver already answers ECONNREFUSED for a node whose
socket is gone (its kobject is a weak pointer and has
expired) and ENOENT only when no node exists, and bind's
existing-node check now answers EADDRINUSE the way the
host does. The abstract-namespace cleanup in the same
destructor is unchanged (abstract names have no node).

Proof: new test testStalePathnameSocket64Syscalls RED on
the base (sendto -2, connect -2, bind over the stale node
0) and GREEN on the fix. The FD-107 and FD-109 pathname
tests re-bind the same paths across their cases and now
fail on the stale node, as the same code would on the
host; their fixtures unlink before binding (commit
`b80adf804`), the correction the FD-109..112 lane already
made once for FD-107's fixture.

## FD-114 — stream bytes keep call order across the two stores

Substance: the receiver keeps two stores — a message
queue (sendmsg) and a byte buffer (plain writes) — and
every read path serves the queue first, so bytes written
earlier through write() were served after later sendmsg
bytes, and a single read never spanned the two stores.
The host serves one call order (write "Z" then sendmsg
"W12" reads "ZW12"; the reverse order reads "W12Z").

Host measurement (`test-results/fd113-115/hostref-fd114.c`):
both orders and a four-way interleave read back in call
order; a descriptor sent with sendmsg arrives attached to
the FIRST byte of its chunk (a byte-at-a-time recvmsg
sees 'Z' without it, then 'W' with it); a plain read over
a carrying chunk returns the bytes and discards the
descriptor, as queued consumption always did here.

Fix (kunixsocket.h/.cpp): the queue becomes the ordered
store once it holds anything. Three rules keep one
invariant — the buffer only ever holds bytes older than
any queued message: (1) sendmsg, before queueing its own
message on a stream socket, moves the peer's buffered
bytes into the queue ahead of it
(`flushRecvBufferToQueueLocked`); (2) the plain write
paths (object write, writeNative, the native nowait
write), when the peer's queue is non-empty, queue their
bytes as a payload-only message instead of buffering
them (`queueStreamBytesLocked`); (3) when the queue is
empty the writes buffer exactly as before, so
single-store traffic (including the X11 wire sockets,
whose peers never queue) is untouched. Read paths are
unchanged: they already serve queue-then-buffer, which
is now a true seniority order, and a read can span a
flushed chunk and the message behind it.

Kept boundary: recvmsg still returns at most one queued
message per call and drops a message's unread remainder
when the guest's segments fill — that shape predates
this lane (FD-110's datagram semantics) and the host's
finer stream recvmsg segmenting was not part of the
filing.

Proof: new test testStreamWriteSendmsgOrder64Syscalls
RED on the base (every interleave case served the queue
first: "W12" before "Z", "BD" from the four-way mix) and
GREEN on the fix, including the ancillary-position,
plain-read-discard and MSG_PEEK cases.

## FD-115 — a gathered write queues one datagram

Substance: writev on a connected datagram socket queued
one datagram per iovec segment; the host queues ONE
datagram holding the whole gather.

Host measurement (`test-results/fd113-115/hostref-fd115.c`,
extending the filing lane's own program): two segments
arrive as one datagram "ABCD"; an empty middle segment
changes nothing; a gather totalling zero returns 0 and
queues nothing; a bad segment answers -EFAULT with
nothing queued (the gather is atomic); a plain
zero-length write is a different case — see FD-116.

Fix: both writev paths gather before queueing. The
object writev (32-bit and shared callers) collects its
segments into one packet inside the existing critical
section, checking every segment's readability before
anything is queued, then queues once through the new
`sendDatagramToPeer` tail (the ENOTCONN / closed-pipe /
queueDatagramLocked sequence factored out of
internal_write's FD-111 branch, which now calls it too);
/dev/log keeps its print behaviour on the gathered bytes.
The 64-bit dispatcher cannot use the object writev (it
answers -K_EINVAL for writev there by design and loops
through sys_write64), so sys_writev64 gathers itself for
datagram unix sockets: the descriptor gate, then every
segment validated with the FD-101 readable-prefix helper
(-EFAULT, nothing queued), then one writeNative of the
gathered packet. A gather past sys_write64's per-call
ceiling keeps the old per-segment loop; an all-zero
gather returns 0 and queues nothing; non-datagram
descriptors are untouched.

Proof: new test testWritevDatagram64Syscalls RED on the
base (two datagrams "AB" then "CD"; the bad-segment
gather queued invented bytes) and GREEN on the fix;
stream writev still delivers "ABCD" through the read
path.

## Filed from this lane

FD-116: a zero-length PLAIN write on a 64-bit datagram
socket queues nothing — sys_write64 returns 0 on a zero
count before resolving the descriptor — where the host
queues an empty datagram (and the emulator's 32-bit
write and sendmsg both queue it). Measured in
hostref-fd115.c; filed, not fixed: it belongs to the
64-bit write family's lane, and FD-115's own zero-total
case is the opposite answer (nothing queued) and is
unaffected.

FD-117: the JS slice test unix-connect-handoff no longer
compiles against the current kunixsocket.cpp — FD-111's
destructor and connect growth (abstract registry,
one-way peer links, the recvBuffer witnesses) outgrew
its stub class. Verified failing identically on the
unmodified base `963272707` in a scratch worktree, so
it is this lineage's pre-existing breakage, not this
lane's; the FD-109..112 lane's un-built-tree JS
accounting (failures sampled) did not isolate it. The
product paths it covers pass in the native suites. The
harness completion is its own piece of work; filed,
not fixed.

## Verification

New x64 tests testStalePathnameSocket64Syscalls,
testStreamWriteSendmsgOrder64Syscalls,
testWritevDatagram64Syscalls: RED on the base `963272707`
(exactly the three new tests failing, each for the filed
reason; the other 81 passing) and GREEN with the fixes.
Full native suites serial: main 668/668, x64 84/84,
0 FAILED. JS unit: 347 passed / 1 failed — the one
failure is FD-117 above. This lane's two harness
touches, both in the register/record commit: the
reviewed silent-failure allowance for syscall64.cpp
moves 119 -> 120 (FD-115's dispatcher branch adds one
counted guest-validation errno return, the -EINVAL mode
gate — the same reviewed move the FD-105..108 lane made
at 117 -> 119), and the socket64-message slice test's
start anchor is tightened to sys_guestReadablePrefix64's
definition, because the fix's forward declaration of
that helper above sys_writev64 had become the anchor's
first match and dragged sys_writev64 into a slice whose
stubs never covered it (the test passes on the base and
passes again with the tightened anchor). Browser build:
BUILD COMPLETE, "Browser export verified"; BUILD_INFO
git_sha `b80adf804` (the build ran at the
fixture-corrections tip), wasm sha256 `ac291689`, the
staged dependency archive matches the pinned
`c4dbe35986a5`. Evidence: `test-results/fd113-115/`.

Build-input note for the merge lane: as the sibling
records describe — bootstrap --local from the retained
runtime-assets-20261101 archive, the node_modules symlink
to the main tree, and the pinned-vintage dxvk builds plus
gstreamer payloads copied from wt-fd38; the staged
archive verifies as `c4dbe35986a5`.
