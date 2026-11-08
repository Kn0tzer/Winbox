import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-168 draw-down 3 proof: four more win32u exports that used to be
// spec stubs now answer with real behaviour (caption drawing compared
// pixel-for-pixel with user32 DrawCaption, the no-output escape form
// agreeing with ExtEscape on the display DC, multi-rectangle pattern
// fills compared pixel-for-pixel with manual PatBlt composition, and
// the physical-point window hit test agreeing with WindowFromPoint).
// The fixture fails the new-export checks on the previous runtime
// build, where all four exports are still stubs.
export default class Win32uFd168Slice3Test extends ProbeTest {
  constructor() {
    super({
      tag: 'win32u-fd168-slice3',
      timeoutMs: 900000,
    });
    this.guestArchitectures = ['x64'];
  }

  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'win32u-fd168s3.exe', await guestFixture('win32u-fd168s3', 'x64'));
    const from = await this.spawnGuest(page, 'win32u-fd168s3.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /win32u-fd168s3: RESULT \d+/, 120000, from);
    process.stdout.write(`${line}\n`);
    if (!/win32u-fd168s3: RESULT 0(?:\D|$)/.test(line)) {
      throw Error(line);
    }
  }
}
