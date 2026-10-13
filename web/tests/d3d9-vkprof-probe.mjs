// D3D9 fpsinput probe: FPS-style input verification (D3D9 through DXVK).
// Validates the Portal input path in miniature: WASD movement, relative-mouse
// FPS aim (WM_MOUSEMOVE deltas), left-click shoot. Relative mouse now works
// (commit 5b0cdb74); injections paced like real pointerlock hardware (120ms).
// Run from repo root: TAG=unique TIMEOUT=1800000 node web/tests/d3d9-fpsinput-probe.mjs
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

const tag = process.env.TAG || `fpsinput-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
let summary = { gameSuccess: false };
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => errors.push(e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
  const timeout = Number(process.env.TIMEOUT || 1800000);

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0&vkprof=1`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const assets = [['fpsinput.exe', process.env.FPSINPUT_PATH || 'tools/dxvk/tri9/fpsinput.exe'],
    ['d3d9.dll', process.env.D3D9_PATH || 'tools/dxvk/build.w64/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', process.env.VK_SHIM_PATH || 'tools/rootfs64/libvk64/libvulkan.so.1']];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (registered !== 1) throw new Error('Guest VFS registration failed for ' + name);
    }, { name, b64 });
  }
  console.log('ASSETS UPLOADED');

  const bootstrapUntil = Date.now() + 90000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) { console.log('BOOTSTRAP SENTINEL SKIPPED (timeout)'); break; }
    await flush();
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');

  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });
  console.log('TIER INSTALLED');

  // Verify input functions exist.
  const fns = await page.evaluate(() => {
    const out = {};
    for (const f of ['bw64_xwire_mouse_move', 'bw64_xwire_mouse_rel', 'bw64_xwire_mouse_button', 'bw64_xwire_key']) {
      try { window.bwRuntime.call(f, ['number', 'number'], [0, 0]); out[f] = true; }
      catch (e) { out[f] = String(e.message || e).slice(0, 80); }
    }
    return out;
  });
  console.log('INPUTFNS', JSON.stringify(fns));

  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\fpsinput.exe', 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']));
  console.log('SPAWN', spawn);
  const drainUntil = Date.now() + 60000;
  while (Date.now() < drainUntil && !logs.some(l => /spawning.*fpsinput/.test(l))) await page.waitForTimeout(1000);
  console.log('SPAWN-DRAIN', logs.some(l => /spawning.*fpsinput/.test(l)) ? 'YES' : 'NO');

  const getStats = () => page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0, rendered: s.v2?.rendered || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);

  // Wait for game start + first render.
  const until = Date.now() + timeout;
  while (Date.now() < until && !logs.some(l => /fpsinput: STARTED/.test(l))) {
    await flush(); await page.waitForTimeout(1000);
  }
  if (!logs.some(l => /fpsinput: STARTED/.test(l))) throw new Error('fpsinput never STARTED');
  console.log('GAME STARTED');
  let lit = 0;
  const litUntil = Date.now() + 120000;
  while (Date.now() < litUntil && lit <= 100) {
    const st = await getStats();
    if (st && st.lit > 0) lit = st.lit;
    await page.waitForTimeout(1000);
  }
  console.log('LITPIXELS', lit);

  const lookLines = () => logs.filter(l => /fpsinput: LOOK yaw=/.test(l));
  const moveLines = () => logs.filter(l => /fpsinput: MOVE x=/.test(l));
  const shotLines = () => logs.filter(l => /fpsinput: SHOT n=/.test(l));
  const keyLines = () => logs.filter(l => /fpsinput: KEYDOWN vk=/.test(l));

  // --- RELATIVE MOUSE LOOK (FPS aim): 50 paced rel moves, like real pointerlock ---
  // First an absolute move into the window so deltas have a base.
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_move', ['number', 'number'], [420, 340]));
  await page.waitForTimeout(400);
  const looksBefore = lookLines().length;
  for (let i = 0; i < 50; i++) {
    await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_rel', ['number', 'number'], [6, -3]));
    await page.waitForTimeout(120);
  }
  await page.waitForTimeout(2000);
  const looksAfter = lookLines().length;
  const looks = looksAfter - looksBefore;
  console.log('LOOKS', looks, 'sample:', JSON.stringify(lookLines().slice(-2)));
  // Verify deltas recovered end-to-end (game logs mdx/mdy per look).
  const mdxOk = lookLines().slice(looksBefore).some(l => /mdx=6 mdy=-3/.test(l));

  // --- KEYBOARD: hold W (SDL scancode 26) -> MOVE + KEYDOWN vk=0x57 ---
  const keysBefore = keyLines().length, movesBefore = moveLines().length;
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [26, 1]));
  await page.waitForTimeout(3000);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [26, 0]));
  await page.waitForTimeout(1000);
  const keysAfter = keyLines().length, movesAfter = moveLines().length;
  console.log('KEYS', keysAfter - keysBefore, 'MOVES', movesAfter - movesBefore);

  // --- LEFT CLICK: shoot ---
  const shotsBefore = shotLines().length;
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number', 'number'], [1, 1]));
  await page.waitForTimeout(600);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number', 'number'], [1, 0]));
  await page.waitForTimeout(2000);
  const shotsAfter = shotLines().length;
  console.log('SHOTS', shotsAfter - shotsBefore);

  // Frame-time: rendered-counter deltas over a timed window.
  const ft0 = await getStats(); const t0 = Date.now();
  await page.waitForTimeout(30000);
  const ft1 = await getStats(); const t1 = Date.now();
  const framesAdvanced = ft1.rendered - ft0.rendered;
  const meanFrameMs = framesAdvanced > 0 ? (t1 - t0) / framesAdvanced : -1;
  console.log('FRAMETIME', JSON.stringify({ framesAdvanced, meanFrameMs }));

  // Clean exit via Escape (VK_ESCAPE -> PostQuitMessage).
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [41, 1]));
  await page.waitForTimeout(500);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [41, 0]));
  const quitUntil = Date.now() + 60000;
  while (Date.now() < quitUntil && !logs.some(l => /fpsinput: RESULT [01]/.test(l))) {
    await flush(); await page.waitForTimeout(1000);
  }
  const joined = logs.join('\n');
  const result = /fpsinput: RESULT 0/.test(joined) ? 0 : /fpsinput: RESULT 1/.test(joined) ? 1 : null;
  const frameLogs = logs.filter(l => /fpsinput: frame \d+ /.test(l));
  console.log('RESULT', result, 'frameLogs', frameLogs.length);

  const renderedOk = lit > 100;
  const lookOk = looks >= 5 && mdxOk;
  const keyOk = (keysAfter - keysBefore) >= 1 && (movesAfter - movesBefore) >= 1;
  const shotOk = (shotsAfter - shotsBefore) >= 1;
  summary = {
    gameSuccess: !!(renderedOk && lookOk && keyOk && shotOk),
    rendered: { litPixels: lit, result, frameLogs: frameLogs.length },
    input: { looks, mdxOk, keysLogged: keysAfter - keysBefore, movesLogged: movesAfter - movesBefore, shotsFired: shotsAfter - shotsBefore },
    frameTime: { windowMs: t1 - t0, framesAdvanced, meanFrameMs },
    errors,
  };
} catch (e) {
  summary.failure = String(e.stack || e);
} finally {
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('GAME VERDICT', JSON.stringify(summary.gameSuccess));
  try { await Promise.race([browser.close(), new Promise((_, rej) => setTimeout(() => rej(new Error('close-timeout')), 15000))]); }
  catch (e) { console.log('BROWSER CLOSE ISSUE:', String(e && e.message || e).slice(0, 120)); }
  server.close(); await slot.release();
  if (!summary.gameSuccess) process.exitCode = 1;
}
