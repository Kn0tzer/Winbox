import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { checkEqual } from '../lib/assert.mjs';

export default class FullscreenInputTest extends ProbeTest {
  constructor() { super({ tag: 'input-fullscreen', timeoutMs: 600000 }); }
  async run({ page }) {
    // This guest confines the cursor (ClipCursor), which puts the page
    // into relative mouse mode with pointer lock. A real browser grants
    // pointer lock only on a user gesture, so a real user reaches this
    // state by clicking; the headless harness grants it at ClipCursor
    // time, before any click. In that state a synthetic browser click
    // cannot deliver the button press this test performs: Playwright's
    // hit test reports the body intercepting the canvas, dispatched
    // mouse events arrive with coordinates frozen at 0,0, and with the
    // lock stubbed off the relative-mode SDL layer still never turns an
    // absolute browser click into a guest button event (all measured).
    // The confinement under test is emulator-side and independent of
    // the browser lock, so keep the browser unlocked and deliver the
    // press through the same injection channel the rest of this test
    // (and input-mouse-rel) uses, at the same point inside the window.
    const unlockPointer = async p => {
      await p.addInitScript(() => { Element.prototype.requestPointerLock = () => {}; });
    };
    await unlockPointer(page);
    for (const arch of this.guestArchitectures) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); await unlockPointer(page); }
      const exe = await guestFixture('cursor-clip', arch);
      await this.bootGuest(page, { arch, winedbg: '+debugstr,+cursor' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 180000);
      await this.stageFile(page, 'cursor-clip.exe', exe);
      const from = await this.spawnGuest(page, 'cursor-clip.exe', { arch, env: 'WINEDEBUG=+debugstr,+cursor' });
      await this.waitForLog(page, /cursor-clip: MAPPED/, 60000, from);
      await this.waitForLog(page, /cursor-clip: READY/, 15000, from);
      await this.waitForLog(page, /cursor-clip: ALIVE/, 15000, from);
      const clicked = this.consoleLines.length;
      await page.evaluate(() => {
        globalThis.bwRuntime.call('bw64_xwire_mouse_move', ['number', 'number'], [240, 180]);
        globalThis.bwRuntime.call('bw64_xwire_mouse_button', ['number', 'number'], [1, 1]);
        globalThis.bwRuntime.call('bw64_xwire_mouse_button', ['number', 'number'], [1, 0]);
      });
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
