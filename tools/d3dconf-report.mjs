// Generates the four-bucket D3D conformance report from a run's results.json.
// Usage: node tools/d3dconf-report.mjs <results-dir>
// Writes MATRIX.md and buckets.json into the results dir.
import { readFile, writeFile } from "node:fs/promises";

const dir = process.argv[2];
if (!dir) { console.error("usage: d3dconf-report.mjs <results-dir>"); process.exit(1); }
const results = JSON.parse(await readFile(`${dir}/results.json`, "utf8"));

// Slow thresholds (justified in MATRIX.md):
// - one-shot op (device/shader/resource creation): ms > 60000
// - present-loop op: mean frame ms = total/frames > 3000
//   (Pong guest-frame baseline mean 276ms; 3000ms ~= 11x baseline)
const ONE_SHOT_SLOW_MS = 60000;
const FRAME_SLOW_MS = 3000;

const RENDER_OPS = new Set(["draw_indexed", "present_first", "draw_textured",
  "vs_handasm_bonus", "present_30f", "draw_present_30f", "draw_present_10f"]);

function bucketize(suiteId, suite) {
  const ops = suite.ops || [];
  const tested = ops.filter(o => o.result !== "SKIP");
  const skipped = ops.filter(o => o.result === "SKIP");
  const lit = suite.pixels && typeof suite.pixels.lit === "number" ? suite.pixels.lit : null;
  const drawsExecuted = (suite.decodedDraws || 0) > 0;
  // If draws executed but nothing painted, render ops are effectively broken.
  const paintsNothing = drawsExecuted && lit === 0;

  const rows = [];
  for (const o of tested) {
    let bucket, note = "";
    // mean frame time for present loops: parse from op name pattern
    let slow = false, slowWhy = "";
    if (o.result === "OK") {
      if (/present_30f|draw_present/.test(o.name)) {
        const m = JSON.stringify(suite).match(new RegExp(o.name.replace(/_30f|_10f/, "") + "[^\\n]*frames=(\\d+) total_ms=(\\d+)"));
        // fallback: use op ms and known frame counts
        const frames = /30f/.test(o.name) ? 30 : /10f/.test(o.name) ? 10 : 1;
        const mean = o.ms / frames;
        if (mean > FRAME_SLOW_MS) { slow = true; slowWhy = `mean frame ${Math.round(mean)}ms > ${FRAME_SLOW_MS}ms`; }
      } else if (o.ms > ONE_SHOT_SLOW_MS) {
        slow = true; slowWhy = `${o.ms}ms > ${ONE_SHOT_SLOW_MS}ms one-shot threshold`;
      }
      if (RENDER_OPS.has(o.name) && paintsNothing) {
        bucket = "broken";
        note = `draws executed (${suite.decodedDraws}) but litPixels=0 — paints nothing`;
      } else if (slow) {
        bucket = "slow";
        note = slowWhy;
      } else {
        bucket = "working";
      }
    } else {
      bucket = "broken";
      note = `hr=${o.hr}`;
    }
    if (o.name === "vs_handasm_bonus" && o.result !== "OK") note += " [test-asset suspect: hand-assembled shader tokens, not verified against a real assembler]";
    rows.push({ suite: suiteId, op: o.name, result: o.result, hr: o.hr, ms: o.ms, bucket, note });
  }
  return {
    suite: suiteId,
    litPixels: lit, decodedDraws: suite.decodedDraws || 0, done: !!suite.done,
    wallMs: suite.wallMs || 0,
    total: tested.length,
    working: rows.filter(r => r.bucket === "working").length,
    slow: rows.filter(r => r.bucket === "slow").length,
    broken: rows.filter(r => r.bucket === "broken").length,
    skipped: skipped.length,
    rows,
  };
}

const buckets = {};
for (const [id, suite] of Object.entries(results)) {
  if (id === "runnerError") continue;
  buckets[id] = bucketize(id, suite);
}
await writeFile(`${dir}/buckets.json`, JSON.stringify(buckets, null, 2));

let md = `# D3D conformance matrix — ${dir}\n\n`;
md += `Generated from in-browser runs. 32-bit: untestable (wineboot blocked, separate lane).\n\n`;
md += `## Slow thresholds\n\n`;
md += `- One-shot ops (device/shader/resource creation): slow if guest-measured ms > ${ONE_SHOT_SLOW_MS}. Justification: prior probes create D3D11 devices in ~10-30s; 60s is 2-6x the observed norm.\n`;
md += `- Present-loop ops: slow if mean frame ms > ${FRAME_SLOW_MS}. Justification: D3D9 Pong guest-frame baseline mean 276ms (test-results/frametime-pong2/); 3000ms ~= 11x baseline.\n`;
md += `- Flaky = inconsistent verdict across runs. All suites ran once; flakiness not assessed unless a rerun disagrees.\n`;
md += `- Render ops (draw/present) with draws executed but litPixels=0 count as broken ("paints nothing").\n\n`;
md += `## Four-bucket summary\n\n`;
md += `| API | total tested | fully working | slow/flaky | fully broken | skipped | litPixels | draws |\n`;
md += `|-----|--------------|----------------|------------|--------------|---------|-----------|-------|\n`;
for (const [id, b] of Object.entries(buckets)) {
  md += `| ${id} | ${b.total} | ${b.working} | ${b.slow} | ${b.broken} | ${b.skipped} | ${b.litPixels ?? "n/a"} | ${b.decodedDraws} |\n`;
}
md += `\n## Per-operation table\n\n`;
md += `| API | op | result | hr | ms | bucket | note |\n`;
md += `|-----|----|--------|----|----|--------|------|\n`;
for (const [id, b] of Object.entries(buckets)) {
  for (const r of b.rows) {
    md += `| ${id} | ${r.op} | ${r.result} | ${r.hr} | ${r.ms} | ${r.bucket} | ${r.note} |\n`;
  }
}
if (results.runnerError) md += `\nRunner error: ${results.runnerError}\n`;
await writeFile(`${dir}/MATRIX.md`, md);
console.log("WROTE", `${dir}/MATRIX.md`, `${dir}/buckets.json`);
for (const [id, b] of Object.entries(buckets)) {
  console.log(`${id}: total=${b.total} working=${b.working} slow=${b.slow} broken=${b.broken} skipped=${b.skipped} lit=${b.litPixels} draws=${b.decodedDraws}`);
}
