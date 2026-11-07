# Audio tone check — sample values, not only frequency (2026-11-07)

Lane: `lane-audio-e2e` (worktree `wt-audio-e2e`), based on integration
tip `334c70be4`. A previous attempt at this lane was stopped by the
2026-11-06 disk emergency before committing anything; this run resumed
from its preserved branch and working copy.

## End state sought

The shipped sound check (`web/tests/suite/browser/audio-tone.test.mjs`)
proves the actual sound values a program produces, not only the pitch:
it verifies the produced tone's loudness shape, channel balance, duty,
duration, and completeness on both 32-bit and 64-bit, and passes
against the current code, with the measured values in its evidence.

## What the check measured before, and what it measures now

Before: the captured audio was reduced to a zero-crossing frequency
per channel and checked at 440 Hz ±5% — the FD-108 blind spot, where
stored values are wrong while every frequency-shaped check passes.
The guest fixture (`tools/guest-fixtures/dsptone.c`) is fully
deterministic, so the values themselves are checkable: a 440 Hz square
wave at ±12000/32768 (≈0.36621), identical on both channels, written
for exactly six seconds at 44100 Hz.

Commit `bcf24f33a` adds, per architecture, on the captured float
samples at the context's sample rate (44100 in every run below):

- peak level (±15% of 0.36621) and RMS level (0.8–1.15× of it);
- wave shape: RMS/peak ratio ≥ 0.8 (a square wave is ~1.0; a sine is
  0.707) and duty cycle 0.46–0.54;
- active duration: 6.0 ± 0.5 s, measured after waiting for the queued
  tone to drain (the capture is polled until it stops growing), so
  duration and completeness cover the whole tone, not a prefix;
- completeness: no silent gap longer than 20 ms inside the tone;
- channel balance: left/right RMS ratio 0.95–1.05, max per-sample
  channel difference ≤ 0.05, sample-count parity within one buffer.

Every measured value is written into the per-architecture evidence
JSON (`audio-x64.json` / `audio-x86.json`) before any assertion runs,
and both architectures are captured before any sample-value assertion
runs, so a failing run still leaves both architectures' measurements
on disk. The pre-existing checks (two channels, cross-origin
isolation, worklet present, frequency ±5%) are unchanged.

## Build

Fresh build in the lane tree: `tools/bw64-build.sh` printed
"Browser export verified" (`test-results/audio-e2e/build.log`);
BUILD_INFO git_sha `334c70be4`, 0 dirty, wasm sha256 `92b36180…`
(5,722,692 bytes — byte-identical to the live batch-10 build, as
expected: this lane changes no product code). Prep note: the resumed
working copy's payload links pointed at the previous attempt's
private store, deleted in the disk emergency; the dangling links were
removed and `winbox-wt-prep.sh` re-linked everything from the shared
store, whose pins verify against the tip's committed manifest.

## Runs

- Run 1 (15:02–15:32 UTC): never tested anything — it waited out its
  full 30-minute probe-slot budget behind the wave's build relay
  (the probe-starvation shape filed separately by the
  probe-parallelism lane). Log: `test-results/audio-e2e/run1-slot-timeout.log`.
- Run 2: reached the guest; the first version of the strengthened
  check caught the defect below on x64 and stopped before x86.
- Run 3: both architectures captured (assertions deferred until after
  both captures); failed on the same values. Evidence:
  `test-results/audio-e2e/run3-evidence/`.
- Run 4: same signature again. Note: `BW_EXPECT_AUDIO_WORKLET=0`
  appears nowhere outside this test — it only relaxes the test's own
  worklet assertion and does not switch the runtime's audio path, so
  run 4 is a third reproduction in the shipped configuration, not a
  separate-path measurement. Evidence:
  `test-results/audio-e2e/run4-evidence/`.

## Measured values (run 3; runs 2 and 4 agree to three decimals)

| Measure | Guest truth | x64 | x86 |
| --- | --- | --- | --- |
| Peak | 0.36621 | 0.36621 (exact) | 0.36621 (exact) |
| Frequency | 440 Hz | 441.9 Hz | 441.1 Hz |
| Duty cycle | ~0.50 | 0.2488 | 0.2490 |
| RMS | ~0.36621 | 0.25830 | 0.25836 |
| RMS/peak | ~1.0 | 0.7053 | 0.7055 |
| Active duration | 6.0 s | 12.01 s | 12.02 s |
| Longest internal silent gap | 0 | 92.88 ms | 92.88 ms |
| Frames captured per channel | 264,600 | 530,432 | 530,432 |
| Channel difference | 0 | 0 (sample-identical) | 0 (sample-identical) |

## What the numbers show — FD-202, filed open

Every sample the guest wrote arrives, bit-exact in value, on the
correct channel, at the right pitch. But an equal number of silence
frames is interleaved with it in blocks — the recurring silent gap is
exactly 4,096 frames (92.88 ms at 44100 Hz) — so half of everything
delivered is silence and the tone takes twice its true duration. A
listener hears any program's sound as roughly 93 ms of sound
alternating with 93 ms of silence. The guest's write side is not the
pacing: the session console shows its six one-second writes
(1,058,400 bytes in total) accepted in ~0.62 s of audio-stage time.

The mechanism is NOT identified here, and this record does not guess
one. The measurements bound it to the delivery path between the
guest's writes and the browser's audio output: downstream of sample
generation (values and channel identity are bit-exact) and upstream
of the output node the check captures. The frequency-only check this
replaces passed throughout, which is how this survived.

Filed as **FD-202** (open) in `docs/defect-register.md`. Its closing
terms are this strengthened check passing on both architectures.

## Register

- **FD-201** — the coverage gap itself (the shipped check verified
  only frequency): FIXED by commit `bcf24f33a`, closed as **CL-141**.
  Both numbers are lane-local and settle at batch-11 composition.
- **FD-202** — the delivered-audio defect above: OPEN.

## Honest remainder

The end state's "passes against the current code" clause is **not**
met, and cannot be met by calibration: the measured values are not
near the tolerances, they describe a different delivered waveform
(half silence) that no honest tolerance for this fixture could
accept. The strengthened check is RED on the current code for that
measured reason, on both architectures, in three runs. It passes
when FD-202 is fixed; fixing the audio delivery path is a separate
lane's work, not this lane's. Everything else in the end state —
the value measurements themselves, both architectures, the measured
values in the evidence — is delivered as specified.
