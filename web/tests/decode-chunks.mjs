// Offline V2 chunk decoder: dumps records per chunk, focusing on
// BUFFER_DATA / VERTEX_BIND / INDEX_BIND / PIPELINE / draw ops.
// Usage: node /tmp/decode-chunks.mjs <chunkdir> [maxchunks]
import { readdir, readFile } from "node:fs/promises";
import { parseV2Chunk, V2_REC, v2Hex64 } from "/home/ubuntu/boxedwine64/web/vkwebgpu.mjs";

const dir = process.argv[2], maxc = Number(process.argv[3] || 1e9);
const files = (await readdir(dir)).filter(f => f.endsWith(".bin")).sort().slice(0, maxc);
const RECNAME = Object.fromEntries(Object.entries(V2_REC).map(([k, v]) => [v, k]));

for (const f of files) {
  const bytes = await readFile(`${dir}/${f}`);
  let chunk;
  try { chunk = parseV2Chunk(bytes); } catch (e) { console.log(`${f}: PARSE ERROR ${e.message}`); continue; }
  const recs = chunk.records;
  console.log(`=== ${f} (${bytes.length}B, seq=${chunk.seq} ${recs.length} records) ===`);
  for (const r of recs) {
    const name = RECNAME[r.type] || `0x${r.type.toString(16)}`;
    if (r.type === V2_REC.BUFFER_DATA) {
      const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
      const bufId = v2Hex64(dv.getBigUint64(0, true));
      const memOff = dv.getBigUint64(8, true).toString();
      const hash = v2Hex64(dv.getBigUint64(16, true));
      const len = dv.getUint32(24, true);
      const payload = r.payload.subarray(28, 28 + Math.min(len, 64));
      console.log(`  BUFFER_DATA buf=${bufId} off=${memOff} len=${len} hash=${hash} first64=${Buffer.from(payload).toString("hex")}`);
    } else if (r.type === V2_REC.VERTEX_BIND) {
      const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
      const n = dv.getUint8(0); let p = 4; const binds = [];
      for (let k = 0; k < n; k++) { binds.push(`b${dv.getUint32(p, true)}:buf=${v2Hex64(dv.getBigUint64(p + 4, true))}@${dv.getBigUint64(p + 12, true)}`); p += 20; }
      console.log(`  VERTEX_BIND ${binds.join(" ")}`);
    } else if (r.type === V2_REC.INDEX_BIND) {
      const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
      console.log(`  INDEX_BIND buf=${v2Hex64(dv.getBigUint64(0, true))}@${dv.getBigUint64(8, true)} type=${dv.getUint32(16, true) ? "u32" : "u16"}`);
    } else if (r.type === V2_REC.PIPELINE) {
      const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
      let p = 0; const pid = v2Hex64(dv.getBigUint64(p, true)); p += 8;
      p += 16; const topo = dv.getUint32(p, true); p += 4; p += 4 * 6;
      const nVb = dv.getUint32(p, true), nVa = dv.getUint32(p + 4, true); p += 8;
      const vbs = [];
      for (let k = 0; k < nVb; k++) { vbs.push(`b${dv.getUint32(p, true)}:stride=${dv.getUint32(p + 4, true)}`); p += 12; }
      const vas = [];
      for (let k = 0; k < nVa; k++) { vas.push(`loc${dv.getUint32(p, true)}:b${dv.getUint32(p + 4, true)}:fmt=${dv.getUint32(p + 8, true)}@off${dv.getUint32(p + 12, true)}`); p += 16; }
      console.log(`  PIPELINE id=${pid} topo=${topo} vb=[${vbs.join(" ")}] va=[${vas.join(" ")}]`);
    } else if (r.type === V2_REC.IMAGE_DATA) {
      const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
      let p = 0; const viewId = v2Hex64(dv.getBigUint64(p, true)); p += 8;
      const imageId = v2Hex64(dv.getBigUint64(p, true)); p += 8;
      const w = dv.getUint32(p, true), h = dv.getUint32(p + 4, true), fmt = dv.getUint32(p + 8, true); p += 12;
      const hash = v2Hex64(dv.getBigUint64(p, true)); p += 8;
      const pixLen = dv.getUint32(p, true); p += 4;
      const payload = r.payload.subarray(p, p + Math.min(pixLen, 32));
      console.log(`  IMAGE_DATA view=${viewId} img=${imageId} ${w}x${h} fmt=${fmt} pixLen=${pixLen} hash=${hash} first32=${Buffer.from(payload).toString("hex")}`);
    } else if (r.type === V2_REC.SAMPLER) {
      console.log(`  SAMPLER (${r.payload.length}B)`);
    } else if (r.type === V2_REC.DRAW) {
      const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
      let p = 0; const vcount = dv.getUint32(p, true), icount = dv.getUint32(p + 4, true); p += 8;
      const firstV = dv.getUint32(p, true); p += 8;
      const indexed = dv.getUint8(p); p += 4;
      const icnt = dv.getUint32(p, true), firstI = dv.getUint32(p + 4, true), voff = dv.getInt32(p + 8, true);
      console.log(`  DRAW indexed=${indexed} vcount=${vcount} icount=${icount} firstV=${firstV} icnt=${icnt} firstI=${firstI} voff=${voff}`);
    } else if (["FRAME_BEGIN", "FRAME_END", "RP_BEGIN", "RP_END", "CLEAR", "PRESENT"].includes(name)) {
      // skip noise
    } else {
      console.log(`  ${name} (${r.payload.length}B)`);
    }
  }
}
