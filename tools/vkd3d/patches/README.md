# Boxedwine64 Surgical Patches

This directory contains standalone patches extracted from the dirty working tree.
Each patch contains ONLY the specific fix, without the unrelated experimental
cruft that accumulates in the working tree during investigation.

## d3d12-effdepthtest-fix.patch

**What:** Fixes the D3D12 bridge `effDepthTest` masking bug in
`source/vulkan/vk64bridge.cpp` (serializeSubmitV2, ~line 1770).

**Why:** The bridge was masking `pipe->depthTest` by `fbHasDepth` (whether the
framebuffer *at submit time* has a depth attachment):

```cpp
// BEFORE (buggy):
U32 effDepthTest = fbHasDepth ? pipe->depthTest : 0;
```

The `fb` variable is the framebuffer from the FIRST `CMD_BEGIN_RP` in the
submitted command buffer — not the render pass where each pipeline is actually
used. For D3D12 scene12:
- Indexed quads pipeline: `pipe->depthTest=1` (needs depth, depthOp=less-equal),
  but `fbHasDepth=0` → V2 reported `depthTest=0` → JS gave `depthCompare='always'`
  → **quads vanished** (33,830 px, triangles only).
- Triangles pipeline: `pipe->depthTest=0` (truly no depth) → V2 correctly
  reported `0` → JS gave `'always'` → triangles rendered.

**Fix:**
```cpp
// AFTER:
(void)fbHasDepth; // V2 carries true intent; JS handles pass matching.
U32 effDepthTest = pipe->depthTest;
U32 effDepthWrite = pipe->depthWrite;
```

The V2_PIPELINE record now carries the pipeline's TRUE depth intent. The JS side
(`web/vkwebgpu.mjs` `getPipelineFor`) already handles WebGPU validation via its
`passHasDepth` parameter:
- `pipe.depthTest=1` + `passHasDepth=1` → real `DEPTH_OP[depthOp]` ('less-equal')
  → quads render.
- `pipe.depthTest=0` + `passHasDepth=1` → dummy `'always'` (validation only)
  → triangles render.
- `pipe.depthTest=0` + `passHasDepth=0` → no depthStencil → correct.

**Expected result:** Both indexed quads AND triangles render. ~78k lit pixels
(33k triangles + 44k quads), significantly higher than either alone.

**Status:** IMPLEMENTED, statically verified correct (no side effects, JS handles
all cases). **AWAITING probe verification** before commit.

**Verification needed:**
```bash
# When probe slot is free and 32-bit work is not blocked:
cd /home/ubuntu/boxedwine64
tools/bw64-build.sh
TAG=scene12-effdepth node web/tests/scene12-probe.mjs
# Expected: BOTH quads and triangles visible, ~78k lit pixels
```

**Commit instructions (ONLY after probe verification):**
```bash
# DO NOT use: git add source/vulkan/vk64bridge.cpp
# That would sweep in 1653 lines of unrelated experimental cruft.
# Instead:
cd /home/ubuntu/boxedwine64
git apply boxwined64-coord/patches/d3d12-effdepthtest-fix.patch
git add source/vulkan/vk64bridge.cpp
git commit -m "fix(d3d12): bridge effDepthTest masking bug ..."
# NEVER commit to master; NEVER push.
```

**Related:**
- Commit 278a1fe0's verification was invalid (probe ran 8 min BEFORE the commit),
  but its WebGPU validation fix is necessary. Do NOT revert 278a1fe0.
- The 98k lit-pixel target is unachievable (old scene12.exe had a bug that
  278a1fe0 fixed). The goal is quads+triangles (~78k), not 98k.
- Static review: the fix is correct, `fbHasDepth` has no other consumers,
  `effDepthTest`/`effDepthWrite` are only used in the V2_PIPELINE emit.

**Date:** 2026-10-09
