import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-168 draw-down 2 proof: five more win32u syscall exports that
// used to be spec stubs now answer with real behaviour (read-only
// queue status that provably does not consume the changed bits,
// top-level window test, transform composition, DC charset query,
// multi-string text output compared pixel-for-pixel with gdi32).
// The fixture fails every check on the previous runtime build,
// where all five exports are still stubs.
export default class Win32uFd168Slice2Test extends ProbeTest {
  constructor() {
    super({
      tag: 'win32u-fd168-slice2',
      timeoutMs: 900000,
    });
    this.guestArchitectures = ['x64'];
  }

  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'win32u-fd168s2.exe', await guestFixture('win32u-fd168s2', 'x64'));
    const from = await this.spawnGuest(page, 'win32u-fd168s2.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /win32u-fd168s2: RESULT \d+/, 120000, from);
    process.stdout.write(`${line}\n`);
    if (!/win32u-fd168s2: RESULT 0(?:\D|$)/.test(line)) {
      throw Error(line);
    }
  }
}
