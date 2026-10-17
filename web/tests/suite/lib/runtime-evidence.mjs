import { readdir, readFile } from 'node:fs/promises';
import { join, relative } from 'node:path';
import { createHash } from 'node:crypto';

// Fingerprint executable browser code, including shader/compiler WASM modules.
// Input archives are pinned separately by tools/browser/bootstrap.py.
export async function runtimeCodeHash(root) {
  const files = [];
  async function visit(dir) {
    for (const entry of await readdir(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) await visit(path);
      else if (/\.(?:js|mjs|wasm|wgsl|spv|html|css)$/.test(entry.name) || ['desktop-pin-provenance.json', 'wine32-font-provenance.json', 'dependencies.json'].includes(entry.name)) files.push(path);
    }
  }
  await visit(root);
  const hash = createHash('sha256');
  for (const file of files.sort()) {
    hash.update(relative(root, file)).update('\0');
    hash.update(await readFile(file)).update('\0');
  }
  return hash.digest('hex');
}
