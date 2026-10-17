import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
export default class MonoRuntimeTest extends ProbeTest {
  constructor() { super({ tag: 'mono-runtime', timeoutMs: 600000 }); }
  async run({ page }) {
    const file = resolve(this.dir, 'managed-program.zip');
    const exe = await guestFixture('mono-runtime', 'x86');
    execFileSync('python3', ['-c', `import zipfile,sys
with zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_STORED) as z:z.write(sys.argv[2],'home/username/apps/mono-runtime.exe')`, file, exe]);
    const program = await this.mountArchive(file, 'managed-program.zip');
    // Only the executable is supplied; Mono belongs to the shared runtime.
    const from = this.consoleLines.length;
    await this.bootGuest(page, { arch: 'x86', gamezip: program.url, gamezipsize: program.bytes,
      p: 'mono-runtime.exe', exe: 'mono-runtime.exe', args: '[]', session: '0', winedbg: '+debugstr' });
    await this.waitForLog(page, /mono-runtime: BEGIN/, 60000, from);
    await this.waitForLog(page, /mono-runtime: THREAD_REFLECTION/, 15000, from);
    await this.waitForLog(page, /mono-runtime: FILE_ROUNDTRIP/, 15000, from);
    await this.waitForLog(page, /mono-runtime: RESULT 0/, 15000, from);
  }
}
