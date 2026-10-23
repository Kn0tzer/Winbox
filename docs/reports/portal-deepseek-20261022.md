# Portal menu rendering — 2026-10-22

## Summary

Portal's D3D9 renderer reached the main menu under the probe (`menuVerified`
true, 1056.9 s to menu) on the pre-change runtime, but the menu background was
black. Two transport defects stacked on the same frame:

1. The bridge dropped a complete submission when its producer ring filled at
   the menu transition. Later frames referenced that submission's buffers and
   images byte-lessly, `gpuBufferFor()` returned null, and `createBindGroup()`
   rejected nine whole frames.
2. Once every submission was retained, the menu frame delivered a ~22.3 MB
   `IMAGE_DATA` record (2048x2048 RGBA plus its mip chain) in one chunk. The
   page's 16 MB ingest cap rejected that chunk, so the background texture was
   still missing and one frame was still rejected.

The change keeps every complete submission for the present hop, recovers
byte-less records by bridge content hash (zero-buffer last resort instead of a
null `GPUBuffer`), and raises the chunk ingest cap to 128 MB so a single
mipmapped 2048-4096 px image record fits in one chunk.

A third defect sat in front of the transport fixes: `vkGetPhysicalDeviceFormatProperties`
advertised no BC formats even though the bridge enables `textureCompressionBC`
and the page requires WebGPU `texture-compression-bc`. Portal therefore
decompressed its 2048x2048 menu textures to BGRA8. The restored native BC path
also removes the 22.3 MB upload: the same 2048x2048 seven-mip image is 2,796,032
bytes as BC1 versus 22,368,256 bytes as BGRA8.

The fixed runtime now passes the Portal probe (`portal-bc2-20261022`):
`menuVerified` true at 1126.6 s, `pass` true, and 108 rendered frames over the
127.7 s measurement window. The menu screenshot shows the live Relaxation Vault
scene with the Portal logo and all menu entries. OCR runs on the left 60% menu
column: on the detailed background the old full-page `--psm 11` pass dropped
`OPTIONS`, while the menu column with `--psm 6` reads the complete menu. The
real launcher app also clears its first-frame overlay and shows the menu
(`demo-portal-bc4-20261022`); its earlier overlay was a startup-probe canvas
bug, not a render failure.

## Reproduction

- Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-portal-20261022`
- Branch: `lane/winbox-portal-20261022` (base `e00a6c105`)
- Command: `GAME=portal PROBE_DIST=<dist> TAG=portal-bc2-20261022 TIMEOUT_MS=1800000 MEASURE_MS=120000 node web/tests/game-probe.mjs`
- Pre-change evidence: `test-results/portal-boot-20261022/` (`menuVerified`
  true, `bootToMenuSeconds` 1056.9, 54 presents, 1.4 fps).
- Fixed evidence: `test-results/portal-bc2-20261022/` (`menuVerified` true,
  `bootToMenuSeconds` 1126.6, `pass` true, 108 rendered frames).
- Reference blurred menu background: `test-results/portal-menu-20261019/current.png`
  and the decoded `background1.vtf`; the pre-change menu used a black background
  (`test-results/portal-launcher-20261019/menu.png`, build `d7ee184e`).

## Root cause

`vk64bridge.cpp` queued complete submission frames in `g_v2Pending` and, when
the pending chunk count exceeded `V2_PRODUCER_CHUNK_RING` (8), popped the
oldest frame before the present hop:

```
vk64: v2 producer ring full, dropped oldest frame (total dropped=1)
```

That frame was emitted at the menu transition (after the 1024x1024 image
creations at 1027–1030 s), so the page never received its buffer/image bytes.
Nine following frames failed at `createBindGroup`:

```
Failed to convert value to 'GPUBuffer'
```

The page's byte-less dedup records could not be resolved: the bridge's periodic
64-frame resend heals buffers that are still referenced, but a one-shot menu
background upload is never resent. After the nine rejects the menu text rendered
from font textures while the background stayed black.

## Changes

- `source/vulkan/vk64bridge.cpp`: never drop a complete submission. The ring
  value is now a throttled warning threshold; the present hop drains the queue.
  Payload deltas can no longer be lost between submissions and the page.
- `web/vkwebgpu.mjs`: added `persistBufByHash` (content hash + usage ->
  `GPUBuffer`) so a byte-less record can reuse bytes uploaded under another
  VkBuffer id. If the content is still unavailable, bind-group creation falls
  back to a zero buffer (`bufUnresolved` metric) instead of returning null,
  which previously made WebGPU reject the whole frame.
- `web/vk-protocol.mjs`: raised `V2_MAX_CHUNK_BYTES` from 16 MB to 128 MB.
  Records cannot span chunks and `IMAGE_DATA` carries the whole mip chain; the
  old cap rejected Portal's ~22.3 MB menu-background upload.
- `web/tests/suite/unit/buffer-cache-keys.test.mjs` and
  `web/tests/suite/unit/v2-producer-payload-retention.test.mjs`: regression
  checks for hash recovery, non-null bind groups and producer payload
  retention.
- `web/tests/suite/unit/v2-transport-contract.test.mjs`: documents the 128 MB
  ingest cap and why 16 MB was insufficient.
- `source/vulkan/vk64bridge.cpp`: restored VkFormats 131-146 (BC1-BC7,
  including sRGB aliases) to `kV2SupportedTextureFormats`, matching the
  already-enabled `textureCompressionBC` feature and the page's required
  `texture-compression-bc`.
- `web/vkwebgpu.mjs`: mirrored the BC formats in
  `V2_SUPPORTED_TEXTURE_FORMATS`; `v2TextureFormat` keeps its adapter-feature
  guard and the mip upload plan already handled BC block rows. Added a
  `largeTexFormats` diagnostic to the persistent stats.
- `web/tests/suite/unit/vulkan-texture-formats.test.mjs`: asserts BC is in both
  format lists and that every BC format maps when the feature is present.
- `web/tests/game-probe.mjs` and `web/tests/portal-ocr.mjs`: OCR the left 60%
  menu column and use `--psm 6`, falling back to the previous sparse pass. The
  word gate (`hasPortalMenu`) is unchanged; the crop removes the detailed room
  that made full-page Tesseract drop the lower menu entries.
- `web/app.mjs`: the startup first-frame probe now reads
  `metricsTier.stats().v2.rendered` and samples the parent `vkCanvas`/
  `run.canvas`. The launcher renders the v2 stream in the parent page while the
  guest runs in an iframe; the old probe sampled the iframe's 2D canvas, so the
  WebGPU D3D9 path could present while the launcher stayed on "Waiting for the
  first frame".

## Validation

- Native compile of `source/vulkan/vk64bridge.cpp` (g++, wasm64-mt-jit defines):
  pass.
- `node web/tests/suite/runner.mjs --unit`: 155 passed, 1 failed
  (`native-socket-safety`, unrelated network tests; failure predates this
  change).
- `node --test` for the changed areas (buffer cache, transport contract,
  chunk format, producer payload retention, BC format tables, image blit):
  41/41 pass.
- WASM build #1 (transport fixes): `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2
  BINARYEN_CORES=2 bash tools/bw64-build.sh`, `wasm_sha256`
  `701d625e8ae59506f4a2390bea8d3044b2569f5a75c95c0887626981ff4b2b24`.
- WASM build #2 (BC formats): same command at 2026-10-22T21:37:31Z,
  `wasm_sha256` `ff617ed29f2fff44e9ff4774342769f99174fc828c901f7818e62d74b6366ecd`,
  `wasm_bytes` 6,271,489, builder `deepseek-portal`.
- Post-change probe `portal-bc2-20261022`: `menuVerified` true (1126.6 s),
  `pass` true, 108 frames / 127.7 s (0.85 fps), `rejected` 0, BC textures
  `2048x2048` and `1024x1024` present. Screenshot:
  `test-results/portal-bc2-20261022/menu.png`.
- Demo-matrix first run (`demo-portal-bc2-20261022`): the launcher mapped the
  window at 27.8 s but stayed on "Waiting for the first frame" because its
  first-frame probe sampled the wrong canvas.
- Demo-matrix after the `web/app.mjs` fix (`demo-portal-bc4-20261022`): mapped
  at 37.4 s; `boot-360s.png` and `input-*.png` show the full Portal menu with
  the app header and no loading overlay, `pageErrors` 0. The matrix's in-page
  `nonblankFraction` remains 0 because it copies the WebGPU `vkCanvas` through
  a 2D context; the PNGs are the valid evidence.

## Related conformance observations

D3D9 checks are 13/15 (`A8R8G8B8`, `X1R5G5B5` rejected with `0x8876086A`);
DXVK's `IsSupportedAdapterFormat` deliberately excludes both, so this is
upstream behavior rather than a Portal regression. D3D11 is 11/12 (`FL 11_0`
returns `0x80070057`); Portal uses D3D9 SM2 and is unaffected.
