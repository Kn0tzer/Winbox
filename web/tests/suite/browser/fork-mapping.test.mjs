import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

export default class ForkMappingTest extends ProbeTest {
  constructor() { super({ tag: 'fork-mapping', timeoutMs: 300000 }); }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fork-mapping.elf', await guestFixture('fork-mapping', 'x64'));
    const from = this.consoleLines.length;
    const result = await page.evaluate(() => globalThis.bwRuntime.call('bw64_spawn', ['string','string'],
      ['/home/username/fork-mapping.elf', '']));
    if (result <= 0) throw new Error(`Fork mapping guest spawn failed: ${result}`);
    await this.waitForLog(page, /fork-mapping: PRIVATE_RESERVATIONS SHARED_ALIAS REMAP_DETACH/, 15000, from);
    await this.waitForLog(page, /fork-mapping: FINAL_THREAD_EXIT_WAITPID/, 15000, from);
    await this.waitForLog(page, /fork-mapping: EXEC_WITH_LIVE_SIBLING/, 30000, from);
    await this.waitForLog(page, /fork-mapping: RESULT 0/, 15000, from);
  }
}
