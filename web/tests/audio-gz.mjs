import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile } from 'node:fs/promises';
const tag = 'audio-gz-' + Date.now();
const dir = 'test-results/' + tag;
await mkdir(dir, { recursive: true });
const server = await serve('dist');
const port = server.address().port;
const browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [];
page.on('console', m => logs.push(m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('pageerror: ' + e.message));
const flush = () => writeFile(dir + '/console.log', logs.join('\n'));
await page.addInitScript(() => {
  window.__bwAudio = { taps: [], ctx: null };
  for (const name of ['AudioContext','webkitAudioContext']) {
    const R = window[name]; if (!R) continue;
    const W = function(...a){ const c = new R(...a); window.__bwAudio.ctx = c;
      const orig = c.createScriptProcessor.bind(c);
      c.createScriptProcessor = function(bs,ic,oc){ const n = orig(bs,ic,oc);
        const tap = orig(bs||4096,1,1); const zg = c.createGain(); zg.gain.value = 0;
        const chunks=[]; tap.onaudioprocess=(e)=>{chunks.push({t:performance.now(),data:new Float32Array(e.inputBuffer.getChannelData(0))});};
        n.connect(tap); tap.connect(zg); zg.connect(c.destination);
        window.__bwAudio.taps.push({chunks, sr:c.sampleRate, len:tap.bufferSize}); return n; };
      return c; };
    W.prototype = R.prototype; window[name] = W;
  }
});
const summary = { tag };
try {
  const gz = encodeURIComponent('http://127.0.0.1:' + port + '/audio.zip');
  await page.goto('http://127.0.0.1:' + port + '/runtime/index.html?gamezip=' + gz + '&gamezipsize=205523&exe=audiotone.exe&session=0&persist=0&gltrace=0');
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 600000 });
  console.log('READY, clicking to unlock during boot...');
  const clickLoop = (async () => {
    for (let i = 0; i < 60; i++) {
      try {
        const ci = await page.evaluate(() => { const c=document.getElementById('canvas')||document.querySelector('canvas');
          if(!c) return null; const r=c.getBoundingClientRect(); return {x:r.x+r.width/2, y:r.y+r.height/2}; });
        if (ci) await page.mouse.click(ci.x, ci.y);
      } catch(e) {}
      await page.waitForTimeout(3000);
      const st = await page.evaluate(() => window.__bwAudio?.ctx?.state || 'none').catch(()=>'err');
      if (st === 'running') { console.log('CTX RUNNING'); break; }
    }
  })();
  const waitFor = async (re, ms) => { const u=Date.now()+ms;
    while(Date.now()<u){ await flush(); const h=logs.find(l=>re.test(l)); if(h) return {t:Date.now()}; await page.waitForTimeout(500); }
    throw new Error('timeout '+re); };
  const ts = await waitFor(/audiotone: TONESTART/, 300000);
  console.log('TONESTART!');
  await clickLoop;
  await page.waitForTimeout(20000);
  const tap = await page.evaluate(() => { const t=(window.__bwAudio?.taps||[])[0]; if(!t) return null;
    const total=t.chunks.reduce((a,c)=>a+c.data.length,0); const out=new Float32Array(total); const times=[]; let o=0;
    for(const c of t.chunks){out.set(c.data,o);o+=c.data.length;times.push(c.t);}
    return {samples:Array.from(out),times,sr:t.sr,cl:t.len}; });
  if (!tap) throw new Error('no tap');
  const s=Float32Array.from(tap.samples), sr=tap.sr;
  let onset=-1; for(let i=0;i+200<s.length;i++){let ok=true; for(let j=0;j<200;j+=10) if(Math.abs(s[i+j])<0.05){ok=false;break;} if(ok){onset=i;break;}}
  const w0=onset>=0?onset+Math.floor(sr*0.7):Math.floor(s.length/2), w1=Math.min(w0+sr,s.length);
  let zc=0; for(let i=w0+1;i<w1;i++) if((s[i-1]<0)!==(s[i]<0)) zc++;
  let peak=0,sum=0; for(let i=w0;i<w1;i++){const a=Math.abs(s[i]); if(a>peak)peak=a; sum+=s[i]*s[i];}
  const freq=zc/2, rms=Math.sqrt(sum/(w1-w0));
  let lat=null; if(onset>=0){const cix=Math.floor(onset/tap.cl); lat=Math.round((tap.times[Math.min(cix,tap.times.length-1)]+(onset%tap.cl)/sr*1000)-ts.t);}
  summary.tone={freqHz:freq,peak:+peak.toFixed(4),rms:+rms.toFixed(4),latencyMs:lat,sr,total:s.length};
  console.log('TONE',JSON.stringify(summary.tone));
  await writeFile(dir+'/summary.json', JSON.stringify(summary,null,2)); await flush();
} catch(e){ summary.fatal=e.message; await writeFile(dir+'/summary.json',JSON.stringify(summary,null,2)).catch(()=>{});
  try{ await writeFile(dir+'/console.log', logs.join('\n')); }catch(_){} console.error('FATAL',e.message); process.exitCode=1; }
finally{ try{await browser.close();}catch(_){} server.close(); }
