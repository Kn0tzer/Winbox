# FD-265 — the guest DNS test's negative stage was fed a fabricated answer

Date: 2026-11-09. Branch: `lane-fd265` (off integration tip
`47a45376b`). Fix commit: `cc6552ad2`.

## End state asked

The tracked register's entry FD-265 is closed: the browser guest test
it names (`winsock-dns`) passes in a suite run, with the cause of its
failures named from evidence, the fix committed on the lane branch,
the register entry closed naming the fixing commit and its proof, and
the light checks through the checking queue matching the standard
acceptance shape.

## What the filing claimed, and what the evidence shows instead

The filing (batch-11 merge lane, 2026-11-08) says the fixture's
witness lines reach the console one character at a time through a
console host, so the test times out even though resolution,
connection, and the HTTP read all complete. That reading is wrong in
its load-bearing half, and the correction matters:

- On the 32-bit guest the witness output does arrive wrapped in the
  console host's ANSI repaint stream (each character between cursor
  hide/show sequences, a space sent as erase-to-end-of-line plus a
  cursor-forward). But the harness has always undone that transform
  before matching (`terminalText` in
  `web/tests/suite/lib/probe-base.mjs`: cursor-forward becomes a
  space, the remaining escapes are stripped), so the test's view of
  the console carries the witness lines as contiguous text. On the
  64-bit guest the same output arrives as plain lines to begin with.
- In every preserved gate console — batch 11, batch 12, batch 13 —
  and in this lane's reproduction run, the guest printed the complete
  witness sequence promptly after spawn. The value that never matched
  the test's `/BW_DNS done=1/` wait was `done=0`. (The batch-13 merge
  record's "zero `BW_DNS` lines in the preserved consoles" is a
  raw-text grep over the escape-interleaved 32-bit stream, which can
  never contain the contiguous string; through the harness's own
  transform the lines are all there.)

So the witness was never blind. The test failed because the fixture's
negative stage genuinely failed: the made-up name resolved and
connected, so the fixture reported `done=0`.

## The mechanism, from an instrumented run on the failing tip

This lane reproduced the failure on the integration tip `47a45376b`
(suite result FAIL winsock-dns, 324,375 ms, the same 300 s wait
expiry as the gates) with temporary page-side logging inside the DNS
answer path and the stream-open path (instrumentation removed before
the fix commit; run log `test-results/fd265/run-diag-current.log`,
console preserved at `test-results/fd265/diag-current/console.log`).
The page logged, for the 32-bit guest:

- `dns q=example.com type=1 ans=172.29.1.0`, then a stream open to
  `example.com` port 80 — the guest printed `gai=0`, `addr=172.29.1.0`,
  `connect=0`, `http=1 bytes=928`.
- `dns q=bw64-nonexistent.invalid type=1 ans=172.29.2.0`, then a
  stream open to `bw64-nonexistent.invalid` port 80 — the guest
  printed `bad_gai=0`, `bad_addr=172.29.2.0`, `bad_connect=0`,
  `done=0`.

Two product behaviours combine to produce that:

1. **The synthetic DNS fabricates an address for any name.** With
   `network=1&wisp=1`, the guest resolver speaks TCP DNS to the
   planted nameservers; the page intercepts port 53 and answers from
   its synthetic map (`web/network.mjs` `dnsResponse`, over
   Emscripten's `DNS.lookup_name`, which hands out a fresh
   `172.29.x.x` address for any syntactically valid hostname and
   never fails). A name that cannot exist gets a successful A answer
   exactly like a real one.
2. **The connect completes locally.** A connect to a synthetic
   address is translated back to the hostname and opened as a relay
   stream (`createWispBridge`); the socket reports open when the
   stream is created, before the relay's own resolution attempt
   returns. The relay does refuse the stream — its log shows the
   open to `bw64-nonexistent.invalid:80` failing with
   `getaddrinfo ENOTFOUND` and closing with reason 3 (seen live
   2026-11-09 06:08 UTC during the batch-13 deploy acceptance; the
   batch-11 filing observed the same refusal) — but by then the
   guest's connect has already returned success. Neither stage of
   the fixture's negative check (fail at resolution, or fail at
   connect) can ever fire.

