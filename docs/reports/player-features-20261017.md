# Player layout, networking and image enhancement

The player groups Restart, Stop and Fullscreen in its toolbar. The redundant status row, run-state badge, iframe status banner, demo descriptions and requested helper copy have been removed. Diagnostics now says Show logs. Startup describes runtime loading, Windows file preparation, program startup and the first frame, with elapsed time. The activity reassurance requires more than fifteen seconds and logs within the last 2.5 seconds. A mapped window alone no longer hides the loading display.

The runtime canvas fits the guest aspect ratio inside the iframe, and the player follows the guest dimensions. Fullscreen centers letterboxing. Original canvas coordinates still drive SDL input; enhancement overlays ignore pointer events.

Settings adds internet access, off by default, through the existing Wisp service at `wss://kn0tzer.work.gd/wss/`. The pinned client is `@mercuryworkshop/wisp-js` 0.5.0. SOCKFS connects outbound IPv4 TCP streams without replacing the browser WebSocket constructor. Guest DNS uses local TCP responses and synthetic addresses; Wisp resolves the mapped hostnames. This handles the relay's blocked port 53 without server changes. TLS stays with the guest. The original fixed loopback relay remains available for existing diagnostic probes; the launcher explicitly selects Wisp.

Optional SDR image processing provides FSR1 EASU and RCAS, SGSRv1 and RCAS, or sharpening alone. WebGPU output is copied through a Canvas2D surface before WebGL upload because direct WebGPU-to-WebGL upload failed on the tested Vulkan backend. The regression checks all three modes before and after browser presentation with both WebGL2 and WebGPU sources. Source windows remain interactive, and renderer cleanup releases only enhancement resources. Shader attribution and upstream licenses ship with the assets.

The README Program support section uses the user's exact seven-program list.

## Verification

- `pnpm typecheck` passed.
- `pnpm test` passed: 95 legacy tests, one existing opt-in skip, and 127 suite unit groups.
- Eighteen focused startup, fitting, networking, enhancement and Vulkan lifetime checks passed after the final changes.
- `pnpm audit --prod --audit-level high` reported no known vulnerabilities.
- `pnpm build:export` passed with the existing pinned native runtime.
- `node web/tests/launcher-ui.mjs` passed all nine demo routes, folder detection and reruns, loading and retry states, appearance, narrow layouts, keyboard focus, ZIP ranges, backups and storage failure handling.
- `node web/tests/enhancement-backends.mjs` passed thirteen GPU pixel samples covering WebGL2, WebGPU and the intermediate copy.
- `node web/tests/player-features.mjs` passed real Win32 pointer coordinates within two guest pixels at 320, 640 and 1440 pixels wide with SGSRv1 enabled, fullscreen, cleanup, and real x86/x64 guest DNS plus HTTP 200 over the live Wisp relay. Coordinate reports are append-only so the browser does not race the guest by truncating its output file.
- Live `https://kn0tzer.work.gd/winbox/` returned the required isolation headers. Eleven deployed assets matched the tested export byte-for-byte. A real Notepad guest painted at the fitted player size and stopped without a notice or page errors.

Browser evidence is under `test-results/player-features` and `test-results/enhancement-backends`. Demo routing checks do not establish gameplay compatibility.

## Deployment

The tested static export replaced `/home/ubuntu/kn0tzer/winbox` using an atomic directory exchange. Existing public tools were retained. No container was restarted. The previous export is preserved at `/home/ubuntu/backups/winbox-player-20261017T212412Z/previous`; exchanging these directories again restores it. Browser installation and save databases were not migrated.

Internet access and image settings apply on the next program launch. UDP, IPv6, incoming connections and HDR output are outside this implementation.
