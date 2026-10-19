import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
test('real X11 pointer handlers preserve exclusive ownership and release on unmap/destroy', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwireconnection.cpp', import.meta.url), 'utf8');
  const handlers = ['X_GrabPointer','X_UngrabPointer','X_UnmapWindow','X_DestroyWindow'].map(name => {
    const body = src.match(new RegExp(`case ${name}: \\{[\\s\\S]*?\\n        \\}`))?.[0];
    assert.ok(body, name); return body;
  }).join('\n');
  const dir = mkdtempSync(join(tmpdir(),'bw64-pointer-grab-'));
  try {
    writeFileSync(join(dir,'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <map>
enum{X_GrabPointer=26,X_UngrabPointer=27,X_UnmapWindow=10,X_DestroyWindow=4};
struct Window{bool mapped=false,isRoot=false;};
struct XWireServer{std::mutex regMutex;std::map<uint32_t,Window> windows;uint32_t presentWindow=0,focusWindow=0,pointerGrabClient=0,pointerGrabWindow=0,pointerConfineWindow=0;void schedulePresent(){}static XWireServer& instance(){static XWireServer s;return s;}};
uint8_t reply[32];void writeToClient(const uint8_t* p,size_t n){assert(n==32);memcpy(reply,p,n);}
uint32_t rd32(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return v;}
void updateWmState(uint32_t,uint32_t){}void ensureWindow(){}
void dispatch(int op,uint32_t clientIdBase,uint32_t window,uint32_t confine=0){uint8_t bytes[24]={};memcpy(bytes+4,&window,4);memcpy(bytes+12,&confine,4);const uint8_t* req=bytes;uint16_t sequence=0x1234;switch(op){${handlers}}}
int main(){
 auto& s=XWireServer::instance();s.windows[0x260]={true,true};s.windows[0x400001]={true,false};s.windows[0x600001]={false,false};
 dispatch(X_GrabPointer,0x400000,0x600001);assert(reply[1]==3 && s.pointerGrabClient==0);
 dispatch(X_GrabPointer,0x400000,0x400001);assert(reply[0]==1 && reply[1]==0 && reply[2]==0x34 && reply[3]==0x12 && s.pointerGrabClient==0x400000);
 assert(s.pointerConfineWindow==0);
 dispatch(X_GrabPointer,0x400000,0x400001,0x400001);assert(reply[1]==0 && s.pointerConfineWindow==0x400001);
 dispatch(X_GrabPointer,0x600000,0x260);assert(reply[1]==1 && s.pointerGrabClient==0x400000);
 dispatch(X_UngrabPointer,0x600000,0);assert(s.pointerGrabClient==0x400000);
 dispatch(X_GrabPointer,0x400000,0x260);assert(reply[1]==0 && s.pointerGrabWindow==0x260);
 dispatch(X_UngrabPointer,0x400000,0);assert(s.pointerGrabClient==0 && s.pointerGrabWindow==0 && s.pointerConfineWindow==0);
 dispatch(X_GrabPointer,0x400000,0x400001,0x400001);dispatch(X_UnmapWindow,0x400000,0x400001);assert(s.pointerGrabClient==0 && s.pointerConfineWindow==0);
 s.windows[0x400001].mapped=true;dispatch(X_GrabPointer,0x400000,0x400001,0x400001);dispatch(X_DestroyWindow,0x400000,0x400001);assert(s.pointerGrabClient==0 && s.pointerConfineWindow==0 && !s.windows.count(0x400001));
}
`);
    execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);
    execFileSync(join(dir,'check'));
  } finally {rmSync(dir,{recursive:true,force:true});}
});

test('confined grabs request SDL relative input on the main thread and release it without blocking', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwirepresentSDL.cpp', import.meta.url), 'utf8');
  const helper = src.match(/static void syncXWirePointerCapture\(\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(helper);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-pointer-capture-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <mutex>
#include <atomic>
#include <thread>
#include <future>
std::atomic<bool> manualPointerCapture{false};
struct XWireServer{std::mutex regMutex;unsigned pointerGrabClient=0,pointerConfineWindow=0;static XWireServer& instance(){static XWireServer s;return s;}};
enum{SDL_FALSE=0,SDL_TRUE=1};
int relative=0,calls=0;bool focused=true;
int SDL_GetRelativeMouseMode(){return relative;}
void* SDL_GetMouseFocus(){return focused ? (void*)1 : nullptr;}
int SDL_SetRelativeMouseMode(int mode){++calls;relative=mode;return 0;}
${helper}
int main(){
 auto& s=XWireServer::instance();
 s.pointerGrabClient=1;syncXWirePointerCapture();assert(calls==0 && !relative);
 s.pointerConfineWindow=2;focused=false;syncXWirePointerCapture();assert(calls==0);
 focused=true;syncXWirePointerCapture();assert(calls==1 && relative);
 syncXWirePointerCapture();assert(calls==1);
 s.pointerGrabClient=0;syncXWirePointerCapture();assert(calls==2 && !relative);
 s.pointerGrabClient=1;
 std::promise<void> held,release;auto released=release.get_future();
 std::thread owner([&]{std::lock_guard<std::mutex> lock(s.regMutex);held.set_value();released.wait();});
 held.get_future().wait();syncXWirePointerCapture();assert(calls==2);release.set_value();owner.join();
 syncXWirePointerCapture();assert(calls==3 && relative);
 s.pointerConfineWindow=0;syncXWirePointerCapture();assert(calls==4 && !relative);
 manualPointerCapture=true;syncXWirePointerCapture();assert(calls==5 && relative);
 manualPointerCapture=false;syncXWirePointerCapture();assert(calls==6 && !relative);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, {recursive:true, force:true}); }
});

test('cursor warps update the queried position and relative baseline without injecting hardware motion', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwireconnection.cpp', import.meta.url), 'utf8');
  const handler = src.match(/case X_WarpPointer: \{[\s\S]*?\n        \}/)?.[0];
  assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-pointer-warp-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <map>
struct Window{int x=0,y=0;};
struct XWireServer{std::mutex regMutex;std::map<uint32_t,Window> windows;static XWireServer& instance(){static XWireServer s;return s;}};
struct Sink{int x=100,y=80,warps=0;void lastPointer(int& a,int& b,uint32_t& s){a=x;b=y;s=0;}void warpPointer(int a,int b){x=a;y=b;++warps;}} sink;
Sink* g_xwirePresentSink=&sink;
uint32_t rd32(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return v;}
uint16_t rd16(const uint8_t* p){uint16_t v;memcpy(&v,p,2);return v;}
enum{X_WarpPointer=41};int rawX=0,rawY=0,relX=0,relY=0;bool relInit=false;
void warp(uint32_t source,uint32_t dest,int16_t x,int16_t y,int16_t sx=0,int16_t sy=0,uint16_t w=0,uint16_t h=0){
 uint8_t bytes[24]={};memcpy(bytes+4,&source,4);memcpy(bytes+8,&dest,4);memcpy(bytes+12,&sx,2);memcpy(bytes+14,&sy,2);memcpy(bytes+16,&w,2);memcpy(bytes+18,&h,2);memcpy(bytes+20,&x,2);memcpy(bytes+22,&y,2);const auto* req=bytes;switch(X_WarpPointer){${handler}}
}
int main(){
 auto& s=XWireServer::instance();s.windows[1]={0,0};s.windows[2]={20,30};
 warp(1,1,512,320);assert(sink.x==512 && sink.y==320 && sink.warps==1 && rawX==512 && rawY==320 && relX==512 && relY==320 && relInit);
 warp(0,0,-12,4);assert(sink.x==500 && sink.y==324);
 warp(0,2,10,15);assert(sink.x==30 && sink.y==45);
 warp(2,1,700,700,0,0,5,5);assert(sink.x==30 && sink.y==45 && sink.warps==3);
 warp(99,1,700,700);assert(sink.warps==3);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, {recursive:true, force:true}); }
});

test('SDL relative movement advances the queried cursor position after recentering', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwirepresentSDL.cpp', import.meta.url), 'utf8');
  const motion = src.match(/case SDL_MOUSEMOTION:[\s\S]*?\n                break;/)?.[0];
  const warp = src.match(/    void warpPointer\(int x, int y\) override \{[\s\S]*?\n    \}/)?.[0].replace(' override', '');
  assert.ok(motion); assert.ok(warp);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-relative-position-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <mutex>
struct XWireInputEvent{enum{EvMotion,EvMotionRel}type;int x=0,y=0,state=0;bool warped=false;};
enum{SDL_MOUSEMOTION=1};bool relative=false;bool SDL_GetRelativeMouseMode(){return relative;}int sdlModState(){return 0;}
struct Sink{std::mutex pointerMutex;int lastX=0,lastY=0;XWireInputEvent last;void push(XWireInputEvent e){last=e;}
${warp}
void move(int x,int y,int dx,int dy){struct{struct{int x,y,xrel,yrel;}motion;}e={{x,y,dx,dy}};XWireInputEvent ie;switch(SDL_MOUSEMOTION){${motion}}}
};
int main(){Sink s;s.move(100,80,100,80);assert(s.lastX==100 && s.lastY==80 && s.last.type==XWireInputEvent::EvMotion);
 s.warpPointer(512,320);relative=true;s.move(0,0,20,-5);assert(s.lastX==532 && s.lastY==315 && s.last.x==20 && s.last.y==-5 && s.last.type==XWireInputEvent::EvMotionRel);
 s.warpPointer(512,320);s.move(0,0,0,0);assert(s.lastX==512 && s.lastY==320);
 relative=false;s.move(200,160,20,30);assert(s.lastX==200 && s.lastY==160 && s.last.type==XWireInputEvent::EvMotion);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, {recursive:true, force:true}); }
});
