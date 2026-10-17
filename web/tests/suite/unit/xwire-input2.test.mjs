import test from 'node:test';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';

test('XI2 replies and raw motion conform to Xorg wire structures, with bounded mask parsing', () => {
  const root = resolve(import.meta.dirname, '../../../..');
  const dir = mkdtempSync(join(tmpdir(), 'bw64-xinput2-'));
  try {
    const source = join(dir, 'check.cpp');
    writeFileSync(source, `
#include <X11/Xproto.h>
#include <X11/extensions/XI2proto.h>
#include <X11/extensions/XIproto.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include "source/x11wire/xwireinput2.h"
template<class T> T decode(const std::vector<uint8_t>& bytes, size_t offset=0) {
 assert(offset+sizeof(T)<=bytes.size()); T value; memcpy(&value,bytes.data()+offset,sizeof(T)); return value;
}
template<class T> XWireInput2::Response ask(XWireInput2& input,T& req) {
 return input.request(reinterpret_cast<uint8_t*>(&req),sizeof(req),0x1234);
}
int main() {
 XWireInput2 input;
 xXIQueryVersionReq version{}; version.ReqType=X_XIQueryVersion;version.major_version=2;version.minor_version=2;
 auto r=ask(input,version);assert(!r.error);auto v=decode<xXIQueryVersionReply>(r.bytes);
 assert(v.major_version==2 && v.minor_version==1 && v.sequenceNumber==0x1234 && v.length==0);
 version.major_version=1;assert(ask(input,version).error==BadValue);
 xXIGetClientPointerReq cp{};cp.ReqType=X_XIGetClientPointer;
 auto p=decode<xXIGetClientPointerReply>(ask(input,cp).bytes);assert(p.set && p.deviceid==2);
 xGetExtensionVersionReq ext{};ext.ReqType=X_GetExtensionVersion;
 auto e=decode<xGetExtensionVersionReply>(ask(input,ext).bytes);assert(e.present && e.major_version==2);
 xXIQueryDeviceReq query{};query.ReqType=X_XIQueryDevice;query.deviceid=2;
 r=ask(input,query);auto reply=decode<xXIQueryDeviceReply>(r.bytes);assert(reply.num_devices==1 && reply.length*4+32==r.bytes.size());
 auto device=decode<xXIDeviceInfo>(r.bytes,32);assert(device.deviceid==2 && device.use==XIMasterPointer && device.attachment==3 && device.num_classes==3);
 size_t offset=32+sizeof(device)+((device.name_len+3)&~3);
 auto button=decode<xXIButtonInfo>(r.bytes,offset);assert(button.type==XIButtonClass && button.num_buttons==3);offset+=button.length*4;
 for(unsigned axis=0;axis<2;axis++) {auto a=decode<xXIValuatorInfo>(r.bytes,offset);assert(a.type==XIValuatorClass && a.number==axis && a.mode==XIModeRelative && a.min.integral==0 && a.max.integral==0);offset+=a.length*4;}
 assert(offset==r.bytes.size());query.deviceid=XIAllMasterDevices;r=ask(input,query);assert(decode<xXIQueryDeviceReply>(r.bytes).num_devices==2);
 query.deviceid=XIAllDevices;r=ask(input,query);assert(decode<xXIQueryDeviceReply>(r.bytes).num_devices==4);
 query.deviceid=4;r=ask(input,query);device=decode<xXIDeviceInfo>(r.bytes,32);assert(device.use==XISlavePointer && device.attachment==2);
 query.deviceid=99;assert(ask(input,query).error==128);
 struct Selection {xXISelectEventsReq header; xXIEventMask mask; uint32_t bits;} selection{};
 selection.header.ReqType=X_XISelectEvents;selection.header.win=0x260;selection.header.num_masks=1;
 selection.mask.deviceid=XIAllMasterDevices;selection.mask.mask_len=1;selection.bits=XI_RawMotionMask;
 assert(!input.request(reinterpret_cast<uint8_t*>(&selection),sizeof(selection),1).error);
 assert(input.selectsRawMotion(0x260) && !input.selectsRawMotion(0x261));
 assert(!input.selectsRawMotion(0x260,4));
 selection.mask.mask_len=0xffff;assert(input.request(reinterpret_cast<uint8_t*>(&selection),sizeof(selection),1).error==BadLength);
 assert(input.selectsRawMotion(0x260));selection.mask.mask_len=1;selection.bits=0;
 assert(!input.request(reinterpret_cast<uint8_t*>(&selection),sizeof(selection),1).error && !input.selectsRawMotion(0x260));
 auto motion=XWireInput2::motion(0x1234,789,1500,-1500);auto raw=decode<xXIRawEvent>(motion);
 assert(raw.type==GenericEvent && raw.extension==152 && raw.evtype==XI_RawMotion && raw.deviceid==2 && raw.time==789 && raw.valuators_len==1);
 assert(raw.length*4+32==motion.size() && decode<uint32_t>(motion,32)==3);
 assert(decode<FP3232>(motion,36).integral==1500 && decode<FP3232>(motion,44).integral==-1500);
 assert(decode<FP3232>(motion,52).integral==1500 && decode<FP3232>(motion,60).integral==-1500);
 assert(raw.sourceid==4);raw=decode<xXIRawEvent>(XWireInput2::motion(1,2,3,4,4));assert(raw.deviceid==4 && raw.sourceid==4);
 selection.mask.deviceid=XIAllDevices;selection.bits=XI_RawMotionMask;
 assert(!input.request(reinterpret_cast<uint8_t*>(&selection),sizeof(selection),1).error);
 assert(input.selectsRawMotion(0x260,2) && input.selectsRawMotion(0x260,4));
 version.major_version=2;version.minor_version=0;assert(!ask(input,version).error);
 assert(input.rawDevices(0x260,true)==std::vector<uint16_t>{4});
 assert((input.rawDevices(0x260,false)==std::vector<uint16_t>{4,2}));
 version.minor_version=2;assert(!ask(input,version).error);
 assert((input.rawDevices(0x260,true)==std::vector<uint16_t>{4,2}));
}
`);
    execFileSync('c++', ['-std=c++17', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', root, source, '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
