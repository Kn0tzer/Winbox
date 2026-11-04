# FD-118..119 — record-oriented unix sockets and the peer-close refusal (2026-11-03)

Branch `lane-fd118-119`, based on `lane-fd116-117` tip
`bf3588622`. Commits: `e29b91cc2` (the guest tests, RED on
the base, and the FD-116 pin flip), `07dbfd12a` (the fix),
`29e91057c` (the record predicate defined out-of-line, a
build fix), `635249dfe` (the unix-connect-handoff stub
completion), `ea69b4a06` (second fix sites: the addressed
sends and the writev gather), `cbb334643` (a test
address-staging correction), plus the register/record
commit carrying this file.

Numbering note: this branch's entry numbers are its own
lineage's. The batch-4 merge audit was already running
when this lane started, so this branch composes in
batch 5 and the audit renumbers there in filing order.
Closures below are CL-69 (FD-118) and CL-70 (FD-119)
provisionally, this lineage's next after CL-68; the new
filings are FD-120..FD-123 in this lineage. Each defect
is therefore also described by its substance.

## FD-118 — record-oriented unix sockets behave as records

Substance: SOCK_SEQPACKET unix sockets are created in
pairs without refusal, message boundaries are preserved
end to end, and an empty write is delivered as an empty
record. The host behaviour was measured first
(`test-results/fd118-119/hostref-fd118-119.c`,
`hostref-fd118-119b.c`, `hostref-fd118-119c.c`, kernel
6.17): a seqpacket socketpair succeeds; writes keep
record boundaries; a short read truncates and discards
the rest of the record; a zero-length write or sendmsg
queues an empty record; writev and sendmsg gather their
segments into one record; MSG_PEEK shows the front
record without consuming it; recvmsg on a pair end
reports an unnamed source; shutdown(SHUT_WR) makes own
writes answer -EPIPE and the peer's drained reads
answer 0; a peer close drains queued records, then
reads answer 0 and writes -EPIPE on the first and later
sends alike; named bind/listen/connect/accept works
with records flowing. Two measured shapes go beyond the
filing: an addressed send on a CONNECTED seqpacket
socket ignores the address and delivers to the
connected peer (an unconnected one answers -ENOTCONN
for every send), and a connect whose type differs from
the bound socket's answers -EPROTOTYPE in both
directions (seqpacket to a stream listener, stream to a
seqpacket listener); same-type not-listening answers
-ECONNREFUSED — that last answer is not modelled (the
connect queues as pending instead) and is filed as
FD-122.

The fix: ksocketpairFds accepts the type; a record
predicate (`isRecordType()`: datagram or seqpacket) on
KUnixSocketObject replaces the datagram-only branch at
the data sites — internal_write, the writev gather,
writeNative, read's payload walk, copyFromQueue's
front-message rule, sendmsg's empty-message queueing —
with the /dev/log print conventions kept datagram-only;
connect() routes seqpacket through the mutual branch
(the X11 wire-server shortcut stays stream-only, and a
new destination-type check answers -EPROTOTYPE in that
branch, for stream clients too); sendto gains the
connected-seqpacket addressed case; the 64-bit
dispatcher's FD-115 writev gather (sys_writev64) names
seqpacket as well, so one guest writev is one record
(the per-segment loop was the one case the guest test
caught arriving as two records). Proof: new x64 guest
test testSeqpacket64Syscalls, RED on the base (the pair
refused -EINVAL and a singly created socket took the
stream paths) and GREEN on the fix, covering pair
boundaries, truncation, empty records by write and by
sendmsg, the writev gather, MSG_PEEK, the unnamed
source, the addressed shapes, named accept, the type
matrix, shutdown and peer close.

## FD-119 — the first send to a peer that has closed answers the host's one-shot refusal

