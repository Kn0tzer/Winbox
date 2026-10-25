import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('Wine message I/O preserves UDP packets and receives the TCP pipe secret without relocking', () => {
  const source = readFileSync(join(repoRoot, 'source/kernel/knativesocket.cpp'), 'utf8');
  const start = source.indexOf('U32 KNativeSocketObject::sendmsg(');
  const end = source.indexOf('U32 KNativeSocketObject::sendto(', start);
  const takeStart = source.indexOf('U32 KNativeSocketObject::takeLoopbackPacket(');
  const takeEnd = source.indexOf('U32 KNativeSocketObject::recvLoopbackFrom(', takeStart);
  const take = takeStart >= 0 ? extractSourceRange(source, takeStart, takeEnd) : '';
  const streamStart = source.indexOf('U32 KNativeSocketObject::recvLoopbackStream(');
  const streamEnd = source.indexOf('void KNativeSocketObject::closeLoopbackStream(', streamStart);
  const dir = mkdtempSync(join(tmpdir(), 'bw-udp-msg-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
using U32=uint32_t;using U16=uint16_t;using U8=uint8_t;using S32=int32_t;
#define BOXEDWINE_MULTI_THREADED 1
#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c) std::unique_lock<std::mutex> boxedWineCriticalSection((c)->m);
#define BOXEDWINE_CONDITION_WAIT(...) assert(false)
#define LOG_SOCK(...)
#define kwarn_fmt(...)
#define kwarn(...)
constexpr U32 K_PAGE_SIZE=4096,K_MSG_OOB=1,K_MSG_PEEK=2,K_MSG_DONTWAIT=64,K_MSG_NOSIGNAL=0x4000,K_SOCK_STREAM=1,K_SOL_SOCKET=1,K_SCM_RIGHTS=1;
constexpr U32 K_EMSGSIZE=90,K_EFAULT=14,K_EWOULDBLOCK=11,K_EINVAL=22,K_EAFNOSUPPORT=97,K_AF_INET=2,K_ENOTCONN=107,K_EINTR=4;
struct MsgHdr{U32 msg_name,msg_namelen,msg_iov,msg_iovlen,msg_control,msg_controllen,msg_flags;};
struct CMsgHdr{U32 cmsg_level,cmsg_type,cmsg_len;};
struct KMemory{
 U8 bytes[32768]={};
 bool canRead(U32 a,U32 n){return a<=sizeof(bytes)&&n<=sizeof(bytes)-a;}bool canWrite(U32 a,U32 n){return canRead(a,n);}
 U32 readd(U32 a){U32 v;std::memcpy(&v,bytes+a,4);return v;}U16 readw(U32 a){U16 v;std::memcpy(&v,bytes+a,2);return v;}
 void writed(U32 a,U32 v){std::memcpy(bytes+a,&v,4);}void writew(U32 a,U16 v){std::memcpy(bytes+a,&v,2);}
 void memcpy(void* out,U32 a,U32 n){assert(canRead(a,n));std::memcpy(out,bytes+a,n);}
 void memcpy(U32 a,const void* in,U32 n){assert(canWrite(a,n));std::memcpy(bytes+a,in,n);}
};
struct KThread{KMemory* memory;U32 id=1;bool terminating=false;static KThread* currentThread(){return nullptr;}};using KFileDescriptorPtr=int;
void readMsgHdr(KThread* t,U32 a,MsgHdr* h){t->memory->memcpy(h,a,sizeof(*h));}void readCMsgHdr(KThread*,U32,CMsgHdr*){}
void readSockAddrIn(sockaddr_in* a,KMemory* m,U32 p){m->memcpy(a,p,16);}void writeSockAddrIn(sockaddr_in* a,KMemory* m,U32 p){m->memcpy(p,a,16);}
struct Condition{std::mutex m;};
struct KNativeSocketObject:std::enable_shared_from_this<KNativeSocketObject>{
 int nativeSocket=-1;U32 type=2,error=0;bool loopbackDatagram=true,loopbackStream=false,loopbackStreamConnected=false,blocking=false;
 struct LoopbackPacket{std::vector<U8> bytes;U32 sourceAddress=0x7f000001;U16 sourcePort=1234;};
 std::shared_ptr<Condition> readingCond=std::make_shared<Condition>();std::deque<U8> loopbackStreamBytes;std::mutex loopbackStreamMutex;bool loopbackStreamPeerEof=false,inClosed=false;
 std::deque<LoopbackPacket> loopbackPackets;std::mutex loopbackPacketsMutex;std::vector<U8> sent;bool destinationSeen=false;
 U32 sendmsg(KThread*,const KFileDescriptorPtr&,U32,U32);U32 recvmsg(KThread*,const KFileDescriptorPtr&,U32,U32);U32 takeLoopbackPacket(LoopbackPacket&,U32);
 U32 sendLoopbackBytes(KThread*,const U8* b,U32 n,U32,const sockaddr_in* d,bool has){sent.assign(b,b+n);destinationSeen=has;if(has)assert(d->sin_family==AF_INET);return n;}
 U32 sendLoopbackStream(const U8*,U32,U32){assert(false);return 0;}U32 recvLoopbackStream(U8*,U32,U32);
};
U32 handleNativeSocketError(const std::shared_ptr<KNativeSocketObject>&,bool){return U32(-5);}
${take}
${extractSourceRange(source, streamStart, streamEnd)}
${extractSourceRange(source, start, end)}
int main(){KMemory m;KThread t{&m};auto s=std::make_shared<KNativeSocketObject>();
 auto header=[&](U32 name=0){MsgHdr h{name,name?16u:0u,128,2,0,0,0};m.memcpy(0,&h,sizeof(h));m.writed(128,512);m.writed(132,2);m.writed(136,528);m.writed(140,3);};
 header();assert(s->recvmsg(&t,0,0,0)==U32(-K_EWOULDBLOCK));
 s->loopbackPackets={{{1,2,3,4,5,6}},{{7,8}}};header(256);
 assert(s->recvmsg(&t,0,0,K_MSG_PEEK)==5);assert(s->loopbackPackets.size()==2);assert(m.bytes[512]==1&&m.bytes[513]==2&&m.bytes[528]==3&&m.bytes[530]==5);assert(m.readd(24)==0x20);assert(m.readw(256)==AF_INET);
 header();assert(s->recvmsg(&t,0,0,0)==5);assert(s->loopbackPackets.size()==1);
 header();assert(s->recvmsg(&t,0,0,0)==2);assert(s->loopbackPackets.empty());assert(m.bytes[512]==7&&m.bytes[513]==8);
 header(256);sockaddr_in addr{};addr.sin_family=AF_INET;m.memcpy(256,&addr,16);m.bytes[512]='h';m.bytes[513]='e';m.bytes[528]='l';m.bytes[529]='l';m.bytes[530]='o';
 assert(s->sendmsg(&t,0,0,0)==5);assert(s->sent==std::vector<U8>({'h','e','l','l','o'})&&s->destinationSeen);
 header();assert(s->sendmsg(&t,0,0,0)==5&&!s->destinationSeen);
 s->loopbackDatagram=false;s->loopbackStream=true;s->loopbackStreamConnected=true;s->type=K_SOCK_STREAM;
 s->loopbackStreamBytes={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};header();m.writed(12,1);m.writed(132,16);
 assert(s->recvmsg(&t,0,0,0)==16&&m.bytes[527]==15);assert(s->loopbackStreamBytes.empty());
 s->loopbackDatagram=true;s->loopbackStream=false;
 header();m.writed(12,1025);assert(s->recvmsg(&t,0,0,0)==U32(-K_EMSGSIZE));
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 3000 });
    assert.equal(run.status, 0, run.error?.message || run.stderr);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
