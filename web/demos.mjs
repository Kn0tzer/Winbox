import { programDefaults, runtimeParameters } from './launch.mjs';
// Owned archives are served by the existing host, independently of Pages exports.
export const archiveBase = 'https://kn0tzer.work.gd/games/';
export const demos = [
  { id: 'notepad', title: 'Notepad', description: 'A familiar place to write', icon: 'notes', builtin: 'notepad.exe', arch: 'x64' },
  { id: 'explorer', title: 'File explorer', description: 'Explore the Windows desktop', icon: 'folders', builtin: 'explorer.exe', arch: 'x64' },
  { id: 'snake', title: 'Snake', description: 'One more apple', icon: 'route', builtin: 'snake.exe', arch: 'x64' },
  { id: 'tetris', title: 'Tetris', description: 'Stack the falling pieces', icon: 'layout-grid', builtin: 'tetris.exe', arch: 'x64' },
  { id: 'doom', title: 'DOOM', description: 'The original shareware episode', icon: 'device-gamepad-2', builtin: 'doom.exe -iwad Z:\\home\\username\\doom1.wad', arch: 'x64' },
  { id: 'celeste', title: 'Celeste', description: 'A mountain to climb', icon: 'mountain', archive: 'celeste-drmfree.zip', size: 2688079907, exe: 'Celeste.exe', arch: 'x86', engine: 'fna' },
  { id: 'chromium', title: 'Chromium', description: 'A browser inside your browser', icon: 'world', archive: 'chromium.zip', size: 468348892, exe: 'chrome.exe', arch: 'x86', engine: 'chromium' },
  { id: 'portal', title: 'Portal', description: 'Think with portals', icon: 'door-enter', archive: 'portal-stored.zip', size: 5186497979, exe: 'hl2.exe', arch: 'x86', engine: 'source' },
  { id: 'gangbeasts', title: 'Gang Beasts', description: 'Physics and friendly chaos', icon: 'users-group', archive: 'gangbeasts.zip', size: 1314310574, exe: 'Gang Beasts.exe', arch: 'x64', engine: 'unity' },
  { id: 'quake3', title: 'Quake III Arena', description: 'The arena awaits', icon: 'crosshair', archive: 'q3.zip', size: 652936549, exe: 'quake3e.x64.exe', arch: 'x64', engine: 'windows', graphics: 'webgpu', args: ['+set', 'r_fullscreen', '0', '+set', 'cl_renderer', 'opengl1', '+set', 'r_vbo', '0', '+map', 'q3dm1'] },
  { id: 'mindustry', title: 'Mindustry', description: 'A tower-defense factory game', icon: 'factory', archive: 'mindustry.zip', size: 128595037, exe: 'Mindustry.exe', arch: 'x64', engine: 'windows', graphics: 'webgpu', args: [] },
  { id: 'winmerge', title: 'WinMerge', description: 'Compare files side by side', icon: 'git-compare', archive: 'winmerge.zip', size: 78100958, exe: 'WinMerge/WinMergeU.exe', arch: 'x86', engine: 'windows', args: ['Z:\\home\\username\\apps\\compare-left', 'Z:\\home\\username\\apps\\compare-right'] },
  { id: 'alkhaser', title: 'Al-Khaser', description: 'Anti-VM environment checker', icon: 'shield-search', archive: 'alkhaser.zip', size: 77842, exe: 'al-khaser.exe', arch: 'x64', engine: 'windows', args: ['--sleep', '5'] },
  { id: 'godot-squash', title: 'Squash the Creeps', description: 'The official Godot 4 3D starter game', icon: 'ghost', archive: 'godot-squash.zip', size: 111031102, exe: 'SquashTheCreeps.exe', arch: 'x64', engine: 'windows', graphics: 'webgpu', args: ['--rendering-driver', 'd3d12', '--resolution', '1024x640'] },
  { id: 'unigine-tropics', title: 'Unigine Tropics', description: 'A tropical-island benchmark on Direct3D 10', icon: 'palmtree', archive: 'unigine-tropics.zip', size: 65843368, exe: 'Tropics/Tropics.exe', arch: 'x86', engine: 'windows', graphics: 'webgpu', args: ['-video_app', 'direct3d10', '-sound_app', 'openal', '-video_fullscreen', '0', '-video_mode', '-1', '-video_width', '1024', '-video_height', '768', '-data_path', './', '-engine_config', 'data/unigine.cfg', '-system_script', 'tropics/unigine.cpp', '-extern_define', 'RELEASE'] },
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
