import { chromium, expect } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { guestFixture, canvasPixels } from './suite/lib/guest-fixtures.mjs';
import { mkdir,mkdtemp,copyFile,writeFile,truncate,rm,readFile } from 'node:fs/promises';
import { join } from 'node:path';
import assert from 'node:assert/strict';
const dir='test-results/launcher-real';await mkdir(dir,{recursive:true});
const temporary=await mkdtemp('/tmp/launcher-real-');
await waitForBuildIdle();const slot=await acquireProbeSlot();let server,browser,page;
const errors=[],steps=[],logs=[];
const build=JSON.parse(await readFile('dist/runtime/BUILD_INFO.json','utf8'));
try {
 server=await serve('dist');browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'/usr/local/bin/chromium',args:['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-swiftshader']});
 page=await browser.newPage({viewport:{width:1440,height:1000}});page.on('pageerror',error=>errors.push(error.message));page.on('console',message=>{logs.push(message.text().slice(0,1000));if(logs.length>500)logs.shift();});
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.wineGames);
 await page.locator('[data-game="notepad"]').click();
 await page.waitForFunction(()=>document.getElementById('log').textContent.includes('first window mapped'),null,{timeout:180000});
 let frame=await page.locator('#display iframe').elementHandle().then(handle=>handle.contentFrame());
 await frame.waitForFunction(()=>window.bwFilesystemReady===true,null,{timeout:30000});
 await page.screenshot({path:dir+'/notepad.png',fullPage:true});
 steps.push('One-click Notepad maps a real guest window with no runtime stubs');
 await page.click('#stop');await page.locator('#launcher').waitFor({state:'visible'});
 const folder=join(temporary,'Launcher test');await mkdir(folder);await copyFile(await guestFixture('launcher-ready','x86'),join(folder,'Launcher test.exe'));
 // Exceed the direct-copy threshold without allocating a giant Node Buffer.
 await writeFile(join(folder,'padding.data'),'');await truncate(join(folder,'padding.data'),129*1024**2);
 const chooser=page.waitForEvent('filechooser');await page.click('#folderButton');await(await chooser).setFiles(folder);
 await waitForCounter(page, '1');
 const source=await page.locator('#display iframe').getAttribute('src'),query=new URL(source,page.url()).searchParams;
 assert.equal(query.get('arch'),'x86');assert.equal(query.get('exe'),'Launcher test.exe');assert.match(query.get('gamezip'),/^blob:/);
 const installation=query.get('installation');frame=await page.locator('#display iframe').elementHandle().then(handle=>handle.contentFrame());
 await expect.poll(async()=> (await canvasPixels(frame)).lit,{timeout:30000,message:'the actual guest paints the uploaded window'}).toBeGreaterThan(1000);
 const pixels=await canvasPixels(frame);
 await page.screenshot({path:dir+'/uploaded-streamed.png',fullPage:true});
 assert.equal((await page.evaluate(()=>window.wineLibrary.listPackages())).length,1);
 await page.click('#restart');
 await waitForCounter(page, '2');
 const again=new URL(await page.locator('#display iframe').getAttribute('src'),page.url()).searchParams;
 assert.equal(again.get('installation'),installation);assert.equal((await page.evaluate(()=>window.wineLibrary.listPackages())).length,1);
 steps.push('One folder-picker action auto-detects x86 and starts a real Win32 app from an OPFS ZIP through Blob URL byte ranges');
 steps.push('Restart reuses the installation and save database, preserves the guest-written counter, and does not duplicate the folder');
 await page.click('#stop');await page.locator('#launcher').waitFor({state:'visible'});assert.equal(await page.locator('#display iframe').count(),0);
 assert.deepEqual(errors,[]);
 await writeFile(dir+'/summary.json',JSON.stringify({pass:true,steps,pixels,installation,errors,build},null,2));console.log(JSON.stringify({pass:true,steps,pixels}));
}catch(error){await page?.screenshot({path:dir+'/failure.png',fullPage:true}).catch(()=>{});await writeFile(dir+'/summary.json',JSON.stringify({pass:false,error:error.stack,steps,errors,build,logs},null,2));throw error;}
finally{await browser?.close();server?.close();await slot.release();await rm(temporary,{recursive:true,force:true});}

async function waitForCounter(page, count) {
  await page.waitForFunction(expected => {
    try {
      const win = document.querySelector('#display iframe')?.contentWindow;
      return win?.bwFilesystemReady === true
        && win.bwRuntime.fs().readFile('/root/home/username/launcher-counter.txt', {encoding:'utf8'}).trim() === expected
        && document.getElementById('log').textContent.includes('first window mapped');
    } catch { return false; }
  }, count, {timeout:180000});
}
