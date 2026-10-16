import { cp, mkdir, copyFile, rm, symlink, stat } from 'node:fs/promises';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { stageVendors } from './stage-vendor.mjs';
const root = resolve(import.meta.dirname, '../..');
const out = resolve(root, process.env.WINE_BUILD_OUT || 'dist');
await mkdir(out, { recursive: true });
if (out === root || out === resolve(root, 'web')) throw new Error('Build output must be separate from source files');
for (const name of ['index.html', 'style.css', 'app.mjs', 'files.mjs', 'games.mjs', 'storage.mjs', 'webgpu.mjs', 'shader.mjs', 'naga-rt.mjs', 'vkwebgpu.mjs', 'vk-protocol.mjs', 'vk-log.mjs', 'wgsl-utils.mjs', 'sparse-memfs.js']) await copyFile(resolve(root, 'web', name), resolve(out, name));
// Built-in game archives are staged under /games so the launcher catalog can
// range-fetch them. WINE_GAMES_DIR selects the source; WINE_GAMES_COPY=1 copies
// the archives instead of linking them (link is the default: they are 100+ MB).
const gamesOut = resolve(out, 'games');
async function findGames() {
  for (const dir of [process.env.WINE_GAMES_DIR, resolve(root, 'web/games')].filter(Boolean)) {
    try { await stat(resolve(dir, 'baldi.zip')); return dir; } catch { /* try the next source */ }
  }
  return null;
}
const gamesSrc = await findGames();
// Always drop the previous staging first, including the no-source branch: a
// stale dist/games left behind by an earlier build would still serve an old
// (or truncated) archive while this build reports the catalog as unavailable.
await rm(gamesOut, { recursive: true, force: true });
if (gamesSrc) {
  if (process.env.WINE_GAMES_COPY === '1') await cp(gamesSrc, gamesOut, { recursive: true });
  else await symlink(gamesSrc, gamesOut, 'dir');
  console.log(`Built-in games: ${gamesOut} -> ${gamesSrc} (${process.env.WINE_GAMES_COPY === '1' ? 'copied' : 'linked'})`);
} else {
  console.warn('Built-in games: no archive source found; the launcher catalog will report its card as unavailable.');
}

await stageVendors();
await stageVendors(resolve(out, 'vendor'));
await mkdir(resolve(out, 'runtime'), { recursive: true });
await copyFile(resolve(root, 'tools/spirvfix/spirvfix.mjs'), resolve(out, 'spirvfix.mjs'));
for (const name of ['boxedwine64.js', 'boxedwine64.wasm', 'BUILD_INFO.json']) await copyFile(resolve(root, 'project/emscripten/Build/Wasm64Mt', name), resolve(out, 'runtime', name));
await copyFile(resolve(root, 'project/emscripten/wine64-launcher.js'), resolve(out, 'runtime/wine64-launcher.js'));
for (const name of ['glibc-rootfs64.zip', 'wine64.zip', 'prefix64.zip', 'glibc-rootfs32.zip', 'wine32.zip', 'prefix32.zip']) await copyFile(resolve(root, 'web/runtime', name), resolve(out, 'runtime', name));
// Keep the pinned base archives intact; replace only our desktop helper in
// the exported prefixes so readiness matches the helper shipped with this tree.
execFileSync('python3', [resolve(root, 'tools/browser/stage-desktop-pin.py'), resolve(out, 'runtime')]);
// DirectWrite needs the actual Tahoma family; a GDI substitute alone is insufficient.
execFileSync('python3', [resolve(root, 'tools/browser/stage-wine32-fonts.py'), resolve(out, 'runtime')]);
await copyFile(resolve(root, 'web/runtime.html'), resolve(out, 'runtime/index.html'));
await copyFile(resolve(root, 'license.txt'), resolve(out, 'LICENSE.txt'));
await copyFile(resolve(root, 'project/emscripten/coi-serviceworker.js'), resolve(out, 'coi-serviceworker.js'));
// Pages cannot set isolation headers. Register the same-origin worker at the
// export root, so both the app and its runtime iframe are controlled.
const { readFile, writeFile } = await import('node:fs/promises');
const index = resolve(out, 'index.html');
await writeFile(index, (await readFile(index, 'utf8')).replace('</head>', '<script src="coi-serviceworker.js"></script></head>'));
console.log(`Browser export: ${out}`);
