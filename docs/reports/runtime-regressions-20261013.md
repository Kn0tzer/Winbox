# Runtime regression checkpoint — 2026-10-13

The full browser suite passed all 11 files. The fast suite passed all 104
unit files and the legacy checks (73 passed, one existing skip). Native
checks passed 602 CPU32 cases, 315 CPU64 cases and the six Vulkan bridge
harness groups. Type checking and the browser runtime build passed.

The tested runtime WASM SHA-256 is `d8ad843c953fed7b5dced8322bf535911e4d44e0e3252b734dda7db4d6c20c3f`. Local evidence
is in `test-results/suite-*/`; D3D12 includes `triangle.png` and `pixels.json`.
Its clear and draw/present programs returned zero, and its compositor
screenshot contained 6,090 red-dominant triangle pixels. Input checks
observed all six key-down/key-up pairs and the relative mouse packet,
burst and negative-delta cases in both guest CPU modes. Audio checked actual
64-bit DSP output PCM, including nonzero samples and RMS.

The fixes cover UNIX acceptance/cancellation, fork descriptor snapshots,
audio callbacks outside the registry lock, guest terminal line framing,
32-bit standalone readiness, Vulkan PE proc lookup and WithCount argument
adapters, and pipeline binding before render attachments exist. Native
regressions reproduce the corresponding failures. The first D3D9 allocator
failure and its symbolized fork stack are preserved in the functional
backup directory; subsequent focused and full browser runs passed.

Celeste 1.4.0.0 reached `GAME DISPLAYED` after loading, but its captured canvas
was black. This boot result does not establish rendering or playability.
Interactive Celeste, Portal and Gang Beasts verification and the requested
optimization phase remain outstanding. These tests exercise selected
operations; they do not establish complete coverage of every API or variant.

## Contract checks added after the first checkpoint

The initial browser result was insufficient: guest return codes could pass
while the presented image was black. A new CPU32 D3D9 variant executes the
same four workloads through 32-bit DXVK and checks the actual Vulkan canvas.
Its clear initially produced zero lit pixels despite all four workloads
returning zero. The Vulkan producer discarded render passes with no draws,
including attachment clears used by the subsequent presentation pass.
Preserving those passes produced 286,296 red-dominant pixels. A native
serialization regression fails before that fix and passes afterward;
the Vulkan harness now passes seven groups.

Small guest contracts also reproduced and verified fixes for shared Wine
clock mappings, consecutive poll deadlines, and fullscreen clipped input.
Both CPU modes now advance Win32 clocks through consecutive sleeps and
deliver a clipped 1,500/-1,500 relative motion packet exactly once while
the window message pump remains responsive. XInput2 replies and pointer-grab
ownership have native protocol tests. The Mono fixture exercises a managed
thread, reflection, a file round trip and garbage collection without
loading Celeste's assets. Native CPU64 now passes 318 cases; the fast suite
passes 108 unit files and the unchanged legacy checks. Type checking passes.

`runner.mjs --game=celeste|portal|gangbeasts` selects executable subsystem
prerequisites and records their runtime hashes, missing checks and remaining
game-specific behavior. A passing preflight never sets `fullGameVerified`.
Interactive verification and optimization remain outstanding.

## Startup and FNA checks — 2026-10-14

The first Celeste preflight failed two probes before their guest entry points
ran. Both logs showed the launcher's old manual Wine server handshake
receiving no reply. The launcher reported readiness as soon as that request
was queued, and the host waited on the browser main loop for replies.
Normal 32-bit Wine now executes real PE programs and initializes its own
startup event; that bypass came from an earlier broken runtime.

The launcher now lets Wine initialize its services. Persistent server and
desktop pins are spawned sequentially in both CPU modes, with readiness
remaining false on a spawn failure or timeout. Executable launcher tests
cover sequencing and readiness; the unused manual protocol implementation
was removed after the fullscreen and clock guest probes passed with the
new path. The failed logs and runtime metadata are preserved under
`/home/ubuntu/backups/boxedwine64-functional-20261014/startup-before/`.

A small FNA integration program also passed: texture upload/readback,
SpriteBatch rendering and keyboard input. Its 16,384-pixel red rectangle
moved from center x=103.5 to x=223.5 after ArrowRight. This uses the same
Wine Mono/FNA/SDL2/OpenGL libraries as Celeste without loading game content.
It is now an additional Celeste preflight requirement, rather than evidence
that Celeste itself is playable.
