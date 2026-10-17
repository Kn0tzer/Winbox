// Exercise FNA/SDL2, shader drawing, texture readback and keyboard integration
// without loading the full game's content archive.
import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture, monoArchive } from '../lib/guest-fixtures.mjs';
import { checkGreaterThan } from '../lib/assert.mjs';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { writeFile } from 'node:fs/promises';

async function redBounds(page) {
  return page.evaluate(() => {
    const source = document.querySelector('#canvas');
    const canvas = document.createElement('canvas');
    canvas.width = source.width; canvas.height = source.height;
    const context = canvas.getContext('2d'); context.drawImage(source, 0, 0);
    const bytes = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let count = 0, sumX = 0;
    for (let i = 0; i < bytes.length; i += 4) {
      if (bytes[i] > 180 && bytes[i + 1] < 70 && bytes[i + 2] < 70) {
        count++; sumX += (i / 4) % canvas.width;
      }
    }
    return { count, centerX: count ? sumX / count : null, width: canvas.width, height: canvas.height };
  });
}
export default class FnaRuntimeTest extends ProbeTest {
  constructor() { super({ tag: 'fna-runtime', timeoutMs: 600000 }); }
  async run({ page }) {
    const archive = resolve(this.dir, 'fna-overlay.zip'); monoArchive(archive, { gl32: true });
    const exe = await guestFixture('fna-runtime', 'x86');
    execFileSync('python3', ['-c', `import zipfile,sys
with zipfile.ZipFile(sys.argv[1],'a',zipfile.ZIP_STORED) as z:z.write(sys.argv[2],'home/username/apps/fna-runtime.exe')`, archive, exe]);
    const mounted = await this.mountArchive(archive, 'fna-overlay.zip');
    await this.bootGuest(page, { arch: 'x86', gamezip: mounted.url, gamezipsize: mounted.bytes,
      p: 'fna-runtime.exe', exe: 'fna-runtime.exe', args: '[]', session: '0',
      ldpath: '/home/username/apps', winedbg: '+debugstr' });
    await this.waitForLog(page, /fna-runtime: BEGIN/, 60000);
    await this.waitForLog(page, /fna-runtime: LOADED TEXTURE_ROUNDTRIP/, 60000);
    await this.waitForLog(page, /fna-runtime: READY/, 30000);
    await page.waitForTimeout(1000);
    const before = await redBounds(page);
    await page.locator('#canvas').screenshot({ path: this.dir + '/before.png' });
    checkGreaterThan(before.count, 10000, 'FNA SpriteBatch must present a red rectangle');
    await page.locator('#canvas').click({ position: { x: 320, y: 240 } });
    const from = this.consoleLines.length;
    await page.keyboard.down('ArrowRight');
    try { await this.waitForLog(page, /fna-runtime: RIGHT x=160/, 15000, from); }
    finally { await page.keyboard.up('ArrowRight'); }
    await page.waitForTimeout(1000);
    const after = await redBounds(page);
    await page.locator('#canvas').screenshot({ path: this.dir + '/after.png' });
    await writeFile(this.dir + '/pixels.json', JSON.stringify({ before, after }, null, 2));
    checkGreaterThan(after.count, 10000, 'FNA must continue drawing after keyboard input');
    checkGreaterThan(after.centerX - before.centerX, 100, 'Keyboard input must move the rendered rectangle');
  }
}
