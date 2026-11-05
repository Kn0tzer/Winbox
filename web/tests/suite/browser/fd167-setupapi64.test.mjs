import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-167 diagnostic split (setupapi group of fd167-contracts64 only):
// isolates the SetupDiGetDeviceInterfaceAlias checks from the
// msvcp140 checks when the combined run cannot finish inside the
// harness budget on a busy box.
export default class Fd167Setupapi64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd167-setupapi64', timeoutMs: 300000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd167-setupapi64.exe', await guestFixture('fd167-setupapi64', 'x64'));
    const from = await this.spawnGuest(page, 'fd167-setupapi64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd167-64: RESULT \d+/, 120000, from);
    if (!/fd167-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
