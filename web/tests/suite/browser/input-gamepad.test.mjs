// Browser gamepad (simulated via a stubbed Gamepad API) -> emulator
//
// The emulator-side chain it exercises — simulated pad -> SDL feed ->
// kernel evdev device -> guest discovery — is in place. The 32-bit arm
// was BLOCKED on FD-188 (the WineBus kernel driver failed to initialise
// in the emulator, like every kernel driver; fixed on lane-fd188 by the
// drivers/ mirror in the wine zips). The 64-bit arm is still BLOCKED on
// FD-189 (the 64-bit prefix has no PlugPlay registration; separate lane,
// not in this tree). This test is the proof vehicle for FD-188's closing
// terms on the 32-bit arm. See docs/reports/fd24-gamepad-20261106.md.
// controller feed -> kernel evdev gamepad -> guest HID -> XInput and
// DirectInput state observed by a purpose-built guest witness program.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// Installs a fake standard-mapping gamepad at index 0 before any page
// script runs. Tests mutate it through page.evaluate on __bwPad.
function installSimulatedGamepad() {
  const buttons = [];
  for (let i = 0; i < 17; i++) buttons.push({ pressed: false, touched: false, value: 0 });
  const pad = {
    id: 'BoxedWine simulated gamepad (STANDARD GAMEPAD Vendor: 045e Product: 028e)',
    index: 0, connected: true, mapping: 'standard', timestamp: 0,
    buttons, axes: [0, 0, 0, 0],
  };
  globalThis.__bwPad = pad;
  navigator.getGamepads = () => [pad, null, null, null];
}

export default class InputGamepadTest extends ProbeTest {
  constructor() { super({ tag: 'input-gamepad', timeoutMs: 1500000 }); }

  async setPad(page, script) {
    await page.evaluate(script);
  }

