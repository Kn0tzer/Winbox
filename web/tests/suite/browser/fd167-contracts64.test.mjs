import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-167 (64-bit): the msvcp140 throw helpers imported by the Gang
// Beasts Xbox-services thunks DLL must throw real MSVC C++ exceptions
// (not the Wine stub exception), and setupapi's
// SetupDiGetDeviceInterfaceAlias (Mindustry's steam_api64.dll) must
// resolve an alias interface in a device info set.
export default class Fd167Contracts64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd167-contracts64', timeoutMs: 300000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd167-contracts64.exe', await guestFixture('fd167-contracts64', 'x64'));
    const from = await this.spawnGuest(page, 'fd167-contracts64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd167-64: RESULT \d+/, 120000, from);
    if (!/fd167-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
