import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const resources = extractSourceSlice(source, 'static void x11_XRRGetScreenResources(CPU* cpu) {', '// XRRScreenResources* XRRGetScreenResourcesCurrent', { sourceName: 'source/x11/x11common.cpp' });
const crtc = extractSourceSlice(source, 'static void x11_XRRGetCrtcInfo(CPU* cpu) {', '// XRROutputInfo* XRRGetOutputInfo', { sourceName: 'source/x11/x11common.cpp' });
const output = extractSourceSlice(source, 'static void x11_XRRGetOutputInfo(CPU* cpu) {', '// int XRRGetOutputProperty', { sourceName: 'source/x11/x11common.cpp' });
const sizeRange = extractSourceSlice(source, 'static void x11_XRRGetScreenSizeRange(CPU* cpu) {', '// Status XRRSetCrtcConfig', { sourceName: 'source/x11/x11common.cpp' });
const setCrtc = extractSourceSlice(source, 'static void x11_XRRSetCrtcConfig(CPU* cpu) {', '// void XRRSetScreenSize', { sourceName: 'source/x11/x11common.cpp' });
const setScreenSize = extractSourceSlice(source, 'static void x11_XRRSetScreenSize(CPU* cpu) {', '// void XRRSelectInput', { sourceName: 'source/x11/x11common.cpp' });
const freeCrtc = extractSourceSlice(source, 'static void x11_XRRFreeCrtcInfo(CPU* cpu) {', '// void XRRFreeOutputInfo', { sourceName: 'source/x11/x11common.cpp' });
const freeOutput = extractSourceSlice(source, 'static void x11_XRRFreeOutputInfo(CPU* cpu) {', '// void XRRFreeScreenResources', { sourceName: 'source/x11/x11common.cpp' });
const freeResources = extractSourceSlice(source, 'static void x11_XRRFreeScreenResources(CPU* cpu) {', '// XRRCrtcInfo* XRRGetCrtcInfo', { sourceName: 'source/x11/x11common.cpp' });

