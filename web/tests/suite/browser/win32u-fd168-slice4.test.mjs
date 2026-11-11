import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-168 draw-down 4 proof: three more win32u exports that used to be
// spec stubs now answer with real behaviour (the top-level-window
// query agreeing with GetAncestor(GA_ROOT) on a top-level window, a
// child, a grandchild, a second top-level window, the desktop, and
// NULL; the text-extent query agreeing with GetTextExtentPoint32W
// and GetTextExtentPointI; the internal glyph-index query agreeing
// with GetGlyphIndicesW in both modes and both subset values). The
// fixture fails the new-export checks on the previous runtime build,
// where all three exports are still stubs.
export default class Win32uFd168Slice4Test extends ProbeTest {
  constructor() {
    super({
      tag: 'win32u-fd168-slice4',
      timeoutMs: 900000,
    });
    this.guestArchitectures = ['x64'];
  }

  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'win32u-fd168s4.exe', await guestFixture('win32u-fd168s4', 'x64'));
    const from = await this.spawnGuest(page, 'win32u-fd168s4.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /win32u-fd168s4: RESULT \d+/, 120000, from);
    process.stdout.write(`${line}\n`);
    if (!/win32u-fd168s4: RESULT 0(?:\D|$)/.test(line)) {
      throw Error(line);
    }
  }
}
