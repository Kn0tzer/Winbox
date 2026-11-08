import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-169 remainder (64-bit): msvcp140_2's C++17 special math exports
// and msvcp140_atomic_wait's execution wait/wake pair, shared-mutex
// pair, CRT allocation pair and tzdb deleters are genuinely
// implemented. On the pre-fix runtime the fixture dies at the first
// stub call (Wine stub exception) and the RESULT line never appears;
// after the fix every group must compute its documented result.
export default class Fd169rSmf64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd169r-smf64', timeoutMs: 3600000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd169r-smf64.exe', await guestFixture('fd169r-smf64', 'x64'));
    const from = await this.spawnGuest(page, 'fd169r-smf64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd169r-64: RESULT \d+/, 600000, from);
    if (!/fd169r-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