The fixture's terms are the right ones — an unresolvable name must
not produce a working connection — and the capability's positive
path was never broken (`winsock-http` passed in the same gates).

One historical note, stated at the strength the evidence supports:
on the FD-04 lane (2026-11-07, batch-9-vintage guest files) the same
fixture printed `bad_gai=11001` and the test passed. The page code,
the fixture, and the guest's Linux rootfs (the resolver library) are
byte-identical between that run and the gates; what changed
underneath is the re-cut guest Wine/prefix file set (batches 10–11)
and the emulator build. Isolating which re-cut file flipped the
guest's answer was attempted and is blocked by the tree's own
guards: a build with the 2026-11-06 file set swapped in is refused
by the runtime-completeness check, which requires the FD-189
prefix sections that set predates (log:
`test-results/fd265/build-diag-oldassets.log`). The flip's carrier
is therefore not isolated. It does not change the disposition: on
every current configuration the guest provably receives the
fabricated answer, and the fabrication is the defect.

## The fix

`cc6552ad2` (`web/network.mjs`): `dnsResponse` now answers NXDOMAIN
(RCODE 3, no answer records, the synthetic map never consulted) for
names under the RFC 6761 special-use endings that can never resolve
— `.invalid`, `.test`, `.example` — exactly as a real resolver
answers them. Names that merely contain a reserved word
(`invalid.example.com`) still resolve through the map. A new unit
test in `web/tests/network.test.mjs` pins the refusal (RCODE, empty
answer section, map untouched) alongside the standing positive
cases; it fails on the unfixed code (exactly that one test red, the
other five green) and passes with the fix (6/6).

Not changed, stated plainly: for a name that does resolve at the
relay but whose upstream connection is refused, the guest's connect
still completes locally and the failure surfaces on send/receive via
the stream close. That is the relay model's standing shape (the
browser side cannot complete a real handshake synchronously); the
FD-265 closing terms require failure at resolution or at connect
for an unresolvable name, and that is now met at resolution.

## Proof

- Fresh build of `cc6552ad2` in this tree: "Browser export verified"
  (`test-results/fd265/build-fix.log`); BUILD_INFO git_sha
  `cc6552ad2`, wasm sha256 `62f6123e…` — byte-identical to the
  batch-13 wasm, as expected: the fix is page-side JavaScript only.
- `winsock-dns` — **PASS (161,936 ms)**, suite result 1 passed /
  0 failed (`test-results/fd265/run-fix.log`). Both architectures'
  witness sequences, from the run's own preserved console
  (`test-results/fd265/proof-fix/console.log`):
  - x86: `BW_DNS gai=0`, `addr=172.29.1.0`, `connect=0`,
    `http=1 bytes=931`, `bad_gai=11001`, `done=1`.
  - x64: `BW_DNS gai=0`, `addr=172.29.2.0`, `connect=0`,
    `http=1 bytes=928`, `bad_gai=11001`, `done=1`, fixture exit
    status 0.
- `winsock-http` — **PASS (21,425 ms)** on the same build
  (`test-results/fd265/run-fix-http.log`): the networking path the
  fix touches shows no regression.
- Network unit tests: 6 passed / 0 failed on the fixed tree,
  including the new NXDOMAIN test; sabotage run against the unfixed
  `network.mjs`: exactly the new test fails.
- Checking queue: see the follow-up commit on this branch recording
  the pipeline verdict on the final tip.

## Register disposition

FD-265 is closed in the register on this branch (see CL-194): the
fixing commit is `cc6552ad2`; the proof is the suite run above. The
entry's original closing terms asked why the spawn attaches a
rendering console host in the composed tree; per the correction
above, the console host attaches on both the passing and failing
configurations (and the harness transform handles it) — it was
never the cause. The terms' substance — `winsock-dns` passing in a
run with the mechanism named — is what this closure rests on.
