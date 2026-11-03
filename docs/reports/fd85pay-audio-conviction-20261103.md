# FD-85 payload lane, second attempt — the audio chain is exonerated; the blocker is an emulator write loss (filed FD-87)

Lane `lane-fd85pay` (worktree `/home/ubuntu/wt-fd85pay`), 2026-11-02/03.
Brief's end state: the common-dependencies test's audio stage passes on the
64-bit stack with the responsible Wine audio payload rebuilt or replaced and
re-pinned, FD-85 closed, 32-bit audio unregressed — or, if unreachable, the
exact blocker with its measurement.

**Outcome: the end state is unreachable by any payload change, and the
blocker is measured to the instruction shape.** The tone never exists in the
client's own memory. Every audio layer below the client — Wine's
mmdevapi/winealsa pair, the ALSA→OSS chain, the emulator's DevDsp/bwPump,
the page capture — is proven sound by direct measurement. The defect is an
emulator-level loss of 64-bit guest writes, filed as **FD-87** with a pure
Win32 minimal repro. FD-85 stays open, re-gated on FD-87 (register addendum
on this branch). No payload was rebuilt or re-pinned, deliberately: the
measurements below show the shipped payload is not the defect, and the one
dist mutation made during diagnosis (a swapped OpenAL32.dll, §4) was
reverted; the dist's dependency pins are byte-identical to how the lane
found them.

## 1. Starting state (verified, not assumed)

