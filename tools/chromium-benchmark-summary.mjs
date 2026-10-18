// Summarize fresh-process Chromium probes; retain failures without averaging them.
// node tools/chromium-benchmark-summary.mjs test-results/suite-opt-*/summary.json
import { readFile } from 'node:fs/promises';

const median = values => {
  const sorted = [...values].sort((a, b) => a - b);
  return (sorted[Math.floor((sorted.length - 1) / 2)] + sorted[Math.ceil((sorted.length - 1) / 2)]) / 2;
};
const p95 = values => [...values].sort((a, b) => a - b)[Math.ceil(values.length * 0.95) - 1];
const paths = process.argv.slice(2);
if (!paths.length) throw Error('Pass one or more Chromium probe summary.json paths');
const runs = [];
for (const path of paths) {
  const d = JSON.parse(await readFile(path, 'utf8'));
  const run = { tag: d.tag, startedAt: d.startedAt, path, pass: d.pass, host: d.host,
    wasmSha256: d.build?.wasm_sha256, runtimeCodeHash: d.runtimeCodeHash,
    hostBrowserVersion: d.hostBrowserVersion, archiveSha256: d.archive?.sha256,
    fixtureSha256: d.fixtureSha256 };
  if (d.pass) {
    if (d.benchmark.workload !== 'integer-sum-v2' || d.benchmark.samples.length !== 8
      || d.benchmark.checksum !== 1783293664 || d.graphics.workload !== 'canvas-scene-v1'
      || d.graphics.frames !== 300 || d.graphics.pixelHash !== 800777293 || d.graphics.colorful !== 7200)
      throw Error('Incompatible or incorrect benchmark: ' + path);
    Object.assign(run, { readySeconds: d.ready.wallMs / 1000,
      startupCpuSeconds: d.startupCpu.rendererSeconds,
      integerSamplesMs: d.benchmark.samples, warmIntegerMs: median(d.benchmark.samples.slice(4)),
      sceneCpuSeconds: d.sceneCpu?.rendererSeconds,
      callbackMedianMs: median(d.graphics.intervals), callbackP95Ms: p95(d.graphics.intervals),
      drawMedianMs: median(d.graphics.drawSamples), clickLatencyMs: d.clickLatencyMs,
      clickActionabilityMs: d.clickActionabilityMs, clickGuestDeliveryMs: d.clickGuestDeliveryMs,
      idlePercentOneCore: d.idleCpu?.rendererPercentOneCore });
  } else run.error = d.error;
  runs.push(run);
}
runs.sort((a, b) => a.startedAt.localeCompare(b.startedAt));
const successful = runs.filter(run => run.pass);
for (const key of ['hostBrowserVersion', 'archiveSha256', 'fixtureSha256']) {
  if (new Set(successful.map(run => run[key])).size > 1) throw Error('Mismatched ' + key);
}
const groups = [];
const labels = [...new Set(successful.map(run => run.tag.replace(/-\d+$/, '')))];
for (const label of labels) {
  const samples = successful.filter(run => run.tag.replace(/-\d+$/, '') === label);
  if (new Set(samples.map(run => run.wasmSha256 + run.runtimeCodeHash)).size !== 1)
    throw Error('Runtime changed within group: ' + label);
  const group = { label, runs: samples.length };
  for (const key of ['readySeconds', 'startupCpuSeconds', 'warmIntegerMs', 'sceneCpuSeconds',
    'callbackMedianMs', 'callbackP95Ms', 'drawMedianMs', 'clickLatencyMs', 'idlePercentOneCore']) {
    const values = samples.map(run => run[key]);
    if (values.every(Number.isFinite)) group[key] = { median: median(values), min: Math.min(...values), max: Math.max(...values) };
  }
  groups.push(group);
}
console.log(JSON.stringify({ generatedAt: new Date().toISOString(), runs, groups }, null, 2));
