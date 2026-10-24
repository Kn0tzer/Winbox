import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const common = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const serverSource = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');
const select = extractSourceSlice(common, 'static void x11_XRRSelectInput(CPU* cpu) {', '// RROutput XRRGetOutputPrimary', { sourceName: 'source/x11/x11common.cpp' });
const changeScreen = extractSourceSlice(serverSource, 'void XServer::changeScreen(U32 width, U32 height) {', 'void XServer::processExit(', { sourceName: 'source/x11/xserver.cpp' });

test('XRRSelectInput receives screen-change events when the mode changes', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-xrandr-events-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
using U32=uint32_t;using S32=int32_t;using U8=uint8_t;using Time=U32;
enum{False=0,RRScreenChangeNotify=0,RRScreenChangeNotifyMask=1,RRCrtcChangeNotifyMask=2,RROutputChangeNotifyMask=4,RRNotify=1,RRNotify_CrtcChange=0,RRNotify_OutputChange=1,RR_Rotate_0=1,XRAND_Base=10000,XA_CARDINAL=6};
#define B(s) s
struct XRRScreenSize{S32 width,height,mwidth,mheight;};struct XEvent{U32 words[48]{};};
struct KMemory{U32 readd(U32){return 0;}};struct Proc{KMemory* memory=nullptr;};using KProcessPtr=std::shared_ptr<Proc>;
struct XrrData{U32 eventWindow=0,eventMask=0,sizesAddress=0,sizesCount=0;};
struct DisplayData{U32 displayAddress=77,serial=0;XrrData* xrrData=nullptr;std::weak_ptr<Proc> process;std::vector<XEvent> events;U32 getNextEventSerial(){return ++serial;}void putEvent(const XEvent&e){events.push_back(e);}};using DisplayDataPtr=DisplayData*;
struct KThread{KMemory*memory;Proc*process;};struct CPU{KMemory*memory;KThread*thread;U32 args[4]{};U32 eax=0;U32 peek32(int i){return args[i];}};
#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define EAX cpu->eax
struct XWindow{U32 id=5;void moveResize(U32,U32,U32,U32){}void setProperty(U32,U32,U32,U32,U8*,bool){}};using XWindowPtr=std::shared_ptr<XWindow>;
struct AtomServer{U32 internAtom(const char*,bool){return 9;}};
struct Entry{DisplayDataPtr value;};struct XServer{DisplayData d;XrrData rd;XWindowPtr root=std::make_shared<XWindow>();AtomServer*server;std::vector<Entry>displays;static XServer*getServer(){static XServer s;return &s;}std::shared_ptr<Proc> proc=std::make_shared<Proc>();XServer(){d.xrrData=&rd;d.process=proc;server=new AtomServer;displays.push_back({&d});}void changeScreen(U32,U32);DisplayDataPtr getDisplayDataByAddressOfDisplay(KMemory*,U32 id){return id==77?&d:nullptr;}XWindowPtr getWindow(U32 id){return id==5?root:nullptr;}};
struct KNativeSystem{static U32 width,height;static void changeScreenSize(U32 w,U32 h){width=w;height=h;}};U32 KNativeSystem::width=0;U32 KNativeSystem::height=0;
struct KSystem{static U32 getMilliesSinceStart(){return 123;}};
void klog(const char*){}
${select}
${changeScreen}
int main(){KMemory m;Proc p;KThread t{&m,&p};CPU c{};c.memory=&m;c.thread=&t;c.args[1]=77;c.args[2]=5;c.args[3]=RRScreenChangeNotifyMask|RRCrtcChangeNotifyMask|RROutputChangeNotifyMask;x11_XRRSelectInput(&c);auto*s=XServer::getServer();assert(s->rd.eventWindow==5&&s->rd.eventMask==(RRScreenChangeNotifyMask|RRCrtcChangeNotifyMask|RROutputChangeNotifyMask));s->changeScreen(1280,720);assert(KNativeSystem::width==1280&&KNativeSystem::height==720&&s->d.events.size()==3);const auto&e=s->d.events[0];assert(e.words[0]==XRAND_Base+RRScreenChangeNotify&&e.words[3]==77&&e.words[4]==5&&e.words[11]==1280&&e.words[12]==720&&e.words[10]==RR_Rotate_0);const auto&cEvent=s->d.events[1];assert(cEvent.words[0]==XRAND_Base+RRNotify&&cEvent.words[5]==RRNotify_CrtcChange&&cEvent.words[6]==1&&cEvent.words[7]==1);const auto&oEvent=s->d.events[2];assert(oEvent.words[0]==XRAND_Base+RRNotify&&oEvent.words[5]==RRNotify_OutputChange&&oEvent.words[6]==1&&oEvent.words[7]==1);c.args[3]=0;x11_XRRSelectInput(&c);s->changeScreen(800,600);assert(s->d.events.size()==3);}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
