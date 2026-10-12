import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('X11 GetInputFocus reports the Vulkan focus window despite a stale GDI desktop',()=>{
 const src=readFileSync(join(root,'source/x11wire/xwireconnection.cpp'),'utf8');
 const body=src.match(/case X_GetInputFocus: \{[\s\S]*?\n        \}/)?.[0];assert.ok(body);
 const inputBody=src.match(/void XWireConnection::deliverInputEvents\(\) \{[\s\S]*?(uint32_t pw;[\s\S]*?)uint32_t presentWindow = pw;/)?.[1];assert.ok(inputBody);
 const dir=mkdtempSync(join(tmpdir(),'xwire-focus-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <map>
struct Window {int16_t x=0,y=0;};
struct XWireServer { std::map<uint32_t,Window> windows;std::mutex regMutex;uint32_t presentWindow=0,focusWindow=0;static XWireServer& instance(){static XWireServer s;return s;} };
uint8_t reply[32];void writeToClient(const uint8_t* p,unsigned n){assert(n==32);memcpy(reply,p,n);}
void query(){uint32_t rootWindow=0x260,sequence=0x1234;enum{X_GetInputFocus=43};switch(X_GetInputFocus){${body}}}
uint32_t chosen=0;void selectInputOwner(uint32_t clientIdBase){const uint32_t clientIdMask=0x1fffff;auto& srv=XWireServer::instance();${inputBody}chosen=pw;}
uint32_t focus(){uint32_t f;memcpy(&f,reply+8,4);return f;}
int main(){auto& s=XWireServer::instance();s.presentWindow=0x200001;s.focusWindow=0x400001;query();assert(focus()==0x400001);assert(reply[0]==1 && reply[2]==0x34 && reply[3]==0x12);
s.presentWindow=0;query();assert(focus()==0x400001);s.focusWindow=0;s.presentWindow=0x200001;query();assert(focus()==0x200001);s.presentWindow=0;query();assert(focus()==0x260);
s.windows.emplace(0x200001,Window{});s.windows.emplace(0x400001,Window{});s.presentWindow=0x200001;s.focusWindow=0x400001;chosen=0;selectInputOwner(0x400000);assert(chosen==0x400001);chosen=0;selectInputOwner(0x200000);assert(chosen==0);s.focusWindow=0;selectInputOwner(0x200000);assert(chosen==0x200001);}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
