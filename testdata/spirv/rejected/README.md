# Rejected shapes — modules the page tier must NOT translate

These modules are valid SPIR-V (spirv-val, vulkan1.1) whose shapes the page
tier (spirvfix + vendored naga) cannot translate today. They are the evidence
for the bridge audit keeping their USED forms REJECTED-LOUDLY at
vkCreateShaderModule (source/vulkan/vk64bridge.cpp, vk64AuditSpirv) instead
of admitting them to fail later as silently skipped draws:

| Module | Shape | Page-tier result (2026-10-25) |
| --- | --- | --- |
| cull-used.vert.spv | OpCapability CullDistance + a BuiltIn CullDistance output that is written | naga: `Unsupported builtin value: CullDistance` |
| draw-used.vert.spv | OpCapability DrawParameters + BaseVertex/DrawIndex loads (glslang 450, GL_ARB_shader_draw_parameters) | spirvfix R5 replace applies but emits invalid SPIR-V for this layout (zero constants are inserted before the first OpVariable; this producer interleaves type declarations after variables, so a constant references a not-yet-declared type) -> naga InvalidId |

Provenance: cull-used.vert.spv is hand-written assembly (cull-used.vert.spvasm,
spirv-as --target-env vulkan1.1); draw-used.vert.spv is compiled from
draw-used.vert with glslangValidator -V --target-env vulkan1.1.

The declared-but-UNUSED forms of both capabilities are servable and live in
the main corpus (p9-cull-unused.vert.spv, p10-drawparams-unused.vert.spv);
the audit admits those. If a future spirvfix/naga fix makes a module here
convert, its audit rejection must be re-dispositioned in
docs/known-gaps/vulkan.md — the unit test pinning this directory says so.
