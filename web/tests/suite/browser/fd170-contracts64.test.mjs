import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-170 (64-bit): three whole-DLL-absence tables gain real behaviour on
// the x86-64 side -- bluetoothapis (Wine 10.0 radio/SDP code plus the
// FD-167 GATT no-radio contracts), winusb (the FD-167 no-device
// contracts) and netutils (Wine 10.0's NetApiBuffer* heap buffers).
// Against the stub runtime the fixture dies at the first stub call and
// no RESULT line prints.
export default class Fd170Contracts64Test extends ProbeTest {
  constructor() {
    // ProbeTest passes this value as the probe-slot wait budget too.
    // At 300 s two attempts died in the slot queue without starting;
    // at the 900 s ProbeTest default a run acquired a slot but the
    // guest was still in its boot sequence (vkboot stage) when the
    // 960 s hung-probe cap fired, at load 13-19 on the shared 4-core
    // box. The fixture itself needs under a minute once launched
    // (and has its own 120 s result wait below); 1800 s is sized to
    // the measured queue + boot reality, not to the fixture.
    super({ tag: 'fd170-contracts64', timeoutMs: 1800000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x64' });
    await this.stageFile(page, 'fd170-contracts64.exe', await guestFixture('fd170-contracts64', 'x64'));
    const from = await this.spawnGuest(page, 'fd170-contracts64.exe', { arch: 'x64' });
    const line = await this.waitForLog(page, /fd170-64: RESULT \d+/, 120000, from);
    if (!/fd170-64: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
