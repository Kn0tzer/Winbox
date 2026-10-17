import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import assert from 'node:assert/strict';
const dir = 'test-results/launcher-redesign';
await mkdir(dir, { recursive: true });
await waitForBuildIdle(); const slot = await acquireProbeSlot();
let browser, server;
const errors = [], checks = [];
const temp = await mkdtemp('/tmp/launcher-ui-');
try {
  server = await serve('dist');
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', args: ['--no-sandbox','--disable-dev-shm-usage'] });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 }, colorScheme: 'light' });
  page.on('pageerror', error => errors.push(error.message));
  let failPortal = false, holdFirstPaint = false;
  await page.route('https://kn0tzer.work.gd/games/**', route => route.fulfill({ status: failPortal && route.request().url().includes('portal-') ? 503 : 200, headers: { 'content-length': '12345678', 'access-control-allow-origin': '*' }, body: '' }));
  // UI routing is exercised independently of game compatibility. Real runtime
  // acceptance is a separate check; this fixture never claims gameplay.
  await page.route(url => url.pathname.endsWith('/runtime/index.html'), route => route.fulfill({ contentType: 'text/html', headers: { 'cross-origin-embedder-policy':'require-corp', 'cross-origin-opener-policy':'same-origin' }, body: `<!doctype html><title>Runtime fixture</title><canvas id="canvas" width="64" height="48"></canvas><script>
  window.bwRuntime={fs:()=>null};window.finishPaint=()=>{const c=document.getElementById('canvas').getContext('2d');c.fillStyle='white';c.fillRect(0,0,64,48);};
  ${holdFirstPaint ? '' : 'finishPaint();'}
  parent.postMessage({wine:true,type:"status",data:"Running..."},location.origin);
  parent.postMessage({wine:true,type:"log",data:"first window mapped"},location.origin);</script>` }));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => window.wineGames && !document.getElementById('library').hasAttribute('aria-busy'));
  assert.equal(await page.locator('[data-game]').count(), 9);
  await page.screenshot({ path: dir + '/desktop-light.png', fullPage: true, animations: 'disabled' });
  checks.push('Fresh library renders without console errors');
  for (const [id, arch, executable] of [['doom','x64',null],['chromium','x86','chrome.exe'],['portal','x86','hl2.exe'],['celeste','x86','Celeste.exe'],['gangbeasts','x64','Gang Beasts.exe'],['explorer','x64',null],['notepad','x64',null],['cube','x64',null],['snake','x64',null]]) {
    await page.locator(`[data-game="${id}"]`).click();
    await page.locator('#display iframe').waitFor();
    await page.waitForFunction(()=>document.getElementById('runState').textContent==='Running');
    const query = new URL(await page.locator('#display iframe').getAttribute('src'), page.url()).searchParams;
    assert.equal(query.get('arch'),arch,id);assert.equal(query.get('installation'),executable?'demo-'+id:'demo',id);
    if (executable) {assert.equal(query.get('exe'),executable);assert.equal(query.get('gamezipsize'),'12345678');}
    else assert.ok(query.get('p'));
    await page.click('#stop'); await page.locator('#display iframe').waitFor({ state: 'detached' });
    await page.waitForFunction(()=>!document.getElementById('folderButton').disabled);
    assert.ok(!await page.locator('#notice').evaluate(element=>element.classList.contains('error')));
  }
  checks.push('All nine demo tiles launch in one click with correct architecture/entry point');
  holdFirstPaint=true;await page.locator('[data-game="notepad"]').click();
  await page.waitForFunction(()=>document.getElementById('runtimeStatus').textContent==='Not frozen, still loading');
  await page.waitForFunction(()=>document.getElementById('runtimeStatus').textContent==='Running...');
  let runtime=await(await page.locator('#display iframe').elementHandle()).contentFrame();
  await runtime.evaluate(()=>parent.postMessage({wine:true,type:'log',data:'Still preparing pixels'},location.origin));
  await page.waitForFunction(()=>document.getElementById('runtimeStatus').textContent==='Not frozen, still loading');
  await runtime.evaluate(()=>parent.postMessage({wine:true,type:'error',data:'Startup test failure'},location.origin));
  await page.waitForFunction(()=>document.getElementById('runtimeStatus').textContent==='Startup test failure',null,{timeout:1000});
  await runtime.evaluate(()=>parent.postMessage({wine:true,type:'log',data:'Late log after failure'},location.origin));
  assert.equal(await page.locator('#runtimeStatus').textContent(),'Startup test failure');
  await page.click('#playerRetry');
  await page.waitForFunction(()=>document.getElementById('runtimeStatus').textContent==='Not frozen, still loading');
  runtime=await(await page.locator('#display iframe').elementHandle()).contentFrame();
  await runtime.evaluate(()=>window.finishPaint());
  await page.waitForFunction(()=>document.getElementById('runtimeStatus').textContent==='Running...');
  await runtime.evaluate(()=>parent.postMessage({wine:true,type:'log',data:'Normal log after painting'},location.origin));
  assert.equal(await page.locator('#runtimeStatus').textContent(),'Running...');
  await page.click('#stop');await page.waitForFunction(()=>!document.getElementById('folderButton').disabled);holdFirstPaint=false;
  checks.push('Startup reassurance follows recent logs, expires when logs stop, and clears after the first visible frame');
  const folder=join(temp,'Fixture'); await mkdir(folder);
  const pe=Buffer.alloc(1024);pe.writeUInt16LE(0x5a4d,0);pe.writeUInt32LE(64,60);pe.writeUInt32LE(0x4550,64);pe.writeUInt16LE(0x14c,68);pe.writeUInt16LE(1,70);pe.writeUInt16LE(224,84);pe.writeUInt16LE(2,86);pe.writeUInt16LE(0x10b,88);pe.writeUInt32LE(512,328);pe.writeUInt32LE(512,332);
  await writeFile(join(folder,'Fixture.exe'),pe); await writeFile(join(folder,'UnityCrashHandler64.exe'),pe);await writeFile(join(folder,'assets.txt'),'test');
  const chooserPromise=page.waitForEvent('filechooser');await page.click('#folderButton');await (await chooserPromise).setFiles(folder);
  await page.locator('#display iframe').waitFor();
  let query=new URL(await page.locator('#display iframe').getAttribute('src'),page.url()).searchParams;
  assert.equal(query.get('arch'),'x86');assert.equal(query.get('exe'),'Fixture.exe');
  await page.waitForFunction(()=>document.getElementById('runState').textContent==='Running');
  await page.click('#stop');
  await rm(folder,{recursive:true}); // Saved files must survive removal of the source folder.
  await page.reload(); await page.waitForFunction(()=>window.wineLibrary&&!document.getElementById('library').hasAttribute('aria-busy'));
  assert.match(await page.locator('#library').textContent(),/Fixture/);
  await page.locator('.saved-run').click();await page.locator('#display iframe').waitFor();
  query=new URL(await page.locator('#display iframe').getAttribute('src'),page.url()).searchParams;assert.equal(query.get('exe'),'Fixture.exe');await page.waitForFunction(()=>document.getElementById('runState').textContent==='Running');await page.click('#stop');
  await page.waitForFunction(()=>!document.getElementById('folderButton').disabled);assert.ok(!await page.locator('#notice').evaluate(element=>element.classList.contains('error')));
  checks.push('Folder selection auto-detects and auto-starts; persisted folder reruns in one click');
  await page.getByRole('button',{name:'Settings for Fixture'}).click();
  await page.locator('#folderDialog').waitFor({state:'visible'});assert.equal(await page.locator('#executable').inputValue(),'Fixture.exe');await page.keyboard.press('Escape');
  await page.click('#settingsButton');await page.locator('#appearance').selectOption('dark');await page.keyboard.press('Escape');
  await page.screenshot({path:dir+'/desktop-dark.png',fullPage:true,animations:'disabled'});
  await page.reload();await page.waitForFunction(()=>window.wineGames);assert.equal(await page.locator('html').getAttribute('data-theme'),'dark');
  checks.push('Native settings dialogs, Escape dismissal and saved appearance work');
  failPortal=true;await page.locator('[data-game="portal"]').click();await page.locator('#playerRetry').waitFor({state:'visible'});assert.match(await page.locator('#status').textContent(),/unavailable/);failPortal=false;
  await page.click('#playerRetry');await page.waitForFunction(()=>document.getElementById('runState').textContent==='Running');await page.click('#stop');await page.waitForFunction(()=>!document.getElementById('folderButton').disabled);
  checks.push('Failed demo download exposes retry and recovers without a reload');
  for (const width of [320,640,1920]) {
    await page.setViewportSize({width,height:1000});
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow at ${width}px`);
    await page.screenshot({path:`${dir}/width-${width}.png`,fullPage:true,animations:'disabled'});
  }
  await page.setViewportSize({width:1280,height:1000});await page.evaluate(()=>document.documentElement.style.zoom='2');
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'overflow under 200% CSS zoom');
  await page.screenshot({path:dir+'/zoom-200.png',fullPage:true,animations:'disabled'});await page.evaluate(()=>document.documentElement.style.zoom='');
  await page.locator('#folderButton').focus();await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement.id),'zipButton');assert.notEqual(await page.locator('#zipButton').evaluate(element=>getComputedStyle(element).outlineStyle),'none');
  assert.ok(await page.locator('#folderButton').evaluate(element=>element.getBoundingClientRect().height>=44));
  checks.push('320/640/1920px and 200% CSS zoom have no horizontal overflow; keyboard focus and touch target verified');
  const archive = await page.evaluate(async()=>{
    const {prepareArchive,removeArchive}=await import('./package-archive.mjs');
    const pkg={id:'stream-test',created:'test',files:[{path:'data.txt',size:11,blob:new Blob(['hello world'])}]};
    const lease=await prepareArchive(pkg);const response=await fetch(lease.url,{headers:{Range:'bytes=0-3'}});const bytes=Array.from(new Uint8Array(await response.arrayBuffer()));
    const repeat=await prepareArchive(pkg);const size=repeat.bytes;repeat.release();lease.release();await removeArchive(pkg);return {status:response.status,bytes,size};
  });
  assert.equal(archive.status,206);assert.deepEqual(archive.bytes,[80,75,3,4]);assert.ok(archive.size>22);
  checks.push('OPFS stored ZIP, cached reopen and Blob URL byte ranges work in a real browser');
  await page.evaluate(async()=>{
    const {listPackages}=await import('./storage.mjs');const {writeStored}=await import('./session-storage.mjs');
    await writeStored((await listPackages())[0].id,[{path:'/root/home/username/save.dat',data:new Uint8Array([4,5,6])}],true);
  });
  await page.getByRole('button',{name:'Settings for Fixture'}).click();
  const downloading=page.waitForEvent('download');await page.click('#export');
  const backup=join(temp,'fixture-backup.zip');await(await downloading).saveAs(backup);
  await page.evaluate(async()=>{
    const {listPackages}=await import('./storage.mjs');const {writeStored}=await import('./session-storage.mjs');
    await writeStored((await listPackages())[0].id,[{path:'/root/home/username/save.dat',data:new Uint8Array([9])}],true);
  });
  await page.locator('#restoreInput').setInputFiles(backup);await page.locator('#folderDialog').waitFor({state:'hidden'});
  await page.reload();await page.waitForFunction(()=>window.wineLibrary&&!document.getElementById('library').hasAttribute('aria-busy'));
  const restored=await page.evaluate(async()=>{
    const {listPackages}=await import('./storage.mjs');const {storedFiles}=await import('./session-storage.mjs');
    const packages=await listPackages(),pkg=packages[0];
    return {count:packages.length,bytes:(await pkg.files.find(file=>file.path==='Fixture.exe').blob.arrayBuffer()).byteLength,saves:Array.from((await storedFiles(pkg.id))[0].data)};
  });
  assert.equal(restored.count,1);assert.equal(restored.bytes,1024);assert.deepEqual(restored.saves,[4,5,6]);
  checks.push('Backup download and streamed restore preserve owned executable Blobs and saves after the original folder is removed');
  const storage = await page.evaluate(async()=>{
    const {getPackage,listPackages}=await import('./storage.mjs');
    const {storedFiles,writeStored}=await import('./session-storage.mjs');
    const {replaceInstallation}=await import('./installation.mjs');
    const previous=(await listPackages())[0];
    await writeStored(previous.id,[{path:'/root/home/username/save.dat',data:new Uint8Array([1,2,3])}],true);
    const original=IDBDatabase.prototype.transaction;
    let failures=0;
    for(const failDb of ['bw64persist-wine-','wine-browser-library-v1']) {
      IDBDatabase.prototype.transaction=function(...args){
        if(this.name.startsWith(failDb)&&args[1]==='readwrite')throw new DOMException('Injected quota failure','QuotaExceededError');
        return original.apply(this,args);
      };
      try{await replaceInstallation(previous,{name:'Replacement'},[{path:'/root/home/username/save.dat',data:new Uint8Array([9])}]);}
      catch(error){if(error.name==='QuotaExceededError')failures++;else throw error;}
      finally{IDBDatabase.prototype.transaction=original;}
      if((await getPackage(previous.id))?.name!==previous.name)throw Error('Original package was replaced on failure');
      if((await storedFiles(previous.id))[0].data[0]!==1)throw Error('Original saves changed on failure');
    }
    const next=await replaceInstallation(previous,{name:'Restored fixture'},[{path:'/root/home/username/save.dat',data:new Uint8Array([9])}]);
    return {failures,count:(await listPackages()).length,previous:await getPackage(previous.id),value:(await storedFiles(next.id))[0].data[0]};
  });
  assert.equal(storage.failures,2);assert.equal(storage.count,1);assert.equal(storage.previous,undefined);assert.equal(storage.value,9);
  checks.push('Failed save staging or package publication preserves the original; successful restore switches package and saves together');
  const extraction=await page.evaluate(async()=>{
    const {fromZip,cleanupExtracted,makeZip}=await import('./files.mjs');
    const root=await navigator.storage.getDirectory();
    const imports=async()=>{const result=[];for await(const[name]of root.entries())if(name.startsWith('wine-import-'))result.push(name);return result.length;};
    const before=await imports();
    const zip=await makeZip([{path:'readme.txt',size:5,blob:new Blob(['hello'])}]);
    const entries=await fromZip(zip,{stream:true});
    const during=await imports();await cleanupExtracted(entries);
    const controller=new AbortController();controller.abort();let cancelled=false;
    try{await fromZip(zip,{stream:true,signal:controller.signal});}catch(error){cancelled=error.name==='AbortError';}
    return {before,during,after:await imports(),cancelled};
  });
  assert.equal(extraction.during,extraction.before+1);assert.equal(extraction.after,extraction.before);assert.equal(extraction.cancelled,true);
  // Invalid auto-detection must also clean successfully extracted temporary data.
  const invalid=join(temp,'invalid.zip');
  const zipBytes=await page.evaluate(async()=>{const {makeZip}=await import('./files.mjs');return Array.from(new Uint8Array(await(await makeZip([{path:'readme.txt',size:5,blob:new Blob(['hello'])}])).arrayBuffer()));});
  await writeFile(invalid,Buffer.from(zipBytes));await page.locator('#zipInput').setInputFiles(invalid);
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('No main Windows application'));
  assert.equal(await page.evaluate(async()=>{let n=0;for await(const[name]of(await navigator.storage.getDirectory()).entries())if(name.startsWith('wine-import-'))n++;return n;}),extraction.before);
  await page.click('#stop');
  checks.push('Streamed extraction cleans temporary storage after invalid imports and supports cancellation');
  assert.deepEqual(errors,[]);
  await writeFile(dir+'/summary.json',JSON.stringify({pass:true,checks,errors},null,2));console.log(JSON.stringify({pass:true,checks}));
} catch(error) {await writeFile(dir+'/summary.json',JSON.stringify({pass:false,error:error.stack,checks,errors},null,2));throw error;}
finally {await browser?.close();server?.close();await slot.release();await rm(temp,{recursive:true,force:true});}
