import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-169 (32-bit): the Wine 10 port of the FD-169 CRT implementations.
// Same contract as fd169-crt64: on the pre-fix runtime the fixture
// dies at the first stub call and the RESULT line never appears;
// after the fix every group computes its documented result and
// _printf_p prints the positional line "hello world".
export default class Fd169Crt32Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd169-crt32', timeoutMs: 3600000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'fd169-crt32.exe', await guestFixture('fd169-crt32', 'x86'));
    const from = await this.spawnGuest(page, 'fd169-crt32.exe', { arch: 'x86' });
    // The fixture prints its groups in order; "hello world" (the
    // positional-printf group) precedes the RESULT line, and on the
    // slower console render path a line can take seconds to appear,
    // so wait for the RESULT line with the generous budget and then
    // verify both lines in the accumulated output.
    const line = await this.waitForLog(page, /fd169-32: RESULT \d+/, 600000, from);
    if (!/fd169-32: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
    const lines = this.consoleLines.slice(from);
    if (!lines.some(l => l.includes('hello world'))) throw Error('positional printf line missing');
  }
}
