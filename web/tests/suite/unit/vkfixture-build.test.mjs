// web/tests/suite/unit/vkfixture-build.test.mjs — vkfixture provenance pin.
//
// The binary/source divergence audit verified tools/rootfs64/libvk64/
// vkfixture (the guest Vulkan boundary fixture, built by buildvk.sh from
// vkfixture.c, linked against the host libc) by hand: dynamic-symbol set
// and section sizes matched a rebuild, but the bytes differ (libc-linked)
// and NOTHING pinned either property — buildvk.sh rebuilds the fixture on
// every run and a drifted binary could be committed silently.
//
// Provenance facts that shape this pin (measured, 2026-10-25):
//  * The committed fixture was built by Debian 12.2 gcc (its .comment says
//    so) — the bw64-libgl64-gcc:bookworm docker path of buildvk.sh, or an
//    equivalent Debian toolchain. This box's Ubuntu gcc 13 produces a
//    different .text/.init/.eh_frame (glibc + codegen version), so section
//    SIZES cannot be rebuild-compared on this box; they are pinned below
//    as golden values on the committed binary instead.
//  * The dynamic-symbol SET is toolchain-stable once this distro's default
//    hardening is neutralized: Ubuntu gcc injects __printf_chk /
//    __stack_chk_fail (fortify + stack protector) that the Debian build
//    does not have. The rebuild below therefore runs buildvk.sh unmodified
//    in a scratch tree, letting its own compiler lookup find a PATH wrapper
//    that adds -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0.
//    With that, the rebuilt fixture's dynsym set must equal the pinned set
//    EXACTLY; any real source change (a new libc call, a dropped dlopen)
//    fails loudly.

