#!/usr/bin/env node
// Aggregate V8 .cpuprofile files from web/tests/profile-demo.mjs into stage
// totals, a top-function list and the guest-side mapped-file split.
//
// Usage: node tools/profile/aggregate.mjs <outdir> [stages.json]
//
// Rules (tools/profile/stages.json):
//   { "stage": "...", "match": { "functionName": "<regex>", "url": "<regex>" } }
// All provided match fields must match. C++ wasm function names are mapped
// through symbols.json to their object source, then to a stage below.
//
// Special cases applied before the rules:
//   (idle)                 -> idle
//   (program)              -> program
//   (garbage collector)    -> garbage collector
//   wasm module != main    -> CPU JIT-compiled code
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { readFile, readdir, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';

const repo = resolve(dirname(new URL(import.meta.url).pathname), '../..');
const outDir = resolve(process.argv[2] || '');
if (!process.argv[2]) {
  console.error('usage: aggregate.mjs <profile-outdir> [stages.json]');
  process.exit(2);
}
const rulesPath = resolve(process.argv[3] || join(repo, 'tools/profile/stages.json'));
const rulesDoc = JSON.parse(await readFile(rulesPath, 'utf8'));
const rules = (rulesDoc.rules || []).map(rule => ({
  stage: rule.stage,
  functionName: rule.match?.functionName ? new RegExp(rule.match.functionName) : null,
  url: rule.match?.url ? new RegExp(rule.match.url) : null,
}));

const symbolPath = join(outDir, 'symbols.json');
const symbols = existsSync(symbolPath)
  ? (JSON.parse(await readFile(symbolPath, 'utf8')).symbols || {})
  : {};
const targetsDoc = existsSync(join(outDir, 'targets.json'))
  ? JSON.parse(await readFile(join(outDir, 'targets.json'), 'utf8'))
  : [];
const targetMeta = new Map(targetsDoc.map(target => [target.profileFile, target]));

const files = (await readdir(outDir)).filter(name => name.endsWith('.cpuprofile')).sort();
if (!files.length) throw new Error(`no .cpuprofile files in ${outDir}`);

// ---------------------------------------------------------------------------
// Load every profile and compute self time per (function, url).
// ---------------------------------------------------------------------------
const targets = [];
for (const file of files) {
  const profile = JSON.parse(await readFile(join(outDir, file), 'utf8'));
  const nodeById = new Map((profile.nodes || []).map(node => [node.id, node]));
  const selfUs = new Map();
  const samples = profile.samples || [];
  const deltas = profile.timeDeltas || [];
  for (let i = 0; i < samples.length; i++) {
    const id = samples[i];
    const node = nodeById.get(id);
    if (!node) continue;
    let delta = Number(deltas[i] ?? 0);
    if (!Number.isFinite(delta) || delta < 0) delta = 0;
    const frame = node.callFrame || {};
    const key = `${frame.functionName || '(anonymous)'}\u0000${frame.url || ''}`;
    selfUs.set(key, (selfUs.get(key) || 0) + delta);
  }
  // Main wasm module = the fetched boxedwine64.wasm URL when available. Some
  // profilers instead expose it as wasm://, where the largest function set is
  // the best fallback. Dynamic JIT blocks are separate wasm:// modules.
  const nodeCountByWasmUrl = new Map();
  let fetchedMainWasmUrl = null;
  for (const node of profile.nodes || []) {
    const url = node.callFrame?.url || '';
    if (/\/boxedwine64\.wasm(?:[?#]|$)/i.test(url)) fetchedMainWasmUrl = url;
    if (url.startsWith('wasm://')) nodeCountByWasmUrl.set(url, (nodeCountByWasmUrl.get(url) || 0) + 1);
  }
  let mainWasmUrl = fetchedMainWasmUrl, mainWasmNodes = -1;
  for (const [url, count] of nodeCountByWasmUrl) {
    if (mainWasmUrl) break;
    if (count > mainWasmNodes) { mainWasmUrl = url; mainWasmNodes = count; }
  }
  const meta = targetMeta.get(file) || {};
  targets.push({
    file, type: meta.type || file.split('-')[0], url: meta.url || '', title: meta.title || '',
    mainWasmUrl, selfUs, functions: (profile.nodes || []).map(node => node.callFrame || {}),
  });
}

// Demangle sampled C++ symbols for display and bridge demangled profiler names
// back to their object source when V8 reports a demangled name.
const allNames = new Set();
for (const target of targets) for (const key of target.selfUs.keys()) allNames.add(key.split('\u0000')[0]);
const displayNames = new Map();
for (const name of allNames) displayNames.set(name, name);
const mangled = [...allNames].filter(name => /^(_Z|__Z|_R|__R)/.test(name)).slice(0, 20000);
const sourceByName = new Map();
for (const name of allNames) if (symbols[name]) sourceByName.set(name, symbols[name]);

function demangle(names) {
  const result = new Map();
  try {
    for (let at = 0; at < names.length; at += 4000) {
      const batch = names.slice(at, at + 4000);
      const output = execFileSync('c++filt', [], {
        input: batch.join('\n'), encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
      }).split(/\r?\n/);
      batch.forEach((name, index) => result.set(name, output[index] || name));
    }
  } catch { /* c++filt is optional */ }
  return result;
}

const displayDemangled = demangle(mangled);
for (const [name, demangled] of displayDemangled) {
  displayNames.set(name, demangled);
  if (!sourceByName.has(name) && symbols[demangled]) sourceByName.set(name, symbols[demangled]);
}

// Build a reverse source lookup for demangled names when V8 emits them. Only
// names that occurred in this profile are retained.
const unresolved = new Set([...allNames].filter(name => !sourceByName.has(name)));
if (unresolved.size) {
  const entries = Object.entries(symbols).filter(([name]) => /^(_Z|__Z|_R|__R)/.test(name));
  for (let at = 0; at < entries.length && unresolved.size; at += 4000) {
    const batch = entries.slice(at, at + 4000);
    const demangledBatch = demangle(batch.map(([name]) => name));
    for (const [name, source] of batch) {
      const demangled = demangledBatch.get(name);
      if (demangled && unresolved.has(demangled)) {
        sourceByName.set(demangled, source);
        unresolved.delete(demangled);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Classification.
// ---------------------------------------------------------------------------
const idleStages = new Set(rulesDoc.nonIdleExclude || ['idle', 'idle/waiting']);
const stages = new Map();                 // stage -> self us (all targets)
const stagePerTarget = new Map();         // target file -> Map(stage -> us)
const unclassifiedFunctions = new Map();  // key -> {name,url,us}
const functionTotals = new Map();         // key -> {name,url,us,stage}

const sourceStageRules = [
  { stage: 'Audio', source: /^source\/kernel\/devs\/(devdsp|devmixer|devsequencer)/ },
  { stage: '64-bit CPU JIT compiler', source: /^source\/emulation\/cpu\/jit64/ },
  { stage: '32-bit CPU JIT compiler', source: /^lib\/asmjit\// },
  { stage: '32-bit CPU JIT compiler', source: /^source\/emulation\/cpu\/(jit\/|wasm\/|x32\/)/ },
  { stage: '64-bit CPU interpreter', source: /^source\/emulation\/cpu\/cpu64(?:RunElf)?\.cpp$/ },
  { stage: '32-bit CPU interpreter', source: /^source\/emulation\/cpu\/(normal\/|common\/|decoder\.cpp$|cpu\.cpp$|incdec\.h$|instructions\.h$|ops\.h$|pushpop\.h$|setcc\.h$|shift[^/]*\.h$|strings[^/]*\.h$)/ },
  { stage: 'Guest memory/MMU', source: /^source\/(kernel\/kmemory(?:64)?\.cpp|emulation\/softmmu\/)/ },
  { stage: 'Kernel syscalls + VFS', source: /^(source\/kernel\/|source\/io\/)/ },
  { stage: 'X11 wire + SDL present', source: /^(source\/(x11wire|x11|sdl)\/|platform\/(sdl|linux)\/)/ },
  { stage: 'Vulkan bridge', source: /^source\/vulkan\// },
  { stage: 'GL bridge to WebGL2', source: /^source\/opengl\// },
];

function sourceStage(name) {
  const source = sourceByName.get(name) || symbols[name] || '';
  for (const rule of sourceStageRules) if (rule.source.test(source)) return rule.stage;
  return null;
}

function classify(name, url, target) {
  if (name === '(idle)') return 'idle';
  if (url.startsWith('wasm://') && url !== target.mainWasmUrl) {
    const hint = `${name} ${url}`;
    if (/(jit64|cpu64|wasm64|x86_64)/i.test(hint)) return '64-bit CPU JIT-compiled code';
    if (/(jit32|cpu32|wasm32|i[3-6]86)/i.test(hint)) return '32-bit CPU JIT-compiled code';
    return 'CPU JIT-compiled code';
  }
  for (const rule of rules) {
    if (rule.functionName && !rule.functionName.test(name)) continue;
    if (rule.url && !rule.url.test(url)) continue;
    return rule.stage;
  }
  // Profiling builds expose the main Emscripten module by its fetched wasm
  // URL, while dynamic JIT blocks use wasm:// URLs. Resolve C++ names in the
  // main module through symbols.json before leaving them unclassified.
  if (/\/boxedwine64\.wasm(?:[?#]|$)/i.test(url)) return sourceStage(name) || 'unclassified';
  if (url.startsWith('wasm://')) return sourceStage(name) || 'unclassified';
  return 'unclassified';
}

for (const target of targets) {
  const perStage = new Map();
  for (const [key, us] of target.selfUs) {
    const [name, url] = key.split('\u0000');
    const stage = classify(name, url, target);
    stages.set(stage, (stages.get(stage) || 0) + us);
    perStage.set(stage, (perStage.get(stage) || 0) + us);
    const total = functionTotals.get(key) || { name, url, us: 0, stage };
    total.us += us;
    functionTotals.set(key, total);
    if (stage === 'unclassified') {
      const entry = unclassifiedFunctions.get(key) || { name, url, us: 0 };
      entry.us += us;
      unclassifiedFunctions.set(key, entry);
    }
  }
  stagePerTarget.set(target.file, perStage);
}

const totalUs = [...stages.values()].reduce((a, b) => a + b, 0);
const idleUs = [...stages].filter(([stage]) => idleStages.has(stage)).reduce((sum, [, us]) => sum + us, 0);
const nonIdleUs = totalUs - idleUs;
const usToMs = us => Number((us / 1000).toFixed(3));

const busiest = targets
  .map(target => {
    const perStage = stagePerTarget.get(target.file);
    const total = [...perStage.values()].reduce((a, b) => a + b, 0);
    const idle = [...perStage].filter(([stage]) => idleStages.has(stage)).reduce((sum, [, us]) => sum + us, 0);
    return { file: target.file, type: target.type, url: target.url, nonIdleUs: total - idle };
  })
  .sort((a, b) => b.nonIdleUs - a.nonIdleUs)[0] || { file: null, type: null, url: null, nonIdleUs: 0 };

const stageRows = [...stages.entries()].map(([stage, us]) => {
  const busiestStageUs = busiest.file ? (stagePerTarget.get(busiest.file)?.get(stage) || 0) : 0;
  return {
    stage,
    selfMs: usToMs(us),
    pctNonIdle: idleStages.has(stage) ? null : (nonIdleUs ? Number((100 * us / nonIdleUs).toFixed(2)) : 0),
    pctBusiest: idleStages.has(stage) ? null : (busiest.nonIdleUs ? Number((100 * busiestStageUs / busiest.nonIdleUs).toFixed(2)) : 0),
  };
}).sort((a, b) => b.selfMs - a.selfMs);

const topFunctions = [...functionTotals.values()]
  .sort((a, b) => b.us - a.us)
  .slice(0, 20)
  .map(entry => ({
    function: displayNames.get(entry.name) || entry.name,
    rawFunction: entry.name,
    url: entry.url,
    stage: entry.stage,
    selfMs: usToMs(entry.us),
  }));

const unclassifiedTop = [...unclassifiedFunctions.values()]
  .sort((a, b) => b.us - a.us)
  .slice(0, 30)
  .map(entry => ({
    function: displayNames.get(entry.name) || entry.name,
    rawFunction: entry.name,
    url: entry.url,
    selfMs: usToMs(entry.us),
  }));
const unclassifiedUs = [...unclassifiedFunctions.values()].reduce((sum, entry) => sum + entry.us, 0);

const result = {
  generatedAt: new Date().toISOString(),
  outDir,
  ruleFile: rulesPath,
  targets: targets.map(target => ({ file: target.file, type: target.type, url: target.url,
    selfMs: usToMs([...target.selfUs.values()].reduce((a, b) => a + b, 0)) })),
  totals: {
    selfMs: usToMs(totalUs), nonIdleMs: usToMs(nonIdleUs), idleMs: usToMs(idleUs),
    unclassifiedMs: usToMs(unclassifiedUs),
    unclassifiedPctNonIdle: nonIdleUs ? Number((100 * unclassifiedUs / nonIdleUs).toFixed(2)) : 0,
  },
  busiestTarget: { file: busiest.file, type: busiest.type, url: busiest.url, nonIdleMs: usToMs(busiest.nonIdleUs) },
  stages: stageRows,
  topFunctions,
  unclassified: unclassifiedTop,
};

const guest = await buildGuestTable(outDir);
result.guestProfile = guest;
await writeFile(join(outDir, 'stages.json'), JSON.stringify(result, null, 2) + '\n');
await writeFile(join(outDir, 'report.md'), markdown(result, guest) + '\n');
console.log(JSON.stringify({ outDir, nonIdleMs: result.totals.nonIdleMs,
  unclassifiedPct: result.totals.unclassifiedPctNonIdle,
  slowestStage: stageRows.find(row => !idleStages.has(row.stage))?.stage || null }));

// ---------------------------------------------------------------------------
// Guest-profile table + grouping.
// ---------------------------------------------------------------------------
function groupFor(file, exe) {
  const base = file.replace(/^.*[\\/]/, '').toLowerCase();
  const exeBase = exe.replace(/^.*[\\/]/, '').toLowerCase();
  if (base === '(unmapped)' || !base) return 'other';
  if (exeBase && base === exeBase) return 'game exe';
  if (/^(d3d9|d3d11|dxgi|d3d10core)\.dll$/.test(base)) return 'DXVK';
  if (/^d3d12\.dll$/.test(base) || /^libvkd3d/.test(base)) return 'vkd3d';
  if (base === 'libvulkan.so.1' || /^libvk64/.test(base)) return 'Vulkan shim';
  if (/\.(dll|so|so\.[0-9]+)$/.test(base) || /wine|windows|system32/.test(file.toLowerCase())) return 'Wine';
  if (base.endsWith('.exe')) return 'game exe';
  return 'other';
}

async function buildGuestTable(directory) {
  const path = join(directory, 'guest-profile.json');
  if (!existsSync(path)) return null;
  let doc;
  try { doc = JSON.parse(await readFile(path, 'utf8')); } catch { return null; }
  const rows = [];
  const rowTotals = new Map();
  const groups = new Map();
  let total = 0;
  for (const process of doc.processes || []) {
    for (const file of process.files || []) {
      const group = groupFor(file.path || '', process.exe || '');
      const pathKey = `${group}\u0000${file.path || ''}`;
      const row = rowTotals.get(pathKey) || { path: file.path || '', samples: 0, group };
      row.samples += file.samples || 0;
      rowTotals.set(pathKey, row);
      groups.set(group, (groups.get(group) || 0) + (file.samples || 0));
      total += file.samples || 0;
    }
  }
  rows.push(...rowTotals.values());
  rows.sort((a, b) => b.samples - a.samples);
  return {
    total,
    rows: rows.slice(0, 15),
    groups: [...groups.entries()].map(([group, samples]) => ({
      group, samples, pct: total ? Number((100 * samples / total).toFixed(2)) : 0,
    })).sort((a, b) => b.samples - a.samples),
  };
}

function markdown(result, guest) {
  const lines = [];
  lines.push('# CPU profile stages', '');
  lines.push(`- outdir: \`${result.outDir}\``);
  lines.push(`- targets: ${result.targets.length}; non-idle: ${result.totals.nonIdleMs} ms; idle: ${result.totals.idleMs} ms`);
  lines.push(`- busiest thread: ${result.busiestTarget.type} \`${result.busiestTarget.url || result.busiestTarget.file}\` (${result.busiestTarget.nonIdleMs} ms non-idle)`);
  lines.push(`- unclassified: ${result.totals.unclassifiedMs} ms (${result.totals.unclassifiedPctNonIdle}% of non-idle)`, '');
  lines.push('| stage | self ms | % non-idle | % busiest thread |', '| --- | ---: | ---: | ---: |');
  for (const row of result.stages) lines.push(`| ${row.stage} | ${row.selfMs} | ${row.pctNonIdle ?? '—'} | ${row.pctBusiest ?? '—'} |`);
  lines.push('', '## Top 20 functions by self time', '', '| function | stage | self ms | url |', '| --- | --- | ---: | --- |');
  for (const row of result.topFunctions) lines.push(`| \`${escapePipes(row.function)}\` | ${row.stage} | ${row.selfMs} | ${escapePipes(row.url)} |`);
  if (guest) {
    lines.push('', '## Guest profile: top mapped files', '',
      '| mapped file | group | samples | % |', '| --- | --- | ---: | ---: |');
    for (const row of guest.rows) {
      lines.push(`| ${escapePipes(row.path)} | ${row.group} | ${row.samples} | ${guest.total ? (100 * row.samples / guest.total).toFixed(2) : '0.00'} |`);
    }
    lines.push('', '### Guest profile: groups', '', '| group | samples | % |', '| --- | ---: | ---: |');
    for (const row of guest.groups) lines.push(`| ${row.group} | ${row.samples} | ${row.pct} |`);
  } else {
    lines.push('', '## Guest profile', '', 'Guest profile data is unavailable.');
  }
  if (result.unclassified.length) {
    lines.push('', '## Top 30 unclassified functions', '', '| function | self ms | url |', '| --- | ---: | --- |');
    for (const row of result.unclassified) lines.push(`| \`${escapePipes(row.function)}\` | ${row.selfMs} | ${escapePipes(row.url)} |`);
  }
  return lines.join('\n');
}

function escapePipes(value) {
  return String(value || '').replaceAll('|', '\\|');
}
