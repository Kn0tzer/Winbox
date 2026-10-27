// kbprobe-input.mjs -- empirical keyboard-delivery probe for the Winbox rig.
//
// A tiny guest program (tools/kbprobe/kbprobe.c -> kbprobe.exe) opens a real
// window and logs every input signal a game could consume (WM_KEYDOWN/UP
// with lParam detail, WM_SYSKEY*, WM_CHAR, WM_INPUT raw keyboard, async
// GetAsyncKeyState edges, focus changes, XInput presence) to guest stdout,
// which reaches the page console with the "kbprobe:" prefix.
//
// Modes (env MODE):
//   native : headless session (novideo=1); keys injected with the product's
//            bw64_xwire_key ccall (SDL scancodes) -- tests XWire -> Wine ->
//            guest with no DOM involvement.
//   dom    : video session, runtime page top-level; keys sent ONLY via
//            Playwright page.keyboard (the DOM/SDL capture path). CLICK=1
//            clicks the SDL canvas first.
//   frame  : video session inside an iframe on a harness page (the product
//            shell topology); keys via page.keyboard. CLICK=1 clicks into
//            the iframe first, CLICK=0 never clicks.
//
// Env: MODE (required), CLICK=0|1, TAG, TIMEOUT (ms, session-ready budget).
// Out: test-results/kbprobe-<mode>-<tag>/{console.log,summary.json}
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdir, writeFile, readFile, rm } from 'node:fs/promises';

