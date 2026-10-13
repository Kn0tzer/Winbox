import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
export default class SharedMappingTest extends ProbeTest {
  constructor() { super({ tag: 'memory-shared', timeoutMs: 300000 }); }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'shared-mapping.elf', await guestFixture('shared-mapping', 'x64'));
    const from = this.consoleLines.length;
    const result = await page.evaluate(() => globalThis.bwRuntime.call('bw64_spawn', ['string','string'],
      ['/home/username/shared-mapping.elf','']));
    if (result <= 0) throw new Error(`Shared mapping guest spawn failed: ${result}`);
    await this.waitForLog(page, /shared-mapping: WRITER_ALIAS/, 15000, from);
    await this.waitForLog(page, /shared-mapping: FILE_IDENTITY/, 15000, from);
    await this.waitForLog(page, /shared-mapping: RESULT 0/, 15000, from);
  }
}
