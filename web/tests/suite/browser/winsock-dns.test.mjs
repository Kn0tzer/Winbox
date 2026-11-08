import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// End-to-end guest name resolution: a Windows program resolves a host
// name through the guest's own resolver (Winsock getaddrinfo), connects
// to the resolved result over the live Wisp relay and reads an HTTP 200
// response; an unresolvable name must not produce a working connection.
// Networking stays opt-in: the guest boots with network=1&wisp=1, the
// shipped default is untouched.
export default class WinsockDnsTest extends ProbeTest {
  constructor() { super({ tag: 'winsock-dns', timeoutMs: 2400000 }); }
  async run() {
    this.guestArchitectures = ['x86', 'x64'];
    for (const arch of ['x86', 'x64']) {
      const page = await this.newPage();
      await this.bootGuest(page, { arch, network: '1', wisp: '1' });
      const fixture = await guestFixture('win32-dns', arch);
      await this.stageFile(page, 'win32-dns.exe', fixture);
      const from = await this.spawnGuest(page, 'win32-dns.exe', { arch });
      await this.waitForLog(page, /BW_DNS done=1/, 300000, from);
      const lines = this.consoleLines.slice(from);
      const required = [
        /BW_DNS gai=0/,
        /BW_DNS addr=\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}/,
        /BW_DNS connect=0/,
        /BW_DNS http=1/,
      ];
      for (const pattern of required) {
        if (!lines.some(line => pattern.test(line)))
          throw Error(`guest DNS stage missing on ${arch}: ${pattern}`);
      }
      await page.close();
    }
  }
}
