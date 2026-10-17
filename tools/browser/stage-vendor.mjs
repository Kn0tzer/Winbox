import { cp, copyFile, mkdir, rm, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(import.meta.dirname, '../..');

export async function stageVendors(destination = resolve(root, 'web/vendor')) {
  await mkdir(destination, { recursive: true });
  for (const [name, source] of [
    ['zip', 'node_modules/@zip.js/zip.js'],
    ['naga', 'node_modules/naga-wasm/dist'],
    ['slang', 'tools/browser/slang'],
  ]) {
    const target = resolve(destination, name);
    await rm(target, { recursive: true, force: true });
    if (name === 'slang') {
      await mkdir(target, { recursive: true });
      for (const asset of ['slang-wasm.js', 'slang-wasm.wasm', 'interface.d.ts', 'third-party-notices']) {
        await cp(resolve(root, source, asset), resolve(target, asset), { recursive: true });
      }
      // Match the imported JS basename so TypeScript uses the upstream binding
      // declarations rather than inferring the generated Emscripten internals.
      await copyFile(resolve(target, 'interface.d.ts'), resolve(target, 'slang-wasm.d.ts'));
    } else {
      await cp(resolve(root, source), target, { recursive: true, dereference: true });
      if (name === 'zip') {
        // zip.js ships its native entry point without a matching declaration;
        // both entry points expose the package's public API.
        await writeFile(resolve(target, 'index-native.d.ts'), "export * from '@zip.js/zip.js';\n");
      }
    }
  }
  const wisp = resolve(destination, 'wisp');
  await mkdir(wisp, { recursive: true });
  for (const name of ['dist/wisp-client.mjs', 'LICENSE', 'README.md']) {
    await copyFile(resolve(root, 'node_modules/@mercuryworkshop/wisp-js', name), resolve(wisp, name.split('/').at(-1)));
  }
}

if (process.argv[1] && fileURLToPath(import.meta.url) === resolve(process.argv[1])) {
  await stageVendors();
  console.log('Staged web/vendor from locked npm packages and pinned Slang assets.');
}
