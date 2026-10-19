// Exercise the actual launcher, without dependency query parameters or helper DLLs.
import { chromium } from '@playwright/test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, writeFile, copyFile, rm, readFile } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { guestFixture, canvasPixels } from './suite/lib/guest-fixtures.mjs';

const scenario = process.env.SCENARIO || 'all';
const includes = name => scenario === 'all' || scenario.split(',').includes(name);
const directory = `test-results/launcher-dependencies-${process.env.TAG || scenario}`;
await mkdir(directory, { recursive: true });
await waitForBuildIdle(); const slot = await acquireProbeSlot();
const temporary = await mkdtemp('/tmp/winbox-dependencies-');
const log = createWriteStream(join(directory, 'console.log'));
const checks = [], errors = [], dependencyReads = [];
let browser, server, page, frame, progressTimer;
async function openLauncher() {
  clearInterval(progressTimer);
  await page?.close();
  page = await browser.newPage();
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => log.write(message.text() + '\n'));
  page.on('response', response => {
    if (response.request().method() !== 'HEAD' && /\/support-[a-f0-9]+\.zip$/.test(response.url())) dependencyReads.push({ status: response.status(), bytes: Number(response.headers()['content-length']), range: response.headers()['content-range'] });
  });
  await page.goto(process.env.BASE_URL || `http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => window.wineGames);
  progressTimer = setInterval(() => page.evaluate(() => ({
    title: document.getElementById('runtimeTitle').textContent,
    loading: !document.getElementById('loading').hidden,
    status: document.getElementById('status').textContent,
    error: document.getElementById('notice').hidden ? '' : document.getElementById('noticeText').textContent,
  })).then(progress => console.log(JSON.stringify({ progress }))).catch(() => {}), 30000);
}
async function runtime() {
  await page.waitForFunction(() => document.querySelector('#display iframe') || document.querySelector('#notice.error:not([hidden])'), null, { timeout: 300000 });
  const element = page.locator('#display iframe');
  if (!await element.count()) throw Error(await page.locator('#noticeText').textContent());
  const query = new URL(await element.getAttribute('src'), page.url()).searchParams;
  assert.equal(query.has('overlay'), false); assert.equal(query.has('ldpath'), false);
  frame = await (await element.elementHandle()).contentFrame();
  await frame.waitForFunction(() => window.bwRuntime?.ready(), null, { timeout: 180000 });
}
async function uploadFolder(path) {
  const choosing = page.waitForEvent('filechooser'); await page.click('#folderButton');
  await (await choosing).setFiles(path); await runtime();
}
async function visible() {
  await page.waitForFunction(() => document.getElementById('loading').hidden, null, { timeout: 180000 });
  const pixels = await canvasPixels(frame);
  assert.ok(pixels.lit > 10000, JSON.stringify(pixels));
  return pixels;
}
async function waitForGame(name) {
  await runtime();
  await page.waitForFunction(() => document.getElementById('log').textContent.includes('GAME DISPLAYED'), null, { timeout: 300000 });
  const pixels = await visible(); assert.ok(pixels.varied > 1000, JSON.stringify(pixels));
  await page.screenshot({ path: join(directory, `${name}.png`), fullPage: true });
  checks.push({ name, pixels }); console.log(JSON.stringify(checks.at(-1)));
}
try {
  if (!process.env.BASE_URL) server = await serve('dist', Number(process.env.PORT || 0));
  // Large real uploads need the storage behavior of a normal browser profile.
  browser = await chromium.launchPersistentContext(process.env.PROFILE || join(temporary, 'profile'), { executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    viewport: { width: 1280, height: 1000 },
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-swiftshader',
      ...(includes('direct3d') ? ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] : [])] });
  if (includes('direct3d')) for (const arch of ['x86', 'x64']) {
    await openLauncher();
    const folder = join(temporary, 'direct3d-' + arch); await mkdir(join(folder, 'portal'), { recursive: true });
    // Only the application is uploaded. The Source profile must select the
    // shared DXVK installation; no overlay, app-local DLL or LD_LIBRARY_PATH.
    await copyFile(await guestFixture('d3d9-clear9', arch), join(folder, 'hl2.exe'));
    await writeFile(join(folder, 'portal/gameinfo.txt'), 'fixture');
    await uploadFolder(folder);
    await page.waitForFunction(() => document.getElementById('log').textContent.includes('clear9: CAPTURE_READY'), null, { timeout: 180000 });
    const pixels = await canvasPixels(page, '#vkCanvas');
    assert.ok(pixels.redDominant > 100000, JSON.stringify(pixels));
    await page.screenshot({ path: join(directory, 'direct3d-' + arch + '.png'), fullPage: true });
    checks.push({ name: `shared Direct3D ${arch} folder upload`, pixels }); console.log(JSON.stringify(checks.at(-1)));
  }
  if (includes('mono')) for (const arch of ['x86', 'x64']) {
    await openLauncher();
    const folder = join(temporary, arch); await mkdir(join(folder, 'nested'), { recursive: true });
    execFileSync('mcs', ['-optimize+', `-platform:${arch}`, '-out:' + join(folder, 'nested/Unrelated.exe'), 'tools/guest-fixtures/automatic-runtime.cs']);
    await writeFile(join(folder, 'README.txt'), 'Test nested executable selection');
    await uploadFolder(folder);
    await frame.waitForFunction(() => {
      try { return window.bwRuntime.fs().readFile('/root/home/username/automatic-runtime.txt', { encoding: 'utf8' }); } catch { return false; }
    }, null, { timeout: Number(process.env.REPORT_TIMEOUT || 60000) });
    const report = await frame.evaluate(() => window.bwRuntime.fs().readFile('/root/home/username/automatic-runtime.txt', { encoding: 'utf8' }));
    assert.equal(report, `bits=${arch === 'x86' ? 32 : 64} result=68`);
    await visible(); await page.screenshot({ path: join(directory, `mono-${arch}.png`), fullPage: true });
    checks.push({ name: `nested managed ${arch} folder upload`, report }); console.log(JSON.stringify(checks.at(-1)));
  }
  if (includes('fna')) {
    await openLauncher();
    const folder = join(temporary, 'fna'); await mkdir(folder);
    await copyFile(await guestFixture('fna-runtime', 'x86'), join(folder, 'Unrelated.exe'));
    await uploadFolder(folder); await visible();
    const redCenter = () => frame.evaluate(() => {
      const source = document.getElementById('canvas'), probe = document.createElement('canvas');
      probe.width = source.width; probe.height = source.height;
      const ctx = probe.getContext('2d'); ctx.drawImage(source, 0, 0);
      const bytes = ctx.getImageData(0, 0, probe.width, probe.height).data;
      let count = 0, sum = 0;
      for (let at = 0; at < bytes.length; at += 4) if (bytes[at] > 180 && bytes[at + 1] < 70 && bytes[at + 2] < 70) { count++; sum += (at / 4) % probe.width; }
      return { count, x: sum / count };
    });
    const before = await redCenter(); assert.ok(before.count > 10000, JSON.stringify(before));
    await frame.locator('#canvas').focus(); await page.keyboard.down('ArrowRight');
    await page.waitForTimeout(1500); await page.keyboard.up('ArrowRight');
    const after = await redCenter(); assert.ok(after.x - before.x > 100, JSON.stringify({ before, after }));
    await page.screenshot({ path: join(directory, 'fna-input.png'), fullPage: true });
    checks.push({ name: 'renamed FNA executable upload renders and accepts input', before, after }); console.log(JSON.stringify(checks.at(-1)));
  }
  if (includes('celeste-demo')) {
    await openLauncher(); await page.locator('[data-game="celeste"]').click(); await waitForGame('celeste-demo');
  }
  if (includes('celeste-upload')) {
    await openLauncher();
    await page.locator('#zipInput').setInputFiles(process.env.CELESTE_ZIP || '/home/ubuntu/kn0tzer/games/celeste-drmfree.zip');
    await waitForGame('celeste-upload');
  }
  if (includes('celeste-saved')) {
    await openLauncher();
    await page.locator('.saved-run').filter({ hasText: 'celeste-drmfree' }).click();
    await waitForGame('celeste-upload-reopened');
  }
  if (includes('native')) {
    const from = dependencyReads.length;
    await openLauncher(); await page.locator('[data-game="notepad"]').click(); await runtime(); await visible();
    const bytes = dependencyReads.slice(from).reduce((sum, read) => sum + read.bytes, 0);
    // Each minizip metadata open may fetch several 512 KiB range blocks.
    assert.ok(bytes < 8 * 1024 ** 2, `Notepad fetched ${bytes} bytes of dependencies`);
    checks.push({ name: 'native Notepad still displays without downloading Mono', dependencyBytes: bytes });
  }
  if (includes('missing')) {
    await openLauncher();
    await page.route(url => url.pathname.endsWith('/runtime/dependencies.json'), route => route.fulfill({ status: 503, body: 'unavailable' }));
    const from = errors.length;
    await page.locator('[data-game="notepad"]').click();
    await page.waitForFunction(() => document.getElementById('loadingTitle').textContent === 'The program could not continue');
    assert.match(await page.locator('#status').textContent(), /dependencies could not be loaded/);
    assert.deepEqual(errors.splice(from), ['Runtime dependencies could not be loaded. Reload the page to try again.']);
    checks.push({ name: 'unavailable dependency assets show a startup error' });
  }
  assert.deepEqual(errors, []);
  if (scenario !== 'missing') {
    assert.ok(dependencyReads.length > 0);
    assert.ok(dependencyReads.every(read => read.status === 206 && read.bytes > 0));
  }
  await writeFile(join(directory, 'summary.json'), JSON.stringify({ pass: true, checks, errors, dependencyReads,
    build: JSON.parse(await readFile('dist/runtime/BUILD_INFO.json', 'utf8')),
    dependencies: JSON.parse(await readFile('dist/runtime/dependencies.json', 'utf8')) }, null, 2));
} catch (error) {
  await page?.screenshot({ path: join(directory, 'failure.png'), fullPage: true }).catch(() => {});
  await writeFile(join(directory, 'summary.json'), JSON.stringify({ pass: false, checks, errors, error: error.stack }, null, 2));
  throw error;
} finally {
  clearInterval(progressTimer);
  await browser?.close(); server?.close(); log.end(); await slot.release(); await rm(temporary, { recursive: true, force: true });
}
