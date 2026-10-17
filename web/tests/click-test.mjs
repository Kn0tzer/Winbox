import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
const tag = 'click-test-' + Date.now();
const dir = 'test-results/' + tag;
const { mkdir, writeFile } = await import('node:fs/promises');
await mkdir(dir, { recursive: true });
const server = await serve('dist');
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [];
page.on('console', m => logs.push(m.type() + ': ' + m.text()));
await page.addInitScript(() => {
  window.__bwAudio = { ctx: null, events: [] };
  for (const name of ['AudioContext','webkitAudioContext']) {
    const R = window[name]; if (!R) continue;
    window[name] = function(...a){ const c = new R(...a); window.__bwAudio.ctx = c;
      window.__bwAudio.events.push('created:'+c.state);
      c.addEventListener('statechange',()=>window.__bwAudio.events.push('state:'+c.state));
      return c; };
    window[name].prototype = R.prototype;
  }
  document.addEventListener('mousedown', ()=>window.__bwAudio.events.push('mousedown'), true);
});
await page.goto('http://127.0.0.1:' + server.address().port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&gltrace=0');
await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 600000 });
console.log('READY');
await page.waitForTimeout(45000);
const info = await page.evaluate(() => {
  const c = document.getElementById('canvas') || document.querySelector('canvas');
  if (!c) return { none: true };
  const r = c.getBoundingClientRect();
  return { x: r.x+r.width/2, y: r.y+r.height/2, w: r.width, h: r.height };
});
console.log('CANVAS', JSON.stringify(info));
await page.mouse.click(info.x, info.y);
await page.waitForTimeout(3000);
const st = await page.evaluate(() => ({ ctx: window.__bwAudio?.ctx?.state || 'none', events: window.__bwAudio.events }));
console.log('AFTER-CLICK', JSON.stringify(st));
await writeFile(dir+'/console.log', logs.join('\n'));
await browser.close(); server.close();
console.log('DONE');
