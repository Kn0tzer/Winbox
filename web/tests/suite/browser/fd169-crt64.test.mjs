import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-169 (64-bit): a first tranche of the Microsoft C runtime stub
// family is genuinely implemented. On the pre-fix runtime the fixture
// dies at the first stub call (Wine stub exception) and the RESULT
// line never appears; after the fix every group must compute its
// documented result, and _printf_p must print the positional line
// "hello world" into the guest log.
export default class Fd169Crt64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd169-crt64', timeoutMs: 3600000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd169-crt64.exe', await guestFixture('fd169-crt64', 'x64'));
    const from = await this.spawnGuest(page, 'fd169-crt64.exe', { arch: 'x64' });
    // The fixture prints its groups in order; "hello world" (the
    // positional-printf group) precedes the RESULT line, and on the
    // slower console render path a line can take seconds to appear,
    // so wait for the RESULT line with the generous budget and then
    // verify both lines in the accumulated output.
    const line = await this.waitForLog(page, /fd169-64: RESULT \d+/, 600000, from);
    if (!/fd169-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
    const lines = this.consoleLines.slice(from);
    if (!lines.some(l => l.includes('hello world'))) throw Error('positional printf line missing');
  }
}
