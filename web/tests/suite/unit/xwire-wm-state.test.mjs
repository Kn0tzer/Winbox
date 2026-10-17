import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('managed X11 windows acknowledge WM_STATE and leave override-redirect windows alone',()=>{
 const source=readFileSync(join(root,'source/x11wire/xwireconnection.cpp'),'utf8');
 const method=source.match(/void XWireConnection::updateWmState\(uint32_t window, uint32_t state\) \{[\s\S]*?\n\}/)?.[0];assert.ok(method,'WM_STATE transitions must be implemented');
 const dir=mkdtempSync(join(tmpdir(),'xwire-wm-state-'));
 try {
  writeFileSync(join(dir,'check.cpp'),`
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <map>
#include <string>
#include <vector>
struct Window{bool overrideRedirect=false,isRoot=false;uint32_t eventMask=0x400000;};
struct XWireServer{struct XWireProp{std::string type;uint8_t format=8;std::vector<uint8_t> data;};std::mutex regMutex;std::map<uint32_t,Window> windows;std::map<std::pair<uint32_t,std::string>,XWireProp> windowProps;static XWireServer& instance(){static XWireServer s;return s;}};
struct Notice{uint32_t window,atom;uint8_t state;};std::vector<Notice> notices;
struct XWireConnection{void updateWmState(uint32_t,uint32_t);uint32_t internAtom(const std::string& name,bool){assert(name=="WM_STATE");return 0x201;}void sendPropertyNotify(uint32_t w,uint32_t a,uint8_t s){notices.push_back({w,a,s});}};
${method}
int main(){auto& s=XWireServer::instance();XWireConnection c;s.windows.emplace(0x400001,Window{});c.updateWmState(0x400001,1);auto& p=s.windowProps.at({0x400001,"WM_STATE"});assert(p.type=="WM_STATE" && p.format==32 && p.data.size()==8);uint32_t state=0,icon=1;memcpy(&state,p.data.data(),4);memcpy(&icon,p.data.data()+4,4);assert(state==1 && icon==0);assert(notices.size()==1 && notices[0].window==0x400001 && notices[0].atom==0x201 && notices[0].state==0);c.updateWmState(0x400001,0);assert(s.windowProps.count({0x400001,"WM_STATE"})==0);assert(notices.back().state==1);s.windows.emplace(0x400002,Window{true,false,0x400000});c.updateWmState(0x400002,1);assert(s.windowProps.count({0x400002,"WM_STATE"})==0);s.windows.emplace(0x260,Window{false,true,0x400000});c.updateWmState(0x260,1);c.updateWmState(0x999,1);assert(notices.size()==2);s.windows[0x400001].eventMask=0;c.updateWmState(0x400001,1);assert(s.windowProps.count({0x400001,"WM_STATE"})==1 && notices.size()==2);}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
