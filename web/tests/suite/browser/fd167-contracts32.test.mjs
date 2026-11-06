import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-167 (32-bit): the demo-reachable Wine exports the DLL export census
// found to be winebuild stubs in the Chromium-facing DLL set must answer
// with their documented contracts instead of killing the caller with the
// stub exception. Fixture covers bluetoothapis GATT, bthprops.cpl,
// winusb, wevtapi EvtRender, mf MFCreateDeviceSource, mfplat
// MFCreateMediaBufferWrapper and propsys VariantCompare.
export default class Fd167Contracts32Test extends ProbeTest {
  constructor() {
    super({ tag: 'fd167-contracts32', timeoutMs: 300000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'fd167-contracts32.exe', await guestFixture('fd167-contracts32', 'x86'));
    const from = await this.spawnGuest(page, 'fd167-contracts32.exe', { arch: 'x86' });
    const line = await this.waitForLog(page, /fd167-32: RESULT \d+/, 120000, from);
    if (!/fd167-32: RESULT 0(?:\D|$)/.test(line)) throw Error(line);
  }
}
