# FD-85 continuation — the Wine audio leg, bisected to the 64-bit Wine payload

Lane `lane-fd85b` (worktree `/home/ubuntu/wt-fd85b`), based on the
integration tip `cc1d89ab3`. Fixtures taken from `lane-fd85`
(`wasapi-tone.c`, `winmm-tone.c`); new fixtures added here
(`alsatone.c`, `dumpstat.c`, `alsadump-*.conf`). No emulator code
changed; the dist used is the base tip's own build (clean bootstrap
from the retained `runtime-assets-20261101` archive;
`bw64-build.sh` "Browser export verified", support archive
`c4dbe35986a5`). Evidence: `test-results/fd85b/` — six diag
sessions, each preserved with its console log and capture JSON.
The lane-local diag tests were removed after use (the FD-85 lane's
precedent); their outputs are the evidence.

## The question this lane inherited

FD-85 (lane-fd85's update) proved the break sits between a Windows
audio client's successful API calls and the emulator's audio input
(`KDspAudioSdl::writeAudio`), inside Wine's mmdevapi → winealsa →
ALSA(OSS-plug) leg or its contract with the emulator's OSS device,
and named the sub-split it could not make: shared-buffer handoff
vs driver feeder vs ALSA plug conversion. Its suggested next
measurements were an ALSA-client probe on the guest's Linux side
and a Wine-side feeder content trace. This lane ran both, plus an
A/B the earlier lane did not have: the same fixture on the 32-bit
Wine stack.

## Measurement 1 — the ALSA-client probe (alsatone)

A native guest ELF (`alsatone.c`, dlopens the guest's own
libasound.so.2) plays the control tone (440 Hz square, amplitude
12000/32768) through the `default` PCM — the exact chain winealsa
uses — in four modes, one session, bracketed by the Wine OpenAL
fixture (silent reference) and the direct-DSP control:

- **S16_LE writei: heard.** `bwPump: S16 input first non-zero
  peak=12000` at the emulator input; page capture peak 0.4550.
- **Float32 writei: heard.** 191,908–204,324 non-zero captured
  samples, peak 0.4550 — the plug layer's format conversion
  serves real data.
- **mmap modes: a separate defect** (now FD-90). mmap_begin
  hands out the full 4,096-frame buffer; the first commit lands
  it at the input full-scale (`S16 input first non-zero
  peak=12000 (n=8192)`, `first push 3250 frames peak=0.4550`);
  after that `snd_pcm_avail_update` returns 0 permanently
  (eight fixed cycles, both formats). The device's position
  reporting is incoherent in both access modes — the same
  client's writei run read `snd_pcm_delay` = **-190,464
  frames** and avail **194,560** on a 4,096-frame buffer.
  Root shape: DevDsp reports GETOPTR as a write position
  modulo capacity and GETODELAY as a constant 0 under bwPump
  (`source/kernel/devs/devdsp.cpp`), so ALSA's hardware
  pointer never advances. Writei clients ignore position;
  mmap clients stall after one buffer.

Two harness lessons are recorded in the evidence, not hidden:
the writei modes dump four seconds of tone into the shared
bwPump ring instantly, and its ~4.4 s drain contaminates the
next capture window — the first diag's mmap windows are
unusable for attribution, which is why diag2 re-ran the mmap
modes first on an empty ring (input-side lines above are from
that session); and a page-capture window can read zero for a
buffer that provably reached the input (diag2's mmap windows
vs the input lines) — input-side evidence outranks capture
windows for one-shot buffers.

## Measurement 2 — what winealsa hands libasound (file-plugin dump)

`~/.asoundrc` wrapped the `default` PCM in libasound's `file`
plugin, so the exact bytes each client hands to libasound land
in a guest file; `dumpstat.c` reports their content. Validated
in-session both times (the native client's dump is its full
float stream: 1,536,000 bytes, all 384,000 samples non-zero,
peak 0.3662).

- Over the stock chain (file → plug → oss), the OpenAL run's
  dump: **72,192 Float32 stereo frames, every sample zero.**
- Over libasound's `null` device (file → null; no emulator
  device anywhere in the path): **100,224 frames, every
  sample zero.** The emulator's OSS position contract —
  Measurement 1's incoherent numbers — is therefore *not* the
  trigger for the zeros.
- Traced rerun (WINEDEBUG=+mmdevapi,+alsa, still over null):
  33,792 more frames of zero, and the trace shows the client
  side healthy — one AudioClient (`Initialize` → `Start`;
  ALSA buffer 1920 frames, MMDevice buffer 1536), 26
  GetBuffer / 24 ReleaseBuffer cycles of the full buffer with
  flags 0, GetCurrentPadding oscillating 0 ↔ 1536, and
  `alsa_write_data` running 118 times at avail 1920. Both
  sides' entry points succeed; the data is lost between
  ReleaseBuffer and the driver's write, inside the prebuilt
  driver.

## Measurement 3 — the other Wine stack

The identical OpenAL fixture on the **32-bit Wine stack**
(WineHQ Wine 10; the 64-bit stack is this project's Wine 8
payload) in the same harness, same emulator, same chain:
**heard** — page capture 26,763 non-zero samples, peak
0.2269; non-zero S16 at the input; `OPENAL: RESULT 0`;
direct-DSP control also heard (peak 0.3662).

## Verdict

FD-85's remaining defect is convicted to the **64-bit Wine
payload's mmdevapi → winealsa render path**: it emits an
unbroken stream of zeros to libasound on every device,
while the client APIs above it succeed and every layer
below it (libasound writei and conversion, the OSS device,
the emulator's audio input, the page) is measured serving
real data, and a different Wine build renders the same
fixture audibly through the identical path. FD-85 stays
open on payload-fix terms (register update carries the
exact statement); the fix it points to — the 64-bit
mmdevapi/winealsa pair rebuilt or replaced and re-pinned
by the FD-73 procedure — is a payload lane of its own and
was deliberately not attempted here, because it re-pins
wine64.zip against the pending batch-2 composition.

Not claimed: which side of the driver's internal
GetBuffer→writei copy drops the data. No emulator-side
measurement reaches inside a prebuilt payload, and the
tracing shows both sides succeeding. Also not claimed:
that the mmap/position defect (FD-90) contributes to any
shipped-test failure today — it is filed on its own
measurement.

One more observation from the runs, recorded not chased:
in one of five OpenAL runs the source never finished
(`OPENAL: FAIL source did not finish`, run5) where the
FD-85 lane's runs and this lane's others completed —
consistent with the same broken consumption accounting,
not separately characterized.
