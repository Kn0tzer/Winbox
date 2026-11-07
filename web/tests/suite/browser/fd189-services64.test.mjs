import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-189 (64-bit): the shipped 64-bit guest prefix has no PlugPlay or
// bus-driver registration -- OpenService answers error 1060 for
// PlugPlay, WineBus and WineUsb and the root enumeration is empty.
// After the fix the 64-bit prefix registers them the way the 32-bit
// one does, and this probe reports all three services present with
// the ROOT\WINE bus devices enumerating. Presence is the criterion;
// whether a service is running is reported, not required (driver
// start is FD-173's separate question).
export default class Fd189Services64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd189-services64', timeoutMs: 1800000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd189-services64.exe', await guestFixture('fd189-services', 'x64'));
    const from = await this.spawnGuest(page, 'fd189-services64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd189-64: RESULT \d+/, 120000, from);
    if (!/fd189-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
