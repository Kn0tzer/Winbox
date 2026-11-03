import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

export default class Win32FontsTest extends ProbeTest {
  constructor() {
    super({ tag: 'win32-fonts', timeoutMs: 600000 });
    this.guestArchitectures = ['x86', 'x64'];
  }
  async run({ page }) {
    for (const arch of this.guestArchitectures) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); }
      await this.bootGuest(page, { arch, winedbg: '+debugstr' });
      await this.stageFile(page, 'win32-fonts.exe', await guestFixture('win32-fonts', arch));
      const from = await this.spawnGuest(page, 'win32-fonts.exe', { arch, env: 'WINEDEBUG=+debugstr' });
      const line = await this.waitForLog(page, /win32-fonts:.*RESULT \d+/, 30000, from);
      if (!/GDI=1 FAMILIES=[1-9]\d* TAHOMA=1 MAPPING=00000000 ARIAL=1 METRICS=1 RESULT 0(?:\D|$)/.test(line)) throw Error(`${arch}: ${line}`);
    }
  }
}
