import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { check } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-90: the ALSA chain's mmap face must stream continuously.
//
// alsatone (tools/guest-fixtures/alsatone.c) is a native ALSA client
// driving the guest's `default` PCM — the exact plug -> oss ->
// /dev/dsp chain winealsa uses — in four modes: MMAP_INTERLEAVED and
// RW_INTERLEAVED writei, each in S16_LE and FLOAT_LE. The mmap modes
// run eight fixed mmap_begin/commit cycles and print avail per
// cycle; the writei modes print avail/delay every second.
//
// On the defective device the playback pointer the chain consumes
// never advances (GETOPTR reported bytes *written*, not bytes
// played), so cycle 0 commits the full 4,096-frame buffer and every
// later cycle sees avail=0 forever, and the writei position readings
// are incoherent (delay -190,464 / avail 194,560 on a 4,096-frame
// buffer). This test fails on exactly those two signatures and
// passes only when the mapped face keeps streaming and every
// reported position stays inside the buffer.
export default class AlsaMmapStreamTest extends ProbeTest {
  constructor() { super({ tag: 'alsa-mmap-stream', timeoutMs: 1200000 }); }

  async run({ page }) {
    const params = new URLSearchParams({
      p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', arch: 'x64', ...extraUrlParams(),
    });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    await this.waitForPageFn(page, () => globalThis.bwRuntime?.call?.('bw64_session_ready', [], []) === 1, 600000);
    await this.waitForLog(page, /session: persistent pins spawned/, 60000);
    // The page-side audio consumer must be running or nothing drains.
    await page.locator('#canvas').click({ position: { x: 30, y: 30 } });
    await page.waitForTimeout(1500);

    const exe = 'alsatone-x64.elf';
    await this.stageFile(page, exe, await guestFixture('alsatone', 'x64'));
    this.testedArchitectures.add('x64');
    const from = this.consoleLines.length;
    const pid = await page.evaluate(exe => globalThis.bwRuntime.call('bw64_spawn', ['string', 'string'],
      [`/home/username/${exe}`, '']), exe);
    if (pid <= 0) throw new Error(`alsatone spawn failed: ${pid}`);
    await this.waitForLog(page, /alsatone: RESULT \d/, 600000, from);

    const lines = this.consoleLines.slice(from);
    const find = re => lines.filter(l => re.test(l));

    // The first mmap commit must actually reach the device's audio input.
    check(find(/S16 input first non-zero peak=12000/).length >= 1,
      'first mmap commit never reached the audio input at full scale');

    for (const mode of ['s16mmap', 'floatmmap']) {
      const cycleRe = new RegExp(`MODE ${mode} CYCLE (\\d+) avail=(-?\\d+) begin offset=\\d+ frames=(\\d+) committed=(-?\\d+)`);
      const cycles = new Map();
      for (const line of lines) {
        const m = line.match(cycleRe);
        if (m) cycles.set(Number(m[1]), { avail: Number(m[2]), committed: Number(m[4]) });
      }
      check(cycles.size === 8, `${mode}: expected 8 mmap cycles, saw ${cycles.size}`);
      const avail = [...cycles.entries()].sort((a, b) => a[0] - b[0]).map(([, c]) => c.avail);
      const laterAvail = avail.slice(1).filter(a => a > 0).length;
      check(laterAvail >= 3,
        `${mode}: avail stayed 0 after the first buffer in ${7 - laterAvail} of 7 later cycles (avail=${avail.join(',')})`);
      const summary = find(new RegExp(`MODE ${mode} MMAP-SUMMARY total_committed=(\\d+)`));
      check(summary.length >= 1, `${mode}: missing MMAP-SUMMARY line`);
      const committed = Number(summary[0].match(/total_committed=(\d+)/)[1]);
      check(committed >= 8192,
        `${mode}: only ${committed} frames committed across 8 cycles — the mapped face stalled after one buffer`);
    }

    for (const mode of ['s16', 'float']) {
      check(find(new RegExp(`MODE ${mode} RESULT 0`)).length >= 1, `${mode}: writei mode did not complete`);
      const progress = find(new RegExp(`MODE ${mode} T\\+\\d+ written=\\d+ avail=(-?\\d+) delay=(-?\\d+)`));
      check(progress.length >= 2, `${mode}: expected per-second progress lines, saw ${progress.length}`);
      for (const line of progress) {
        const avail = Number(line.match(/avail=(-?\d+)/)[1]);
        const delay = Number(line.match(/delay=(-?\d+)/)[1]);
        check(avail >= 0 && avail <= 4096, `${mode}: incoherent avail ${avail} on a 4096-frame buffer`);
        check(delay >= 0 && delay <= 4096, `${mode}: incoherent delay ${delay} on a 4096-frame buffer`);
      }
    }
    check(find(/alsatone: RESULT 0/).length >= 1, 'alsatone reported failures');
  }
}
