import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

export default class Win32FontsTest extends ProbeTest {
  constructor() {
    super({ tag: 'win32-fonts', timeoutMs: 300000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86', winedbg: '+debugstr' });
    await this.stageFile(page, 'win32-fonts.exe', await guestFixture('win32-fonts', 'x86'));
    const from = await this.spawnGuest(page, 'win32-fonts.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
    const line = await this.waitForLog(page, /win32-fonts:.*RESULT \d+/, 30000, from);
    if (!/GDI=1 FAMILIES=[1-9]\d* TAHOMA=1 MAPPING=00000000 RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
