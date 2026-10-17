# D3D9 Pong frame breakdown — ranked hotspots

Source: `test-results/frametime-profile-pong2/` (211 frames, LIT=2324, game renders).
Wall equation `submit + trapOther + present + guest = wall` closes to <0.001ms.

## Ranked list (mean ms/frame, % of 226.9ms wasm wall)

| # | Hotspot | ms/frame | % wall | Notes |
|---|---|---|---|---|
| 1 | `vkQueueSubmit` record encode (bridge) | 150.7 | 66.4% | THE target. Was assumed to be guest-CPU; it is bridge C++ |
| 1a | └ `emitImageData` | 74.0 | 32.6% | **0 bytes hashed/sent** — pure per-call overhead |
| 1b | └ submit remainder (cmdbuf walk, `v2QueueFrame`) | 74.4 | 32.8% | Needs finer split (pass-2: ser2/queue timers) |
| 1c | └ `emitBufferData` | 2.3 | 1.0% | 1,051,136 B hashed, 1,048,576 B (1 MiB) sent/frame — throughput fine (~450 MB/s) |
| 2 | Other vk64 traps (48 calls/frame) | 34.5 | 15.2% | `vkCmd*` recording etc.; ~0.7ms/call avg — needs per-fnId split (pass-2: topT) |
| 3 | Guest interpreter (Wine+DXVK+Pong) | 27.0 | 11.9% | 369k insns/frame; NOT the bottleneck |
| 3a | └ `d3d9.dll` (DXVK) | ~5.9 | 2.6% | 21.9% of interpreter |
| 3b | └ `ntdll.dll` (Wine) | ~3.7 | 1.6% | 13.8% of interpreter |
| 3c | └ `ntdll.so` + `libc.so.6` + X11 | ~4.9 | 2.2% | |
| 3d | └ `tri9pong.exe` (game logic) | ~0.1 | 0.1% | Game itself is trivial |
| 4 | `vkQueuePresentKHR` | 14.7 | 6.5% | Includes hop |
| 4a | └ chunk hop to page (3 chunks, 1 MB) | 6.0 | 2.6% | EM_ASM + JS memcpy |
| 5 | Page-tier chunk decode | 1.0 | 0.4% | Negligible |

## Key implications for optimization
- **Do NOT optimize the interpreter first**: it is only 12% of the frame.
  A 2× interpreter speedup buys ~13ms.
- **Attack `vkQueueSubmit`**: 66% of the frame. Halving it buys ~75ms.
  - `emitImageData` burns 74ms while moving ZERO bytes — find and remove the
    per-call cost (suspects: `objOf` map lookups, `imageDone` set ops, or
    `readGuestRange` on empty ranges; get per-call counts first).
  - The other 74ms needs the ser2/queue split to attribute.
- **`trapOther` 34ms / 48 calls**: identify the expensive traps (pass-2 topT).
- **Transfer is fine**: 6ms for 1 MB; page decode 1ms. No action needed.
- The 1 MiB/frame buffer upload (bufdata) is throughput-healthy; if the buffer
  is actually static, the cross-frame dedup is already skipping re-hash
  (`dirtyGen` fast path) — verify with `bufB` hashed vs sent.

## Open questions (need pass-2 probe or code inspection)
1. `emitImageData` call count per frame and per-call cost breakdown.
2. `submit remainder` split: cmdbuf walk vs `v2QueueFrame` vs `emit` overhead.
3. Which 48 traps dominate `trapOther` and why ~0.7ms each.
4. Identity of the 1 MiB/frame buffer (DXVK staging? readback?).
