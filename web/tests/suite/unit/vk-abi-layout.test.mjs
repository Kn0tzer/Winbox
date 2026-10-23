// web/tests/suite/unit/vk-abi-layout.test.mjs — guest <-> host Vulkan ABI parity.
//
// Three silent-corruption classes are pinned here (the Info2L pad bug made vkCmdCopyBufferToImage2
// read regionCount from the wrong offset and the bridge silently dropped every upload):
//   1. VK64_fn_* ids: vk64bridge_abi.h (host, shared with the 32-bit bridge) vs the enum copies in
//      libvk64.c / libvk32.c. Every id both sides name must agree; ids the shim does not name are
//      pinned in an explicit list so a new gap fails loudly.
//   2. Hand-written guest mirror structs (Vk*L in libvk64.c / libvk32.c): every named member sits at
//      the same offset with the same size as in the real vulkan_core.h struct compiled for the guest
//      ABI (x86_64 / i686), and sizeof matches.
//   3. The generated guest-layout headers the host reads guest memory through (vk64_guest.h,
//      vk32_guest.h): every struct/union member offset and size equals the real header's layout.
// Layouts are measured with the real cross compilers (`-S`, the same ones build_vulkan_shims.py uses)
// so no guest code has to run. Without the compilers the layout tests skip with a message.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const read = rel => readFileSync(join(repo, rel), 'utf8');
const stripComments = src => src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

/** Parse the VK64_fn_* enum (explicit `= N` and C auto-increment) out of a header/shim source. */
export function parseFnIds(source) {
  const m = /enum\s*\{([^{}]*VK64_fn__MAX[^{}]*)\}/.exec(stripComments(source));
  assert.ok(m, 'VK64_fn_ enum with VK64_fn__MAX not found');
  const ids = new Map();
  let next = 0;
  for (const part of m[1].split(',')) {
    const text = part.trim();
    if (!text) continue;
    const e = /^(\w+)\s*(?:=\s*(\w+))?$/.exec(text);
    assert.ok(e, `unparseable enum entry: ${text}`);
    if (e[2] !== undefined) next = Number(e[2]);
    ids.set(e[1], next++);
  }
  return ids;
}

const hostIds = parseFnIds(read('source/vulkan/vk64bridge_abi.h'));

// Host ids the guest shim does not name. They are entry points the shim never traps for (the
// loader hands back the benign "unimplemented" stub, which the bridge logs). Pinned so that adding
// an id to the host without teaching the shim (or vice versa) is a deliberate, visible edit.
const GUEST_GAPS = {
  'tools/rootfs64/libvk64/libvk64.c': [
    'VK64_fn_vkGetPhysicalDeviceProperties2KHR',
    'VK64_fn_vkGetPhysicalDeviceFeatures2KHR',
    'VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR',
  ],
};

for (const shim of ['tools/rootfs64/libvk64/libvk64.c', 'tools/rootfs64/libvk32/libvk32.c']) {
  test(`${shim}: VK64_fn ids agree with vk64bridge_abi.h`, () => {
    const guest = parseFnIds(read(shim));
    const mismatched = [];
    const missing = [];
    for (const [name, id] of hostIds) {
      if (!guest.has(name)) missing.push(name);
      else if (guest.get(name) !== id) mismatched.push(`${name}: host ${id} guest ${guest.get(name)}`);
    }
    const unknown = [...guest.keys()].filter(name => !hostIds.has(name));
    assert.deepEqual(mismatched, [], 'ids differ between host and guest: the trap would dispatch the wrong call');
    assert.deepEqual(unknown, [], 'guest names ids the host does not define');
    if (shim.includes('libvk64')) assert.deepEqual(missing, GUEST_GAPS[shim] || [], 'host ids missing from the 64-bit shim');
    else {
      // The 32-bit shim is a deliberate subset; the count may only shrink as calls are ported.
      assert.ok(missing.length <= 40, `32-bit shim lacks ${missing.length} host ids: ${missing.join(', ')}`);
    }
  });
}

function compiler(arch) {
  const cc = `${arch}-linux-gnu-gcc`;
  const probe = spawnSync(cc, ['--version'], { encoding: 'utf8' });
  return probe.status === 0 ? cc : null;
}

/** Split a struct body into [{ name }] members, skipping bitfields and padding. */
function structMembers(body) {
  const members = [];
  for (const raw of stripComments(body).split(';')) {
    const decl = raw.trim();
    if (!decl || decl.includes(':')) continue;
    const fn = /\(\s*\*\s*(\w+)\s*\)/.exec(decl);
    const m = fn || /(\w+)\s*(?:\[[^\]]*\]\s*)*$/.exec(decl);
    if (!m || /^_pad/.test(m[1])) continue;
    members.push(m[1]);
  }
  return members;
}

