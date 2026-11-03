# FD-109..FD-112 — the four defects the FD-105..108 lane filed: recvmsg's fabricated source name, the message queue invisible to read/recvfrom, named datagram delivery with no route, the vanishing zero-length datagram

Lane `lane-fd109-112`, 2026-11-03, from `lane-fd105-108` tip
`20ab45fc0`. Test commit `901efb53a`; fix commit `3a04d7682`;
follow-up commit `0523a2d85` (the stream-recvfrom
compatibility answer, a null-thread guard on the native write
path, and the FD-107 test fixture correction below). Host
reference programs and outputs in `test-results/fd109-112/`
(host kernel 6.17.0-1020-oracle).

Work note: this lane's first staging area was lost when the
local machine rebooted mid-session (its /tmp was wiped before
any product edit had been pushed to the box). Every edit was
reconstructed from the lane's design and diff reviews into a
durable stage and re-verified file by file against the base;
the RED/GREEN runs below are all from the reconstructed tree.

## FD-109 — recvmsg reports a fabricated source name

The object layer's recvmsg never wrote msg_namelen on the
queue path, so the 64-bit bridge's initialised value — the
offered capacity — survived as the reported length and that
many bounce-buffer bytes were copied out as the "address".
Measured on the host (`hostref-names-fd109.c`): a pathname
sender reports 2 + strlen(path) + 1 (the trailing NUL counts),
an abstract sender 2 + 1 + strlen(name); a short offer still
reports the full length and copies what fits; flags stay 0.
There is no autobind — an unbound sender reports 0 via sendto
and via datagram connect alike, and a socketpair peer reports
0 through recvmsg and datagram recvfrom. A relative-path bind
is reported with its literal relative name, and replying to a
zero-length sender address answers ENOTCONN.

The fix carries the sender with the message: bind records the
socket's bound address image (`localAddr`), abstract names
resolve through a length-exact registry keyed by the bytes
after the leading NUL, and each queued message stores its
sender's image. recvmsg writes the truncated copy and always
the full length; the stream-buffer fallback reports the
connected peer's bound address instead of the hardcoded 0 it
used for stream sockets.

RED on the base (`test-results/fd109-112/red-run.log`): a
socketpair recvmsg reported namelen 128 — the fabrication
itself — and a named sender's message reported the offered
128 rather than its real length. GREEN on the fix.

## FD-110 — the message queue is invisible to read/recvfrom

`KUnixSocketObject::read` drained only the stream buffer, and
recvfrom routed through it, so anything queued by sendmsg (or
sendto) was unreachable by the single-call receives. Measured
on the host (`hostref-read-fd110.c`): read() on a datagram
socket returns the whole datagram with its segments
concatenated, one datagram per call; a small buffer truncates
and discards the rest; recvfrom behaves as read; MSG_PEEK
repeats without consuming; an empty datagram ahead of a real
one is consumed by a read returning 0. On a stream pair,
sendmsg bytes are readable via read().

