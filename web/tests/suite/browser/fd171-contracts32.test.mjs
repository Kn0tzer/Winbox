import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-171 (32-bit): 96 long-tail stub exports in the WineHQ 10.0 i386
// DLLs are genuinely implemented (the cfgmgr32/setupapi
// config-manager family, ntdll splay generic tables, pdh formatted
// counter arrays, combase HSTRING marshaling, kernelbase
// HeapSummary/ReadConsoleInputEx, propsys PropVariantGetStringElem,
// and the single-function DLLs). On the pre-fix runtime the fixture
// dies at the first stub call (Wine stub exception) and the RESULT
// line never appears; after the fix every group must compute its
// documented result.
export default class Fd171Contracts32Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd171-contracts32', timeoutMs: 900000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'fd171-contracts32.exe', await guestFixture('fd171-contracts', 'x86'));
    const from = await this.spawnGuest(page, 'fd171-contracts32.exe', { arch: 'x86' });
    const line = await this.waitForLog(page, /fd171-32: RESULT \d+/, 120000, from);
    if (!/fd171-32: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
