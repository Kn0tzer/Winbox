import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';

const root = resolve(import.meta.dirname, '../../../..');
const fixtures = {
  'launcher-ready': ['tools/guest-fixtures/launcher-ready.c', '-luser32'],
  'input-witness': ['tools/guest-fixtures/input-witness.c', '-luser32'],
  'window-state': ['tools/guest-fixtures/window-state.c', '-luser32'],
  'cursor-clip': ['tools/guest-fixtures/cursor-clip.c', '-luser32'],
  'win32-tcp': ['tools/guest-fixtures/win32-tcp.c', '-lws2_32'],
  'win32-internet': ['tools/guest-fixtures/win32-internet.c', '-lws2_32'],
  'player-pointer': ['tools/guest-fixtures/player-pointer.c', '-luser32', '-lgdi32'],
  'win32-clock': ['tools/guest-fixtures/win32-clock.c'],
  // Deterministic 64-bit integer/branch/memory workload with a checksum. The
  // PE64 build (mingw, CRT printf) and the ELF64 build (freestanding, direct
  // syscalls) share the same source and print the same checksum for the same
  // pass count, so a CPU64 interpreter/JIT difference shows up as a differing
  // checksum in either guest format.
  'jitbench64-pe': ['tools/guest-fixtures/jitbench64.c', '-fno-tree-vectorize'],
  'jitbench32-pe': ['tools/guest-fixtures/jitbench32.c', '-fno-tree-vectorize'],
  'jitbenchmix-pe': ['tools/guest-fixtures/jitbenchmix.c', '-fno-tree-vectorize'],
  'mshtml-document': ['tools/guest-fixtures/mshtml-document.c', '-lole32', '-loleaut32', '-luuid'],
  'openal-tone': ['tools/guest-fixtures/openal-tone.c', '-lm'],
  'win32-fonts': ['tools/guest-fixtures/win32-fonts.cpp', '-ldwrite', '-lgdi32', '-luser32'],
  dsptone: ['tools/guest-fixtures/dsptone.c', '-lm'],
  trigl: ['tools/openglTest/trigl.c', '-lopengl32', '-lgdi32', '-luser32'],
  'gl-framebuffer': ['tools/guest-fixtures/gl-framebuffer.c', '-lopengl32', '-lgdi32', '-luser32'],
};
const elfFixtures = { 'shared-mapping': ['tools/guest-fixtures/shared-mapping.c', '-static'] };
elfFixtures['dsptone-stereo'] = ['tools/guest-fixtures/dsptone.c', '-nostdlib', '-fno-builtin', '-fno-stack-protector',
  '-fno-pie', '-no-pie', '-Wl,-e,_start', '-DBW64_DSPS_TONE_ELF'];
elfFixtures['fork-mapping'] = ['tools/guest-fixtures/fork-mapping.c', '-static', '-pthread'];
elfFixtures['cpu32-sse3'] = ['tools/guest-fixtures/cpu32-sse3.c', '-static', '-msse3', '-nostdlib', '-fno-builtin', '-fno-stack-protector', '-fno-pie', '-no-pie'];
elfFixtures['cpu32-jit-growth'] = ['tools/guest-fixtures/cpu32-jit-growth.c', '-static', '-msse3', '-nostdlib', '-fno-builtin', '-fno-stack-protector', '-fno-pie', '-no-pie'];
elfFixtures['cpu32-branches'] = ['tools/guest-fixtures/cpu32-branches.c', '-static', '-nostdlib', '-fno-builtin', '-fno-stack-protector', '-fno-pie', '-no-pie'];
elfFixtures['jitbench64'] = ['tools/guest-fixtures/jitbench64.c', '-static', '-nostdlib', '-fno-builtin',
  '-fno-stack-protector', '-fno-pie', '-no-pie', '-fno-tree-vectorize', '-DJITBENCH_FREESTANDING'];
const managedFixtures = { 'mono-runtime': ['tools/guest-fixtures/mono-runtime.cs'] };
const architectureOptions = { 'jitbenchmix-pe': { x86: ['-mfpmath=387'], x64: [] } };
managedFixtures['fna-runtime'] = ['tools/guest-fixtures/fna-runtime.cs',
  '-r:tools/rootfs64/mono32/mono/mono-2.0/lib/mono/gac/FNA/22.9.1.0__0738eb9f132ed756/FNA.dll'];
