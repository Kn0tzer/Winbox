// Sampler-state dispositions at the WebGPU tier boundary (docs/known-gaps/vulkan.md):
//  - MIRROR_CLAMP_TO_EDGE: the samplerMirrorClampToEdge feature is not advertised,
//    so creation is REJECTED with VK_ERROR_FEATURE_NOT_PRESENT.
//  - nonzero mipLodBias: governed by the advertised maxSamplerLodBias limit, which
//    is 0 because WebGPU samplers have no LOD bias. A conforming guest cannot
//    request a nonzero bias (VUID-VkSamplerCreateInfo-mipLodBias-01069: |mipLodBias|
//    must be <= maxSamplerLodBias); out-of-contract requests are logged and take
//    the spec clamp of the effective bias to +/-0. Creation is not rejected.
//  - CLAMP_TO_BORDER: core Vulkan with no feature bit and no limit, and WebGPU
//    has no border addressing. Creation succeeds, the protocol decoder maps the
//    mode to clamp-to-edge, and the approximation is logged per sampler. This is
//    a documented deviation in the register; it is pinned here so the behaviour
//    cannot change silently in either direction.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const bridge = readFileSync(new URL("../../../../source/vulkan/vk64bridge.cpp", import.meta.url), "utf8");
const protocol = readFileSync(new URL("../../../../web/vk-protocol.mjs", import.meta.url), "utf8");

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

test("mirror clamp rejects sampler creation, for both ABIs", () => {
  const audit = body(bridge, "static bool vk64AuditSampler(");
  assert.ok(audit.includes("vk64Unsupported(\"mirror-clamp-to-edge\""), "greppable diagnostic");
  assert.match(audit, /return reject;/);
  // Only the feature-gated mode rejects; border and bias must not make DXVK throw from sampler creation.
  assert.equal((audit.match(/reject = true/g) || []).length, 1);
  assert.match(audit.slice(audit.indexOf("mode == 4"), audit.indexOf("mode == 3")), /reject = true/);
  const cases = [...bridge.matchAll(/case VK64_fn_vkCreateSampler: \{([\s\S]*?)\n    \}/g)].map((m) => m[1]);
  assert.equal(cases.length, 2);
  for (const handler of cases) {
    assert.match(handler, /vk64AuditSampler\(/);
    assert.match(handler, /VK_ERROR_FEATURE_NOT_PRESENT/);
    assert.ok(handler.indexOf("vk64AuditSampler(") < handler.indexOf("createObj"), "reject before creating a sampler");
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

test("clamp-to-border: documented approximation is pinned (created, logged, decoded to clamp-to-edge)", () => {
  const audit = body(bridge, "static bool vk64AuditSampler(");
  assert.ok(audit.includes("vk64Unsupported(\"sampler-border-colour\""), "greppable diagnostic");
  assert.ok(audit.includes("borderColor="), "log reports the border colour that cannot be honoured");
  assert.match(protocol, /WebGPU has no border addressing/, "protocol decoder documents the tier limitation");
  assert.match(protocol, /3: .clamp-to-edge./, "CLAMP_TO_BORDER decodes to clamp-to-edge");
});
