// Real guest Chromium acceptance/performance probe. See docs/reports/chromium-20261016.md.
// CHROMIUM_ZIP=/path/to/chromium.zip node web/tests/chromium-probe.mjs
import { mkdir, readFile, writeFile, copyFile, symlink, stat } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { execFileSync, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';
import { ProbeTest, withTimeout } from './suite/lib/probe-base.mjs';

const exec = promisify(execFile);
const source = resolve(process.env.CHROMIUM_ZIP || '/tmp/chromium-bringup/chromium109.zip');
const tag = process.env.TAG || 'chromium-acceptance';
const timeoutMs = Number(process.env.TIMEOUT_MS || 180000);
const probe = new ProbeTest({ tag, timeoutMs });
const chromeArgs = process.env.CHROME_ARGS ? JSON.parse(process.env.CHROME_ARGS) : [
  '--no-sandbox', '--no-zygote', '--single-process', '--in-process-gpu',
  '--disable-gpu', '--disable-gpu-compositing', '--disable-software-rasterizer', '--use-gl=disabled',
  '--disable-features=CalculateNativeWinOcclusion,DirectComposition', '--disable-backgrounding-occluded-windows',
  '--disable-dev-shm-usage', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
  '--disable-sync', '--disable-breakpad', '--disable-background-networking',
];
const result = { tag, startedAt: new Date().toISOString(), chromeArgs, pass: false };
const events = [];
let page;
try {
  await probe.setup();
  result.build = probe.buildInfo;
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
  page.on('console', message => {
    const match = message.text().match(/BW_CHROMIUM_JSON (\{.*\})/);
    if (!match) return;
    try { events.push({ wallMs: Date.now() - started, ...JSON.parse(match[1]) }); } catch {}
  });
  const base = probe._server.url;
  const args = [...chromeArgs, '--enable-logging=stderr', '--log-level=0', '--window-size=1000,700',
    '--user-data-dir=Z:\\home\\username\\apps\\profile', 'file:///Z:/home/username/apps/chromium-smoke.html'];
  const params = new URLSearchParams({ arch: 'x86', gamezip: base + '/__fixtures/chromium.zip',
    gamezipsize: String(result.archive.bytes), exe: 'chrome.exe', args: JSON.stringify(args),
    overlay: base + '/__fixtures/smoke.zip', overlaysize: String((await stat(overlay)).size),
    session: '0', persist: '0', fresh: '1', winedbg: process.env.WINEDEBUG || '-all' });
  result.launchParams = Object.fromEntries(params);
  const started = Date.now();
  await page.goto(base + '/runtime/index.html?' + params, { timeout: 60000 });
  async function waitEvent(predicate, label, remaining = timeoutMs) {
    const end = Math.min(started + timeoutMs, Date.now() + remaining);
    let lastLiveness = 0;
    while (Date.now() < end) {
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

  let word, inputWord, benchmarkWord;
  const renderEnd = Math.min(started + timeoutMs, Date.now() + 30000);
  do {
    await page.screenshot({ path: join(probe.dir, 'rendered.png') });
    const { stdout } = await exec('tesseract', [join(probe.dir, 'rendered.png'), 'stdout', '--psm', '6', 'tsv'], { timeout: 15000 });
    const words = stdout.split('\n').map(line => line.split('\t'));
    word = words.find(row => row.length >= 12 && /^Count$/i.test(row[11]));
    inputWord = words.find(row => row.length >= 12 && /^Type$/i.test(row[11]));
    benchmarkWord = words.find(row => row.length >= 12 && /^Run$/i.test(row[11]));
    if (!word || !inputWord || !benchmarkWord) await page.waitForTimeout(1000);
  } while ((!word || !inputWord || !benchmarkWord) && Date.now() < renderEnd);
  if (!word || !inputWord || !benchmarkWord) throw Error('Guest controls are not readable in the screenshot');
  result.clickTarget = { x: Number(word[6]) + Number(word[8]) / 2, y: Number(word[7]) + Number(word[9]) / 2, canvas: await page.locator('#canvas').boundingBox() };
  result.beforeClick = await withTimeout(page.evaluate(() => JSON.parse(globalThis.Module.ccall('bw64_input_snapshot', 'string', [], []))), 5000, 'input snapshot');
  const point = row => ({ x: Number(row[6]) + Number(row[8]) / 2 - result.clickTarget.canvas.x, y: Number(row[7]) + Number(row[9]) / 2 - result.clickTarget.canvas.y });
  result.inputTarget = point(inputWord);
  result.benchmarkTarget = point(benchmarkWord);
  const beforeClick = Date.now();
  const target = { x: result.clickTarget.x - result.clickTarget.canvas.x, y: result.clickTarget.y - result.clickTarget.canvas.y };
  await page.locator('#canvas').click({ position: target, timeout: 10000 });
  await page.waitForTimeout(500);
  if (!events.some(e => e.event === 'click')) await page.locator('#canvas').click({ position: target, timeout: 10000 });
  result.click = await waitEvent(e => e.event === 'click' && e.clicks >= 1, 'mouse click', 30000);
  result.clickLatencyMs = result.click.wallMs - (beforeClick - started);
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
  await page.waitForTimeout(1000);
  await page.screenshot({ path: join(probe.dir, 'interaction.png') });
  result.inputSnapshot = await page.evaluate(() => JSON.parse(globalThis.Module.ccall('bw64_input_snapshot', 'string', [], [])));
  result.pass = true;
} catch (error) {
  result.error = error.stack || String(error);
  result.inputSnapshot = await withTimeout(page?.evaluate(() => JSON.parse(globalThis.Module.ccall('bw64_input_snapshot', 'string', [], []))), 5000, 'failure snapshot').catch(() => null);
  await page?.screenshot({ path: join(probe.dir, 'failure.png'), timeout: 10000 }).catch(() => {});
} finally {
  result.finishedAt = new Date().toISOString();
  result.events = events;
  result.benchmark ||= events.find(e => e.event === 'benchmark');
  await mkdir(probe.dir, { recursive: true });
  await writeFile(join(probe.dir, 'summary.json'), JSON.stringify(result, null, 2));
  await withTimeout(probe.teardown(), 45000, 'Chromium probe teardown').catch(() => {});
}
console.log(JSON.stringify({ dir: probe.dir, pass: result.pass, ready: result.ready, benchmark: result.benchmark,
  clickLatencyMs: result.clickLatencyMs, error: result.error }));
process.exit(result.pass ? 0 : 1);
