// Browser keyboard -> runtime -> actual guest WM_KEYDOWN/WM_KEYUP.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
export default class InputKeyboardTest extends ProbeTest {
  constructor() { super({ tag: 'input-keyboard', timeoutMs: 900000 }); }
  async run({ page }) {
    for (const arch of this.guestArchitectures) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); }
      const exe = await guestFixture('input-witness', arch);
      await this.bootGuest(page, { arch, winedbg: '+debugstr' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
      await this.stageFile(page, 'input-witness.exe', exe);
      const from = await this.spawnGuest(page, 'input-witness.exe', { arch, env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /input-witness: READY/, 180000, from);
      const clicked = this.consoleLines.length;
      await page.locator('#canvas').click({ position: { x: 240, y: 180 } });
      await this.waitForLog(page, /input-witness: BUTTON UP/, 30000, clicked);
      for (const [key, vk] of [['w',87], ['a',65], ['s',83], ['d',68], ['Space',32], ['ArrowLeft',37]]) {
        const before = this.consoleLines.length;
        await page.keyboard.down(key);
        await this.waitForLog(page, new RegExp(`input-witness: KEY DOWN vk=${vk}\\b`), 30000, before);
        if (key.length === 1 || key === 'Space') {
          const character = key === 'Space' ? 32 : key.charCodeAt(0);
          await this.waitForLog(page, new RegExp(`input-witness: CHAR code=${character}\\b`), 30000, before);
        }
        await page.keyboard.up(key);
        await this.waitForLog(page, new RegExp(`input-witness: KEY UP vk=${vk}\\b`), 30000, before);
      }
      const seen = this.consoleLines.slice(from).filter(l => /input-witness: KEY DOWN/.test(l));
      check(Array.isArray(seen), 'guest observed keys must be an array');
      checkGreaterThan(seen.length, 0, `${arch}: keyboard events must reach guest`);
    }
  }
}
