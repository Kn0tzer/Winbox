import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { ProbeTest, withTimeout } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { attachLoopbackTcpRelay } from '../lib/loopback-tcp-relay.mjs';

export default class WinsockHttpTest extends ProbeTest {
  constructor() { super({ tag: 'winsock-http', timeoutMs: 180000 }); }
  async run() {
    this.guestArchitectures = ['x86'];
    this.relay = attachLoopbackTcpRelay(this._server);
    await writeFile(join(this._root, '__fixtures/winsock.txt'), 'BW_TCP_HTTP_OK');
    const page = await this.newPage();
    await this.bootGuest(page, { arch: 'x86', network: '1' });
    const fixture = await guestFixture('win32-tcp', 'x86');
    await this.stageFile(page, 'win32-tcp.exe', fixture);
    const from = await this.spawnGuest(page, 'win32-tcp.exe', { arch: 'x86' });
    await this.waitForLog(page, /BW_TCP connected_event=1/, 30000, from);
    await this.waitForLog(page, /BW_TCP done=1/, 10000, from);
    if (!this.relay.stats.toTcpBytes || !this.relay.stats.toWebSocketBytes)
      throw Error('HTTP response did not cross the loopback relay');
  }
  async teardown() {
    try {
      if (this.relay) {
        await writeFile(join(this.dir, 'relay.json'), JSON.stringify(this.relay.stats, null, 2));
        await withTimeout(this.relay.close(), 5000, 'relay close');
      }
    } finally { await super.teardown(); }
  }
}