export function parseStructs(source) {
  const out = new Map();
  const re = /typedef\s+(struct|union)\s+(\w+)\s*\{([\s\S]*?)\n?\}\s*(\w+)\s*;/g;
  let m;
  while ((m = re.exec(source))) if (m[2] === m[4]) out.set(m[2], { kind: m[1], members: structMembers(m[3]) });
  return out;
}

/** Compile `body` with `-S` and return the integers of the `bw_tab` table. */
function measure(cc, dir, tag, includes, body, count) {
  const file = join(dir, `${tag}.c`);
  writeFileSync(file, `${includes}\nconst unsigned int bw_tab[] = {\n${body}\n};\n`);
  const r = spawnSync(cc, ['-std=gnu2x', '-ffreestanding', '-O0', '-S', '-o', '-', `-I${join(repo, 'source/vulkan')}`, file], { encoding: 'utf8', maxBuffer: 64 << 20 });
  assert.equal(r.status, 0, `${tag}: compile failed\n${(r.stderr || '').split('\n').slice(0, 12).join('\n')}`);
  const lines = r.stdout.split('\n');
  const start = lines.findIndex(l => l.trim() === 'bw_tab:');
  assert.ok(start >= 0, `${tag}: bw_tab not in assembly`);
  const values = [];
  for (let i = start + 1; i < lines.length && values.length < count; i++) {
    const v = /^\s*\.(?:long|word)\s+(-?\d+)\s*$/.exec(lines[i]);
    if (v) values.push(Number(v[1]));
    else if (/^\s*\.(?:size|section|text|globl)\b/.test(lines[i])) break;
  }
  assert.equal(values.length, count, `${tag}: expected ${count} table entries, parsed ${values.length}`);
  return values;
}

