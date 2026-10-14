# Ordered work

- [x] **S: Close the cold-thread JIT helper failure.** Owners:
  `source/emulation/cpu/{jit,normal}` and the targeted unit regression.
  Acceptance: the regression fails before the fix, passes in production
  and test modes, and the cold `LOCK INC` run and full CPU32 table pass.
  Depends on the existing native test build.
  Verified: focused native test passed in one second; the full CPU32 table
  passed with zero failures in 187 seconds; all 110 fast unit files passed.
- [x] **M: Repair the reproduced loading-screen memory race.** Inspect the isolated WASM
  sanitizer probe; fix the owning subsystem, add a small regression, and
  repeat the original audio scenario. A passing retry is insufficient.
  Verified: the native ASan regression fails before the label lock and
  passes afterward; three consecutive browser audio runs passed on runtime
  `c89c7870ff0d`, with measured nonzero PCM. Full-suite repetition remains
  part of the separate complete-test gate. Commit: `694abc75`.
- [x] **M: Validate and commit inherited memory fixes.** Owners:
  memory64, guest fixtures and suite harness. Acceptance: focused browser
  mapping checks, fast suite and native memory checks pass. Verified:
  native CPU64 321/321, native ASan lifetime checks, browser fork mapping
  and the independent QEMU/Linux fixture. Commit: `98c3a8e3`.
- [ ] **S: Validate inherited probe timing and input selection changes.**
  Owners: suite input/clock probes and probe slots. Acceptance: both CPU
  modes still pass input and clock assertions; D3D9 compositor pixels pass
  in both modes. Preserve the original motion totals and packet assertions.
- [x] **M: Repair framebuffer presentation and binding contracts.**
  Verified a failing guest fixture before the fix and a passing browser
  capture afterward: 320×240 drawable, 4,800 red blit pixels and over 60,000
  retained green backbuffer pixels. FNA and all 18 Celeste prerequisites
  passed; the real title and logo rendered. Commit: `7d53f065`.
- [x] **S: Repair valid FYL2XP1 numerical failures.**
  Tiny positive/negative values and negative zero failed before `log1p`.
  Exact QEMU differential comparisons now pass all 1,560 cases across 167
  forms, and native CPU64 passes 327 checks. The full rebuilt CPU32 table
  also passes with zero failures in 192 seconds. Commit: `2e2f7e1c`.
- [ ] **S: Close the real Celeste input stall.**
  A targeted test reproduces a platform pump blocking on a busy guest
  connection. The nonblocking fix passes that regression and all 113 unit
  files. Browser repetition and actual game controls remain required.
- [ ] **M: Close the remaining Wine startup heap faults.**
  The full browser run on `59d71ee62ed7` exposed allocator faults in audio,
  D3D10 and x64 input startup. A forced concurrent ZIP seek produced an
  ASan 1,028-byte write into a four-byte buffer. Locking ZIP positions and
  using atomic KFile preads closes that regression; 114 fast unit files and
  native CPU64 327/327 pass. Repeat the failing browser cases on the rebuilt
  runtime before closing the startup failures. Commit: `b6d2bc2a`.
- [ ] **S: Run the complete test gate.** Acceptance: all commands in
  `plan.md` pass against a recorded build. Depends on the repair tasks.
- [ ] **M: Verify Celeste gameplay.** Depends on its passing preflight.
  Save visible level, movement and jump evidence from the real game.
- [ ] **M: Verify Portal gameplay.** Depends on its passing preflight.
  Save menu, map, movement and camera/action evidence from the real game.
- [ ] **M: Verify Gang Beasts gameplay.** Depends on the first two games
  and its passing preflight. Save match, movement and grab evidence.
- [ ] **M per change: Measure and optimize Portal.** Depends on verified
  game correctness. Keep an attempt log with before/after measurements,
  variance and verdict; rerun affected checks after each retained change.
