# FD-202 — the delivered audio was whole; the check's capture was not (2026-11-07)

Lane: `lane-fd202-audio` (worktree `wt-fd202-audio`), based on
integration tip `334c70be4`, carrying lane-audio-e2e's three commits
(the strengthened check, the FD-201/FD-202 filings, the queue
addendum) cherry-picked so the fix stands on the filing.

## End state sought

Sound a program plays inside the emulator is heard whole: the
shipped audio check, which verifies the actual sample values,
passes on both 32-bit and 64-bit, and the delivered sound matches
what the program generated (right length, no inserted silence).

## The filing

FD-202 (lane-audio-e2e, record
`docs/reports/audio-tone-values-20261107.md`) measured, in three
runs on both architectures, a tone whose every sample arrives
bit-exact at the right pitch and peak, but with duty cycle 0.249
(truth ~0.50), RMS exactly peak/√2, active span ~12 s (truth 6 s),
and recurring silent gaps of exactly 4,096 frames — half of
everything "delivered" silence. The filing bounded the mechanism
to the delivery path between the guest's writes and the output
node the check captures, and did not guess further.

## What the preserved evidence already bounded

Two facts in the audio lane's own preserved console log
(`test-results/audio-e2e/run3-evidence/console.log` in
wt-audio-e2e) narrow the field before any new run:

- `bwPump: first push 44100 frames` — the emulator pushes the
  guest's audio into the output ring in one-second blocks, and
  the guest's six one-second writes were accepted in ~0.62 s of
  audio-stage time. A writer that delivers everything within the
  first second cannot pace out 4,096-frame blocks at half rate
  for twelve seconds, so a product-side delivery pattern of
  alternating sound/silence blocks has no mechanism.
- The page contains exactly one ScriptProcessorNode besides the
  worklet path: SDL's own emscripten pump. The bridge comment in
  `platform/sdl/kdspaudio.cpp` states that pump never delivers
  audio to the destination in this build — the emulator's
  writeAudio bypasses SDL's queue entirely via the bwPump ring —
  so that node's output buffers are silence by design. The
  strengthened check's capture, however, appends **every**
  ScriptProcessorNode's output buffer into the same array as the
  AudioWorkletNode tap. 4,096 frames is exactly that node's
  buffer size.

## The discriminating run

A one-off source-separated variant of the check
(`fd202-capture-split`, preserved with its evidence in
`test-results/suite-fd202-split/`) repeated the capture against
the live build with the two sources kept apart, and collected
the worklet's own `bw-audio-metrics`. One x64 run:

- **Worklet path alone** (the runtime's output node — what a
  listener hears): captured 262,144 frames per channel; peak
  0.3662109375 (exact), RMS 0.3662109375 (exact), duty 0.5001,
  active 5.933 s, **longest internal gap 0 frames**. Worklet
  metrics: `framesWritten 264600` (the guest's exact frame
  count), `framesRead 220544` with 44,184 still queued at the
  snapshot, `underrunFrames 0`, `totalUnderrunFrames 0`, every
  output frame nonzero, input peak = output peak = 0.3662109375,
  no worklet error.
- **ScriptProcessor path alone**: one node created with
  `bufferSize 4096, inputChannels 0, outputChannels 2`; 65
  buffers captured, **all 65 all-zero**; 266,240 frames of pure
  silence.
- **The shipped check's merged array, replicated in the same
  run in true callback order**: 528,384 frames, duty 0.2479,
  RMS 0.2578, active 11.97 s, longest gap exactly 4,096 frames,
  in clean alternating 4,096-frame data / 4,096-frame zero runs
  — FD-202's signature to three decimals, composed of one whole
  stream and one silent one.

## Verdict

The delivered audio is whole. FD-202's defect is in the check's
capture: it measured two output objects as one stream, and the
silent half belonged to a node that carries no audio in this
build. No product code changed in this lane, because the
product was not the defect.

## The fix

Commit `bf1a97710` (`web/tests/suite/browser/audio-tone.test.mjs`
only): ScriptProcessor output buffers now go to a separate
array, and the measured stream is the AudioWorkletNode's tapped
output; the ScriptProcessor array is measured only when no
worklet node exists (the runtime's fallback output path, where
its own output is a ScriptProcessorNode). No tolerance, fixture,
or assertion changed — only which stream is measured. The
diagnostic split probe was removed from the discovered suite
after use; its file and evidence are preserved in
`test-results/suite-fd202-split/`.

## Proof

The fixed shipped check, run against the live batch-10 build
(the probe dist was the live tree; its audio-path product code
is byte-identical to this branch's base, and this lane changes
no product code): **PASS on both architectures**. Values from
the check's own evidence JSONs
(`test-results/suite-audio-tone/audio-x64.json`,
`audio-x86.json` in wt-fd202-audio):

| Measure | Guest truth | x64 | x86 |
| --- | --- | --- | --- |
| Peak | 0.36621 | 0.36621 | 0.36621 |
| Frequency | 440 Hz | 440.75 Hz | 440.47 Hz |
| Duty cycle | ~0.50 | 0.50038 | 0.50046 |
| RMS | ~0.36621 | 0.36621 | 0.36621 |
| Active duration | 6.0 s | 5.954 s | 5.991 s |
| Longest internal silent gap | 0 | 0 ms | 0 ms |
| Channel difference | 0 | 0 | 0 |

Negative control: the same statistics over the pre-fix merged
array (preserved in the split evidence) fail every value
assertion — duty 0.248 (bar 0.46–0.54), RMS 0.258 (bar ≥ 0.293),
duration 11.97 s (bar ≤ 6.5 s), gap 92.88 ms (bar ≤ 20 ms) — so
the fixed check still catches this signature whenever it is
present in the stream it measures.

## Register

- **FD-202** — FIXED as a check defect, closed as **CL-142**
  (lane-local; settles at batch-11 composition). Its closing
  terms (the strengthened check passing on both architectures:
  duty ~0.50, RMS ~peak, duration ~6 s, no gap above 20 ms)
  are met by the pass above.
- **FD-203** — filed open: in the no-worklet fallback
  configuration, the check's fallback array still folds SDL's
  pump-node buffers into the runtime's own ScriptProcessor
  output, so the conflation can recur there. Low severity,
  check only; the shipped, gated configuration is unaffected.

## Honest remainder

- The proof ran against the live build, not a fresh build of
  this branch: the branch changes only the check (plus the
  cherry-picked filings/record), so a branch build's audio
  path is the same code the live build ships. The branch's
  light checks belong to the checking queue / batch-11 gate
  like every lane's.
- The fallback-configuration limitation is real and is filed
  (FD-203) rather than smoothed over: distinguishing the two
  ScriptProcessor nodes needs a runtime mark or an equivalent
  identification, which this lane did not add.