/** pairs: [{ guestType, realType, members: [[guestMember, realMember]] }]. Compares sizes and offsets. */
function compareLayouts(cc, guestIncludes, realIncludes, pairs, label) {
  const dir = mkdtempSync(join(tmpdir(), 'vk-abi-'));
  try {
    const row = (type, members, pick) => [`sizeof(${type})`].concat(members.flatMap(m => {
      const member = pick(m);
      return [`__builtin_offsetof(${type}, ${member})`, `sizeof((( ${type}*)0)->${member})`];
    }));
    const guestRows = pairs.map(p => row(p.guestType, p.members, m => m[0]));
    const realRows = pairs.map(p => row(p.realType, p.members, m => m[1]));
    const total = guestRows.reduce((n, r) => n + r.length, 0);
    const g = measure(cc, dir, 'guest', guestIncludes, guestRows.map(r => r.join(', ')).join(',\n'), total);
    const r = measure(cc, dir, 'real', realIncludes, realRows.map(x => x.join(', ')).join(',\n'), total);
    const diffs = [];
    let at = 0;
    for (const p of pairs) {
      const sz = [g[at], r[at]];
      if (sz[0] !== sz[1]) diffs.push(`${p.guestType}: sizeof ${sz[0]} != real ${sz[1]}`);
      at++;
      for (const m of p.members) {
        if (g[at] !== r[at]) diffs.push(`${p.guestType}.${m[0]}: offset ${g[at]} != real ${r[at]}`);
        if (g[at + 1] !== r[at + 1]) diffs.push(`${p.guestType}.${m[0]}: size ${g[at + 1]} != real ${r[at + 1]}`);
        at += 2;
      }
    }
    assert.deepEqual(diffs, [], `${label}: guest layout differs from vulkan_core.h`);
    return pairs.length;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

// Guest-layout members the generator flattens out of a nested real member.
const FLATTENED = {
  'VkRenderingInfo.renderAreaOffsetX': 'renderArea.offset.x',
  'VkRenderingInfo.renderAreaOffsetY': 'renderArea.offset.y',
  'VkRenderingInfo.renderAreaWidth': 'renderArea.extent.width',
  'VkRenderingInfo.renderAreaHeight': 'renderArea.extent.height',
};

const realHeader = read('source/vulkan/vk/vulkan_core.h');
const realStructs = parseStructs(realHeader);
const REAL_INCLUDES = '#include "vk/vulkan_core.h"';

for (const [arch, shim, hostHeader] of [
  ['x86_64', 'tools/rootfs64/libvk64/libvk64.c', 'vk64_guest.h'],
  ['i686', 'tools/rootfs64/libvk32/libvk32.c', 'vk32_guest.h'],
]) {
  test(`${shim}: hand-written Vk*L mirror structs match vulkan_core.h (${arch})`, t => {
    const cc = compiler(arch);
    if (!cc) return t.skip(`${arch}-linux-gnu-gcc not installed`);
    const source = read(shim);
    // Non-"pad" members are compared; the structs are re-emitted verbatim after the real header,
    // which supplies the handle typedefs the shim declares for itself.
    const mirrors = [...parseStructs(source)].filter(([name]) => /L$/.test(name));
    assert.ok(mirrors.length > 0, 'no Vk*L mirror structs found');
    const text = mirrors.map(([name]) => new RegExp(`typedef\\s+struct\\s+${name}\\s*\\{[\\s\\S]*?\\}\\s*${name}\\s*;`).exec(source)[0]).join('\n');
    // VkImageResolve2L embeds the whole region; the real struct spells the region's members inline.
    const SPECIAL = { 'VkImageResolve2L.region': 'srcSubresource' };
    const pairs = mirrors.map(([name, info]) => {
      const realName = name === 'VkImageResolveRegionL' ? 'VkImageResolve' : name.replace(/L$/, '');
      assert.ok(realStructs.has(realName), `${name}: no ${realName} in vulkan_core.h`);
      return { guestType: name, realType: realName, members: info.members.map(m => [m, SPECIAL[`${name}.${m}`] || m]) };
    });
    // Both sides measured in one TU each: mirrors + real header (disjoint names).
    const dir = mkdtempSync(join(tmpdir(), 'vk-abi-'));
    try {
      const rows = pairs.map(p => {
        const part = (type, pick) => [`sizeof(${type})`].concat(p.members.flatMap(m => [`__builtin_offsetof(${type}, ${pick(m)})`, `sizeof(((${type}*)0)->${pick(m)})`]));
        return { g: part(p.guestType, m => m[0]), r: part(p.realType, m => m[1]) };
      });
      const total = rows.reduce((n, r) => n + r.g.length, 0);
      const includes = `${REAL_INCLUDES}\n${text}`;
      const tab = measure(cc, dir, 'mirror', includes, rows.flatMap(r => [...r.g, ...r.r]).join(',\n'), total * 2);
      const diffs = [];
      let at = 0;
      for (const [i, p] of pairs.entries()) {
        const n = rows[i].g.length;
        const g = tab.slice(at, at + n), r = tab.slice(at + n, at + 2 * n);
        at += 2 * n;
        if (g[0] !== r[0]) diffs.push(`${p.guestType}: sizeof ${g[0]} != ${p.realType} ${r[0]}`);
        p.members.forEach((m, k) => {
          if (g[1 + 2 * k] !== r[1 + 2 * k]) diffs.push(`${p.guestType}.${m[0]}: offset ${g[1 + 2 * k]} != ${p.realType}.${m[1]} ${r[1 + 2 * k]}`);
          if (g[2 + 2 * k] !== r[2 + 2 * k] && !SPECIAL[`${p.guestType}.${m[0]}`]) diffs.push(`${p.guestType}.${m[0]}: size ${g[2 + 2 * k]} != ${p.realType}.${m[1]} ${r[2 + 2 * k]}`);
        });
      }
      assert.deepEqual(diffs, [], 'guest mirror struct layout differs from vulkan_core.h');
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

  test(`${hostHeader}: every guest-layout struct matches vulkan_core.h (${arch})`, t => {
    const cc = compiler(arch);
    if (!cc) return t.skip(`${arch}-linux-gnu-gcc not installed`);
    const guest = parseStructs(read(`source/vulkan/${hostHeader}`));
    const pairs = [];
    const notInCore = [];
    for (const [name, info] of guest) {
      const real = realStructs.get(name);
      if (!real) { notInCore.push(name); continue; }
      const shared = info.members.filter(m => real.members.includes(m) || FLATTENED[`${name}.${m}`]);
      pairs.push({ guestType: name, realType: name, members: shared.map(m => [m, FLATTENED[`${name}.${m}`] || m]) });
    }
    assert.ok(pairs.length > 100, `only ${pairs.length} structs compared; header parse regressed`);
    // Members the generator drops (and structs it renames) would hide drift; none are tolerated.
    const dropped = [];
    for (const [name, info] of guest) {
      const real = realStructs.get(name);
      if (real) for (const m of info.members) if (!real.members.includes(m) && !FLATTENED[`${name}.${m}`]) dropped.push(`${name}.${m}`);
    }
    assert.deepEqual(dropped, [], 'guest-layout members absent from vulkan_core.h');
    assert.deepEqual(notInCore, [], 'guest-layout structs absent from vulkan_core.h (compare by hand)');
    const count = compareLayouts(cc, `#include "${hostHeader}"`, REAL_INCLUDES, pairs, hostHeader);
    assert.equal(count, pairs.length);
  });
}
