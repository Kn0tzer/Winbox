#!/usr/bin/env node
// Build a wasm symbol -> source file map from the emscripten object tree.
//
// Usage: node tools/profile/symbol-map.mjs [buildDir] [outFile]
//   buildDir default: project/emscripten/Build/Wasm64MtJit
//   outFile  default: <profile-outdir>/symbols.json, or stdout
//
// llvm-nm is read from $LLVM_NM, then /opt/emsdk/upstream/bin/llvm-nm, then nm.
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { readdir, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';

const repo = resolve(dirname(new URL(import.meta.url).pathname), '../..');
const buildDir = resolve(process.argv[2] || join(repo, 'project/emscripten/Build/Wasm64MtJit'));
const outFile = process.argv[3] ? resolve(process.argv[3]) : null;
const nm = [process.env.LLVM_NM, '/opt/emsdk/upstream/bin/llvm-nm', '/usr/bin/llvm-nm']
  .filter(Boolean).find(path => existsSync(path)) || 'nm';

async function walk(directory) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) result.push(...await walk(path));
    else if (entry.name.endsWith('.o')) result.push(path);
  }
  return result;
}

const objects = existsSync(buildDir) ? await walk(buildDir) : [];
if (!objects.length) {
  console.error(`symbol-map: no .o files under ${buildDir}`);
  process.exit(1);
}

const symbols = {};
let defined = 0;
for (const object of objects) {
  let output;
  try {
    output = execFileSync(nm, ['-j', '--defined-only', object], {
      encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, stdio: ['ignore', 'pipe', 'ignore'],
    });
  } catch { continue; }
  const match = /(source\/.+?)\.o$/.exec(object);
  const source = match ? match[1] : object.replace(buildDir + '/', '').replace(/\.o$/, '');
  for (const line of output.split('\n')) {
    const name = line.trim();
    if (!name || name.startsWith('.')) continue;
    if (!(name in symbols)) { symbols[name] = source; defined++; }
  }
}

const doc = { generatedAt: new Date().toISOString(), buildDir, objectCount: objects.length, symbolCount: defined, symbols };
const text = JSON.stringify(doc) + '\n';
if (outFile) await writeFile(outFile, text);
else process.stdout.write(text);
console.error(`symbol-map: ${defined} symbols from ${objects.length} objects -> ${outFile || 'stdout'}`);
