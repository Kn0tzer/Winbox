// Real guest Chromium acceptance/performance probe. See docs/reports/chromium-20261016.md.
// CHROMIUM_ZIP=/path/to/chromium.zip node web/tests/chromium-probe.mjs
import { mkdir, readFile, writeFile, copyFile, symlink, stat } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { execFileSync, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';
import { cpus, loadavg } from 'node:os';
import { ProbeTest, withTimeout, extraUrlParams } from './suite/lib/probe-base.mjs';
import { attachLoopbackTcpRelay } from './suite/lib/loopback-tcp-relay.mjs';

const exec = promisify(execFile);
const source = resolve(process.env.CHROMIUM_ZIP || '/tmp/chromium-bringup/chromium109.zip');
const tag = process.env.TAG || 'chromium-acceptance';
const timeoutMs = Number(process.env.TIMEOUT_MS || 180000);
// Explicit test mode: serve the guest fixture through the owned loopback relay.
const httpFixture = process.env.HTTP_FIXTURE === '1';
const longScene = process.env.LONG_SCENE === '1';
const sceneFrames = longScene ? 300 : 30;
const probe = new ProbeTest({ tag, timeoutMs });
const chromeArgs = process.env.CHROME_ARGS ? JSON.parse(process.env.CHROME_ARGS) : [
  '--no-sandbox', '--no-zygote', '--single-process', '--in-process-gpu',
  '--disable-gpu', '--disable-gpu-compositing', '--disable-software-rasterizer', '--use-gl=disabled',
  '--disable-features=CalculateNativeWinOcclusion,DirectComposition', '--disable-backgrounding-occluded-windows',
  '--disable-dev-shm-usage', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
  '--disable-sync', '--disable-breakpad', '--disable-background-networking',
];
const result = { httpFixture, tag, startedAt: new Date().toISOString(), chromeArgs, sceneFrames, pass: false,
  host: { arch: process.arch, logicalCpus: cpus().length, cpu: cpus()[0]?.model, loadAverage: loadavg() } };
const events = [];
let page, browserCdp, readyCpu, sceneCpuBefore, sceneCpuAfter, relay;
try {
  await probe.setup();
  result.build = probe.buildInfo;
  result.hostBrowserVersion = probe._browser.version();
  result.runtimeCodeHash = probe.runtimeCodeHash;
  const archiveHash = createHash('sha256');
  for await (const chunk of createReadStream(source)) archiveHash.update(chunk);
  result.archive = { path: source, bytes: (await stat(source)).size, sha256: archiveHash.digest('hex') };
  await symlink(source, join(probe._root, '__fixtures/chromium.zip'));
  const stage = join(probe._root, 'smoke');
  await mkdir(stage);
  const html = resolve('tools/guest-fixtures/chromium-smoke.html');
  result.fixtureSha256 = createHash('sha256').update(await readFile(html)).digest('hex');
  await copyFile(html, join(stage, 'chromium-smoke.html'));
  const overlay = join(probe._root, '__fixtures/smoke.zip');
  execFileSync('python3', ['tools/make_gamezip.py', stage, overlay], { stdio: 'pipe' });
  page = await probe.newPage();
  result.runtimeAssetRequests = [];
  page.on('request', request => {
    if (/\/boxedwine64\.(?:js|wasm)$/.test(new URL(request.url()).pathname)) result.runtimeAssetRequests.push(request.url());
  });
  browserCdp = await withTimeout(probe._browser.newBrowserCDPSession(), 5000, 'CPU accounting session');
  page.on('console', message => {
    const match = message.text().match(/BW_CHROMIUM_JSON (\{.*\})/);
    if (!match) return;
    try {
      const event = { wallMs: Date.now() - started, ...JSON.parse(match[1]) };
      events.push(event);
      if (event.event === 'ready') {
        readyCpu = withTimeout(browserCdp.send('SystemInfo.getProcessInfo'), 5000, 'ready CPU snapshot')
          .then(value => ({ value }), error => ({ error: String(error) }));
      }
      if (event.event === 'benchmark' || event.event === 'graphics') {
        const snapshot = withTimeout(browserCdp.send('SystemInfo.getProcessInfo'), 5000, 'scene CPU snapshot')
          .then(value => ({ value }), error => ({ error: String(error) }));
        if (event.event === 'benchmark') sceneCpuBefore = snapshot;
        else sceneCpuAfter = snapshot;
      }
    } catch {}
  });
  const base = probe._server.url;
  let guestUrl = 'file:///Z:/home/username/apps/chromium-smoke.html';
  if (httpFixture) {
    await copyFile(html, join(probe._root, '__fixtures/chromium-smoke.html'));
    guestUrl = base + '/__fixtures/chromium-smoke.html';
    relay = attachLoopbackTcpRelay(probe._server);
    result.httpRequests = [];
    result.webSockets = [];
    probe._server.on('request', request => {
      if (request.url?.split('?')[0] === '/__fixtures/chromium-smoke.html')
        result.httpRequests.push({ method: request.method, url: request.url, userAgent: request.headers['user-agent'] });
    });
    page.on('websocket', socket => result.webSockets.push(socket.url()));
  }
  const args = [...chromeArgs, '--enable-logging=stderr', '--log-level=0', '--window-size=1000,700',
    '--user-data-dir=Z:\\home\\username\\apps\\profile', guestUrl + (longScene ? '?longScene=1' : '')];
  const params = new URLSearchParams({ arch: 'x86', gamezip: base + '/__fixtures/chromium.zip',
    gamezipsize: String(result.archive.bytes), exe: 'chrome.exe', args: JSON.stringify(args),
    overlay: base + '/__fixtures/smoke.zip', overlaysize: String((await stat(overlay)).size),
    session: '0', persist: '0', fresh: '1', winedbg: process.env.WINEDEBUG || '-all' });
  // Engine selection for A/B runs (BW_TEST_URL_PARAMS='jit=1' etc.): the same
  // public parameters the Winbox launcher uses, forwarded to the runtime page.
  for (const [key, value] of Object.entries(extraUrlParams())) params.set(key, value);
  if (httpFixture) params.set('network', '1');
  result.launchParams = Object.fromEntries(params);
  const cpuBefore = await withTimeout(browserCdp.send('SystemInfo.getProcessInfo'), 5000, 'initial CPU snapshot');
  const started = Date.now();
  await page.goto(base + '/runtime/index.html?' + params, { timeout: 60000 });
  async function waitEvent(predicate, label, remaining = timeoutMs) {
    const end = Math.min(started + timeoutMs, Date.now() + remaining);
    let lastLiveness = 0;
    while (Date.now() < end) {
      const runtimeError = probe.consoleLines.find(line => /\[pageerror\]|Native panic stack|RuntimeError: memory access out of bounds/.test(line));
      if (runtimeError) throw Error('Runtime failed before ' + label + ': ' + runtimeError);
      if (Date.now() - lastLiveness > 2000) {
        lastLiveness = Date.now();
        const boot = probe.consoleLines.find(line => /bootPid=\d+/.test(line))?.match(/bootPid=(\d+)/);
        if (boot && Date.now() - started > 15000) {
          result.mainPid = Number(boot[1]);
          const alive = await withTimeout(page.evaluate(pid => globalThis.Module.ccall('bw64_pid_alive', 'number', ['number'], [pid]), result.mainPid), 5000, 'guest liveness');
          if (alive === 0) throw Error('Guest Chromium main process exited before ' + label);
        }
      }
      const found = events.find(predicate);
      if (found) return found;
      await page.waitForTimeout(250);
    }
    throw Error('Timed out waiting for guest ' + label);
  }
  result.ready = await waitEvent(e => e.event === 'ready', 'JavaScript readiness');
  const capturedCpu = await readyCpu;
  if (!capturedCpu?.value) throw Error(capturedCpu?.error || 'Missing ready CPU snapshot');
  const before = new Map(cpuBefore.processInfo.map(p => [p.id, p]));
  const cpuReady = capturedCpu.value.processInfo;
  const processes = cpuReady.map(p => ({ ...p, deltaSeconds: p.cpuTime - (before.get(p.id)?.cpuTime || 0) }));
  // All guest Wasm workers run in one host renderer. Chromium can replace an
  // idle spare during navigation, so account the stable active renderer only.
  const active = processes.filter(p => p.type === 'renderer' && p.deltaSeconds > 1);
  if (active.length !== 1 || !before.has(active[0].id)) throw Error('Guest renderer was replaced or CPU ownership is ambiguous');
  result.startupCpu = { rendererId: active[0].id, rendererSeconds: active[0].deltaSeconds,
    before: cpuBefore.processInfo, ready: cpuReady, processes };

  let word, inputWord, benchmarkWord;
  const renderEnd = Math.min(started + timeoutMs, Date.now() + 30000);
  do {
    await page.screenshot({ path: join(probe.dir, 'rendered.png') });
    // Automatic page segmentation and a uniform-block pass recognize
    // different controls in the HTTP/file screenshots. Require all three
    // labels in one pass before sending input; never guess coordinates.
    for (const mode of [6, 3]) {
      const { stdout } = await exec('tesseract', [join(probe.dir, 'rendered.png'), 'stdout', '--psm', String(mode), 'tsv'],
        { timeout: 15000, env: { ...process.env, OMP_THREAD_LIMIT: '1' } });
      await writeFile(join(probe.dir, `controls-ocr-${mode}.tsv`), stdout);
      const words = stdout.split('\n').map(line => line.split('\t'));
      word = words.find(row => row.length >= 12 && /^Count$/i.test(row[11]));
      inputWord = words.find(row => row.length >= 12 && /^Type$/i.test(row[11]));
      benchmarkWord = words.find(row => row.length >= 12 && /^Run$/i.test(row[11]));
      if (word && inputWord && benchmarkWord) { result.ocrMode = mode; break; }
    }
    if (!word || !inputWord || !benchmarkWord) await page.waitForTimeout(1000);
  } while ((!word || !inputWord || !benchmarkWord) && Date.now() < renderEnd);
  if (!word || !inputWord || !benchmarkWord) throw Error('Guest controls are not readable in the screenshot');
  result.clickTarget = { x: Number(word[6]) + Number(word[8]) / 2, y: Number(word[7]) + Number(word[9]) / 2, canvas: await page.locator('#canvas').boundingBox() };
  result.beforeClick = await withTimeout(page.evaluate(() => JSON.parse(globalThis.Module.ccall('bw64_input_snapshot', 'string', [], []))), 5000, 'input snapshot');
  const point = row => ({ x: Number(row[6]) + Number(row[8]) / 2 - result.clickTarget.canvas.x, y: Number(row[7]) + Number(row[9]) / 2 - result.clickTarget.canvas.y });
  result.inputTarget = point(inputWord);
  result.benchmarkTarget = point(benchmarkWord);
  // Separate Playwright's actionability wait from delivery inside the guest.
  await page.evaluate(() => {
    globalThis.bwProbeMouseDown = null;
    document.addEventListener('mousedown', () => { globalThis.bwProbeMouseDown = Date.now(); }, { capture: true, once: true });
  });
  const beforeClick = Date.now();
  const target = { x: result.clickTarget.x - result.clickTarget.canvas.x, y: result.clickTarget.y - result.clickTarget.canvas.y };
  await page.locator('#canvas').click({ position: target, timeout: 10000 });
  result.click = await waitEvent(e => e.event === 'click' && e.clicks >= 1, 'mouse click', 30000);
  result.clickLatencyMs = result.click.wallMs - (beforeClick - started);
  const hostMouseDown = await page.evaluate(() => globalThis.bwProbeMouseDown);
  if (!Number.isFinite(hostMouseDown)) throw Error('Host mouse-down event was not observed');
  result.clickActionabilityMs = hostMouseDown - beforeClick;
  result.clickGuestDeliveryMs = result.click.wallMs - (hostMouseDown - started);
  const beforeFieldClick = Date.now() - started;
  await page.locator('#canvas').click({ position: result.inputTarget, timeout: 10000 });
  result.focus = await waitEvent(e => e.event === 'focus' && e.wallMs >= beforeFieldClick, 'text field focus', 10000);
  await page.locator('#canvas').focus();
  await page.waitForTimeout(1000);
  await page.keyboard.type('boxedwine works', { delay: 60 });
  result.input = await waitEvent(e => e.event === 'input' && e.text === 'boxedwine works', 'keyboard input', 30000);
  await page.locator('#canvas').click({ position: result.benchmarkTarget, timeout: 10000 });
  result.benchmark = await waitEvent(e => e.event === 'benchmark', 'benchmark');
  if (result.benchmark.checksum !== 1783293664 || result.benchmark.workload !== 'integer-sum-v2' || result.benchmark.samples.length !== 8) throw Error('Guest benchmark result is incorrect');
  result.graphics = await waitEvent(e => e.event === 'graphics', 'graphics benchmark');
  if (result.graphics.workload !== 'canvas-scene-v1' || result.graphics.frames !== sceneFrames || result.graphics.intervals.length !== sceneFrames || result.graphics.colorful < 1000 || result.graphics.intervals.some(ms => !Number.isFinite(ms) || ms <= 0)) throw Error('Guest graphics benchmark is incorrect');
  const sceneBefore = await sceneCpuBefore, sceneAfter = await sceneCpuAfter;
  if (!sceneBefore?.value || !sceneAfter?.value) throw Error('Missing scene CPU snapshots');
  const rendererBefore = sceneBefore.value.processInfo.find(p => p.id === result.startupCpu.rendererId);
  const rendererAfter = sceneAfter.value.processInfo.find(p => p.id === result.startupCpu.rendererId);
  if (!rendererBefore || !rendererAfter) throw Error('Scene renderer identity changed');
  result.sceneCpu = { rendererSeconds: rendererAfter.cpuTime - rendererBefore.cpuTime,
    elapsedMs: result.graphics.wallMs - result.benchmark.wallMs,
    before: sceneBefore.value.processInfo, after: sceneAfter.value.processInfo };
  await page.waitForTimeout(1000);
  await page.screenshot({ path: join(probe.dir, 'interaction.png') });
  result.inputSnapshot = await page.evaluate(() => JSON.parse(globalThis.Module.ccall('bw64_input_snapshot', 'string', [], [])));
  if (httpFixture) {
    if (!result.httpRequests.some(request => request.method === 'GET' && /Windows NT/.test(request.userAgent || '') && /Chrome\/109\./.test(request.userAgent || '')))
      throw Error('No HTTP fixture request from guest Windows Chromium 109');
    if (!result.webSockets.length || result.webSockets.some(url => url !== relay.url))
      throw Error('Guest socket did not use the fixed same-origin relay');
    if (!relay.stats.toTcpBytes || !relay.stats.toWebSocketBytes)
      throw Error('Relay did not forward both directions');
  }
  if (process.env.MEASURE_IDLE === '1') {
    const before = await withTimeout(browserCdp.send('SystemInfo.getProcessInfo'), 5000, 'idle CPU before');
    const idleStarted = performance.now();
    await page.waitForTimeout(2000);
    const after = await withTimeout(browserCdp.send('SystemInfo.getProcessInfo'), 5000, 'idle CPU after');
    const elapsedMs = performance.now() - idleStarted;
    const initial = new Map(before.processInfo.map(process => [process.id, process]));
    const processes = after.processInfo.map(process => ({ ...process,
      deltaSeconds: initial.has(process.id) ? process.cpuTime - initial.get(process.id).cpuTime : null }));
    const renderer = processes.find(process => process.id === result.startupCpu.rendererId);
    if (!renderer || renderer.deltaSeconds === null) throw Error('Idle renderer identity changed');
    result.idleCpu = { elapsedMs, rendererSeconds: renderer.deltaSeconds,
      rendererPercentOneCore: renderer.deltaSeconds * 100000 / elapsedMs,
      before: before.processInfo, after: after.processInfo, processes };
  }
  result.pass = true;
} catch (error) {
  result.error = error.stack || String(error);
  result.inputSnapshot = await withTimeout(page?.evaluate(() => JSON.parse(globalThis.Module.ccall('bw64_input_snapshot', 'string', [], []))), 5000, 'failure snapshot').catch(() => null);
  await page?.screenshot({ path: join(probe.dir, 'failure.png'), timeout: 10000 }).catch(() => {});
} finally {
  if (relay) {
    result.relay = { url: relay.url, ...relay.stats };
    await withTimeout(relay.close(), 5000, 'loopback relay close').catch(error => {
      result.cleanupError = String(error);
      result.pass = false;
    });
  }
  result.finishedAt = new Date().toISOString();
  result.jitStats = await withTimeout(page?.evaluate(() => {
    const module = globalThis.Module;
    if (!module?._bw64_jit_stats_json_text) return null;
    return JSON.parse(module.ccall('bw64_jit_stats_json_text', 'string', [], []));
  }), 5000, 'JIT statistics').catch(() => null);
  result.events = events;
  result.benchmark ||= events.find(e => e.event === 'benchmark');
  result.graphics ||= events.find(e => e.event === 'graphics');
  await mkdir(probe.dir, { recursive: true });
  await writeFile(join(probe.dir, 'summary.json'), JSON.stringify(result, null, 2));
  await withTimeout(probe.teardown(), 45000, 'Chromium probe teardown').catch(() => {});
}
console.log(JSON.stringify({ dir: probe.dir, pass: result.pass, ready: result.ready, benchmark: result.benchmark,
  clickLatencyMs: result.clickLatencyMs, startupCpuSeconds: result.startupCpu?.rendererSeconds, error: result.error }));
process.exit(result.pass ? 0 : 1);
