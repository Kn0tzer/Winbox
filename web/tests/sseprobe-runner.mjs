// sseprobe runner: launches the sseprobe demo (staged into dist by the
// caller) with the JIT on or off and captures its SSEPROBE console lines.
// Env: DIST_DIR (absolute), JIT (0|1), OUT (dir), TIMEOUT_S.
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

// Copied from demo-matrix.mjs (importing that module runs its DEMO env
// validation, which does not know the staged sseprobe demo).
async function applyJitSetting(page, jit) {
  await page.waitForFunction(() => window.wineGames, null, { timeout: 60000 });
  await page.click('#settingsButton');
  await page.locator('#jit').selectOption(jit === '1' ? 'on' : 'off');
  await page.keyboard.press('Escape');
  await page.reload();
  await page.waitForFunction(() => window.wineGames, null, { timeout: 60000 });
  const actualJit = await page.locator('#jit').inputValue();
  if (actualJit !== (jit === '1' ? 'on' : 'off')) throw new Error(`Settings reload selected JIT=${actualJit}, expected ${jit}`);
}
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { appendFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const distDir = process.env.DIST_DIR;
if (!distDir) throw new Error('DIST_DIR required');
const jit = process.env.JIT || '1';
const timeoutS = Number(process.env.TIMEOUT_S || 600);
const outDir = resolve(process.env.OUT || `test-results/framefloor/sseprobe-${jit}-${Date.now()}`);
await mkdir(outDir, { recursive: true });

const loadavg = async () => { try { return (await readFile('/proc/loadavg', 'utf8')).trim(); } catch { return '?'; } };
console.log('LOAD at start:', await loadavg());
if (process.env.SKIP_BUILD_WAIT !== '1') await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
const server = await serve(distDir);
const baseUrl = `http://127.0.0.1:${server.address().port}/`;
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
const t0 = Date.now();
const now = () => Date.now() - t0;
const consoleLines = [];
const streamPath = outDir + '.console.jsonl';
try { writeFileSync(streamPath, ''); } catch {}
page.on('console', m => {
  const text = m.text();
  consoleLines.push({ t: now(), type: m.type(), text });
  try { appendFileSync(streamPath, JSON.stringify({ t: now(), type: m.type(), text }) + '\n'); } catch {}
});
page.on('pageerror', e => consoleLines.push({ t: now(), type: 'pageerror', text: String(e && e.message || e) }));

await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
await applyJitSetting(page, jit);
console.log('JIT SETTING APPLIED', jit);
await page.locator('[data-game="sseprobe"]').click();
console.log('CLICKED sseprobe at', now());

const deadline = Date.now() + timeoutS * 1000;
let checksumLine = null, timingLine = null;
while (Date.now() < deadline) {
  checksumLine = consoleLines.find(l => l.text.includes('SSEPROBE checksum')) || checksumLine;
  timingLine = consoleLines.find(l => l.text.includes('SSEPROBE timing')) || timingLine;
  if (checksumLine && timingLine) break;
  await page.waitForTimeout(2000);
}
const metrics = consoleLines.filter(l => l.text.includes('bw64: METRICS')).slice(-1)[0]?.text || null;
const result = { jit, distDir, checksumLine: checksumLine?.text || null, timingLine: timingLine?.text || null, metricsTail: metrics, consoleLines };
await writeFile(resolve(outDir, 'probe.json'), JSON.stringify(result, null, 1));
console.log('CHECKSUM:', result.checksumLine);
console.log('TIMING:', result.timingLine);
console.log('WROTE', resolve(outDir, 'probe.json'), 'console lines:', consoleLines.length);
await browser.close();
server.close();
slot.release();