test('XRandR reports the advertised modes and a matching connected CRTC/output', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-xrandr-resources-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
using U32=uint32_t; using S32=int32_t; using Time=U32; using RRMode=U32; using RRCrtc=U32; using RROutput=U32; using XID=U32; using XRRModeFlags=U32;
enum { RR_Rotate_0=1, RRSetConfigSuccess=0, RRSetConfigFailed=3, False=0, True=1 };
struct XRRScreenSize{S32 width,height,mwidth,mheight;};
struct XRRModeInfo{RRMode id;U32 width,height,dotClock,hSyncStart,hSyncEnd,hTotal,hSkew,vSyncStart,vSyncEnd,vTotal,name,nameLength;XRRModeFlags modeFlags;};
struct XRRScreenResources{Time timestamp,configTimestamp;S32 ncrtc;U32 crtcs;S32 noutput;U32 outputs;S32 nmode;U32 modes;};
struct XRRCrtcInfo{Time timestamp;S32 x,y;U32 width,height;RRMode mode;U32 rotation;S32 noutput;U32 outputs;U32 rotations;S32 npossible;U32 possible;};
struct XRROutputInfo{Time timestamp;RRCrtc crtc;U32 name;S32 nameLen;U32 mm_width,mm_height,connection,subpixel_order;S32 ncrtc;U32 crtcs;S32 nclone;U32 clones;S32 nmode,npreferred;U32 modes;};
struct KMemory{uint8_t b[65536]{};void writed(U32 a,U32 v){::memcpy(b+a,&v,4);}U32 readd(U32 a){U32 v;::memcpy(&v,b+a,4);return v;}void* getRamPtr(U32 a,U32,bool){return b+a;}void memcpy(U32 a,const void*p,U32 n){::memcpy(b+a,p,n);}};
struct KProcess{U32 next=1024;std::set<U32> freed;U32 alloc(struct KThread*,U32 n){U32 a=next;next=(next+n+3)&~3;return a;}void free(U32 a){freed.insert(a);}};
struct KThread{KMemory* memory;KProcess* process;};
void klog(const char*){}
struct CPU{KMemory* memory;KThread* thread;U32 args[12]{};U32 eax=0;U32 peek32(int n){return args[n];}};
#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define ARG6 cpu->peek32(6)
#define ARG7 cpu->peek32(7)
#define ARG8 cpu->peek32(8)
#define ARG9 cpu->peek32(9)
#define ARG10 cpu->peek32(10)
#define EAX cpu->eax
struct DisplayData{};using DisplayDataPtr=DisplayData*;
struct Screen{U32 w=1920,h=1080;U32 screenWidth(){return w;}U32 screenHeight(){return h;}};using KNativeScreenPtr=Screen*;struct KNativeSystem{static KNativeScreenPtr getScreen(){static Screen s;return &s;}};
struct XServer{DisplayData d;static XServer* getServer(){static XServer s;return &s;}DisplayDataPtr getDisplayDataByAddressOfDisplay(KMemory*,U32 dpy){return dpy==7?&d:nullptr;}void* getWindow(U32 w){return w==1?this:nullptr;}void changeScreen(U32 w,U32 h){KNativeSystem::getScreen()->w=w;KNativeSystem::getScreen()->h=h;}};
struct KSystem{static U32 getMilliesSinceStart(){return 1234;}};
U32 XrrGetSizes(KThread* t,const DisplayDataPtr&,U32,U32 countAddress){U32 a=256;t->memory->writed(a,1920);t->memory->writed(a+4,1080);t->memory->writed(a+16,1280);t->memory->writed(a+20,720);t->memory->writed(a+32,800);t->memory->writed(a+36,600);t->memory->writed(countAddress,3);return a;}
${resources}
${crtc}
${output}
${freeCrtc}
${freeOutput}
${freeResources}
${sizeRange}
${setCrtc}
${setScreenSize}
int main(){KMemory m;KProcess p;KThread t{&m,&p};CPU c{};c.memory=&m;c.thread=&t;c.args[1]=7;c.args[2]=1;x11_XRRGetScreenResources(&c);assert(c.eax);U32 ra=c.eax;auto*r=(XRRScreenResources*)m.getRamPtr(ra,sizeof(XRRScreenResources),true);assert(r->ncrtc==1&&r->noutput==1&&r->nmode==3);auto* modes=(XRRModeInfo*)m.getRamPtr(r->modes,sizeof(XRRModeInfo)*3,true);assert(modes[0].id==1&&modes[0].width==1920&&modes[0].height==1080);assert(modes[1].id==2&&modes[1].width==1280&&modes[1].height==720);assert(m.readd(r->crtcs)==1&&m.readd(r->outputs)==1);
 c.args[2]=ra;c.args[3]=1;x11_XRRGetCrtcInfo(&c);assert(c.eax);auto*ci=(XRRCrtcInfo*)m.getRamPtr(c.eax,sizeof(XRRCrtcInfo),true);assert(ci->width==1920&&ci->height==1080&&ci->mode==1&&ci->rotation==RR_Rotate_0&&ci->noutput==1&&m.readd(ci->outputs)==1);
 x11_XRRGetOutputInfo(&c);assert(c.eax);U32 outputInfoAddress=c.eax;auto*oi=(XRROutputInfo*)m.getRamPtr(c.eax,sizeof(XRROutputInfo),true);assert(oi->crtc==1&&oi->nmode==3&&oi->npreferred==1&&oi->nameLen==11);assert(std::memcmp(m.getRamPtr(oi->name,12,true),"Boxedwine-0",12)==0);
 c.args[2]=1;c.args[3]=300;c.args[4]=304;c.args[5]=308;c.args[6]=312;x11_XRRGetScreenSizeRange(&c);assert(c.eax==True);assert(m.readd(300)==800&&m.readd(304)==600&&m.readd(308)==1920&&m.readd(312)==1080);
 c.args[2]=ra;c.args[3]=1;c.args[4]=1234;c.args[5]=0;c.args[6]=0;c.args[7]=2;c.args[8]=RR_Rotate_0;c.args[9]=400;c.args[10]=1;m.writed(400,1);x11_XRRSetCrtcConfig(&c);assert(c.eax==RRSetConfigSuccess);assert(KNativeSystem::getScreen()->screenWidth()==1280&&KNativeSystem::getScreen()->screenHeight()==720);
 c.args[7]=99;x11_XRRSetCrtcConfig(&c);assert(c.eax==RRSetConfigFailed);assert(KNativeSystem::getScreen()->screenWidth()==1280);c.args[7]=2;c.args[8]=2;x11_XRRSetCrtcConfig(&c);assert(c.eax==RRSetConfigFailed);assert(KNativeSystem::getScreen()->screenWidth()==1280);
 c.args[2]=1;c.args[3]=800;c.args[4]=600;x11_XRRSetScreenSize(&c);assert(KNativeSystem::getScreen()->screenWidth()==800&&KNativeSystem::getScreen()->screenHeight()==600);
 c.args[2]=ra;c.args[3]=1;x11_XRRGetCrtcInfo(&c);U32 crtcInfoAddress=c.eax;auto*ci2=(XRRCrtcInfo*)m.getRamPtr(crtcInfoAddress,sizeof(XRRCrtcInfo),true);U32 crtcOutputs=ci2->outputs,crtcPossible=ci2->possible;c.args[1]=crtcInfoAddress;x11_XRRFreeCrtcInfo(&c);assert(p.freed.count(crtcOutputs)&&p.freed.count(crtcPossible)&&p.freed.count(crtcInfoAddress));
 c.args[1]=outputInfoAddress;auto*oi2=(XRROutputInfo*)m.getRamPtr(outputInfoAddress,sizeof(XRROutputInfo),true);U32 outputName=oi2->name,outputCrtcs=oi2->crtcs,outputModes=oi2->modes;x11_XRRFreeOutputInfo(&c);assert(p.freed.count(outputName)&&p.freed.count(outputCrtcs)&&p.freed.count(outputModes)&&p.freed.count(outputInfoAddress));
 c.args[1]=ra;U32 modeArray=r->modes,rootCrtcs=r->crtcs,rootOutputs=r->outputs,firstModeName=modes[0].name;x11_XRRFreeScreenResources(&c);assert(p.freed.count(firstModeName)&&p.freed.count(modeArray)&&p.freed.count(rootCrtcs)&&p.freed.count(rootOutputs)&&p.freed.count(ra));
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