import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { chmodSync, cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const libvk64 = join(repoRoot, 'tools/rootfs64/libvk64');
const fixturePath = join(libvk64, 'vkfixture');

const TYPE_NAMES = ['NOTYPE', 'OBJECT', 'FUNC', 'SECTION', 'FILE', 'COMMON', 'TLS'];
const BIND_NAMES = ['LOCAL', 'GLOBAL', 'WEAK'];

function cstr(bytes, off) {
  return bytes.toString('utf8', off, bytes.indexOf(0, off));
}

// Parse an ELF64 executable: section sizes, and the dynamic symbol set as
// "TYPE|BIND|UND-or-DEF|name@GLIBC_x.y" tuples (the audit's comparison unit).
function parseElf64(bytes) {
  assert.equal(bytes.toString('latin1', 0, 4), '\x7fELF');
  assert.equal(bytes[4], 2, '64-bit ELF');
  assert.equal(bytes[5], 1, 'little-endian ELF');
  const shoff = Number(bytes.readBigUInt64LE(40));
  const shentsize = bytes.readUInt16LE(58);
  const shnum = bytes.readUInt16LE(60);
  const shstrndx = bytes.readUInt16LE(62);
  const raw = [];
  for (let i = 0; i < shnum; i++) {
    const s = shoff + i * shentsize;
    raw.push({
      nameOff: bytes.readUInt32LE(s), type: bytes.readUInt32LE(s + 4),
      offset: Number(bytes.readBigUInt64LE(s + 24)), size: Number(bytes.readBigUInt64LE(s + 32)),
      link: bytes.readUInt32LE(s + 40), entsize: Number(bytes.readBigUInt64LE(s + 56)),
      headerOffset: s,
    });
  }
  const shstr = raw[shstrndx];
  const sections = new Map();
  for (const s of raw) {
    s.name = s.nameOff === 0 ? '' : cstr(bytes, shstr.offset + s.nameOff);
    if (s.name) sections.set(s.name, s);
  }
  // version index -> version name, from .gnu.version_r (VerNeed/VernAux)
  const verNames = new Map();
  const verneed = sections.get('.gnu.version_r');
  const versym = sections.get('.gnu.version');
  if (verneed && versym) {
    const strOff = raw[verneed.link].offset;
    let vn = verneed.offset;
    for (;;) {
      const cnt = bytes.readUInt16LE(vn + 2);
      let aux = vn + bytes.readUInt32LE(vn + 8);
      for (let i = 0; i < cnt; i++) {
        verNames.set(bytes.readUInt16LE(aux + 6), cstr(bytes, strOff + bytes.readUInt32LE(aux + 8)));
        const next = bytes.readUInt32LE(aux + 12);
        if (!next) break;
        aux += next;
      }
      const next = bytes.readUInt32LE(vn + 12);
      if (!next) break;
      vn += next;
    }
  }
  const dynsym = sections.get('.dynsym');
  const dynstrOff = raw[dynsym.link].offset;
  const count = dynsym.size / dynsym.entsize;
  const tuples = [];
  for (let i = 1; i < count; i++) { // skip the null entry
    const s = dynsym.offset + i * dynsym.entsize;
    const name = cstr(bytes, dynstrOff + bytes.readUInt32LE(s));
    const info = bytes[s + 4];
    const type = TYPE_NAMES[info & 15] ?? `TYPE${info & 15}`;
    const bind = BIND_NAMES[info >> 4] ?? `BIND${info >> 4}`;
    const und = bytes.readUInt16LE(s + 6) === 0 ? 'UND' : 'DEF';
    let label = name;
    if (versym) {
      const ver = bytes.readUInt16LE(versym.offset + i * 2) & 0x7fff;
      if (ver >= 2) label = `${name}@${verNames.get(ver)}`;
    }
    tuples.push(`${type}|${bind}|${und}|${label}`);
  }
  return { sections, dynsyms: tuples.sort(), dynstr: raw[dynsym.link] };
}

// The committed fixture's exact dynamic contract (17 symbols: what it
// imports from libc/libdl plus the stdout copy-relocation), as readelf
// reports it and the audit verified it.
const PINNED_DYNSYMS = [
  'FUNC|GLOBAL|UND|__libc_start_main@GLIBC_2.34',
  'FUNC|GLOBAL|UND|dlclose@GLIBC_2.34',
  'FUNC|GLOBAL|UND|dlerror@GLIBC_2.34',
  'FUNC|GLOBAL|UND|dlopen@GLIBC_2.34',
  'FUNC|GLOBAL|UND|dlsym@GLIBC_2.34',
  'FUNC|GLOBAL|UND|exit@GLIBC_2.2.5',
  'FUNC|GLOBAL|UND|fflush@GLIBC_2.2.5',
  'FUNC|GLOBAL|UND|printf@GLIBC_2.2.5',
  'FUNC|GLOBAL|UND|putchar@GLIBC_2.2.5',
  'FUNC|GLOBAL|UND|puts@GLIBC_2.2.5',
  'FUNC|GLOBAL|UND|strcmp@GLIBC_2.2.5',
  'FUNC|GLOBAL|UND|strtoul@GLIBC_2.2.5',
  'FUNC|WEAK|UND|__cxa_finalize@GLIBC_2.2.5',
  'NOTYPE|WEAK|UND|_ITM_deregisterTMCloneTable',
  'NOTYPE|WEAK|UND|_ITM_registerTMCloneTable',
  'NOTYPE|WEAK|UND|__gmon_start__',
  'OBJECT|GLOBAL|DEF|stdout@GLIBC_2.2.5',
].sort();

// The committed fixture's section sizes, byte counts. Golden-pinned (see
// header: not reproducible with this box's gcc/glibc, stable for the
// Debian-12.2-built artifact). A rebuilt-and-committed fixture from the
// wrong toolchain changes these and fails here until re-pinned on purpose.
const PINNED_SECTION_SIZES = {
  '.interp': 28, '.note.gnu.property': 32, '.note.gnu.build-id': 36,
  '.note.ABI-tag': 32, '.gnu.hash': 40, '.dynsym': 432, '.dynstr': 219,
  '.gnu.version': 36, '.gnu.version_r': 48, '.rela.dyn': 360,
  '.rela.plt': 264, '.init': 23, '.plt': 192, '.plt.got': 8, '.text': 17803,
  '.fini': 9, '.rodata': 12932, '.eh_frame_hdr': 68, '.eh_frame': 384,
  '.init_array': 8, '.fini_array': 8, '.data.rel.ro': 48, '.dynamic': 480,
  '.got': 40, '.got.plt': 112, '.data': 16, '.bss': 200, '.comment': 39,
  '.symtab': 1824, '.strtab': 1242, '.shstrtab': 295,
};

function sectionSizes(parsed) {
  const out = {};
  for (const [name, s] of parsed.sections) out[name] = s.size;
  return out;
}

function scratchBuildvk(sabotageSource) {
  const dir = mkdtempSync(join(tmpdir(), 'vkfixture-build-'));
  const rootfs = join(dir, 'tools/rootfs64');
  cpSync(join(repoRoot, 'tools/rootfs64/buildvk.sh'), join(rootfs, 'buildvk.sh'));
  cpSync(libvk64, join(rootfs, 'libvk64'), { recursive: true });
  cpSync(join(repoRoot, 'source/vulkan/vk'), join(dir, 'source/vulkan/vk'), { recursive: true });
  if (sabotageSource) {
    const src = join(rootfs, 'libvk64/vkfixture.c');
    writeFileSync(src, readFileSync(src, 'utf8') + sabotageSource);
  }
  // Let buildvk.sh's own compiler lookup (command -v x86_64-linux-gnu-gcc)
  // find a wrapper that neutralizes this distro's default hardening, which
  // the Debian-built committed binary does not carry (see header).
  const bin = join(dir, 'bin');
  mkdirSync(bin, { recursive: true });
  const wrapper = join(bin, 'x86_64-linux-gnu-gcc');
  writeFileSync(wrapper,
    '#!/bin/sh\nexec /usr/bin/x86_64-linux-gnu-gcc -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 "$@"\n');
  chmodSync(wrapper, 0o755);
  execFileSync('bash', [join(rootfs, 'buildvk.sh')],
    { stdio: 'pipe', env: { ...process.env, PATH: `${bin}:${process.env.PATH}` } });
  return { dir, built: join(rootfs, 'libvk64/vkfixture') };
}

test('committed vkfixture matches the pinned dynsym set and section sizes', () => {
  const parsed = parseElf64(readFileSync(fixturePath));
  assert.deepEqual(parsed.dynsyms, PINNED_DYNSYMS,
    'vkfixture dynamic-symbol set drifted; rebuild it with the Debian bookworm toolchain (buildvk.sh docker path) or re-pin deliberately');
  assert.deepEqual(sectionSizes(parsed), PINNED_SECTION_SIZES,
    'vkfixture section sizes drifted; the committed binary is not the Debian-12.2 build of vkfixture.c — rebuild via buildvk.sh where its docker fallback is used, or re-pin deliberately');
});

test('a buildvk.sh scratch rebuild reproduces the pinned dynsym set', () => {
  const { dir, built } = scratchBuildvk(null);
  try {
    assert.deepEqual(parseElf64(readFileSync(built)).dynsyms, PINNED_DYNSYMS,
      'a fresh buildvk.sh build of vkfixture.c has a different dynamic-symbol set than the committed fixture; the source and the shipped binary have drifted');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('the dynsym pin detects a flipped symbol-name byte in a copy', () => {
  const bytes = readFileSync(fixturePath);
  const parsed = parseElf64(bytes);
  const start = parsed.dynstr.offset;
  const at = bytes.indexOf('dlopen\0', start);
  assert.ok(at >= start && at < start + parsed.dynstr.size, 'dlopen must live in .dynstr');
  bytes[at] ^= 0x01; // 'dlopen' -> 'elopen' in the copy only
  assert.notDeepEqual(parseElf64(bytes).dynsyms, PINNED_DYNSYMS,
    'the pin must notice a corrupted dynamic symbol name');
});

test('the section-size pin detects a corrupted section header in a copy', () => {
  const bytes = readFileSync(fixturePath);
  const text = parseElf64(bytes).sections.get('.text');
  bytes[text.headerOffset + 32] ^= 0x01; // low byte of sh_size
  assert.notDeepEqual(sectionSizes(parseElf64(bytes)), PINNED_SECTION_SIZES,
    'the pin must notice a changed section size');
});

test('a buildvk.sh rebuild of sabotaged source breaks the dynsym pin', () => {
  // Adds a libc reference (getpid) the committed fixture does not have.
  const { dir, built } = scratchBuildvk(
    '\nextern long getpid(void);\nstatic void *volatile bw64_pin_sabotage = (void *)&getpid;\n');
  try {
    const dynsyms = parseElf64(readFileSync(built)).dynsyms;
    assert.ok(dynsyms.some((t) => t.includes('getpid')), 'sabotage must reach the rebuilt binary');
    assert.notDeepEqual(dynsyms, PINNED_DYNSYMS,
      'a source change that alters what vkfixture imports must fail the pin');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