const mode = process.env.MODE;
if (!['native', 'dom', 'frame'].includes(mode)) throw new Error('MODE must be native|dom|frame');
const click = process.env.CLICK === '1';
const tag = process.env.TAG || `${mode}${click ? '-click' : ''}-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must be word chars');
const dir = `test-results/kbprobe-${tag}`;
await mkdir(dir, { recursive: true });
const timeout = Number(process.env.TIMEOUT || 1200000);
const exePath = process.env.KBPROBE_PATH || '/tmp/kbprobe/kbprobe.exe';

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT kbprobe', mode, 'click', click);
const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const port = server.address().port;
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => errors.push(e.message));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const kbLines = () => logs.filter(l => l.includes('kbprobe:')).map(l => l.replace(/^[^:]*: /, ''));
const waitKb = async (re, ms) => {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { if (kbLines().some(l => re.test(l))) return true; await page.waitForTimeout(500); }
  return false;
};

// [name, SDL scancode (native), expected Windows VK (guest), Playwright key (dom)]
const BATTERY = [
  ['A', 4, 0x41, 'a'], ['W', 26, 0x57, 'w'], ['S', 22, 0x53, 's'], ['D', 7, 0x44, 'd'], ['M', 16, 0x4D, 'm'],
  ['RETURN', 40, 0x0D, 'Enter'], ['SPACE', 44, 0x20, 'Space'], ['ESCAPE', 41, 0x1B, 'Escape'],
  ['TAB', 43, 0x09, 'Tab'], ['BACKSPACE', 42, 0x08, 'Backspace'],
  ['UP', 82, 0x26, 'ArrowUp'], ['DOWN', 81, 0x28, 'ArrowDown'], ['LEFT', 80, 0x25, 'ArrowLeft'], ['RIGHT', 79, 0x27, 'ArrowRight'],
  ['LSHIFT', 225, 0xA0, 'Shift'], ['LCTRL', 224, 0xA2, 'Control'], ['LALT', 226, 0xA4, 'Alt'],
  ['F1', 58, 0x70, 'F1'], ['F4', 61, 0x73, 'F4'], ['F10', 67, 0x79, 'F10'], ['F11', 68, 0x7A, 'F11'], ['F12', 69, 0x7B, 'F12'],
  ['HOME', 74, 0x24, 'Home'], ['END', 77, 0x23, 'End'], ['PAGEUP', 75, 0x21, 'PageUp'], ['PAGEDOWN', 78, 0x22, 'PageDown'],
  ['INSERT', 73, 0x2D, 'Insert'], ['DELETE', 76, 0x2E, 'Delete'],
  ['PRINTSCREEN', 70, 0x2C, null], ['SCROLLLOCK', 71, 0x91, null], ['PAUSE', 72, 0x13, null],
  ['CAPSLOCK', 57, 0x14, 'CapsLock'], ['NUMLOCK', 83, 0x90, 'NumLock'],
  ['RCTRL', 228, 0xA3, null], ['RALT', 230, 0xA5, null], ['LGUI', 227, 0x5B, 'Meta'],
  ['KP_0', 98, 0x60, null], ['KP_1', 89, 0x61, null], ['KP_ENTER', 88, 0x0D, null],
  ['DIGIT1', 30, 0x31, '1'], ['DIGIT5', 34, 0x35, '5'], ['DIGIT0', 39, 0x30, '0'],
  ['RSHIFT', 229, 0xA1, null], ['KP_5', 93, 0x65, null], ['KP_PLUS', 87, 0x6B, null], ['KP_MINUS', 86, 0x6D, null],
];
const summary = { mode, click, tag, keys: {}, phases: {}, errors };
let frameHandle = null; // frame mode: the runtime frame for ccall fallback checks

try {
  const sessionParams = mode === 'native'
    ? 'p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0'
    : 'p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&gltrace=0';
  if (mode === 'frame') {
    await writeFile('dist/kbframe-harness.html',
      `<!doctype html><html><head><meta charset="utf-8"><title>kbframe</title></head>` +
      `<body style="margin:0;background:#222"><div id="display" style="width:1024px;height:640px">` +
      `<iframe id="rt" title="runtime" style="width:100%;height:100%;border:0" ` +
      `src="runtime/index.html?${sessionParams}"></iframe></div></body></html>`);
    await page.goto(`http://127.0.0.1:${port}/kbframe-harness.html`);
    frameHandle = page.frames().find(f => f.url().includes('runtime/index.html'));
    if (!frameHandle) throw new Error('runtime iframe not found');
    await frameHandle.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  } else {
    await page.goto(`http://127.0.0.1:${port}/runtime/index.html?${sessionParams}`);
    await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  }
  console.log('SESSION READY');
  summary.phases.sessionReady = true;
  const rt = frameHandle || page;

  // Upload kbprobe.exe into the guest home and register it in the VFS.
  const bytes = await readFile(exePath);
  await rt.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/kbprobe.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/kbprobe.exe']);
    if (registered !== 1) throw new Error('register_file failed');
  }, { b64: bytes.toString('base64') });
  const spawn = await rt.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\home\\username\\kbprobe.exe\n2400', 'WINEDEBUG=-all']));
  console.log('SPAWN', spawn);
  summary.phases.spawn = spawn;
  if (!await waitKb(/READY/, 600000)) throw new Error('kbprobe READY not seen');
  console.log('KBPROBE READY');
  summary.phases.guestReady = true;
  await page.waitForTimeout(2000);
  await flush();

  if (mode === 'native') {
    const sendKey = (sc, down) => rt.evaluate(([c, d]) =>
      window.bwRuntime.call('bw64_xwire_key', ['number', 'number'], [c, d]), [sc, down ? 1 : 0]);
    for (const [name, sc] of BATTERY.map(([n, s]) => [n, s])) {
      await sendKey(sc, true); await page.waitForTimeout(300);
      await sendKey(sc, false); await page.waitForTimeout(300);
    }
    console.log('BATTERY DONE');
    // HOLD W for 3s: count WM_KEYDOWN repeats + async state duration.
    const before = kbLines().filter(l => /WM_KEYDOWN vk=0x57/.test(l)).length;
    await sendKey(26, true); await page.waitForTimeout(3000); await sendKey(26, false);
    await page.waitForTimeout(1000);
    const after = kbLines().filter(l => /WM_KEYDOWN vk=0x57/.test(l)).length;
    summary.phases.holdW = { keydownsDuringHold: after - before };
    console.log('HOLD W keydowns', after - before);
    // CHORD: LCTRL held + C tap.
    await sendKey(224, true); await page.waitForTimeout(200);
    await sendKey(6, true); await page.waitForTimeout(200); await sendKey(6, false);
    await page.waitForTimeout(200); await sendKey(224, false);
    await page.waitForTimeout(500);
    // RAPID: 10x Space at 100 ms spacing.
    for (let i = 0; i < 10; i++) { await sendKey(44, true); await page.waitForTimeout(50); await sendKey(44, false); await page.waitForTimeout(50); }
    await page.waitForTimeout(1500);
    summary.phases.rapidSpace = kbLines().filter(l => /WM_KEYDOWN vk=0x20/.test(l)).length;
    console.log('RAPID space keydowns total', summary.phases.rapidSpace);
  } else {
    // DOM path. Optionally click the display canvas first (product gesture).
    if (click) {
      if (mode === 'frame') {
        const box = await page.locator('#rt').boundingBox();
        await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
      } else {
        await page.mouse.click(640, 450);
      }
      await page.waitForTimeout(1000);
      summary.phases.clicked = true;
    }
    for (const [name, , , pk] of BATTERY) {
      if (!pk) continue;
      await page.keyboard.press(pk, { delay: 60 }).catch(() => {});
      await page.waitForTimeout(250);
    }
    console.log('BATTERY DONE');
    const before = kbLines().filter(l => /WM_KEYDOWN vk=0x57/.test(l)).length;
    await page.keyboard.down('w'); await page.waitForTimeout(3000); await page.keyboard.up('w');
    await page.waitForTimeout(1000);
    const after = kbLines().filter(l => /WM_KEYDOWN vk=0x57/.test(l)).length;
    summary.phases.holdW = { keydownsDuringHold: after - before };
    console.log('HOLD W keydowns', after - before);
    await page.keyboard.down('Control'); await page.waitForTimeout(200);
    await page.keyboard.press('c'); await page.waitForTimeout(200); await page.keyboard.up('Control');
    await page.waitForTimeout(500);
    for (let i = 0; i < 10; i++) { await page.keyboard.press('Space'); await page.waitForTimeout(50); }
    await page.waitForTimeout(1500);
    summary.phases.rapidSpace = kbLines().filter(l => /WM_KEYDOWN vk=0x20/.test(l)).length;
    console.log('RAPID space keydowns total', summary.phases.rapidSpace);
  }

  // Parse the guest log into per-key verdicts.
  const lines = kbLines();
  for (const [name, , vk] of BATTERY) {
    const hex = '0x' + vk.toString(16).toUpperCase().padStart(2, '0');
    const kd = lines.filter(l => l.includes(`WM_KEYDOWN vk=${hex}`) || l.includes(`WM_SYSKEYDOWN vk=${hex}`)).length;
    const ku = lines.filter(l => l.includes(`WM_KEYUP vk=${hex}`) || l.includes(`WM_SYSKEYUP vk=${hex}`)).length;
    const raw = lines.filter(l => l.includes('RAWKEY') && l.includes(`vkey=${hex}`)).length;
    const asyncEdge = lines.filter(l => l.startsWith('kbprobe:') && l.includes(`ASYNC`) && l.includes('DOWN')).length; // global only
    summary.keys[name] = { vk: hex, keydown: kd, keyup: ku, rawkey: raw };
  }
  // KP_ENTER shares VK 0x0D with RETURN; distinguish by the extended-key
  // bit in lParam (scan=0x1C ext=1 vs ext=0).
  summary.keys.KP_ENTER.extendedKeydowns = lines.filter(l => /WM_KEYDOWN vk=0x0D scan=0x1C ext=1/.test(l)).length;
  summary.keys.RETURN.plainKeydowns = lines.filter(l => /WM_KEYDOWN vk=0x0D scan=0x1C ext=0/.test(l)).length;
  summary.guest = {
    focusEvents: lines.filter(l => /FOCUSSTATE|WM_SETFOCUS/.test(l)).slice(0, 12),
    xinput: lines.filter(l => /XINPUT/.test(l)),
    rawDevices: lines.filter(l => /RAWINPUTDEVICES/.test(l)),
    window: lines.filter(l => /WINDOW|READY/.test(l)),
    asyncEdges: lines.filter(l => /ASYNC/.test(l)).slice(0, 80),
    totalKbLines: lines.length,
  };
} catch (e) {
  summary.failure = String(e.stack || e);
  console.error('FATAL', e.message);
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary.keys));
  await browser.close(); server.close();
  if (mode === 'frame') await rm('dist/kbframe-harness.html', { force: true });
  await slot.release();
}
