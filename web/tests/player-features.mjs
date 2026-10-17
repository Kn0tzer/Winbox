import { chromium } from '@playwright/test';
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { guestFixture } from './suite/lib/guest-fixtures.mjs';

const directory = 'test-results/player-features';
await mkdir(directory, { recursive: true });
await waitForBuildIdle();
const slot = await acquireProbeSlot();
let browser, server, page;
const errors = [], checks = [], logs = [];
async function guestReport(path) {
  const frame = await page.locator('#display iframe').elementHandle().then(handle => handle.contentFrame());
  return frame.evaluate(path => { try { return window.bwRuntime.fs().readFile('/root/home/username/' + path, { encoding: 'utf8' }); } catch { return ''; } }, path);
}
async function waitReport(path, pattern, timeout = 180000) {
  const until = Date.now() + timeout;
  while (Date.now() < until) { const value = await guestReport(path); if (pattern.test(value)) return value; await page.waitForTimeout(100); }
  throw Error(`Timed out waiting for ${path}: ${await guestReport(path)}`);
}
try {
  server = await serve('dist');
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-swiftshader'] });
  page = await browser.newPage({ viewport: { width: 1440, height: 1100 } });
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { logs.push(message.text().slice(0, 500)); if (logs.length > 200) logs.shift(); });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => window.wineGames);
  const shaders = await page.evaluate(async () => {
    const { createEnhancer } = await import('./enhancement.mjs');
    const results = [];
    const container = document.createElement('div');
    container.style.cssText = 'position:fixed;top:0;left:0;width:512px;height:384px';
    document.body.append(container);
    const source = document.createElement('canvas'); source.width = 64; source.height = 48;
    const context = source.getContext('2d');
    context.fillStyle = 'red'; context.fillRect(0, 0, 32, 24);
    context.fillStyle = 'green'; context.fillRect(32, 0, 32, 24);
    context.fillStyle = 'blue'; context.fillRect(0, 24, 32, 24);
    context.fillStyle = 'white'; context.fillRect(32, 24, 32, 24);
    for (const mode of ['fsr1', 'sgsr1', 'sharpen']) {
      const failures = [];
      const enhancer = createEnhancer(container, () => source, { mode, onError: error => failures.push(error.message) });
      enhancer.render();
      const canvas = enhancer.canvas, gl = canvas.getContext('webgl2');
      const pixel = (horizontal, vertical) => {
        const bytes = new Uint8Array(4); gl.readPixels(Math.round(canvas.width * horizontal), Math.round(canvas.height * vertical), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, bytes); return Array.from(bytes);
      };
      results.push({ mode, failures, topLeft: pixel(0.25, 0.75), bottomLeft: pixel(0.25, 0.25), frames: canvas.dataset.frames });
      enhancer.destroy();
    }
    container.remove(); return results;
  });
  for (const result of shaders) {
    assert.deepEqual(result.failures, []); assert.ok(Number(result.frames) > 0);
    assert.ok(result.topLeft[0] > 220 && result.topLeft[2] < 25, JSON.stringify(result));
    assert.ok(result.bottomLeft[2] > 220 && result.bottomLeft[0] < 25, JSON.stringify(result));
  }
  checks.push('FSR1, SGSRv1 and sharpening compile, draw actual pixels and preserve image orientation');
  await page.click('#settingsButton'); await page.selectOption('#enhancement', 'sgsr1'); await page.keyboard.press('Escape');
  await page.locator('#zipInput').setInputFiles(await guestFixture('player-pointer', 'x86'));
  await waitReport('pointer-result.txt', /ready/);
  await page.waitForFunction(() => document.getElementById('loading').hidden, null, { timeout: 30000 });
  await page.locator('canvas[data-enhancement]').waitFor({ state: 'visible' });
  for (const width of [1440, 640, 320]) {
    await page.setViewportSize({ width, height: 1100 });
    const frame = await page.locator('#display iframe').elementHandle().then(handle => handle.contentFrame());
    const canvas = frame.locator('#canvas');
    const dimensions = await canvas.evaluate(element => ({ width: element.width, height: element.height, style: element.getBoundingClientRect().toJSON() }));
    const region = await page.locator('#display').boundingBox();
    assert.ok(Math.abs(dimensions.style.height - region.height + 2) < 3, JSON.stringify({ dimensions, region }));
    assert.equal(await frame.locator('#status').count(), 0);
    for (const [horizontal, vertical] of [[0.25, 0.25], [0.7, 0.75]]) {
      const previous = await guestReport('pointer-result.txt');
      await canvas.click({ position: { x: dimensions.style.width * horizontal, y: dimensions.style.height * vertical } });
      const report = await waitReport('pointer-result.txt', { test: value => value.length > previous.length }, 30000);
      const match = [...report.matchAll(/WINBOX_POINTER x=(\d+) y=(\d+)/g)].at(-1);
      assert.ok(Math.abs(Number(match[1]) - dimensions.width * horizontal) <= 2, `${width}px horizontal: ${match[0]}`);
      assert.ok(Math.abs(Number(match[2]) - dimensions.height * vertical) <= 2, `${width}px vertical: ${match[0]}`);
    }
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await page.screenshot({ path: `${directory}/pointer-${width}.png`, fullPage: true });
  }
  checks.push('A real x86 Win32 program receives scaled click coordinates within two guest pixels at 320/640/1440px, with enhancement enabled');
  await page.click('#fullscreen'); await page.waitForFunction(() => Boolean(document.fullscreenElement));
  await page.screenshot({ path: `${directory}/fullscreen.png` });
  await page.evaluate(() => document.exitFullscreen()); await page.click('#stop');
  assert.equal(await page.locator('#notice').isVisible(), false);
  assert.equal(await page.locator('[data-enhancement]').count(), 0);
  checks.push('Fullscreen works; stopping removes the enhancement canvas without a stopped notice');
  await page.setViewportSize({ width: 1440, height: 1100 });
  await page.click('#settingsButton'); await page.selectOption('#internet', 'on'); await page.selectOption('#enhancement', 'off'); await page.keyboard.press('Escape');
  for (const arch of ['x86', 'x64']) {
    await page.locator('#zipInput').setInputFiles(await guestFixture('win32-internet', arch));
    const network = await waitReport('internet-result.txt', /WINBOX_INTERNET http200=|dns=[1-9-]|connect=-1/);
    assert.match(network, /WINBOX_INTERNET dns=0/); assert.match(network, /WINBOX_INTERNET http200=1/);
    checks.push(`An actual ${arch} Windows executable resolves example.com through guest DNS and receives HTTP 200 over the live Wisp relay`);
    await page.click('#stop');
  }
  assert.deepEqual(errors, []);
  await writeFile(`${directory}/summary.json`, JSON.stringify({ pass: true, checks, shaders, errors }, null, 2));
  console.log(JSON.stringify({ pass: true, checks }));
} catch (error) {
  await page?.screenshot({ path: `${directory}/failure.png`, fullPage: true }).catch(() => {});
  await writeFile(`${directory}/summary.json`, JSON.stringify({ pass: false, error: error.stack, checks, errors, logs }, null, 2));
  throw error;
} finally { await browser?.close(); server?.close(); await slot.release(); }
