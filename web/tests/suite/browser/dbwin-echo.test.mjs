import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-40 proof B: the guest debug-output (DBWIN) channel still works
// for its real purpose -- when a program in the session is actually
// listening (creates DBWIN_BUFFER + events + mutex), another
// program's OutputDebugStringA output reaches it verbatim.
export default class DbwinEchoTest extends ProbeTest {
  constructor() {
    super({ tag: 'dbwin-echo', timeoutMs: 300000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'dbwin-echo.exe', await guestFixture('dbwin-echo', 'x64'));
    const from = await this.spawnGuest(page, 'dbwin-echo.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /dbwin-echo: RESULT \d+/, 120000, from);
    if (!/dbwin-echo: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
