# Audio Real-World Verification

## Overview

`audio-verify.mjs` is the consolidated end-to-end audio verification probe for Boxedwine64.
It verifies that audio from a Windows guest actually reaches the browser's WebAudio output
-- not just that it buffers to SDL_QueueAudio.

## What It Verifies

| Phase | Test | Pass Criteria |
|-------|------|---------------|
| 1 | Spawn 440Hz tone WITHOUT user click | Silence: all abs(sample) < 0.02 (autoplay gate blocks) |
| 2 | Real Playwright click on canvas | clickHit=true AND AudioContext.state becomes 'running' |
| 3 | Spawn 440Hz tone AFTER click | freq 430-450Hz (zero-crossing), peak > 0.05, rms > 0.01 |
| 4 | DirectSound via dstone.exe | dsPeak > 0.05 (audible output) |

Also measures end-to-end latency: guest TONESTART log timestamp to audible onset
in captured PCM (page-time milliseconds).

## Infrastructure

- Probe coordination: uses `probe-slots.mjs` (acquireProbeSlot) -- waits for a free
  probe slot instead of the legacy exclusive /tmp/bw64-probe.lock.
- Build coordination: uses `build-lock.mjs` (waitForBuildIdle) -- waits for any
  in-progress WASM build to finish before launching the browser.
- Audio capture: installs an AudioContext tap via page.addInitScript() BEFORE any
  page script runs. Intercepts createScriptProcessor, attaches a zero-gain tap node
  that copies input PCM into timestamped chunks.

## Test Binaries (on the box)

- /tmp/audioprobe/audiotone.exe (257KB) -- waveOut 440Hz sine, 3.5s, 44100Hz/16-bit/mono.
  Logs "audiotone: TONESTART" when waveOutWrite is called.
- /tmp/audioprobe/dstone.exe (257KB) -- DirectSound 440Hz tone.
  Logs "dstone: DSOUNDSTART" on start, "dstone: stopped" on completion.
- Sources: /tmp/audioprobe/audiotone.c, /tmp/audioprobe/dstone.c

## Running

From repo root on the box:

    TAG=audio-verify-1 node web/tests/audio-verify.mjs

Output in test-results/<TAG>/:
- console.log -- full browser console
- summary.json -- phase results, pass object, overallPass boolean
- tap3.json -- captured PCM metadata (sample rate, count, chunk timestamps)

## Known Issues / Caveats

1. WHDR_DONE never set: winealsa does not mark wave headers done in this setup.
   The probe does NOT wait for WHDR_DONE; it waits a fixed 15s (phase 1) / 25s (phase 3)
   then kills the process. This is intentional.
2. Stuck spawns: bw64_spawn processes sometimes stall around wineboot-event handling.
   The probe retries each spawn up to 3 times, killing stuck PIDs between attempts.
3. Requires video mode: audio is gated on KSystem::soundEnabled, forced false at
   Emscripten startup until a real SDL mouse click on the canvas. The probe uses
   video mode (no novideo=1) and performs a real Playwright click in Phase 2.
   bw64_xwire_mouse_button injection BYPASSES the gate and does NOT unlock audio.
4. 32-bit audio untested: test binaries are 64-bit.

## Related Files

- web/tests/audio-realworld-probe.mjs -- original 4-phase probe (consolidated here
  with probe-slot and build-lock coordination)
- web/tests/audio-tone-probe-v2.mjs -- streamlined single-tone version
- web/tests/audio-boot-probe.mjs, audio-boot2-probe.mjs -- boot-time audio checks
- web/tests/audio-gz.mjs, audio-direct-p.mjs -- gamezip and direct variants
- web/tests/click-test.mjs -- canvas click verification helper
