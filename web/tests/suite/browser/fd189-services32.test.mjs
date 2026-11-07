import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-189 (32-bit control): the 32-bit prefix was produced by a real
// wineboot and already carries the PlugPlay, WineBus and WineUsb
// registrations plus the ROOT\WINE bus devices. This leg proves the
// probe itself works -- it passes unchanged on the 32-bit side, so a
// 64-bit failure is the prefix, not the probe.
export default class Fd189Services32Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd189-services32', timeoutMs: 1800000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'fd189-services32.exe', await guestFixture('fd189-services', 'x86'));
    const from = await this.spawnGuest(page, 'fd189-services32.exe', { arch: 'x86' });
    const line = await this.waitForLog(page, /fd189-32: RESULT \d+/, 120000, from);
    if (!/fd189-32: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
