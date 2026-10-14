import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';

const root = resolve(import.meta.dirname, '../../../..');
const fixtures = {
  'input-witness': ['tools/guest-fixtures/input-witness.c', '-luser32'],
  'window-state': ['tools/guest-fixtures/window-state.c', '-luser32'],
  'cursor-clip': ['tools/guest-fixtures/cursor-clip.c', '-luser32'],
  'win32-clock': ['tools/guest-fixtures/win32-clock.c'],
  dsptone: ['tools/guest-fixtures/dsptone.c', '-lm'],
  trigl: ['tools/openglTest/trigl.c', '-lopengl32', '-lgdi32', '-luser32'],
};
const elfFixtures = { 'shared-mapping': ['tools/guest-fixtures/shared-mapping.c', '-static'] };
const managedFixtures = { 'mono-runtime': ['tools/guest-fixtures/mono-runtime.cs'] };
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
  const options = managed ? ['-optimize+', '-platform:x86', '-sdk:4.5', ...libraries] : ['-O2', ...linkOptions, ...libraries];
  const digest = createHash('sha256').update(await readFile(join(root, source))).update(version)
    .update(JSON.stringify({ options, name, arch }));
  for (const option of libraries) if (option.startsWith('-r:')) digest.update(await readFile(option.slice(3)));
  const hash = digest.digest('hex');
  const dir = join(root, 'tools/guest-fixtures/build', hash);
  const output = join(dir, `${name}-${arch}.${linux ? 'elf' : 'exe'}`);
  try { await stat(output); return output; } catch {}
  await mkdir(dir, { recursive: true });
  execFileSync(compiler, managed ? [...options, '-out:' + output, join(root, source)]
    : ['-O2', ...linkOptions, '-o', output, join(root, source), ...libraries], { stdio: 'pipe' });
  await writeFile(join(dir, 'provenance.json'), JSON.stringify({ source, compiler: version, options, hash, arch }, null, 2));
  return output;
}

export function monoArchive(destination, { gl32 = false } = {}) {
  const mono = resolve(root, process.env.MONO32_DIR || 'tools/rootfs64/mono32');
  execFileSync('python3', [join(root, 'tools/rootfs64/prepare-mono32.py'), '--check', '--output', mono], { stdio: 'pipe' });
  execFileSync('python3', ['-c', `import zipfile,pathlib,sys
root=pathlib.Path(sys.argv[2])
with zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_STORED) as z:
 if sys.argv[3]=='1':z.write(sys.argv[4],'home/username/apps/libGL.so.1')
 for p in sorted(root.rglob('*')):
  if p.is_file():z.write(p,'home/username/apps/'+p.relative_to(root).as_posix())
`, destination, mono, gl32 ? '1' : '0', join(root, 'tools/rootfs64/libgl32/libGL.so.1')]);
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
