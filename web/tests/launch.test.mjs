import test from 'node:test';
import assert from 'node:assert/strict';
import { detectProgram, programDefaults, runtimeParameters } from '../launch.mjs';
function exe(path, arch='x86') {
 const data=new Uint8Array(1024),v=new DataView(data.buffer);
 v.setUint16(0,0x5a4d,true);v.setUint32(60,64,true);v.setUint32(64,0x4550,true);v.setUint16(68,arch==='x64'?0x8664:0x14c,true);v.setUint16(70,1,true);v.setUint16(84,arch==='x64'?240:224,true);v.setUint16(86,2,true);v.setUint16(88,arch==='x64'?0x20b:0x10b,true);const section=88+(arch==='x64'?240:224);v.setUint32(section+16,512,true);v.setUint32(section+20,512,true);
 return {path,size:data.length,blob:new Blob([data])};
}
const asset=path=>({path,size:1,blob:new Blob(['x'])});
test('auto-selects the folder main executable over helpers and reads its PE header',async()=>{
 const p=await detectProgram([exe('UnityCrashHandler64.exe','x64'),exe('redist/setup.exe'),exe('bin/tool.exe'),exe('My Game.exe','x64'),asset('UnityPlayer.dll')],{name:'My Game'});
 assert.equal(p.exe,'My Game.exe');assert.equal(p.arch,'x64');assert.equal(p.engine,'unity');
});
test('Chromium selects its real main binary and applies the tested launch flags',async()=>{
 const p=await detectProgram([exe('chrome_proxy.exe'),exe('chrome.exe'),asset('109/chrome.dll')],{name:'Chrome'});
 assert.equal(p.exe,'chrome.exe');assert.equal(p.arch,'x86');assert.equal(p.engine,'chromium');assert.ok(p.args.includes('--single-process'));assert.ok(p.args.includes('--no-sandbox'));
});
test('Source and FNA profiles use the detected program rather than installer tools',async()=>{
 const p=await detectProgram([exe('Portal.exe'),exe('hl2.exe'),asset('portal/gameinfo.txt')]);
 assert.equal(p.exe,'hl2.exe');assert.ok(p.args.includes('portal'));assert.equal(p.arch,'x86');
 const fna=await detectProgram([exe('Celeste.exe'),exe('Microsoft.NET/Framework/csc.exe'),asset('FNA.dll')]);assert.equal(fna.exe,'Celeste.exe');assert.equal(fna.engine,'fna');
});
test('never silently starts an uninstaller or malformed executable',async()=>{
 await assert.rejects(detectProgram([exe('uninstall.exe')]),/main|application/i);
 await assert.rejects(detectProgram([asset('game.exe')]),/PE|Windows|executable/i);
 await assert.rejects(detectProgram([asset('readme.txt')]),/Windows|executable/i);
});
test('ranks names independently of upload order and supports nested entry points',async()=>{
 const files=[exe('tools/editor.exe'),exe('bin/App.exe','x64'),exe('helper.exe')];
 const a=await detectProgram(files,{name:'App'}),b=await detectProgram([...files].reverse(),{name:'App'});
 assert.equal(a.exe,'bin/App.exe');assert.deepEqual(a,b);
});
test('runtime parameters preserve spaces/arguments and always select detected architecture',()=>{
 const q=runtimeParameters({...programDefaults('source'),exe:'Folder/hl2.exe',arch:'x86'},{installation:'one',archive:'https://host.test/game.zip',archiveBytes:1234});
 assert.equal(q.get('arch'),'x86');assert.equal(q.get('exe'),'Folder/hl2.exe');assert.deepEqual(JSON.parse(q.get('args')),programDefaults('source').args);assert.equal(q.get('gamezipsize'),'1234');assert.equal(q.get('session'),'0');
 assert.throws(()=>runtimeParameters({exe:'../bad.exe',arch:'x86',args:[]}));
 assert.throws(()=>runtimeParameters({exe:'good.exe',arch:'arm64',args:[]}));
 assert.throws(()=>runtimeParameters({exe:'good.exe',arch:'x86',args:['bad\narg']}));
});

test('a game containing an embedded browser still selects the game',async()=>{const p=await detectProgram([exe('Game.exe'),exe('browser/chrome.exe'),asset('browser/chrome.dll')],{name:'Game'});assert.equal(p.exe,'Game.exe');assert.equal(p.engine,'windows');});
test('Source and Unity launches select their native Direct3D libraries for demos and saved uploads',()=>{
 for(const [engine,arch,overrides] of [['source','x86','d3d9=n'],['unity','x64','d3d11=n;dxgi=n']]){
  const program={...programDefaults(engine),exe:'Game.exe',arch};
  assert.equal(runtimeParameters(program).get('dlloverrides'),overrides);
  assert.equal(runtimeParameters(JSON.parse(JSON.stringify(program))).get('dlloverrides'),overrides);
 }
 assert.ok(programDefaults('unity').args.includes('-force-d3d11'));
 assert.equal(runtimeParameters({exe:'App.exe',arch:'x64'}).has('dlloverrides'),false);
});
test('rejects a PE signature without the complete image headers and data',async()=>{const file=exe('broken.exe');file.blob=file.blob.slice(0,128);await assert.rejects(detectProgram([file]),/PE|image|headers/i);});