Substance: on a datagram socket whose peer has closed,
the first send answers -ECONNREFUSED and later sends
-ENOTCONN. Measured
(`test-results/fd118-119/hostref-fd118-119b.c`): the
one-shot holds on every connected send path (write,
sendto without an address, sendmsg, writev, the
zero-length write) and is per-socket, not per-path —
whichever path sends first spends it. Receiving what
the peer queued before closing does not spend it. An
addressed sendto to a live third peer succeeds and
does not spend it. A one-way connected datagram socket
(FD-111's shape: connected to a bound pathname peer)
shows the same one-shot when that peer closes.

The fix: the pair destructor arms a
peerCloseRefusalPending flag on a datagram survivor;
every no-peer send tail answers through
peerCloseSendAnswer — write and the writev gather via
sendDatagramToPeer, the native write, sendto's
no-address tail, sendmsg's no-destination tail — which
spends the flag as -ECONNREFUSED once and answers
-ENOTCONN otherwise. The one-way shape has no
back-pointer for a destructor to mark, so its shot is
spent lazily: a socket still marked connected and
one-way-linked whose peer object is gone gets the same
one-shot at its first no-peer send; a (dis)connect
resets the accounting. Addressed sends never consult
the flag, and the receive paths do not touch it.

The lane's own test caught one more site of the same
substance, fixed in `ea69b4a06`: sendto's addressed
branch and sendmsg's shared body answered the
broken-pipe path whenever outClosed was set, but for a
datagram socket outClosed is only ever set by its pair
peer's death (a datagram socket's own shutdown is
unimplemented), so after a pair close an addressed
datagram to a live peer was refused instead of
delivered — the measurement above says it delivers and
does not spend the refusal. Both sites now leave
datagram out of that check. Proof: new x64 guest test
testDatagramPeerCloseRefusal64Syscalls, RED on the
base (every first send -ENOTCONN) and GREEN on the fix;
the FD-116 zero-length test's closed-peer datagram pin
flipped to the same accounting in the tests commit and
passes.

## Filed from this lane

- FD-120 — a datagram pair survivor's drained read
  answers 0 (end of stream) where the host answers
  -EAGAIN; the destructor's inClosed marking reaches
  the read wait loop for every type. Receive-side
  neighbour of FD-119, outside its filed send shape;
  the FD-119 guest test pins the standing 0 with this
  entry as its reference.
- FD-121 — a receive on a never-connected socket
  answers -EPIPE for stream and seqpacket alike, where
  the host answers -EINVAL (stream), -ENOTCONN
  (seqpacket), -EAGAIN (datagram). Pre-existing
  stream-family gates, measured during FD-118's
  unconnected pass.
- FD-122 — a connection-oriented connect to a bound
  but not listening socket queues as pending (blocking
  waits, nonblocking -EINPROGRESS) where the host
  answers -ECONNREFUSED at once; the mutual connect
  branch never consults the listening state, for stream
  and seqpacket alike.
- FD-123 — a connected stream socket's sendto with a
  destination answers -EOPNOTSUPP where the host
  answers -EISCONN (106) on this kernel; an earlier
  lane's -EOPNOTSUPP measurement (FD-111) did not
  record whether its socket was connected, so the two
  are filed together for the audit to reconcile.

## Verification

- Guest tests: RED on the base
  (`test-results/fd118-119/red-run.log` — the
  zero-length test's closed-peer case answered
  -ENOTCONN twice, the seqpacket pair was refused
  -EINVAL, every first send in the refusal test
  answered -ENOTCONN) and GREEN on the fix
  (`green-run.log`), both at one worker.
- Native suites serial, one worker: main 668/668 and
  x64 87/87, 0 FAILED (`main-run.log`, `x64-run.log`;
  the x64 count is the base's 85 plus the two new
  tests; the main count is unchanged). The eight
  neighbouring socket tests (indices 33..40) also pass
  standalone on the fixed tree.
- JS unit on the built tree: 348 passed / 0 failed
  (`js-unit.log`), including unix-connect-handoff —
  its stub gained the K_SOCK_SEQPACKET constant and the
  two accounting members the extracted destructor and
  connect now reference — and the silent-failure
  scanners. The silent-failure allowance for
  syscall64.cpp stays 120: the writev gather gate adds
  no counted site.
- Browser build: BUILD COMPLETE, "Browser export
  verified" (`browser-build.log`); BUILD_INFO git_sha
  `cbb334643` (the tip the build ran at), wasm sha256
  `bdc64f37`, staged dependency archive `c4dbe35986a5`
  as pinned.
- Build inputs, as the sibling records describe:
  bootstrap --local from the retained
  runtime-assets-20261101 archive, the node_modules
  symlink to the main tree, and the pinned-vintage dxvk
  builds plus gstreamer payloads copied from wt-fd38.
