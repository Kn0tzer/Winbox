// tools/spirvfix/gate.mjs — corpus conversion gate (node, no browser).
// Usage: node tools/spirvfix/gate.mjs
// Exit 0 when every testdata/spirv/*.spv and testdata/spirv/dxvk-live/*.spv
// converts through naga after the
// pre-pass; prints per-module applied rewrites + WGSL length.
import { applySpirvFix } from './spirvfix.mjs';
import * as naga from '../../web/vendor/naga/node.js';
import fs from 'node:fs';
import path from 'node:path';

const dirs = [new URL("../../testdata/spirv/", import.meta.url).pathname,
              new URL("../../testdata/spirv/dxvk-live/", import.meta.url).pathname];
let ok = 0, total = 0;
for (const dir of dirs) for (const f of fs.readdirSync(dir).filter(f => f.endsWith(".spv")).sort()) {
  total++;
  const b = new Uint8Array(fs.readFileSync(path.join(dir, f)));
  const r = applySpirvFix(b);
  try {
    const out = String(naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes }));
    ok++;
    console.log('OK  ', f, JSON.stringify(r.applied), out.length);
  } catch (e) {
    console.log('FAIL', f, JSON.stringify(r.applied), '-', e.message);
    for (const l of r.log) console.log('      |', l);
  }
}
console.log(`${ok}/${total} modules convert`);
if (ok !== total) process.exit(1);
