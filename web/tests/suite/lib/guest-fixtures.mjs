import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';

const root = resolve(import.meta.dirname, '../../../..');
const fixtures = {
  'input-witness': ['tools/guest-fixtures/input-witness.c', '-luser32'],
  dsptone: ['tools/guest-fixtures/dsptone.c', '-lm'],
  trigl: ['tools/openglTest/trigl.c', '-lopengl32', '-lgdi32', '-luser32'],
};

export async function guestFixture(name, arch = 'x86') {
  if (!fixtures[name] || !['x86', 'x64'].includes(arch)) throw new Error('Invalid guest fixture');
  const [source, ...libraries] = fixtures[name];
  const compiler = arch === 'x86' ? 'i686-w64-mingw32-gcc' : 'x86_64-w64-mingw32-gcc';
  const version = execFileSync(compiler, ['--version'], { encoding: 'utf8' }).split('\n')[0];
  const hash = createHash('sha256').update(await readFile(join(root, source))).update(version).update(JSON.stringify(libraries)).digest('hex');
  const dir = join(root, 'tools/guest-fixtures/build', hash);
  const output = join(dir, `${name}-${arch}.exe`);
  try { await stat(output); return output; } catch {}
  await mkdir(dir, { recursive: true });
  execFileSync(compiler, ['-O2', '-o', output, join(root, source), ...libraries], { stdio: 'pipe' });
  await writeFile(join(dir, 'provenance.json'), JSON.stringify({ source, compiler: version, hash, arch }, null, 2));
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
