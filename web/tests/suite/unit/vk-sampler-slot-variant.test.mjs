// Sampler slot-variant rule (page tier): whether a GPUSampler is a
// comparison sampler is decided by the SHADER SLOT it binds into, not by
// the guest sampler record's compareEnable alone.
//
// Origin (Gang Beasts, title phase): DXVK encodes D3D11 comparison as
// compareOp = the state's ComparisonFunc (always populated) plus
// compareEnable = the state's comparison-FILTER bit only (d3d11_sampler.cpp:
// compareToDepth follows filter bits 0x180). Unity's shadow samplers -
// including its ComparisonFunc=NEVER dummy states - therefore arrive with
// compareEnable=0 and a meaningful compareOp, while the guest shaders
// still execute Dref sampling (D3D11 SampleCmp keys off ComparisonFunc
// regardless of the filter bit). The page tier built the GPUSampler from
// the record alone (non-comparison) and fed it into sampler_comparison
// bind-group slots; WebGPU rejected the bind group ("Non-comparison
// sampler incompatible with comparison sampler binding") and one frame's
// encoding was dropped as [Invalid BindGroup].
//
// Fix (vkwebgpu.mjs): v2SamplerDescriptor(s, comparison) materializes the
// compare function from the record's compareOp whenever the slot is a
// comparison slot and never for a filtering slot; gpuSamplerFor caches
// the two variants under distinct keys; the bind-group fingerprint
// carries the variant so cached groups cannot alias across slot kinds.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { v2SamplerDescriptor } from "../../../vkwebgpu.mjs";

const pageTier = readFileSync(new URL("../../../vkwebgpu.mjs", import.meta.url), "utf8");

const base = {
  mag: "linear", min: "linear", mipmap: "linear",
  addrU: "repeat", addrV: "repeat", addrW: "repeat",
  minLod: 0, maxLod: 32, anisotropyEnable: false, maxAnisotropy: 1,
};

function body(source, header) {
  const start = source.indexOf(header);
  assert.notEqual(start, -1, `missing ${header}`);
  const open = source.indexOf("{", start);
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === "{") depth++;
    if (source[i] === "}") { depth--; if (depth === 0) return source.slice(start, i + 1); }
  }
  throw new Error(`unterminated ${header}`);
}

test("comparison slot + compareEnable=0 record gets the record compareOp", () => {
  // The Gang Beasts shape: Unity NEVER dummy shadow sampler.
  assert.equal(v2SamplerDescriptor({ ...base, compareEnable: false, compareOp: 0 }, true).compare, "never");
  assert.equal(v2SamplerDescriptor({ ...base, compareEnable: false, compareOp: 4 }, true).compare, "greater");
});

test("comparison slot + unusable compareOp falls back to 'always'", () => {
  assert.equal(v2SamplerDescriptor({ ...base, compareEnable: false, compareOp: undefined }, true).compare, "always");
});

test("filtering slot never receives a compare function, even for compareEnable=1 records", () => {
  const d = v2SamplerDescriptor({ ...base, compareEnable: true, compareOp: 2 }, false);
  assert.equal("compare" in d, false);
});

test("default (no slot stated) stays record-faithful", () => {
  assert.equal(v2SamplerDescriptor({ ...base, compareEnable: true, compareOp: 2 }).compare, "equal");
  assert.equal("compare" in v2SamplerDescriptor({ ...base, compareEnable: false, compareOp: 2 }), false);
});

test("gpuSamplerFor keys both caches by the slot variant and forwards it", () => {
  const b = body(pageTier, "function gpuSamplerFor");
  assert.match(b, /comparison \? ':cmp' : ':flt'/);
  assert.match(b, /v2SamplerDescriptor\(s, comparison\)/);
  assert.match(b, /persistSampCache\.set\(key, sm\)/);
  assert.match(b, /F\.sampCache\.set\(key, sm\)/);
});

test("bind-group entry site states the slot kind", () => {
  assert.match(pageTier, /gpuSamplerFor\(sHex, s, t === 'sampler_comparison'\)/);
});

test("bind-group fingerprint carries the sampler variant", () => {
  assert.match(pageTier, /sHex \+ \(t === 'sampler_comparison' \? ':c' : ':f'\)/);
});
