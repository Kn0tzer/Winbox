# Winbox profiling (partial)

This is a partial report. Only the original Doom JIT=1 run produced a profile; its screenshots showed the DOOM title/intro screen, so it does not answer the real-gameplay question. The later focused Doom JIT=1 attempt was stopped during wrap-up before it reached a profile window.

## Method and build

- Profiling build: `wasm_sha256=519129fa32be6d64e45afdef242f34ba4c5b772bd102227251ffc21587467e96`, source `git_sha=be54a75667db5f3c7de22e37b433406abbe6c68b`, `profiling=true`.
- The harness used a 1 ms CDP sampling interval and the guest PC sampler. Intended profile window was 60 seconds.
- Original Doom JIT=1 run: load average at gate start 8.09; waited 2012 s until 2.23; load at run start 2.23, window start 4.92, and run end 12.4.
- Window: 2026-10-20 14:52:11.625Z run start; profile window elapsed seconds 2129.343–2191.558.
- Raw profile directory: `test-results/profile/winbox-profile-20261020/doom-jit1`.

## Doom, JIT=1 (original run; title/intro screen, not valid gameplay)

Largest measured flowchart stage was Guest memory/MMU at 11.78% of non-idle self time. Emscripten runtime accounted for 23.3%, but is not one of the requested flowchart boxes. Total non-idle self time was 98,529.423 ms; unclassified self time was 1.20%.

| Stage | Self time share |
|---|---:|
| Guest memory/MMU | 11.78% |
| 64-bit CPU JIT compiler | 10.39% |
| 64-bit CPU JIT-compiled code | 10.38% |
| 64-bit CPU interpreter | 9.07% |
| X11 wire server + SDL present | 8.61% |
| 32-bit CPU interpreter | 4.37% |
| Kernel syscalls + VFS | 2.11% |
| Audio | 0.09% |
| 32-bit CPU JIT compiler | 0.01% |

Guest mapped-file samples: Wine 45.39%, game executable 13.17%, other 41.44%; DXVK, vkd3d, and Vulkan shim had no samples. Most `other` samples were `(unmapped)` wineserver samples, so this split is incomplete. Top-function details and machine-readable output remain in the raw profile directory.

## Runs without usable profiles

- Doom JIT=0 did not map a game window after the 1200 s retry. The log reports `doom.exe` exiting with status `4294967295`.
- Doom JIT=1 focused retry was interrupted during wrap-up before completion; it is not verified.
- Portal JIT=1, Celeste JIT=1, and Notepad JIT=1 were not completed.

No slowest-box conclusion for actual gameplay is available from these runs. Next step: diagnose the Doom interpreter-mode Wine launch/exit, then complete the five gameplay profiles and regenerate the full stage, function, and guest tables.
