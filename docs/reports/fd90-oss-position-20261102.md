# FD-90 — the OSS position face reports the playback position

Lane: `lane-fd90` (worktree `wt-fd90`), based on integration tip
`cc1d89ab3`, carrying lane-fd85b's fixture commit (`d0b3409b8`) and
register commit (`3bd68edf6`) cherry-picked unchanged.

Fix: **`dd00eee9b`** — *FD-90 fix: the OSS position face reports the
playback position* (`source/kernel/devs/devdsp.cpp`,
`include/kdspaudio.h`, `platform/sdl/kdspaudio.cpp`).
Proof test: `223cb8fbf` (suite test), proof adjustments `adb032921`
(fixture pacing + criteria, see below).

## The defect, as filed

A native ALSA client using MMAP_INTERLEAVED over the guest `default`
PCM (plug -> oss -> /dev/dsp — the chain winealsa uses) received the
full 4,096-frame buffer, committed it at full scale, and then saw
`snd_pcm_avail_update` return 0 permanently; the same client's writei
run read delay = -190,464 frames and avail 194,560 on a 4,096-frame
buffer. The register named the root shape (GETOPTR = write position
modulo capacity; GETODELAY = constant 0 under bwPump) and the fix
shape (a played-frames counter sourced from the bwPump ring
consumer). The investigation confirmed that shape and found two
further defects in the same contract, each proven from device-side
instrumentation before being fixed.

## What the evidence showed, in order

1. **Write-position pointer (the filed defect).** GETOPTR answered
   from `bytesWritten`; with the queue permanently non-empty under
   bwPump, the derived hardware pointer had no playback motion.
2. **RESET clobbers the negotiated geometry.** Device-side logging
   showed SETFRAGMENT negotiate frag=4096 / capacity=16384 during
   hw_params, after which every GETOPTR reported cap=8192 /
   frag=1024. The ALSA plug->oss prepare issues SNDCTL_DSP_RESET
   *after* hw_params, and DevDsp's RESET handler reset the fragment
   size/count to defaults (and format/rate/channels). The position
   modulus therefore disagreed with the stream buffer for the
   stream's whole life, corrupting ALSA's pointer unwrap (this alone
   explains the writei incoherence: delay -190,464 / avail 194,560).
3. **The pointer aliases under a non-paced sink.** With (1) and (2)
   fixed, the device reported the truth — and a run in which the
   Web Audio sink drained the whole buffer between two guest reads
   showed avail stuck at 0 with GETOPTR answering bytes=16384,
   ptr=0: the pointer had lapped exactly one buffer and read as
   "no motion". Disassembly of the shipped guest plugin
   (`libasound_module_pcm_oss.so`, alsa-lib 1.2.8) shows its pointer
   callback consumes **only** `count_info.ptr` (struct offset 8);
   `bytes`/`blocks` do not feed the hardware pointer. A ptr-only
   contract is only meaningful when successive reads advance by
   less than half the buffer — guaranteed on real hardware by the
   sample-paced DAC and fragment interrupts, and not guaranteed
   here, where the worklet sink drains in bursts (whole buffer at
   once in one run; ~46 ms of audio then a long stall in another).

## The fix

- **Played counter from the ring consumer.** KDspAudioSdl counts
  frames pushed into the bwPump ring (`bwAudioPushJs` now returns
  the frames actually pushed) against the JS read index the
  AudioWorklet advances; DevDsp computes played = bytesWritten -
  queued, the queue expressed in device bytes and capped at the
  buffer capacity. GETOPTR reports played bytes / fragment blocks /
  in-buffer ptr; GETODELAY reports the same bounded queue. On the
  native path `getQueuedBytes()` falls back to the SDL queue size,
  as before.
- **RESET keeps stream configuration.** SNDCTL_DSP_RESET still
  drops queued audio and restarts the position counters, but no
  longer resets format/rate/channels/fragment geometry: it is a
  prepare/drop, and the configuration hw_params negotiated must
  survive it.
- **The reported position is slew-limited.** GETOPTR's reported
  played counter advances toward the true counter by at most half
  the buffer per read and never rewinds. It never claims audio as
  played before the ring consumed it; it is exact whenever the
  inter-read drain is under half the buffer (every sample-paced
  consumer); and it restores the successive-read invariant the
  ptr-only contract is built on.

## Why this does not revive the polling stalls

The GETODELAY constant-0 fiction existed to keep waveOut/mmdevapi
pollers from stalling. Three reasons the truthful answers do not
bring the stalls back:

