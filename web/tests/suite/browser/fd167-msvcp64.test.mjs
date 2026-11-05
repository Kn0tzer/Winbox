import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-167 diagnostic split (msvcp140 group of fd167-contracts64 only):
// isolates the throw-helper checks from the setupapi checks when the
// combined run cannot finish inside the harness budget on a busy box.
export default class Fd167Msvcp64Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd167-msvcp64', timeoutMs: 300000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd167-msvcp64.exe', await guestFixture('fd167-msvcp64', 'x64'));
    const from = await this.spawnGuest(page, 'fd167-msvcp64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd167-64: RESULT \d+/, 120000, from);
    if (!/fd167-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