  async run({ page }) {
    for (const arch of this.guestArchitectures) {
      if (page) { await page.close().catch(() => {}); }
      page = await this.newPage();
      await page.addInitScript(installSimulatedGamepad);
      const exe = await guestFixture('gamepad-witness', arch);
      await this.bootGuest(page, { arch, winedbg: '+debugstr' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
      await this.stageFile(page, 'gamepad-witness.exe', exe);
      const from = await this.spawnGuest(page, 'gamepad-witness.exe', { arch, env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /gamepad-witness: READY/, 180000, from);

      // Detection: slot 0 connects through XInput; no other slot does.
      await this.waitForLog(page, /gamepad-witness: XINPUT slot=0 CONNECTED/, 120000, from);
      const caps = await this.waitForLog(page, /gamepad-witness: XINPUT slot=0 CAPS .*buttons=0x/, 60000, from);
      check(/buttons=0x[0-9a-f]*[1-9a-f]/.test(caps), `${arch}: XInput capabilities must advertise at least one button`);
      check(!this.consoleLines.slice(from).some(l => /XINPUT slot=[1-3] CONNECTED/.test(l)),
        `${arch}: XInput slots 1-3 must stay disconnected`);
      // Resting state arrives with every value at rest.
      await this.waitForLog(page, /XINPUT slot=0 STATE buttons=0x0000 lt=0 rt=0 lx=0 ly=0 rx=0 ry=0/, 120000, from);
      // DirectInput enumerates exactly the one controller.
      await this.waitForLog(page, /gamepad-witness: DINPUT devices=1 /, 60000, from);

      // Button A (Gamepad API button 0) -> XInput GAMEPAD_A (0x1000),
      // and DirectInput button 0.
      let mark = this.consoleLines.length;
      await this.setPad(page, () => { __bwPad.buttons[0].pressed = true; __bwPad.buttons[0].value = 1; __bwPad.timestamp++; });
      await this.waitForLog(page, /XINPUT slot=0 STATE buttons=0x1000 /, 60000, mark);
      await this.waitForLog(page, /DINPUT STATE buttons=0x00000001 /, 60000, mark);
      mark = this.consoleLines.length;
      await this.setPad(page, () => { __bwPad.buttons[0].pressed = false; __bwPad.buttons[0].value = 0; __bwPad.timestamp++; });
      await this.waitForLog(page, /XINPUT slot=0 STATE buttons=0x0000 /, 60000, mark);

      // Left stick: full right on X, then full up vs full down on Y must
      // produce large values of opposite sign (axis direction preserved).
      mark = this.consoleLines.length;
      await this.setPad(page, () => { __bwPad.axes[0] = 1; __bwPad.timestamp++; });
      const lxLine = await this.waitForLog(page, /XINPUT slot=0 STATE .* lx=(\d+) /, 60000, mark);
      check(Number(lxLine.match(/lx=(\d+)/)[1]) > 30000, `${arch}: left stick full right must read near +32767, got: ${lxLine}`);
      mark = this.consoleLines.length;
      await this.setPad(page, () => { __bwPad.axes[1] = -1; __bwPad.timestamp++; });
      const lyUp = await this.waitForLog(page, /XINPUT slot=0 STATE .* ly=(-?\d+) /, 60000, mark);
      const lyUpValue = Number(lyUp.match(/ly=(-?\d+)/)[1]);
      check(Math.abs(lyUpValue) > 30000, `${arch}: left stick full up must read near full scale, got: ${lyUp}`);
      mark = this.consoleLines.length;
      await this.setPad(page, () => { __bwPad.axes[1] = 1; __bwPad.timestamp++; });
      const lyDown = await this.waitForLog(page, /XINPUT slot=0 STATE .* ly=(-?\d+) /, 60000, mark);
      const lyDownValue = Number(lyDown.match(/ly=(-?\d+)/)[1]);
      check(Math.sign(lyDownValue) === -Math.sign(lyUpValue) && Math.abs(lyDownValue) > 30000,
        `${arch}: left stick down must flip the Y sign at full scale, got: ${lyDown}`);
      await this.setPad(page, () => { __bwPad.axes[0] = 0; __bwPad.axes[1] = 0; __bwPad.timestamp++; });

      // Analog triggers: left full (255), right half (~127).
      mark = this.consoleLines.length;
      await this.setPad(page, () => {
        __bwPad.buttons[6].pressed = true; __bwPad.buttons[6].value = 1;
        __bwPad.buttons[7].pressed = true; __bwPad.buttons[7].value = 0.5;
        __bwPad.timestamp++;
      });
      // Wait for the line that evidences the trigger press itself:
      // the stick return just before this phase legitimately prints
      // a resting lt=0 rt=0 line, and console delivery can lag the
      // mark, so a pattern accepting lt=0 can match that in-flight
      // line instead of the trigger state.
      const trig = await this.waitForLog(page, /XINPUT slot=0 STATE .* lt=255 rt=(\d+) /, 60000, mark);
      const rt = Number(trig.match(/rt=(\d+)/)[1]);
      check(rt >= 120 && rt <= 135, `${arch}: right trigger half must read about 127, got: ${trig}`);
      await this.setPad(page, () => {
        __bwPad.buttons[6].pressed = false; __bwPad.buttons[6].value = 0;
        __bwPad.buttons[7].pressed = false; __bwPad.buttons[7].value = 0;
        __bwPad.timestamp++;
      });

      // D-pad: left then up arrive as XInput dpad bits.
      mark = this.consoleLines.length;
      await this.setPad(page, () => { __bwPad.buttons[14].pressed = true; __bwPad.buttons[14].value = 1; __bwPad.timestamp++; });
      await this.waitForLog(page, /XINPUT slot=0 STATE buttons=0x0004 /, 60000, mark);
      mark = this.consoleLines.length;
      await this.setPad(page, () => {
        __bwPad.buttons[14].pressed = false; __bwPad.buttons[14].value = 0;
        __bwPad.buttons[12].pressed = true; __bwPad.buttons[12].value = 1;
        __bwPad.timestamp++;
      });
      await this.waitForLog(page, /XINPUT slot=0 STATE buttons=0x0001 /, 60000, mark);
      await this.setPad(page, () => { __bwPad.buttons[12].pressed = false; __bwPad.buttons[12].value = 0; __bwPad.timestamp++; });
    }
  }
}
