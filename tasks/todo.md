# Ordered work

- [x] **S: Close the cold-thread JIT helper failure.** Owners:
  `source/emulation/cpu/{jit,normal}` and the targeted unit regression.
  Acceptance: the regression fails before the fix, passes in production
  and test modes, and the cold `LOCK INC` run and full CPU32 table pass.
  Depends on the existing native test build.
  Verified: focused native test passed in one second; the full CPU32 table
  passed with zero failures in 187 seconds; all 110 fast unit files passed.
- [ ] **M: Close the browser allocator crash.** Inspect the isolated WASM
  sanitizer probe; fix the owning subsystem, add a small regression, and
  repeat the original audio scenario. A passing retry is insufficient.
- [ ] **M: Validate and commit inherited memory/probe fixes.** Owners:
  memory64, guest fixtures and suite harness. Acceptance: focused browser
  mapping/input checks, fast suite and native memory checks pass.
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
