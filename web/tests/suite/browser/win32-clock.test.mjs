import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
export default class Win32ClockTest extends ProbeTest {
  constructor() { super({ tag: 'win32-clock', timeoutMs: 600000 }); }
  async run({ page }) {
    for (const arch of ['x86', 'x64']) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); }
      await this.bootGuest(page, { arch, winedbg: '+debugstr' });
      await this.stageFile(page, 'win32-clock.exe', await guestFixture('win32-clock', arch));
      const from = await this.spawnGuest(page, 'win32-clock.exe', { arch, env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /win32-clock: BEGIN/, 60000, from);
      await this.waitForLog(page, /win32-clock: ELAPSED/, 15000, from);
      await this.waitForLog(page, /win32-clock: RESULT 0/, 15000, from);
    }
  }
}