The fix serves the queue from both read paths the guest can
reach — the object read/recvfrom (which now also reports the
queued message's sender) and the native read the 64-bit
bridge's plain read/write syscalls actually use
(`readNative`/`writeNative`; discovering that the bridge
bypasses the object read/write entirely was part of this
lane's work, and the connected datagram write on the native
path previously discarded its payload while reporting it
sent). Consumption is shared through one helper with host
semantics: datagrams pop whole, stream queues trim by bytes,
and a zero-length request still retires an empty datagram.

Two measured boundaries are recorded, not hidden. The host
preserves call order between stream write() and sendmsg()
(`ZW12` in the reference); the emulator's two stores are read
queue first, so interleaved calls can read back out of order
— filed as FD-114. And one compatibility answer is kept from
the base: recvfrom from an unnamed connected STREAM peer
reports family + length 2 (`hostref-stream-addr.c`, measured
after the main suite's testKernelGrace pinned it), while a
datagram peer reports 0 — on the host the stream form is the
exception, not the rule.

RED on the base: every read of queued data answered
-EWOULDBLOCK. GREEN on the fix.

## FD-111 — named datagram delivery has no route

sendto's destination branch answered -EDESTADDRREQ
unconditionally, and datagram connect() set the connected
flag and returned -EISCONN without linking a peer. Measured
on the host (`hostref-connect-fd111.c`, `hostref-tail-fd111.c`,
`hostref-unspec-fd111.c`): datagram connect returns 0,
re-connect returns 0 and switches the peer, getpeername
reports the peer's name; addressed sendmsg/sendto on a
connected socket deliver to the named destination (the
address wins for that call); an AF_UNSPEC connect disconnects
(afterwards sends answer ENOTCONN); a missing pathname
answers ENOENT, a missing abstract name or a non-socket node
ECONNREFUSED, a stream-bound path EPROTOTYPE, an addressed
stream send EOPNOTSUPP, a foreign-family destination EINVAL,
and an unaddressed unconnected send ENOTCONN — not
EDESTADDRREQ.

The fix resolves named peers through one helper shared by
sendto, sendmsg's header destination, connect and
getpeername: pathname targets resolve through the filesystem
(ENOENT / ECONNREFUSED / EPROTOTYPE as measured), abstract
targets through FD-109's registry, delivery through the same
queueing helper the connected paths use. Datagram connect
links the peer one-way — the peer is not poisoned at this
socket's close the way a socketpair end is — and the kconnect
wrapper no longer answers -EISCONN ahead of the object for
datagram sockets, which is what made reconnect and
AF_UNSPEC disconnect unreachable. The dispatcher's bare
sendto/recvfrom branches never bounced their address
arguments at all (a recvfrom address was silently dropped);
they now bounce the sockaddr both ways like the msg forms.

One measured host behaviour is NOT matched and is filed as
FD-113: after a bound pathname socket closes, its node
remains on the host and addressed sends answer ECONNREFUSED
until unlink; the emulator unlinks the node in the socket's
destructor, so the same send answers ENOENT.

RED on the base: addressed sendto reported 4 bytes sent with
nothing delivered; connect answered -106 (-EISCONN);
unconnected sendmsg answered -89 (-EDESTADDRREQ). GREEN on
the fix.

## FD-112 — a zero-length datagram queues nothing

`applySocketFilter` returns the packet length when no filter
is installed — 0 for an empty packet — and sendmsg's queueing
tail treated `!kept` as filtered. Measured on the host: an
empty datagram makes the receiver readable, recvmsg returns
0 with flags 0, an empty datagram carrying SCM_RIGHTS
delivers the descriptor, and a zero-length STREAM sendmsg
queues nothing (recvmsg answers EAGAIN). The fix treats only
a non-empty packet's 0 as filtered; the empty datagram queues
with its credentials and descriptors, and the empty stream
case still queues nothing.

RED on the base: the receiver never became readable and
recvmsg answered -EWOULDBLOCK. GREEN on the fix, stream guard
included (it passed on the base too, as designed).

## The FD-107 fixture correction

With FD-109 fixed, the sibling lane's
testRecvmsgNameCopyback64Syscalls failed its two fault
scenarios: its sender was an unnamed socketpair peer, and on
the base the bridge attempted a 128-byte name copyback only
because FD-109's fabrication reported the offered length for
that unnamed sender. The host faults only when a name is
actually copied, and FD-107's own host reference used a named
sender. The fixture now binds its sender to a pathname and
connects it through FD-111's datagram connect (25-byte
reported address, so the crossing geometry still crosses);
every original assertion is unchanged and passes. The FD-107
closure (CL-58) carries the note.

## Verification

New tests testRecvmsgSenderName64Syscalls,
testReadQueuedDatagram64Syscalls,
testNamedDatagram64Syscalls,
testSendmsgZeroLength64Syscalls (x64 suite), RED on the base
for the filed reasons and GREEN on the fix; the sibling tests
(FD-105..108) pass beside them, FD-107's with the fixture
correction above. Full native suites serial: main 668/668,
x64 81/81, 0 FAILED. JS unit on this lane's un-bootstrapped
tree: 272 passed / 76 failed, every sampled failure a missing
generated web/vendor file — the same accounting the
FD-58..60 lane recorded for an un-built tree; the
socket-relevant silent-failure tests pass (no-silent-failure,
no-silent-drops, silent-failure-scanner), and the reviewed
silent-failure allowance for syscall64.cpp stays 119.
Browser build: BUILD COMPLETE, "Browser export verified";
BUILD_INFO git_sha `0523a2d85`, wasm sha256 `a691ae8f`; the
staged dependency archive matches the pinned `c4dbe35986a5`.

Build-input note for the merge lane: as the FD-105..108
record describes — the retained runtime-assets archive for
this branch's pinned vintage (20261101) via bootstrap
--local, the node_modules symlink to the main tree, and the
pinned-vintage dxvk builds plus gstreamer payloads copied
from wt-fd38.

Filed from this lane: FD-113 (a closed pathname socket
answers ENOENT instead of the host's ECONNREFUSED), FD-114
(stream write/sendmsg interleave order across the two
stores), FD-115 (writev queues one datagram per segment
instead of one per gather — measured in
`hostref-writev-fd115.c`). Also recorded as boundaries in
the FD-110 work: read(0)/recvfrom(0) consuming an empty
datagram matches the host; descriptors attached to an empty
STREAM sendmsg go nowhere, as the message itself does.
Evidence: `test-results/fd109-112/`.