for (const name of ['clear9', 'texquad9', 'vshade9', 'scene9'])
  fixtures[`d3d9-${name}`] = [`tools/d3dtests/${name}.cpp`, '-ld3d9', '-lgdi32', '-luser32'];
fixtures['d3d9-clear9'].push('-DBW64_PROBE_HOLD_MS=10000');

export async function guestFixture(name, arch = 'x86') {
  const linux = Boolean(elfFixtures[name]);
  const managed = Boolean(managedFixtures[name]);
  const definition = managed ? managedFixtures[name] : linux ? elfFixtures[name] : fixtures[name];
  if (!definition || !['x86', 'x64'].includes(arch) || (managed && arch !== 'x86')) throw new Error('Invalid guest fixture');
  const [source, ...libraryNames] = definition;
  const libraries = libraryNames.map(option => option.startsWith('-r:') ? '-r:' + join(root, option.slice(3)) : option);
  const driver = source.endsWith('.cpp') ? 'g++' : 'gcc';
  const compiler = managed ? 'mcs' : linux ? (arch === 'x86' ? 'i686-linux-gnu-gcc' : 'x86_64-linux-gnu-gcc')
    : `${arch === 'x86' ? 'i686' : 'x86_64'}-w64-mingw32-${driver}`;
  const version = execFileSync(compiler, ['--version'], { encoding: 'utf8' }).split('\n')[0];
  const linkOptions = driver === 'g++' ? ['-static-libgcc', '-static-libstdc++'] : [];
  const options = managed ? ['-optimize+', '-platform:x86', '-sdk:4.5', ...libraries]
    : ['-O2', ...(architectureOptions[name]?.[arch] || []), ...linkOptions, ...libraries];
  const digest = createHash('sha256').update(await readFile(join(root, source))).update(version)
    .update(JSON.stringify({ options, name, arch }));
  for (const option of libraries) if (option.startsWith('-r:')) digest.update(await readFile(option.slice(3)));
  const hash = digest.digest('hex');
  const dir = join(root, 'tools/guest-fixtures/build', hash);
  const output = join(dir, `${name}-${arch}.${linux ? 'elf' : 'exe'}`);
  const compilerArgs = managed ? [...options, '-out:' + output, join(root, source)]
    : ['-O2', ...(architectureOptions[name]?.[arch] || []), ...linkOptions,
      '-o', output, join(root, source), ...libraries];
  try {
    await stat(output);
    if (!architectureOptions[name]?.[arch]?.length) return output;
    const provenance = JSON.parse(await readFile(join(dir, 'provenance.json'), 'utf8'));
    if (JSON.stringify(provenance.compilerArgs) === JSON.stringify(compilerArgs)) return output;
  } catch {}
  await mkdir(dir, { recursive: true });
  execFileSync(compiler, compilerArgs, { stdio: 'pipe' });
  await writeFile(join(dir, 'provenance.json'), JSON.stringify({ source, compiler: version, options, compilerArgs, hash, arch }, null, 2));
  return output;
}

export async function canvasPixels(page, selector = '#canvas') {
  return page.evaluate(selector => {
    const canvas = document.querySelector(selector);
    if (!canvas?.width || !canvas?.height) return { lit: 0, varied: 0 };
    const copy = document.createElement('canvas'); copy.width = canvas.width; copy.height = canvas.height;
    const context = copy.getContext('2d'); context.drawImage(canvas, 0, 0);
    const bytes = context.getImageData(0, 0, copy.width, copy.height).data;
    let lit = 0, varied = 0, redDominant = 0;
    for (let i = 0; i < bytes.length; i += 4) {
      if (bytes[i] + bytes[i+1] + bytes[i+2] > 30) lit++;
      if (Math.max(bytes[i], bytes[i+1], bytes[i+2]) - Math.min(bytes[i], bytes[i+1], bytes[i+2]) > 40) varied++;
      if (bytes[i] > bytes[i+1] + 20 && bytes[i] > bytes[i+2] + 20) redDominant++;
    }
    return { lit, varied, redDominant, width: copy.width, height: copy.height };
  }, selector);
}
