# Geometry-stage disposition fixtures (NOT conversion-gate members)

These modules are bridge-disposition fixtures for
`source/vulkan/vk_gs_passthrough.h`. They are deliberately NOT part of
`tools/spirvfix/gate.mjs` (which sweeps only `testdata/spirv/` and
`testdata/spirv/dxvk-live/`): a geometry-stage module can never convert to
WGSL — WebGPU has no geometry stage. What the bridge does with them is the
subject of `web/tests/suite/unit/vk-gs-passthrough.test.mjs`:

- `dxvk-fullscreen.geom.spv` — the real `dxvk_fullscreen_geom` module from
  the vendored DXVK 2.4.1 build headers
  (`tools/dxvk/build.w64/src/dxvk/libdxvk.a.p/dxvk_fullscreen_geom.h`),
  sha256 c89cd4f81a36b355092dfd23fc89175f0459f579003fa4cf08e25c7c4c7f2f2f.
  DXVK creates it (with `dxvk_fullscreen_vert`) for meta copy/blit/resolve
  pipelines whenever the driver does not advertise shaderOutputLayer — this
  bridge honestly does not. It is a pure layer-passthrough: for each input
  vertex it forwards texcoord/Position unchanged and writes
  gl_Layer = gl_InstanceIndex. The matcher must recognise it
  (`isLayerPassthroughGeom` = true); paired with the meta fragment modules
  in `../dxvk-live/` (which consume no location inputs) pipeline draws are
  served by stage omission under the tier's layer-0 disposition.
- `mutant-m1-amp.spv` — OutputVertices 3 → 6 (vertex amplification): false.
- `mutant-m2-layerconst.spv` — gl_Layer stored from a nonzero constant: false.
- `mutant-m3-posconst.spv` — gl_Position stored from a constant composite: false.
- `mutant-m4-viewport.spv` — additionally writes gl_ViewportIndex: false.
- `mutant-m5-fadd.spv` — texcoord forwarded through an OpFAdd: false.

Mutants were produced by editing the real module's spirv-dis output and
reassembling with spirv-as; all are valid SPIR-V (spirv-val clean), so a
`false` verdict is the matcher's judgement, not a parse failure.
