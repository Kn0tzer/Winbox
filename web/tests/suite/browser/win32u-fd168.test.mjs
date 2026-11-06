import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-168 proof: the thirteen win32u syscall exports implemented by the
// wine8-win32u-fd168-backports patch answer with real, cross-checked
// behaviour in the guest. On the pre-patch build every direct call hits
// the Wine spec stub, the fixture records each as a failure, and the
// RESULT line is nonzero.
export default class Win32uFd168Test extends ProbeTest {
  constructor() {
    super({ tag: 'win32u-fd168', timeoutMs: 900000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'win32u-fd168.exe', await guestFixture('win32u-fd168', 'x64'));
    const from = await this.spawnGuest(page, 'win32u-fd168.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /win32u-fd168: RESULT \d+/, 120000, from);
    if (!/win32u-fd168: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
