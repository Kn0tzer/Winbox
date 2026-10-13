// Comprehensive input verification: tri9input3-32.exe (D3D9 game)
// Tests: mouse move, left/right/middle click, wheel, X1/X2, WASD, arrows, space
// Run: TAG=unique TIMEOUT=1800000 node web/tests/input32-probe.mjs
import { chromium } from '@playwright/test';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `input32-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG bad');
const dir = `test-results/input32-${tag}`;
await mkdir(dir, { recursive: true });

// wait for any in-flight WASM rebuild to finish (avoid torn dist/)
await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);

// serve dist
const { serve } = await import('/home/ubuntu/boxedwine64/tools/browser/serve.mjs');
const server = await serve(process.env.WINE_DIST_DIR || '/home/ubuntu/boxedwine64/dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => errors.push(e.message));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = Number(process.env.TIMEOUT || 1800000);
let summary = { inputSuccess: false, details: {} };

try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?arch=x86&session=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const assets = [['tri9input3-32.exe', '/home/ubuntu/boxedwine64/tools/dxvk/tri9/tri9input3-32.exe'],
    ['d3d9.dll', '/home/ubuntu/boxedwine64/tools/dxvk/build.w32/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', '/home/ubuntu/boxedwine64/tools/rootfs64/libvk32/libvulkan.so.1']];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
    }, { name, b64 });
  }
  console.log('ASSETS STAGED');

  // install vk tier + key forwarder
  await page.evaluate(async () => {
    const { installVkPageTier } = await import("../vkwebgpu.mjs");
    window.bwVkTier = installVkPageTier();
  });

  // check mouse fns exist
  const mouseFns = await page.evaluate(() => {
    const fns = ['bw64_xwire_mouse_move', 'bw64_xwire_mouse_rel', 'bw64_xwire_mouse_button', 'bw64_xwire_mouse_wheel', 'bw64_xwire_key'];
    const out = {};
    for (const f of fns) {
      try { window.bwRuntime.call(f, f.startsWith('bw64_xwire_key') ? ['number','number'] : ['number','number'], [0,0]); out[f] = true; }
      catch (e) { out[f] = false; }
    }
    return out;
  });
  console.log('MOUSEFNS', JSON.stringify(mouseFns));
  summary.details.mouseFns = mouseFns;

  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine\nZ:\\\\home\\\\username\\\\tri9input3-32.exe', 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username\nBW64_XWIRE=1']));
  console.log('SPAWN', spawn);

  // wait for game to start (input3: WinMain)
  const startUntil = Date.now() + 180000;
  while (Date.now() < startUntil && !logs.some(l => /WinMain start/.test(l))) {
    await page.waitForTimeout(2000); await flush();
  }
  if (!logs.some(l => /WinMain start/.test(l))) throw new Error('game never started');
  console.log('GAME STARTED');

  // wait for first frame
  const frameUntil = Date.now() + 120000;
  while (Date.now() < frameUntil && !logs.some(l => /frame \d+ mouse=/.test(l))) {
    await page.waitForTimeout(2000); await flush();
  }
  console.log('FRAMES FLOWING');

  const has = (re) => logs.some(l => re.test(l));
  const count = (re) => logs.filter(l => re.test(l)).length;

  // --- INPUT TESTS ---
  console.log('=== MOUSE MOVE ===');
  // Move mouse to several positions via injection, PACED like real hardware.
  // winex11 merges back-to-back MotionNotify duplicates (keeps last), so a
  // synchronous burst collapses to a single WM_MOUSEMOVE; paced events each
  // survive. The game logs MOUSE every 10th move, so inject 12 to cross it.
  const mouseBefore = count(/MOUSE \d/);
  const spots = [[100,100],[300,200],[150,250],[400,100],[120,320],[350,150],[200,200],[100,300],[400,300],[250,120],[180,180],[320,320]];
  for (const [x, y] of spots) {
    await page.evaluate(([x, y]) => window.bwRuntime.call('bw64_xwire_mouse_move', ['number','number'], [x, y]), [x, y]);
    await page.waitForTimeout(400);
  }
  await flush();
  const mouseOk = count(/MOUSE \d/) > mouseBefore;
  console.log('MOUSE_MOVE', mouseOk ? 'PASS' : 'FAIL', 'MOUSE lines ' + mouseBefore + ' -> ' + count(/MOUSE \d/));
  summary.details.mouseMove = mouseOk;

  console.log('=== LEFT CLICK ===');
  const shotsBefore = count(/BTN 1 DOWN/);
  await page.evaluate(() => {
    window.bwRuntime.call('bw64_xwire_mouse_move', ['number','number'], [240, 180]);
    window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [1, 1]);
  });
  await page.waitForTimeout(500);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [1, 0]));
  await page.waitForTimeout(800); await flush();
  const leftOk = count(/BTN 1 DOWN/) > shotsBefore && has(/BTN 1 UP/);
  console.log('LEFT_CLICK', leftOk ? 'PASS' : 'FAIL');
  summary.details.leftClick = leftOk;

  console.log('=== RIGHT CLICK ===');
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [3, 1]));
  await page.waitForTimeout(300);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [3, 0]));
  await page.waitForTimeout(800); await flush();
  const rightOk = has(/(BTN 3 DOWN|zoom=\d)/) && has(/BTN 3 UP/);
  console.log('RIGHT_CLICK', rightOk ? 'PASS' : 'FAIL');
  summary.details.rightClick = rightOk;

  console.log('=== MIDDLE CLICK ===');
  // 600ms separation: the game pumps at ~8fps under SwiftShader and the
  // button DOWN/UP can land in the same wine batch (or a slow frame) and one
  // gets lost wine-side. Longer separation makes the test deterministic.
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [2, 1]));
  await page.waitForTimeout(600);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [2, 0]));
  await page.waitForTimeout(800); await flush();
  const midOk = has(/BTN 2 DOWN/) && has(/BTN 2 UP/);
  console.log('MIDDLE_CLICK', midOk ? 'PASS' : 'FAIL');
  summary.details.middleClick = midOk;

  console.log('=== WHEEL ===');
  const wheelsBefore = count(/WHEEL /);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_wheel', ['number','number'], [0, 1]));
  await page.waitForTimeout(500);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_wheel', ['number','number'], [0, -1]));
  await page.waitForTimeout(800); await flush();
  const wheelOk = count(/WHEEL /) > wheelsBefore;
  console.log('WHEEL', wheelOk ? 'PASS' : 'FAIL', 'count=', count(/WHEEL /));
  summary.details.wheel = wheelOk;

  console.log('=== X1/X2 ===');
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [8, 1]));
  await page.waitForTimeout(300);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [8, 0]));
  await page.waitForTimeout(500);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [9, 1]));
  await page.waitForTimeout(300);
  await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_button', ['number','number'], [9, 0]));
  await page.waitForTimeout(800); await flush();
  const x1Ok = has(/BTN 8 DOWN/) && has(/BTN 8 UP/);
  const x2Ok = has(/BTN 9 DOWN/) && has(/BTN 9 UP/);
  console.log('X1', x1Ok ? 'PASS' : 'FAIL', 'X2', x2Ok ? 'PASS' : 'FAIL');
  summary.details.x1 = x1Ok; summary.details.x2 = x2Ok;

  console.log('=== KEYBOARD (WASD + arrows + space) ===');
  // SDL scancodes: W=26? No - use the probe's mapping. SDL_SCANCODE_W = 26
  // Actually from xwirepresentSDL: W->25, A->38, S->39, D->40 (these are X11 codes)
  // For bw64_xwire_key we pass SDL scancodes. SDL_SCANCODE_W=26, A=4, S=22, D=7, SPACE=44, UP=82, DOWN=81
  const SC = { KeyW: 26, KeyA: 4, KeyS: 22, KeyD: 7, Space: 44, ArrowUp: 82, ArrowDown: 81, ArrowLeft: 80, ArrowRight: 79 };
  const keysBefore = count(/KEYDOWN/);
  for (const code of ['KeyW', 'KeyA', 'KeyS', 'KeyD', 'Space', 'ArrowUp', 'ArrowDown']) {
    await page.evaluate(([sc]) => {
      window.bwRuntime.call('bw64_xwire_key', ['number','number'], [sc, 1]);
    }, [SC[code]]);
    await page.waitForTimeout(400);
    await page.evaluate(([sc]) => {
      window.bwRuntime.call('bw64_xwire_key', ['number','number'], [sc, 0]);
    }, [SC[code]]);
    await page.waitForTimeout(400);
  }
  await flush();
  const keysAfter = count(/KEYDOWN/);
  const keyOk = (keysAfter - keysBefore) >= 5;
  console.log('KEYBOARD', keyOk ? 'PASS' : 'FAIL', `(${keysAfter - keysBefore} KEYDOWNs)`);
  summary.details.keyboard = keyOk;

  console.log('=== RELATIVE MOUSE ===');
  // PACED like real pointerlock hardware (see MOUSE MOVE note): a synchronous
  // burst would be merged by winex11 into one WM_MOUSEMOVE. Paced events yield
  // ~2/3 WM_MOUSEMOVEs (verified by rel-diag probe) but the exact count varies
  // with pump timing; 50 events reliably cross two MOUSE thresholds (game logs
  // every 10th move).
  const relBefore = count(/MOUSE \d/);
  for (let i = 0; i < 50; i++) {
    await page.evaluate(() => window.bwRuntime.call('bw64_xwire_mouse_rel', ['number','number'], [6, -3]));
    await page.waitForTimeout(120);
  }
  await page.waitForTimeout(1500); await flush();
  const relAfter = count(/MOUSE \d/);
  const relOk = relAfter > relBefore;
  console.log('REL_MOUSE', relOk ? 'PASS' : 'FAIL', `MOUSE lines ${relBefore} -> ${relAfter}`);
  summary.details.relMouse = relOk;

  // screenshot showing game state
  await page.screenshot({ path: `${dir}/gameplay.png` });
  console.log('SCREENSHOT saved');

  // final summary from game
  const sumLine = logs.find(l => /SUMMARY/.test(l));
  console.log('GAME_SUMMARY', sumLine || 'none yet');

  const allPass = mouseOk && leftOk && rightOk && midOk && wheelOk && x1Ok && x2Ok && keyOk && relOk;
  summary.inputSuccess = allPass;
  console.log('OVERALL', allPass ? 'ALL PASS' : 'SOME FAILED');

} catch (e) {
  console.log('PROBE_ERROR', e.message);
  summary.error = e.message;
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));
  await browser.close();
  server.close();
  await slot.release();
}
