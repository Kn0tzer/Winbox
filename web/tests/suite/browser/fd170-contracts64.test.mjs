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
    // 900000 = the ProbeTest default: ProbeTest passes this value as
    // the probe-slot wait budget too, and on the shared box the slot
    // queue alone can exceed 300 s (two FD-170 RED attempts died in
    // the queue at exactly 300 s without ever starting).
    super({ tag: 'fd170-contracts64', timeoutMs: 900000 });
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
