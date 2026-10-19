import { validateExecutable, safePath } from './files.mjs';

const chromiumArgs = [
  '--no-sandbox', '--no-zygote', '--single-process', '--in-process-gpu',
  '--disable-gpu', '--disable-gpu-compositing', '--disable-software-rasterizer', '--use-gl=disabled',
  '--disable-features=CalculateNativeWinOcclusion,DirectComposition', '--disable-backgrounding-occluded-windows',
  '--disable-dev-shm-usage', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
  '--disable-sync', '--disable-breakpad', '--disable-background-networking', '--window-size=1024,640',
];
export function programDefaults(engine = 'windows') {
  const args = engine === 'chromium' ? [...chromiumArgs, 'about:blank']
    : engine === 'source' ? ['-game', 'portal', '-novid', '-windowed', '-w', '1024', '-h', '640']
    : engine === 'unity' ? ['-force-d3d11', '-screen-width', '1024', '-screen-height', '640', '-screen-fullscreen', '0'] : [];
  return { engine, args, graphics: 'webgl' };
}
const normalized = value => String(value).toLowerCase().replace(/\.exe$/i, '').replace(/[^a-z0-9]/g, '');
const auxiliary = /(?:^|\/)(?:unins\d*|uninstall(?:er)?|unitycrashhandler(?:32|64)?|crash(?:pad|handler|reporter).*|chrome_proxy|notification_helper|vc_redist.*|vcredist.*|dxsetup|ngen|csc|regasm|regsvcs|installutil|setup-helper|helper)\.exe$/i;
/** Select an entry point using metadata only; never execute imported files. */
export async function detectProgram(files, { name = '' } = {}) {
  const paths = files.map(file => safePath(file.path));
  const lower = paths.map(path => path.toLowerCase());
  const engine = lower.some(p => /(?:^|\/)chrome\.exe$/.test(p)) && lower.some(p => /(?:^|\/)chrome\.dll$/.test(p)) ? 'chromium'
    : lower.some(p => /(?:^|\/)portal\/gameinfo\.txt$/.test(p)) ? 'source'
    : lower.some(p => /(?:^|\/)(unityplayer\.dll|globalgamemanagers)$|_data\/.*(?:resources\.assets|globalgamemanagers)$/.test(p)) ? 'unity'
    : lower.some(p => /(?:^|\/)fna\.dll$/.test(p)) ? 'fna' : 'windows';
  const preferred = engine === 'chromium' ? 'chrome.exe' : engine === 'source' ? 'hl2.exe' : null;
  const candidates = files.filter(f => /\.exe$/i.test(f.path) && !auxiliary.test(f.path));
  if (!candidates.length) throw Error('No main Windows application found. Choose the folder containing the app’s EXE and its files.');
  const score = file => {
    const base = file.path.split('/').at(-1).toLowerCase();
    return (base === preferred ? 5000 : 0) + (name && normalized(base) === normalized(name) ? 10000 : 0)
      + (base === 'celeste.exe' && engine === 'fna' ? 1000 : 0)
      - (file.path.split('/').length - 1) * 25
      - (/(?:^|\/)(?:redist|_redist|tools|microsoft\.net|support)\//i.test(file.path) ? 500 : 0)
      - (/^(?:setup|install|update|launcher)\.exe$/i.test(base) ? 100 : 0);
  };
  candidates.sort((a, b) => score(b) - score(a) || a.path.localeCompare(b.path, 'en'));
  let firstError;
  for (const file of candidates) {
    try {
      const base = file.path.split('/').at(-1).toLowerCase();
      const selectedEngine = engine === 'chromium' && base !== 'chrome.exe' ? 'windows' : engine === 'source' && base !== 'hl2.exe' ? 'windows' : engine;
      return { ...programDefaults(selectedEngine), exe: file.path, arch: await validateExecutable(file.blob) };
    } catch (error) { firstError ||= error; }
  }
  throw Error(`No supported Windows executable found. ${firstError?.message || 'Use an x86 or x64 Windows application.'}`);
}
/** @param {any} program @param {{installation?:string, archive?:string, archiveBytes?:number, graphics?:string}} [options] */
export function runtimeParameters(program, { installation = 'demo', archive, archiveBytes, graphics } = {}) {
  if (!['x86', 'x64'].includes(program.arch)) throw Error('Unsupported program architecture');
  const args = program.args || [];
  if (!Array.isArray(args) || args.length > 32 || args.some(a => typeof a !== 'string' || a.length > 4096 || /[\x00-\x1f]/.test(a))) throw Error('Program arguments must be a JSON array of strings.');
  const query = new URLSearchParams({ installation, arch: program.arch, session: '0', gpu: (graphics || program.graphics) === 'webgpu' ? '1' : '0', gltrace: '0', winedbg: '-all' });
  // The shared runtime installs DXVK in Wine's architecture-specific DLL path.
  // Derive this from the stored engine too, so existing saved games get it.
  if (program.engine === 'source') query.set('dlloverrides', 'd3d9=n');
  if (program.engine === 'unity') query.set('dlloverrides', 'd3d11=n;dxgi=n');
  if (program.builtin) query.set('p', program.builtin);
  else { query.set('exe', safePath(program.exe)); query.set('args', JSON.stringify(args)); }
  if (archive) {
    if (!Number.isSafeInteger(archiveBytes) || archiveBytes < 22) throw Error('Invalid program archive size');
    query.set('gamezip', archive); query.set('gamezipsize', String(archiveBytes));
  }
  return query;
}