- **Mechanism.** The only answers that changed are GETOPTR's
  position and GETODELAY's value. GETOSPACE and `isWriteReady()`
  — the surfaces a poller actually waits on — are byte-for-byte
  unchanged, and DevDsp writes still never block. Nothing in the
  write or poll path changed its timing.
- **The new delay is the audible truth, bounded.** GETODELAY is
  measured from the actual ring consumer and reaches 0 exactly
  when the audible queue empties — the property the discarded
  wall-clock estimate was trying to fake. A client polling delay
  sees it fall at the rate audio is really consumed; a client
  polling GETOSPACE sees exactly what it saw before the fix.
- **Measurement.** The shipped Wine-audio regression
  (`common-dependencies`: Gecko/MSHTML + OpenAL through this
  chain) behaves identically on the fixed build and on a control
  build of the base tip under the same conditions (both currently
  time out at the test's 180 s MSHTML wait on the heavily loaded
  shared box, with the guest verifiably progressing and the audio
  stage not yet entered — the FD-85-era recorded state, reaching
  the FD-85 payload audio wait, was taken under lighter load).
  `audio-tone` passes on the fixed build.

## Proof

New suite test `web/tests/suite/browser/alsa-mmap-stream.test.mjs`
drives the lane-fd85b fixture (`tools/guest-fixtures/alsatone.c`)
in a fresh x64 session:

- **RED on the base build** (unfixed `cc1d89ab3` tree): assertion
  failed with the recorded signature — `avail stayed 0 after the
  first buffer in 7 of 7 later cycles
  (avail=4096,0,0,0,0,0,0,0)`. Evidence:
  `test-results/fd90/red-run.log`.
- **GREEN on the fixed build** (build stamp git_sha `adb032921`,
  wasm sha256 `c699b04d...`): `SUITE RESULT: 1 passed, 0 failed`.
  s16mmap: avail recovers every cycle, 18,432 frames committed
  (4.5 buffers); floatmmap: 22,528 frames (5.5 buffers); writei
  s16: avail=574 delay=3522 (sum 4096), float: avail=1288
  delay=2808 (sum 4096), RESULT 0, xruns=0. Evidence:
  `test-results/fd90/final-run.log`,
  `test-results/fd90/green-final-console.log`.
- Fixture pacing note: the fixture's mmap cycles now sleep 500 ms
  between cycles (`adb032921`). On this chain `snd_pcm_wait`
  returns without blocking (the poll face never sleeps — by
  design, above), so an unpaced loop samples a single instant
  eight times; the sleep restores the DAC-paced cadence a real
  mmap client runs at. Pass criteria negate the recorded defect
  exactly: more than one buffer committed, avail recovering in at
  least two later cycles, all position readings within
  [0, buffer].

## Regressions on the fixed build

- `audio-tone`: PASS.
- `common-dependencies`: identical to the base-tip control build
  under the same box load (see above); no worse in any stage.
- Legacy JS suite: only the standing FD-64 four
  (`vkwebgpu-v2` DESC_SET trailing-bytes set, byte-identical
  error), as on the integration tip.
- Suite unit tests: pass (the probe-slots test failed once inside
  a parallel chain under load 20 and passes standalone).
- Native suites, serial (gate mode): main **0 FAILED** (358 s),
  x64 **0 FAILED** (21 s). (An earlier parallel-mode run showed
  two timing/CPU flakes — setitimer probes, Xadd — both green in
  serial, as the gate runs them.)

## Edges and limits, stated

- The ring is shared by every SDL audio device in the session.
  `openAudio` re-snapshots the read base when it re-arms the
  bridge (it already reset `bwRingRead = bwRingWrite` before this
  change); frames another device queued before that reset are
  discarded by the bridge exactly as before, and any resulting
  over-count of this device's consumption clamps at zero queued.
- Audio still inside SDL_AudioStream's resampler (pushed to the
  stream, not yet converted into the ring) counts as played; that
  residue is bounded by the stream's conversion buffer.
- Dropped frames (ring full at push time) advance the write index
  past the read index and so count as consumed; they were never
  audible, and counting them keeps the position contract coherent
  for the streams that remain.
- Capture (GETIPTR) is untouched; FD-90 is a playback defect.
- FD-85 is untouched by this fix and stays open on its own terms:
  its break is inside the 64-bit Wine payload above this chain,
  and this lane's fixture proves the chain itself now serves both
  access modes coherently.
