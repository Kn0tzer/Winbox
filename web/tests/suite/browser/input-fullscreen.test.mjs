import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { checkEqual } from '../lib/assert.mjs';

export default class FullscreenInputTest extends ProbeTest {
  constructor() { super({ tag: 'input-fullscreen', timeoutMs: 600000 }); }
  async run({ page }) {
    for (const arch of this.guestArchitectures) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); }
      const exe = await guestFixture('cursor-clip', arch);
      await this.bootGuest(page, { arch, winedbg: '+debugstr,+cursor' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 180000);
      await this.stageFile(page, 'cursor-clip.exe', exe);
      const from = await this.spawnGuest(page, 'cursor-clip.exe', { arch, env: 'WINEDEBUG=+debugstr,+cursor' });
      await this.waitForLog(page, /cursor-clip: MAPPED/, 60000, from);
      await this.waitForLog(page, /cursor-clip: READY/, 15000, from);
      await this.waitForLog(page, /cursor-clip: ALIVE/, 15000, from);
      const clicked = this.consoleLines.length;
      await page.locator('#canvas').click({ position: { x: 240, y: 180 } });
      await this.waitForLog(page, /cursor-clip: BUTTON UP/, 15000, clicked);
      const before = this.consoleLines.length;
      await page.evaluate(() => globalThis.bwRuntime.call('bw64_xwire_mouse_rel', ['number','number'], [1500,-1500]));
      await this.waitForLog(page, /cursor-clip: RAW .*dx=1500 dy=-1500/, 15000, before);
      await page.keyboard.press('ArrowRight');
      await this.waitForLog(page, /cursor-clip: KEY UP vk=39/, 15000, before);
      const alive = this.consoleLines.length;
      await this.waitForLog(page, /cursor-clip: ALIVE/, 15000, alive);
      const raw = new Set(this.consoleLines.slice(before)
        .map(l => l.match(/cursor-clip: RAW seq=(\d+) .*dx=1500 dy=-1500/))
        .filter(Boolean).map(m => m[1]));
      checkEqual(raw.size, 1, `${arch}: clipped raw movement is delivered exactly once`);
    }
  }
}
