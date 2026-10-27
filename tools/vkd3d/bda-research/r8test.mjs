import { readFileSync, writeFileSync } from "node:fs";
import { applySpirvFix } from "/home/ubuntu/wt-vkd3d-bda/tools/spirvfix/spirvfix.mjs";
const inp = new Uint8Array(readFileSync("ps_uav_bindless.spv"));
const out = applySpirvFix(inp, {});
console.log("applied:", out.applied);
console.log("log:", out.log);
writeFileSync("ps_uav_fixed.spv", out.bytes);
