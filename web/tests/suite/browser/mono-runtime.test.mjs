import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture, monoArchive } from '../lib/guest-fixtures.mjs';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
export default class MonoRuntimeTest extends ProbeTest {
  constructor() { super({ tag: 'mono-runtime', timeoutMs: 600000 }); }
  async run({ page }) {
    const file = resolve(this.dir, 'mono-overlay.zip'); monoArchive(file);
    const exe = await guestFixture('mono-runtime', 'x86');
    execFileSync('python3', ['-c', `import zipfile,sys
with zipfile.ZipFile(sys.argv[1],'a',zipfile.ZIP_STORED) as z:z.write(sys.argv[2],'home/username/apps/mono-runtime.exe')`, file, exe]);
    const overlay = await this.mountArchive(file, 'mono-overlay.zip');
    // Additional archives are mounted through the launcher's gamezip path.
    // Start the managed fixture directly with the same layout as Celeste.
    const from = this.consoleLines.length;
    await this.bootGuest(page, { arch: 'x86', gamezip: overlay.url, gamezipsize: overlay.bytes,
      p: 'mono-runtime.exe', exe: 'mono-runtime.exe', args: '[]', session: '0', winedbg: '+debugstr' });
    await this.waitForLog(page, /mono-runtime: BEGIN/, 60000, from);
    await this.waitForLog(page, /mono-runtime: THREAD_REFLECTION/, 15000, from);
    await this.waitForLog(page, /mono-runtime: FILE_ROUNDTRIP/, 15000, from);
    await this.waitForLog(page, /mono-runtime: RESULT 0/, 15000, from);
  }
}
