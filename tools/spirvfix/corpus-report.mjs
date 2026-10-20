// Report translation and GPUDevice-free WGSL validation for the checked-in
// SPIR-V corpus. Uses the same shader entry point as replay (toWGSL), then
// Naga's parser and validator instead of a browser GPUDevice.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as naga from '../../web/vendor/naga/node.js';
import { toWGSL } from '../../web/shader.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const corpusDir = path.join(root, 'testdata/spirv/corpus');
const index = JSON.parse(fs.readFileSync(path.join(corpusDir, 'corpus.json'), 'utf8'));
const failures = new Map();
let ok = 0;

for (const item of index.modules) {
  const bytes = new Uint8Array(fs.readFileSync(path.join(corpusDir, item.file)));
  try {
    const stage = item.stages.find(s => ['vertex', 'fragment', 'compute'].includes(s)) || 'vertex';
    const wgsl = await toWGSL('spirv', bytes, stage);
    naga.validate(naga.parseWgsl(wgsl));
    ok++;
  } catch (error) {
    const firstLine = String(error?.message || error).split(/\r?\n/, 1)[0];
    if (!failures.has(firstLine)) failures.set(firstLine, []);
    failures.get(firstLine).push(item.file);
  }
}

const total = index.modules.length;
console.log(`Corpus: total=${total} ok=${ok} failed=${total - ok}`);
if (failures.size) {
  console.log('Failure class (first error line) | count | modules');
  for (const [message, files] of [...failures].sort((a, b) => b[1].length - a[1].length || a[0].localeCompare(b[0])))
    console.log(`${message} | ${files.length} | ${files.join(', ')}`);
}
process.exitCode = failures.size ? 1 : 0;
