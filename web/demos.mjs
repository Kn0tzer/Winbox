import { programDefaults, runtimeParameters } from './launch.mjs';
// Owned archives are served by the existing host, independently of Pages exports.
export const archiveBase = 'https://kn0tzer.work.gd/games/';
export const demos = [
  { id: 'doom', title: 'DOOM', description: 'The original shareware episode', icon: 'device-gamepad-2', builtin: 'doom.exe -iwad Z:\\home\\username\\doom1.wad', arch: 'x64' },
  { id: 'chromium', title: 'Chromium', description: 'A browser inside your browser', icon: 'world', archive: 'chromium.zip', exe: 'chrome.exe', arch: 'x86', engine: 'chromium' },
  { id: 'portal', title: 'Portal', description: 'Think with portals', icon: 'door-enter', archive: 'portal-stored.zip', exe: 'hl2.exe', arch: 'x86', engine: 'source' },
  { id: 'celeste', title: 'Celeste', description: 'A mountain to climb', icon: 'mountain', archive: 'celeste-drmfree.zip', exe: 'Celeste.exe', arch: 'x86', engine: 'fna' },
  { id: 'gangbeasts', title: 'Gang Beasts', description: 'Physics and friendly chaos', icon: 'users-group', archive: 'gangbeasts.zip', exe: 'Gang Beasts.exe', arch: 'x64', engine: 'unity' },
  { id: 'explorer', title: 'File explorer', description: 'Explore the Windows desktop', icon: 'folders', builtin: 'explorer.exe', arch: 'x64' },
  { id: 'notepad', title: 'Notepad', description: 'A familiar place to write', icon: 'notes', builtin: 'notepad.exe', arch: 'x64' },
  { id: 'cube', title: 'OpenGL cube', description: 'A little graphics test', icon: 'cube', builtin: 'glcube.exe', arch: 'x64' },
  { id: 'snake', title: 'Snake', description: 'One more apple', icon: 'route', builtin: 'snake.exe', arch: 'x64' },
];
export async function demoParameters(demo, { base = archiveBase, fetcher = fetch } = {}) {
  if (!demo) throw Error('Choose a demo from the library.');
  let archive, archiveBytes;
  if (demo.archive) {
    archive = new URL(demo.archive, base).href;
    const response = await fetcher(archive, { method: 'HEAD', signal: AbortSignal.timeout(15000) });
    archiveBytes = Number(response.headers.get('content-length'));
    if (!response.ok || !Number.isSafeInteger(archiveBytes) || archiveBytes < 22) throw Error(`${demo.title} files are unavailable. Try again in a moment.`);
  }
  return runtimeParameters({ ...programDefaults(demo.engine), ...demo }, { installation: demo.builtin ? 'demo' : 'demo-' + demo.id, archive, archiveBytes });
}
