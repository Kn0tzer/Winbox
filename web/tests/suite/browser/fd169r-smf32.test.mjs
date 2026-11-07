import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-169 remainder (32-bit): msvcp140_2's C++17 special math exports
// and msvcp140_atomic_wait's execution wait/wake pair, shared-mutex
// pair, CRT allocation pair and tzdb deleters are genuinely
// implemented. On the pre-fix runtime the fixture dies at the first
// stub call (Wine stub exception) and the RESULT line never appears;
// after the fix every group must compute its documented result.
export default class Fd169rSmf32Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd169r-smf32', timeoutMs: 3600000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'fd169r-smf32.exe', await guestFixture('fd169r-smf32', 'x86'));
    const from = await this.spawnGuest(page, 'fd169r-smf32.exe', { arch: 'x86' });
    const line = await this.waitForLog(page, /fd169r-32: RESULT \d+/, 600000, from);
    if (!/fd169r-32: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
