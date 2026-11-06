import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-171 (64-bit): 107 long-tail stub exports across the shipped
// system libraries are genuinely implemented (propsys conversions,
// ntdll splay generic tables, the cfgmgr32/setupapi config-manager
// family, pdh formatted counter arrays, combase HSTRING marshaling,
// kernelbase HeapSummary/ReadConsoleInputEx, mfplat helpers, and the
// single-function DLLs). On the pre-fix runtime the fixture dies at
// the first stub call (Wine stub exception) and the RESULT line never
// appears; after the fix every group must compute its documented
// result. RoFailFastWithErrorContext is intentionally not called --
// its contract is process termination.
export default class Fd171Contracts64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd171-contracts64', timeoutMs: 900000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd171-contracts64.exe', await guestFixture('fd171-contracts', 'x64'));
    const from = await this.spawnGuest(page, 'fd171-contracts64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd171-64: RESULT \d+/, 120000, from);
    if (!/fd171-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
