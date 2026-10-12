import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('mapped managed windows receive their advertised WM_TAKE_FOCUS protocol with a real timestamp',()=>{
 const source=readFileSync(join(root,'source/x11wire/xwireconnection.cpp'),'utf8');
 const method=source.match(/void XWireConnection::sendTakeFocus\(uint32_t window\) \{[\s\S]*?\n\}/)?.[0];
 assert.ok(method,'Wine uses WM_TAKE_FOCUS to activate managed windows');
 const map=source.match(/case X_MapWindow: \{[\s\S]*?\n        \}/)?.[0];
 assert.ok(map.indexOf('sendTakeFocus(wid)')>map.indexOf('sendMapNotify(wid)'));
 const dir=mkdtempSync(join(tmpdir(),'xwire-take-focus-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <map>
#include <string>
#include <vector>
struct Window{bool overrideRedirect=false,isRoot=false;};
struct XWireServer{struct Prop{std::string type;uint8_t format=8;std::vector<uint8_t> data;};std::mutex regMutex;std::map<uint32_t,Window> windows;std::map<std::pair<uint32_t,std::string>,Prop> windowProps;static XWireServer& instance(){static XWireServer s;return s;}};
struct KSystem{static uint32_t getMilliesSinceStart(){return 123456;}};
std::vector<std::vector<uint8_t>> events;
struct XWireConnection{uint32_t sequence=0x1234;void sendTakeFocus(uint32_t);uint32_t internAtom(const std::string& n,bool){return n=="WM_TAKE_FOCUS"?0x401:0x402;}void writeToClient(const uint8_t* p,unsigned n){events.emplace_back(p,p+n);}};
${method}
uint32_t word(unsigned off){uint32_t v;memcpy(&v,events.back().data()+off,4);return v;}
int main(){auto& s=XWireServer::instance();XWireConnection c;s.windows.emplace(0x400001,Window{});c.sendTakeFocus(0x400001);assert(events.empty());
auto& p=s.windowProps[{0x400001,"WM_PROTOCOLS"}];p.type="ATOM";p.format=32;uint32_t protocols[]={0x301,0x401};p.data.assign((uint8_t*)protocols,(uint8_t*)protocols+sizeof(protocols));c.sendTakeFocus(0x400001);
assert(events.size()==1 && events[0].size()==32);assert(events[0][0]==(33|128) && events[0][1]==32 && events[0][2]==0x34 && events[0][3]==0x12);assert(word(4)==0x400001 && word(8)==0x402 && word(12)==0x401 && word(16)==123456);
s.windows[0x400001].overrideRedirect=true;c.sendTakeFocus(0x400001);s.windows[0x400001].overrideRedirect=false;s.windows[0x400001].isRoot=true;c.sendTakeFocus(0x400001);c.sendTakeFocus(0x999);assert(events.size()==1);
s.windows[0x400001].isRoot=false;p.format=8;c.sendTakeFocus(0x400001);p.format=32;p.type="STRING";c.sendTakeFocus(0x400001);p.type="ATOM";p.data.resize(4);c.sendTakeFocus(0x400001);assert(events.size()==1);}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
