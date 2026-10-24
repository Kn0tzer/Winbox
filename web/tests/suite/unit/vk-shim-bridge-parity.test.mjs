// Guest shim <-> host bridge parity, per ABI (libvk64 -> vk64Bridge, libvk32 -> vk32Bridge).
//
// The failure this guards: a function the bridge implements is still a TAIL stub in
// one shim, so a guest on that ABI gets a silent VK_ERROR_FEATURE_NOT_PRESENT/benign
// zero (libvk32 shipped 23 of these: vkCmdFillBuffer, every indirect draw, blits,
// copies, clears, vkCmdExecuteCommands, vkCmdDispatch*, vkGetBufferDeviceAddress).
//   1. no TAIL stub names a function that the bridge handles for that ABI;
//   2. every vkGetProcAddr table entry E(name, id) points at an id the bridge handles;
//   3. every id a shim enum declares has the value in vk64bridge_abi.h (append-only ABI);
//   4. every exported vk* wrapper that traps into the bridge is reachable via the table.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const read = (p) => readFileSync(new URL(p, root), 'utf8');
const strip = (s) => s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

function parseEnum(src) {
  const body = strip(src);
  const start = body.indexOf('VK64_fn_');
  const out = new Map();
  let next = 0;
  for (const m of body.slice(body.lastIndexOf('enum', start)).matchAll(/\bVK64_fn_(\w+)\s*(?:=\s*(\d+))?\s*[,}]/g)) {
    if (m[2] !== undefined) next = Number(m[2]);
    out.set(m[1], next++);
    if (m[1] === '_MAX') break;
  }
  return out;
}

const abi = parseEnum(read('source/vulkan/vk64bridge_abi.h'));
const bridge = strip(read('source/vulkan/vk64bridge.cpp'));
const split = bridge.indexOf('U64 vk32Bridge(');
assert.ok(split > 0);
const cases = {
  64: new Set([...bridge.slice(0, split).matchAll(/case VK64_fn_(\w+):/g)].map((m) => m[1])),
  32: new Set([...bridge.slice(split).matchAll(/case VK64_fn_(\w+):/g)].map((m) => m[1])),
};

for (const arch of [64, 32]) {
  const src = read(`tools/rootfs64/libvk${arch}/libvk${arch}.c`);
  const code = strip(src);
  const shimEnum = parseEnum(src);
  const tails = [...code.matchAll(/^TAIL\((\w+)\)/gm)].map((m) => m[1]);
  const table = [...code.matchAll(/^\s*E\((\w+),\s*VK64_fn_(\w+)\)/gm)].map((m) => ({ name: m[1], id: m[2] }));

  test(`libvk${arch}: no TAIL stub names a function the vk${arch}Bridge implements`, () => {
    assert.ok(tails.length > 20, 'TAIL list parsed');
    const bad = tails.filter((n) => cases[arch].has(n));
    assert.deepEqual(bad, [], `bridge handles these but the shim only reports them as unimplemented: ${bad.join(', ')}`);
  });

  test(`libvk${arch}: every proc-table entry traps to an id the bridge handles`, () => {
    assert.ok(table.length > 100, 'table parsed');
    const bad = table.filter((e) => !cases[arch].has(e.id)).map((e) => `${e.name}->${e.id}`);
    assert.deepEqual(bad, [], `advertised via vkGet*ProcAddr but no case in vk${arch}Bridge: ${bad.join(', ')}`);
  });

  test(`libvk${arch}: shim enum ids equal the append-only ABI header`, () => {
    assert.ok(shimEnum.size > 100);
    const bad = [...shimEnum].filter(([n, v]) => n !== '_MAX' && abi.get(n) !== v).map(([n, v]) => `${n}=${v} (abi ${abi.get(n)})`);
    assert.deepEqual(bad, []);
  });

  test(`libvk${arch}: every wrapper that traps a bridge-handled id is in the proc table`, () => {
    const inTable = new Set(table.map((e) => e.name));
    const missing = [];
    for (const m of code.matchAll(/^API [^\n(]*?\b(vk\w+)\(/gm)) {
      const name = m[1];
      if (inTable.has(name) || tails.includes(name)) continue;
      // its own id (or aliased wrapper) must not be bridge-handled, or PFN lookup returns the benign stub
      if (cases[arch].has(name)) missing.push(name);
    }
    assert.deepEqual(missing, [], `wrapper exists and bridge handles it, but vkGet*ProcAddr would return the benign stub: ${missing.join(', ')}`);
  });
}
