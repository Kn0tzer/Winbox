import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

export default class Cpu32Sse3Test extends ProbeTest {
  constructor() { super({ tag: 'cpu32-sse3', timeoutMs: 300000 }); }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'cpu32-sse3.elf', await guestFixture('cpu32-sse3', 'x86'));
    const from = this.consoleLines.length;
    const pid = await page.evaluate(() => globalThis.bwRuntime.call('bw64_spawn', ['string', 'string'],
      ['/home/username/cpu32-sse3.elf', '']));
    if (pid <= 0) throw Error(`CPU32 SSE3 guest spawn failed: ${pid}`);
    const line = await this.waitForLog(page, /cpu32-sse3:.*RESULT \d+/, 15000, from);
    if (!line.endsWith('MOVDDUP MOVSLDUP MOVSHDUP HADDPD PAGE_GUARD RESULT 0')) throw Error(line);
  }
}
