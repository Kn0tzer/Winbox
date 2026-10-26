import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XGetWindowAttributes reports guest geometry, visual, masks, and map state', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_GetWindowAttributes\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-window-attrs-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
using U32=uint32_t; using S32=int32_t;
constexpr U32 BadWindow=3,True=1,False=0; constexpr S32 IsUnmapped=0,IsUnviewable=1,IsViewable=2;
struct CPU { struct KMemory* memory; U32 args[4]{}; U32 eax=0; struct KThread* thread; };
#define ARG1 cpu->args[1]
#define ARG2 cpu->args[2]
#define ARG3 cpu->args[3]
#define EAX cpu->eax
struct KMemory { unsigned char b[512]{}; U32 readd(U32 a){U32 v;std::memcpy(&v,b+a,4);return v;} void writed(U32 a,U32 v){std::memcpy(b+a,&v,4);} void memcpy(U32 a,const void*p,U32 n){std::memcpy(b+a,p,n);} void memcpy(U32 a,const void*p,size_t n){std::memcpy(b+a,p,n);} };
struct Visual { int tag=1; }; using VisualPtr=std::shared_ptr<Visual>; static VisualPtr testVisual=std::make_shared<Visual>();
struct Depth{}; struct KThread;
struct Display { static void iterateVisuals(KThread*,U32,std::function<bool(S32,U32,Depth*,Visual*)> cb){Depth d;cb(0,0x234,&d,testVisual.get());} };
struct ColorMap { U32 id=0x77; }; using XColorMapPtr=ColorMap*;
struct XSetWindowAttributes { S32 bit_gravity=1,win_gravity=2,backing_store=1,save_under=1,event_mask=0x45,do_not_propagate_mask=0x18,override_redirect=1;U32 backing_planes=0xffffffff,backing_pixel=0x123456,colormap=0x77; };
struct DisplayData { U32 root=0x22;U32 getEventMask(U32){return 0x45;} }; using DisplayDataPtr=DisplayData*;
struct XWindow { ~XWindow(){delete colorMap;} U32 id=7;S32 x=13,y=17;U32 w=640,h=480,d=24,b=2;S32 c_class=1;XSetWindowAttributes a;VisualPtr v=testVisual;XColorMapPtr colorMap=new ColorMap();bool mapped(){return true;}bool isThisAndAncestorsMapped(){return true;}U32 width(){return w;}U32 height(){return h;}U32 getDepth(){return d;}void getPlacement(S32&ox,S32&oy,U32&ob){ox=x;oy=y;ob=b;}VisualPtr getVisual(){return v;}const XSetWindowAttributes&getAttributes(){return a;} }; using XWindowPtr=XWindow*;
struct KThread{};
void kpanic(const char*) { assert(false && "XGetWindowAttributes must not abort"); }
struct XServer { XWindow window;DisplayData display;static XServer* getServer(){static XServer s;return &s;}XWindow* getWindow(U32 id){return id==7?&window:nullptr;}DisplayData* getDisplayDataByAddressOfDisplay(KMemory*,U32){return &display;}XWindow* getRoot(){return &window;}U32 getAllEventMask(U32){return 0x5d;} };
${handler}
int main(){KMemory m;KThread t;CPU c{&m};c.thread=&t;c.args[1]=100;c.args[2]=7;c.args[3]=128;m.writed(100+0x8c,0x300);x11_GetWindowAttributes(&c);assert(c.eax==True);U32 words[23];std::memcpy(words,m.b+128,sizeof(words));assert(words[0]==13&&words[1]==17&&words[2]==640&&words[3]==480&&words[4]==2&&words[5]==24);assert(words[6]==0x234&&words[7]==0x22&&words[8]==1);assert(words[12]==0xffffffff&&words[13]==0x123456&&words[14]==1&&words[15]==0x77);assert(words[16]==1&&words[17]==2&&words[18]==0x5d&&words[19]==0x45&&words[20]==0x18&&words[21]==1&&words[22]==0x300);c.args[2]=8;c.eax=0;x11_GetWindowAttributes(&c);assert(c.eax==BadWindow);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('XServer all_event_masks unions event subscriptions across displays', () => {
  const src = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');
  const method = src.match(/U32 XServer::getAllEventMask\(U32 wndId\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(method);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-all-event-mask-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <vector>
using U32=uint32_t;
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(x) ((void)0)
struct Mutex{};struct Entry{U32 key;};struct DisplayData{U32 mask;U32 getEventMask(U32){return mask;}};using DisplayDataPtr=DisplayData*;
struct Registry{std::vector<Entry> entries;DisplayData* values[3]{};DisplayData* get(U32 id){return id<3?values[id]:nullptr;}auto begin(){return entries.begin();}auto end(){return entries.end();}};
struct XServer{Mutex displayMutex;Registry displays;U32 getAllEventMask(U32);};
${method}
int main(){XServer s;DisplayData a{0x02},b{0x40};s.displays.entries={{1},{2},{3}};s.displays.values[1]=&a;s.displays.values[2]=&b;assert(s.getAllEventMask(9)==0x42);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
