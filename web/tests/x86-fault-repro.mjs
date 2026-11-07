// x86-fault-repro.mjs — diagnostic driver for FD-201 (not a check).
//
// Boots 32-bit sessions through the shipped page path (runtime/index.html,
// bwRuntime.ready()) N times against a local dist, with the launcher's
// restart budget pre-exhausted (bwretry=2, the batch-10 deploy driver's
// setting, so no start is a rescue). Chromium is spawned with a DevTools
// port and every target (page, iframe, emulator workers) is auto-attached
// over CDP with Runtime enabled, so a WebAssembly trap inside any worker
// is recorded in exceptions.jsonl with its full stack trace (wasm function
// indices of the build under test) instead of only the message string the
// launcher forwards to the console. NOTE: indices are per-build -- a
// BW64_PROFILE=1 build's function bodies differ from the shipped build's
// at the same indices (measured 2026-11-07), so its name section must not
// be used to interpret another build's stack; identify functions against
// the trapping build's own wasm.
//
// Env:
//   DIST       dist to serve (default: dist)
//   N          boots to attempt (default 12)
//   BUDGET_MS  per-boot ready budget (default 240000)
//   TAG        results dir under test-results/x86-start-fault/ (default repro-<stamp>)
//   EXTRA      extra URL params, e.g. "drvinit=1"
import { chromium } from '@playwright/test';
import { WebSocket } from 'ws';
import { spawn } from 'node:child_process';
import { appendFile, mkdir, mkdtemp, readdir, symlink, rm, readFile, writeFile } from 'node:fs/promises';
import { readFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { tmpdir } from 'node:os';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

const DIST = resolve(process.env.DIST || 'dist');
const N = Number(process.env.N || 12);
const BUDGET_MS = Number(process.env.BUDGET_MS || 240000);
const TAG = process.env.TAG || ('repro-' + new Date().toISOString().replace(/[-:T]/g, '').slice(0, 14));
const EXTRA = process.env.EXTRA ? Object.fromEntries(new URLSearchParams(process.env.EXTRA)) : {};
const OUT = join('test-results', 'x86-start-fault', TAG);
const MEMFAULT = /memory access out of bounds/i;

const loadavg = () => readFileSync('/proc/loadavg', 'utf8').trim().split(/\s+/).slice(0, 3).map(Number);
await mkdir(OUT, { recursive: true });

// --- CDP plumbing (pattern after web/tests/fd128-godot.mjs) ---
let ws = null;
let nextCdpId = 0;
const cdpPending = new Map();
const cdpSessions = new Map(); // sessionId -> targetInfo
let currentBoot = 0;
let bootStartMs = 0;
const exceptions = [];
function cdpSend(method, params = {}, sessionId = undefined) {
  const id = ++nextCdpId;
  return new Promise((res, rej) => {
    cdpPending.set(id, { res, rej, method });
    ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
  });
}
async function handleCdp(message) {
  if (message.id && cdpPending.has(message.id)) {
    const item = cdpPending.get(message.id);
    cdpPending.delete(message.id);
    if (message.error) item.rej(new Error(`${item.method}: ${message.error.message}`));
    else item.res(message.result);
    return;
  }
  if (message.method === 'Target.attachedToTarget') {
    const { sessionId, targetInfo } = message.params;
    if (cdpSessions.has(sessionId)) return;
    cdpSessions.set(sessionId, targetInfo);
    try { await cdpSend('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, sessionId); } catch {}
    try { await cdpSend('Runtime.enable', {}, sessionId); } catch {}
    return;
  }
  if (message.method === 'Target.detachedFromTarget') { cdpSessions.delete(message.params.sessionId); return; }
  if (message.method === 'Runtime.exceptionThrown') {
    const info = cdpSessions.get(message.sessionId) || {};
    const details = message.params.exceptionDetails || {};
    const row = { boot: currentBoot, atMs: bootStartMs ? Date.now() - bootStartMs : -1,
      targetType: info.type || '', targetUrl: info.url || '', exceptionDetails: details };
    exceptions.push(row);
    await appendFile(join(OUT, 'exceptions.jsonl'), JSON.stringify(row) + '\n').catch(() => {});
    const desc = details.exception?.description || details.text || '';
    console.log(`[repro] exception in ${row.targetType || '?'} target (boot ${currentBoot}): ${String(desc).split('\n')[0]}`);
  }
}
async function waitForDevTools(directory, child) {
  const file = join(directory, 'DevToolsActivePort');
  for (let i = 0; i < 300; i++) {
    try {
      const text = await readFile(file, 'utf8');
      const [port, path] = text.trim().split('\n');
      if (port && path) return { port, path };
    } catch {}
    if (child.exitCode !== null) throw new Error(`chromium exited early (${child.exitCode})`);
    await new Promise(r => setTimeout(r, 200));
  }
  throw new Error('DevToolsActivePort never appeared');
}

// --- serve the dist like probe-base/boot-rate do ---
const root = await mkdtemp(join(tmpdir(), 'bw-faultrepro-'));
for (const f of await readdir(DIST)) await symlink(join(DIST, f), join(root, f));
const server = await serve(root);
server.url = 'http://127.0.0.1:' + server.address().port;
const buildInfo = JSON.parse(await readFile(join(DIST, 'runtime/BUILD_INFO.json'), 'utf8'));
console.log(`x86-fault-repro: dist git_sha=${buildInfo.git_sha} wasm=${buildInfo.wasm_sha256.slice(0, 8)} profiling=${buildInfo.profiling} n=${N} budget=${BUDGET_MS}ms tag=${TAG}`);

const userDataDir = await mkdtemp('/tmp/winbox-faultrepro-');
const chromeChild = spawn(process.env.CHROME_PATH || '/usr/local/bin/chromium', [
  '--headless=new', '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
  '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
  '--enable-features=Vulkan', '--remote-debugging-port=0', '--remote-debugging-address=127.0.0.1',
  `--user-data-dir=${userDataDir}`, 'about:blank',
], { stdio: 'ignore' });
const devtools = await waitForDevTools(userDataDir, chromeChild);
ws = new WebSocket(`ws://127.0.0.1:${devtools.port}${devtools.path}`);
await new Promise((res, rej) => { ws.on('open', res); ws.on('error', rej); });
ws.on('message', m => { handleCdp(JSON.parse(m.toString())).catch(e => console.error('cdp:', e.message)); });
await cdpSend('Target.setDiscoverTargets', { discover: true });
await cdpSend('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });
const browser = await chromium.connectOverCDP(`http://127.0.0.1:${devtools.port}`);

const results = [];
async function oneBoot(i) {
  currentBoot = i;
  const dir = join(OUT, `boot-${String(i).padStart(2, '0')}`);
  await mkdir(dir, { recursive: true });
  const lines = [];
  let context = null, page = null;
  try { context = await browser.newContext(); } catch { context = browser.contexts()[0]; }
  page = await context.newPage();
  page.on('console', m => lines.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => lines.push(`[pageerror] ${e.message}`));
  const loadStart = loadavg();
  const t0 = Date.now();
  bootStartMs = t0;
  const params = new URLSearchParams({ p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', arch: 'x86', bwretry: '2', ...EXTRA });
  let cls = 'other', readyMs = null, note = '';
  const excBase = exceptions.length;
  try {
    await page.goto(`${server.url}/runtime/index.html?${params}`);
    let faultAt = 0, serverExitAt = 0;
    while (Date.now() - t0 < BUDGET_MS) {
      let ready = false;
      try { ready = await page.evaluate(() => globalThis.bwRuntime?.ready?.() === true); } catch {}
      if (ready) { cls = 'started'; readyMs = Date.now() - t0; break; }
      const text = lines.join('\n');
      const now = Date.now();
      if (!faultAt && (MEMFAULT.test(text) || exceptions.slice(excBase).some(r => MEMFAULT.test(r.exceptionDetails?.exception?.description || '')))) faultAt = now;
      if (faultAt && now - faultAt > 8000) { cls = 'faulted'; note = 'guest memfault delivered'; break; }
      if (!serverExitAt && /exit_group.*wineserver/.test(text)) serverExitAt = now;
      if (serverExitAt && now - serverExitAt > 10000) { note = 'wineserver exited'; break; }
      await page.waitForTimeout(1000);
    }
    if (cls === 'other' && !note) note = 'budget exhausted';
  } catch (e) {
    note = 'harness error: ' + String(e).slice(0, 200);
  } finally {
    await writeFile(join(dir, 'console.log'), lines.join('\n') + '\n');
    try { await page.close(); } catch {}
    if (context && context !== browser.contexts()[0]) { try { await context.close(); } catch {} }
  }
  const rec = { boot: i, class: cls, readyMs, wallMs: Date.now() - t0, loadStart, loadEnd: loadavg(),
    memfaultLines: lines.filter(l => MEMFAULT.test(l)).length,
    exceptionsThisBoot: exceptions.slice(excBase).length, note };
  results.push(rec);
  console.log(`boot ${i}: ${cls}${readyMs ? ' in ' + (readyMs / 1000).toFixed(1) + 's' : ''} (load ${loadStart[0]} -> ${rec.loadEnd[0]}) ${note}`);
  await writeFile(join(OUT, 'results.jsonl'), results.map(r => JSON.stringify(r)).join('\n') + '\n');
  return rec;
}

for (let i = 1; i <= N; i++) {
  await waitForBuildIdle();
  let slot;
  try { slot = await acquireProbeSlot({ timeoutMs: 900000, kind: 'heavy' }); }
  catch (e) { console.error('could not acquire probe slot: ' + String(e).slice(0, 200)); break; }
  try { await oneBoot(i); } finally { await slot.release(); }
}

const summary = { dist: { git_sha: buildInfo.git_sha, wasm: buildInfo.wasm_sha256, profiling: buildInfo.profiling },
  n: results.length, started: results.filter(r => r.class === 'started').length,
  faulted: results.filter(r => r.class === 'faulted').length,
  other: results.filter(r => r.class === 'other').length, budgetMs: BUDGET_MS, arch: 'x86' };
await writeFile(join(OUT, 'summary.json'), JSON.stringify(summary, null, 2));
console.log('SUMMARY ' + JSON.stringify(summary));
try { await browser.close(); } catch {}
try { chromeChild.kill('SIGKILL'); } catch {}
server.close();
await rm(root, { recursive: true, force: true });
await rm(userDataDir, { recursive: true, force: true });
