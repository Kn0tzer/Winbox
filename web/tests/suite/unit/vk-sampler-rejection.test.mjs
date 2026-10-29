// Sampler-state dispositions at the WebGPU tier boundary (docs/known-gaps/vulkan.md):
//  - MIRROR_CLAMP_TO_EDGE: the samplerMirrorClampToEdge feature is not advertised,
//    but the vkd3d lane serves it as mirror-repeat (D3D12 mirror-once needs it;
//    ADDR[4]='mirror-repeat' in vk-protocol.mjs). Creation succeeds; the audit
//    logs mirror-clamp-to-edge with the approximation note (identical on [-1, 2];
//    beyond that range coordinates keep mirroring instead of pinning the edge texel).
//  - nonzero mipLodBias: governed by the advertised maxSamplerLodBias limit, which
//    is 0 because WebGPU samplers have no LOD bias. A conforming guest cannot
//    request a nonzero bias (VUID-VkSamplerCreateInfo-mipLodBias-01069: |mipLodBias|
//    must be <= maxSamplerLodBias); out-of-contract requests are logged and take
//    the spec clamp of the effective bias to +/-0. Creation is not rejected.
//  - CLAMP_TO_BORDER: core Vulkan with no feature bit and no limit, and WebGPU
//    has no border addressing. Creation succeeds and is logged per sampler;
//    the GPUSampler decodes to clamp-to-edge, the decoder retains the raw
//    modes, and the page tier emulates the border colour in the shader for
//    sampled-image ops (vk-border-emulation.test.mjs). Gather and cube
//    sampling keep the clamp-to-edge approximation.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const bridge = readFileSync(new URL("../../../../source/vulkan/vk64bridge.cpp", import.meta.url), "utf8");
const protocol = readFileSync(new URL("../../../../web/vk-protocol.mjs", import.meta.url), "utf8");
const wgslUtils = readFileSync(new URL("../../../../web/wgsl-utils.mjs", import.meta.url), "utf8");
const pageTier = readFileSync(new URL("../../../../web/vkwebgpu.mjs", import.meta.url), "utf8");

function body(source, header) {
  const start = source.indexOf(header);
  assert.notEqual(start, -1, `missing ${header}`);
  const open = source.indexOf("{", start);
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === "{") depth++;
    else if (source[i] === "}" && --depth === 0) return source.slice(open, i + 1);
  }
  throw new Error(`unbalanced function ${header}`);
}

test("mirror clamp is served as mirror-repeat, for both ABIs", () => {
  const audit = body(bridge, "static bool vk64AuditSampler(");
  assert.ok(audit.includes("vk64Unsupported(\"mirror-clamp-to-edge\""), "greppable diagnostic");
  assert.match(audit, /served by the page tier as mirror-repeat/);
  // Served, not rejected: the audit must not set reject for mirror-clamp.
  assert.doesNotMatch(audit.slice(audit.indexOf("mode == 4"), audit.indexOf("mode == 3")), /reject = true/);
  assert.ok(protocol.includes("4: 'mirror-repeat'"), "decoder maps mode 4 to mirror-repeat");
  const cases = [...bridge.matchAll(/case VK64_fn_vkCreateSampler: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(cases.length, 2);
  for (const handler of cases) {
    assert.match(handler, /vk64AuditSampler\(/);
    assert.ok(handler.indexOf("vk64AuditSampler(") < handler.indexOf("createObj"), "audit before creating a sampler");
  }
});

test("mip LOD bias: advertised maxSamplerLodBias is 0; out-of-contract requests are logged, not rejected", () => {
  const limits = body(bridge, "void fillLimits(");
  assert.match(limits, /maxSamplerLodBias = 0\.0f;/, "fillLimits advertises maxSamplerLodBias 0 (shared by both ABIs)");
  assert.doesNotMatch(limits, /maxSamplerLodBias = [^0]/, "no nonzero LOD bias capability is advertised");
  const audit = body(bridge, "static bool vk64AuditSampler(");
  assert.ok(audit.includes("vk64Unsupported(\"sampler-mip-lod-bias\""), "greppable diagnostic");
  assert.ok(audit.includes("maxSamplerLodBias=0"), "log names the advertised limit the request exceeds");
});

test("clamp-to-border: created and logged; the page tier emulates the border colour in shader", () => {
  const audit = body(bridge, "static bool vk64AuditSampler(");
  assert.ok(audit.includes("vk64Unsupported(\"sampler-border-colour\""), "greppable diagnostic");
  assert.ok(audit.includes("borderColor="), "log reports the border colour");
  assert.ok(audit.includes("emulated in shader"), "log names the page-tier shader emulation");
  assert.match(protocol, /WebGPU has no border addressing/, "the GPUSampler itself still has no border mode");
  assert.match(protocol, /3: .clamp-to-edge./, "the GPUSampler address mode still decodes to clamp-to-edge");
  assert.match(protocol, /addrModeU: addrU/, "the decoder retains the raw modes the shader plan needs");
  assert.ok(wgslUtils.includes("rewriteBorderSamplersWGSL"), "the shader-tier border rewrite exists");
  assert.ok(pageTier.includes("rewriteBorderSamplersWGSL"), "the renderer builds border pipeline variants");
  assert.ok(pageTier.includes("frameHasBorderSampler"), "border variants are gated on a bound border sampler");
});