The previous attempt ended without a report. Its dist was left unbootable:
it had swapped OpenAL32.dll (official 1.23.1) into both support archives,
so the archive (516,965,571 B, sha `091a266f…`) no longer matched the pin
(517,564,189 B, `c4dbe359…`) and `loadDependencies` rejected it. This lane
restored both support archives byte-identical from the live tree and killed
the attempt's stale suite runner by exact pid. Its instrumented driver
sources and its "P4" comment (a claim that a writing thread's stores become
visible to the driver's feeder thread only after the writer blocks) were
treated as unverified claims and re-measured from scratch.

## 2. The audio path is sound (driver-level bisection)

Instruments: the previous attempt's self-built driver trio
(`mmdevapi.dll`, `winealsa.drv`, `winealsa.so`) carrying PE-side and
unix-side scans of the render buffer at every release, swapped into the
dist's wine64.zip (member hashes verified after each swap), plus purpose-
built fixtures. Runs are `test-results/suite-fd85pay-diag9/` (e31),
`-diag10/` (e33), `-diag11/` (e34) in the worktree.

- A raw WASAPI full-ring client (1536-frame cycles, distinct constant fill
  per cycle, immediate readback after each fill) works perfectly, in both
  a tight polling arm and a `Sleep(60)` arm: every cycle's fill reads back
  exactly, the driver's scans match, bwPump logs the tone, the page
  capture counts 20,158 non-zero samples (diag9).
- The event-callback shape (AUDCLNT_STREAMFLAGS_EVENTCALLBACK, the mode
  OpenAL uses) works identically (diag10, arm C), as does a worker-thread
  lifecycle with staging-buffer memcpy writes (diag11, 9,643 non-zero).
- Cross-thread buffer visibility in all directions is exact (diag12).
- In the same sessions, OpenAL's releases are all zeros — at the driver's
  PE scan, at the unix scan, and in the page capture — deterministically.

## 3. Inside OpenAL (library-level bisection)

OpenAL Soft 1.25.2 source was cloned and read; the shipped DLL is the
official `soft_oal.dll` (pin `3963b06e…`, 3,834,368 B).

- Its own log at level 3 is clean: WASAPI backend, stereo float 48 kHz,
  480/1536 update/buffer, no errors, no disconnects (diag14, e45).
- Forcing scalar code paths changes nothing: `disable-cpu-exts = all` via
  config — still silent (diag15, e43); a from-source scalar rebuild
  (mingw GCC 13, all `ALSOFT_CPUEXT_*` off; import profile identical to
  the shipped DLL) swapped into a re-pinned support archive — still
  silent, and the source still "finishes" at 44.1 kHz (diag16, e44).
- An instrumented from-source build (prints inside the WASAPI mixer after
  each render and inside the buffer-upload copy) gives the conviction
  (diag18, e46/e47): every mix prints absum 0.0, and the upload print
  reads `bytes=52920 s16count=26460 first=0 0 0 0 absum=0.0` — **the
  stored copy of the fixture's sine table is zeros because the source
  span itself reads as zeros** (`ALPROBE srcspan: ptr=00000000010b7eb0
  size=52920 first=0 0 0 0 absum=0.0`, diag19/e49). The pointer is
  identical on the fixture side and the library side; the destination
  span's size is correct. The mixer's subsequent zeros are faithful.

## 4. The fixture's own buffer is zeros (client-level conviction)

`openal-probe.exe` — the shipped fixture plus its own prints — reads its
sample buffer back as all zeros **immediately after filling it**
(`OAP: ptr=00000000010b7eb0 filled head=0 0 0 0 absum=0.0`, e49), on the
same thread, before any OpenAL call. From there the investigation left
audio entirely; the `fpprobe` series (pure Win32 console, committed under
`tools/guest-fixtures/`) bisects the shape:

- libm is innocent: runtime `sin` with memory arguments is exact,
  including sin(100.0) = −0.506366; cos/exp/log/tan/atan/asin/acos/pow/
  sqrt/fabs/floor all exact (diag21, fpprobe2). Constants from the pool,
  `.rdata`, `.data` all load exactly (diag22, fpprobe3).
- Integer and double stores in call-free loops persist and read back
  correctly after the loop; double multiply-add arithmetic is exact
  (diag20, fpprobe T2/T3/T7).
- The failing shape is writes interleaved with repeated cross-module
  calls. fpprobe6 (diag25): `s = sin(db[i])` stored per iteration reads
  back **all zeros after the loop** (true accumulated sum 184.1776).
  fpprobe7 (diag26): the same loop with an immediate readback after each
  store verifies **every write in-loop (mismatch count 0 of 1000, for a
  direct store and for a memcpy store)** — and the post-loop reads are
  still all zeros. A `.data` accumulator over the calls reads back
  0.0000; with a printf inside the loop the accumulator survives
  partially (71.8690 of 184.1776, fpprobe5 diag24), so the loss is
  shape-sensitive, not a clean page discard. An accumulator across a
  local noinline function or across `fabs` is exact — the trigger needs
  the repeated cross-module call in the loop.

That is the whole of FD-85: the shipped fixture fills its sine table in
exactly this shape, the fill is lost, OpenAL faithfully uploads and mixes
zeros, and the (proven-sound) audio chain delivers the silence it is
handed. The 32-bit stack does not show the loss — the identical fixture
is heard there (lane-fd85b's record) — matching the repro's 64-bit scope.

## 5. What was not done, and why

- **No payload fix was shipped.** Rebuilding or replacing mmdevapi,
  winealsa, or OpenAL32.dll cannot recover samples that never persisted
  in the client's own buffer; the register's FD-85 entry records this
  and FD-85 remains open on FD-87's terms.
- **32-bit audio regression check:** moot on this lane — no payload or
  shared-code change was made; the 32-bit stack is untouched and its
  tone was already proven by lane-fd85b.
- The shipped test was never loosened; no test file was modified. The
  lane-local diag tests used throughout were removed after use.

## 6. State left behind

- Register (this branch): FD-85 addendum (conviction + re-gate) and the
  new FD-87 filing; renumbering collisions with sibling branches are
  noted in the entry for the composing merge's audit.
- Evidence on disk: `test-results/fd85pay/` (diag logs e31–e56, the
  instrumented builds' provenance, `alprobe-alsosoft.patch`, built
  repro fixtures under `fixtures/`), `test-results/suite-fd85pay-diag*/`.
- The repro sources are committed: `tools/guest-fixtures/fpprobe.c` …
  `fpprobe7.c`, `openal-probe.c`.
- Generated state stripped (dist, `.scratch` with its Wine build trees,
  probe-slot leftovers); the worktree's dist dependency pins are restored
  to the found state (support `c4dbe359…`, 517,564,189 B).
