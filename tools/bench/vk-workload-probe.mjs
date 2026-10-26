// Bounded D3D9/Pong frame attribution. Run each repetition SOLO against the
// same build and options. Summary's sampled guest= is an unattributed residual.
// OUT=/tmp/... BW_BENCH_FRAMES=40 BW_BENCH_DETAIL=1 node tools/bench/vk-workload-probe.mjs
import { chromium } from '@playwright/test';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { serve } from '../browser/serve.mjs';
import { acquireProbeSlot } from '../../web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '../../web/tests/build-lock.mjs';
import { writeVkCapture } from './vk-capture-artifact.mjs';
const out = resolve(process.env.OUT || '/tmp/winbox-finish/work-sol-b/pong-profile');
const requestedFrames = Number(process.env.BW_BENCH_FRAMES || 40);
const timeoutMs = Number(process.env.BW_BENCH_TIMEOUT_S || 180) * 1000;
if (!Number.isInteger(requestedFrames) || requestedFrames < 2 || !Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new Error('Invalid frame count or probe deadline');
await mkdir(out, { recursive: true });
await waitForBuildIdle();
const slot = await acquireProbeSlot();
let server, browser, page, flushTimer;
const lines = [], errors = [];
let summary = { pass: false, requestedFrames };
const flush = () => writeFile(join(out, 'console.log'), lines.join('\n') + '\n');
try {
  const build = JSON.parse(await readFile('dist/runtime/BUILD_INFO.json', 'utf8'));
  server = await serve('dist');
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] });
  page = await browser.newPage({ viewport: { width: 1024, height: 768 } });
  page.on('console', m => lines.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => errors.push(String(e)));
  flushTimer = setInterval(() => flush().catch(() => {}), 2000);
  const params = new URLSearchParams({ p: 'cmd /c echo vkboot', session: '1', novideo: '1', persist: '0', gltrace: '0', vkprof: '1', vkprofdetail: process.env.BW_BENCH_DETAIL || '0' });
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?${params}`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: timeoutMs });
  const deadline = Date.now() + timeoutMs;
  while (!lines.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > deadline) throw new Error('bootstrap sentinel not observed');
    await page.waitForTimeout(200);
  }
  for (const [name, path] of [['tri9pong.exe', 'tools/dxvk/tri9/tri9pong.exe'], ['d3d9.dll', 'tools/dxvk/build.w64/src/d3d9/d3d9.dll'], ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1']]) {
    const b64 = (await readFile(path)).toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs(); fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      if (window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]) !== 1) throw new Error('Guest VFS registration failed');
    }, { name, b64 });
  }
  await page.evaluate(async capture => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    installVkPageTier(); if (capture) window.bwVkCapture.arm();
    window.addEventListener('keydown', e => { const key = { ArrowUp: 82, ArrowDown: 81 }[e.code]; if (key) window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [key, 1]); });
    window.addEventListener('keyup', e => { const key = { ArrowUp: 82, ArrowDown: 81 }[e.code]; if (key) window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [key, 0]); });
    window.bwRuntime.call('bw64_spawn', ['string', 'string'], ['/usr/lib/wine/wine64\nZ:\\home\\username\\tri9pong.exe', 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']);
  }, process.env.BW_BENCH_CAPTURE === '1');
  const gameDeadline = Date.now() + timeoutMs;
  let samples = 0;
  while ((samples = lines.filter(l => /vk64: VKPROF frame=/.test(l)).length) < requestedFrames) {
    if (lines.some(l => /DxvkBufferView: Failed|tri9pong: RESULT 1/.test(l))) throw new Error('D3D9 device/workload failure; see console.log');
    if (Date.now() > gameDeadline) throw new Error(`only ${samples} measured frames before probe deadline`);
    await page.waitForTimeout(250);
  }
  await page.screenshot({ path: join(out, 'before-input.png') });
  await page.keyboard.down('ArrowUp'); await page.waitForTimeout(1000); await page.keyboard.up('ArrowUp');
  await page.keyboard.down('ArrowDown'); await page.waitForTimeout(1000); await page.keyboard.up('ArrowDown');
  await page.screenshot({ path: join(out, 'after-input.png') });
  const stats = await page.evaluate(() => window.bwVkStats?.());
  summary = { ...summary, pass: !!stats?.v2?.rendered && errors.length === 0, build, measuredFrames: samples, stats,
    inputLogs: lines.filter(l => /KEYDOWN|paddle/.test(l)), errors };
} catch (error) { summary.error = String(error.stack || error); }
finally {
  clearInterval(flushTimer);
  try {
    if (page) {
      await page.screenshot({ path: join(out, 'final.png') }).catch(() => {});
      if (process.env.BW_BENCH_CAPTURE === '1') {
        const capture = await page.evaluate(() => window.bwVkCapture?.report || { status: 'no-capture' });
        await writeVkCapture(join(out, 'capture'), capture);
      }
    }
    await flush(); await writeFile(join(out, 'summary.json'), JSON.stringify(summary, null, 2));
  } finally {
    await browser?.close();
    if (server) await new Promise(r => server.close(r));
    await slot.release();
  }
}
console.log(JSON.stringify({ pass: summary.pass, measuredFrames: summary.measuredFrames || 0, error: summary.error, out }));
if (!summary.pass) process.exitCode = 1;
