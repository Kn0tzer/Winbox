// site-proof.mjs — Al-Khaser through the real website path, against a
// locally served copy of the live site shell with THIS lane's built runtime.
// Usage: node site-proof.mjs <siteUrl> <outDir>
// The driver records the full runtime log + screenshots; verdict markers:
//  - crash: the known "Unhandled privileged instruction" line appears
//  - int2dGood: an "Interrupt_0x2d ... [ GOOD ]" check line appears
//  - laterSection: a section header after Debugger Detection appears
import { createRequire } from 'node:module';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web/tests/probe-slots.mjs';

const require = createRequire('/home/ubuntu/boxedwine64/package.json');
const { chromium } = require('@playwright/test');

const SITE = process.argv[2];
const OUT = process.argv[3];
const TIMEOUT_S = Number(process.env.PROOF_TIMEOUT_S || 1500);
if (!SITE || !OUT) { console.error('usage: node site-proof.mjs <siteUrl> <outDir>'); process.exit(2); }
await mkdir(OUT, { recursive: true });

const t0 = Date.now();
const el = () => Math.round((Date.now() - t0) / 1000);
const loadavg = async () => {
  try { const s = await readFile('/proc/loadavg', 'utf8'); return s.split(' ').slice(0, 3).map(Number); }
  catch { return null; }
};
const summary = { site: SITE, startedAt: new Date().toISOString(), loadAtStart: await loadavg(),
                  crashSeen: false, int2dGood: false, laterSection: false, verdict: 'timeout' };
const slot = await acquireProbeSlot();
console.log(`[${el()}s] SLOT acquired`);
let ctx = null;
try {
  ctx = await chromium.launchPersistentContext(`/tmp/alkhaser-proof-profile`, {
    executablePath: '/usr/local/bin/chromium', headless: true,
    viewport: { width: 1280, height: 720 },
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
      '--enable-features=Vulkan'],
  });
  const page = ctx.pages()[0] || await ctx.newPage();
  page.on('pageerror', e => console.log(`[${el()}s] pageerror: ${String(e.message || e).slice(0, 200)}`));
  await page.addInitScript(() => {
    window.__logs = [];
    window.addEventListener('message', e => {
      let d = e.data;
      if (d && typeof d === 'object' && d.wine === true && typeof d.data === 'string') d = d.data;
      if (typeof d !== 'string') return;
      window.__logs.push({ t: Date.now(), s: d.slice(0, 1000) });
      if (window.__logs.length > 30000) window.__logs.splice(0, 5000);
    });
  });
  const readLogs = () => page.evaluate(() => window.__logs || []).catch(() => []);

  console.log(`[${el()}s] goto ${SITE}`);
  await page.goto(SITE, { waitUntil: 'domcontentloaded', timeout: 120000 });
  await page.waitForSelector('button.program-card', { timeout: 90000 });
  const card = page.locator('button.program-card', { hasText: 'Al-Khaser' }).first();
  await card.click({ timeout: 30000 });
  console.log(`[${el()}s] clicked Al-Khaser card`);

  const LATER = ['Anti-VM', 'Virtualization', 'Timing', 'Anti-Dumping', 'Code Injection',
                 'Analysis-Tools', 'Suspicious', 'Human Interaction', 'Office Macros'];
  let logs = [], shotIdx = 0, pastAt = 0;
  const start = Date.now();
  while (Date.now() - start < TIMEOUT_S * 1000) {
    await page.waitForTimeout(15000);
    logs = await readLogs();
    const all = logs.map(l => l.s).join('\n');
    if (all.includes('Unhandled privileged instruction')) { summary.crashSeen = true; summary.verdict = 'crash'; break; }
    if (/Inter+upt_0x2d[^\n]*\[\s*GOOD\s*\]/.test(all)) summary.int2dGood = true;
    const i2 = all.indexOf('Interrupt_0x2d');
    if (i2 >= 0 && LATER.some(h => all.indexOf(h, i2) > i2)) summary.laterSection = true;
    if (summary.int2dGood && summary.laterSection) {
      if (!pastAt) { pastAt = Date.now(); console.log(`[${el()}s] PAST int2d + later section reached`); }
      else if (Date.now() - pastAt > 120000) { summary.verdict = 'ran-past-crash-point'; break; }
    }
    await writeFile(`${OUT}/runtime-log-live.json`, JSON.stringify(logs, null, 1)).catch(() => {});
    shotIdx++;
    if (shotIdx % 2 === 1) await page.screenshot({ path: `${OUT}/shot-${String(shotIdx).padStart(2, '0')}.png` }).catch(() => {});
    console.log(`[${el()}s] poll: logs=${logs.length} crash=${summary.crashSeen} int2dGood=${summary.int2dGood} later=${summary.laterSection}`);
  }
  logs = await readLogs();
  await page.screenshot({ path: `${OUT}/final.png` }).catch(() => {});
  await writeFile(`${OUT}/runtime-log.json`, JSON.stringify(logs, null, 1));
  summary.logLinesTotal = logs.length;
  summary.elapsedSec = el();
  summary.loadAtEnd = await loadavg();
  if (summary.verdict === 'timeout' && summary.int2dGood) summary.verdict = 'past-int2d-timeout';
  await writeFile(`${OUT}/summary.json`, JSON.stringify(summary, null, 2));
  console.log(JSON.stringify(summary, null, 2));
} finally {
  if (ctx) await ctx.close().catch(() => {});
  await slot.release();
  console.log(`[${el()}s] SLOT released`);
}
