import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
const source = readFileSync('source/kernel/knativesocket.cpp', 'utf8');
function run(code, sanitizer = false) {
  const dir = mkdtempSync(join(tmpdir(), 'bw64-native-socket-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), code);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', ...(sanitizer ? ['-fsanitize=address', '-g'] : []), join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const result = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000, env: { ...process.env, ASAN_OPTIONS: 'detect_leaks=0' } });
    assert.equal(result.status, 0, result.stderr);
  } finally { rmSync(dir, { recursive: true, force: true }); }
}

test('native sendmsg snapshots descriptors and rejects overflowing or excessive vectors', () => {
  const start = source.indexOf('U32 KNativeSocketObject::sendmsg(');
  const end = source.indexOf('    struct sockaddr_in dest', start);
  assert.ok(start >= 0 && end > start);
  run(`
#include <cassert>
#include <cstdint>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstring>
using U32=uint32_t;using U8=uint8_t;
constexpr U32 K_SOL_SOCKET=1,K_SCM_RIGHTS=1,K_EMSGSIZE=90,K_EFAULT=14;
#define kwarn_fmt(...)
#define kwarn(...)
struct MsgHdr {U32 msg_control=0,msg_controllen=0,msg_iov=256,msg_iovlen=2;};
struct CMsgHdr {U32 cmsg_level=0,cmsg_type=0,cmsg_len=0;};
static MsgHdr fixture;
struct KMemory {
 U32 words[4]={1024,4,2048,4},reads[4]={};bool mutate=false;U32 copies=0;
 U32 readd(U32 address){assert(address>=256 && address<272);U32 slot=(address-256)/4;U32 count=reads[slot]++;return mutate && slot%2 && count ? 65536 : words[slot];}
 void memcpy(U8* buffer,U32 address,U32 length){if(length>8192)throw std::runtime_error("oversized guest copy");copies++;std::memset(buffer,address==1024?'a':'b',length);}
};
struct KThread{KMemory* memory;};using KFileDescriptorPtr=int;
static void readMsgHdr(KThread*,U32,MsgHdr* output){*output=fixture;}
static void readCMsgHdr(KThread*,U32,CMsgHdr*){}
static std::vector<U8> captured;
struct KNativeSocketObject {U32 sendmsg(KThread*,const KFileDescriptorPtr&,U32,U32);};
${source.slice(start,end)}
 captured.assign(buffer,buffer+len);return len;
}
int main(){
 KNativeSocketObject socket;KMemory memory;KThread thread{&memory};
 assert(socket.sendmsg(&thread,0,0,0)==8);assert(captured==std::vector<U8>({'a','a','a','a','b','b','b','b'}));
 memory=KMemory{};memory.mutate=true;assert(socket.sendmsg(&thread,0,0,0)==8);assert(memory.reads[1]==1 && memory.reads[3]==1);
 memory=KMemory{};memory.words[1]=0xfffffff0u;memory.words[3]=32;assert(socket.sendmsg(&thread,0,0,0)==U32(-K_EMSGSIZE));assert(memory.copies==0);
 memory=KMemory{};fixture.msg_iovlen=1025;assert(socket.sendmsg(&thread,0,0,0)==U32(-K_EMSGSIZE));assert(memory.reads[0]==0);
 fixture.msg_iovlen=2;fixture.msg_iov=0xfffffff8u;assert(socket.sendmsg(&thread,0,0,0)==U32(-K_EFAULT));
 fixture.msg_iov=256;memory.words[0]=0xfffffffdu;assert(socket.sendmsg(&thread,0,0,0)==U32(-K_EFAULT));assert(memory.copies==0);
}
`);
});

test('native readiness retains conditions after a socket unregisters', () => {
  const start = source.indexOf('void updateWaitingList()');
  const end = source.indexOf('void setNativeBlocking(', start);
  assert.ok(start >= 0 && end > start);
  run(`
#include <cassert>
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using U32=uint32_t;
#define __EMSCRIPTEN__ 1
#define BOXEDWINE_MULTI_THREADED 1
#define kpanic_fmt(...) abort()
static int notifications=0;
struct Condition{U32 parents=1;U32 parentsCount(){return parents;}U32 waitCount(){return parents;}};
#define BOXEDWINE_CONDITION std::shared_ptr<Condition>
#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c)
#define BOXEDWINE_CONDITION_SIGNAL_ALL(c) do{assert(c);notifications++;}while(0)
static std::function<void()> afterUnlock;
struct Guard{~Guard(){if(afterUnlock)afterUnlock();}};
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) Guard JOIN(guard,__LINE__)
struct Socket{int nativeSocket;BOXEDWINE_CONDITION readingCond=std::make_shared<Condition>(),writingCond=std::make_shared<Condition>();Socket(int fd):nativeSocket(fd){writingCond->parents=0;}};
static std::vector<std::shared_ptr<Socket>> waitingNativeSockets;
static fd_set waitingReadset,waitingWriteset,waitingErrorset;static int maxSocketId,nativeSocketPipe[2];
${source.slice(start,end)}
int main(){
 int input[2];assert(pipe(input)==0);assert(pipe(nativeSocketPipe)==0);
 waitingNativeSockets.push_back(std::make_shared<Socket>(input[0]));assert(write(input[1],"x",1)==1);
 int unlocks=0;afterUnlock=[&]{if(++unlocks==2)waitingNativeSockets.clear();};
 assert(checkWaitingNativeSockets(0));assert(notifications==1);assert(waitingNativeSockets.empty());
 close(input[0]);close(input[1]);close(nativeSocketPipe[0]);close(nativeSocketPipe[1]);
}
`, true);
});

test('one native poller cannot cancel another poller subscription', () => {
  const start = source.indexOf('void KNativeSocketObject::waitForEvents(');
  const end = source.indexOf('U32 KNativeSocketObject::writeNative(', start);
  assert.ok(start >= 0 && end > start);
  run(`
#include <cassert>
#include <cstdint>
#include <memory>
#include <set>
#include <mutex>
using U32=uint32_t;
#define BOXEDWINE_MULTI_THREADED 1
constexpr U32 K_POLLIN=1,K_POLLOUT=4;
struct Condition{std::set<Condition*> parents;U32 parentsCount(){return parents.size();}void addParentCondition(const std::shared_ptr<Condition>& p){parents.insert(p.get());}void removeParentCondition(const std::shared_ptr<Condition>& p){parents.erase(p.get());}};
#define BOXEDWINE_CONDITION std::shared_ptr<Condition>
#define BOXEDWINE_CONDITION_ADD_PARENT(c,p) (c)->addParentCondition(p)
#define BOXEDWINE_CONDITION_REMOVE_PARENT(c,p) (c)->removeParentCondition(p)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) const std::lock_guard<std::recursive_mutex> listLock(m)
static std::recursive_mutex waitingNodeMutex,checkWaitingNativeSocketsThreadMutex;
struct KNativeSocketObject:std::enable_shared_from_this<KNativeSocketObject>{virtual ~KNativeSocketObject()=default;int nativeSocket=7;bool loopbackDatagram=false,loopbackStream=false,loopbackStreamConnected=false;U32 loopbackPort=0;BOXEDWINE_CONDITION readingCond=std::make_shared<Condition>(),writingCond=std::make_shared<Condition>();void waitForEvents(BOXEDWINE_CONDITION&,U32);};
static bool monitored=false;
static void addWaitingNativeSocket(const std::shared_ptr<KNativeSocketObject>&){monitored=true;}
static void removeWaitingSocket(int){monitored=false;}
${source.slice(start,end)}
int main(){auto socket=std::make_shared<KNativeSocketObject>();auto a=std::make_shared<Condition>(),b=std::make_shared<Condition>();
 socket->waitForEvents(a,K_POLLIN);socket->waitForEvents(b,K_POLLIN);assert(monitored);
 socket->waitForEvents(a,0);assert(monitored && "remaining reader must stay monitored");socket->waitForEvents(b,0);assert(!monitored);
 socket->waitForEvents(a,K_POLLIN);socket->waitForEvents(b,K_POLLOUT);socket->waitForEvents(a,0);assert(monitored);socket->waitForEvents(b,0);assert(!monitored);
}
`);
});

test('native socket creation reports descriptor exhaustion without publishing an invalid fd', () => {
  const translate = source.slice(source.indexOf('S32 translateNativeSocketError('), source.indexOf('S32 handleNativeSocketError('));
  const constructor = source.slice(source.indexOf('KNativeSocketObject::KNativeSocketObject('), source.indexOf('KNativeSocketObject::~KNativeSocketObject('));
  const errors = readFileSync(new URL('../../../../include/kerror.h', import.meta.url), 'utf8');
  run(`
#include <cassert>
#include <cstdint>
#include <memory>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
using U32=uint32_t;using S32=int32_t;
${errors}
#define BOXEDWINE_MULTI_THREADED 1
#define LOG_SOCK(...)
#define B(s) s
constexpr U32 K_SOCK_STREAM=1,K_SOCK_DGRAM=2,K_SOCK_RAW=3,K_SOCK_RDM=4,K_SOCK_SEQPACKET=5,KTYPE_NATIVE_SOCKET=1;
struct BoxedWineCondition{BoxedWineCondition(const char*){}};
struct KSocketObject{KSocketObject(U32,U32,U32,U32){}};
struct KNativeSocketObject:KSocketObject{U32 error=0;S32 nativeSocket=-1;bool connecting;std::shared_ptr<BoxedWineCondition> readingCond,writingCond;KNativeSocketObject(U32,U32,U32);};
static void closesocket(int fd){close(fd);}
${translate}
${constructor}
int main(){
 struct rlimit previous;assert(getrlimit(RLIMIT_NOFILE,&previous)==0);struct rlimit low=previous;low.rlim_cur=3;assert(setrlimit(RLIMIT_NOFILE,&low)==0);
 KNativeSocketObject exhausted(2,1,6);assert(exhausted.nativeSocket==-1);assert(exhausted.error==U32(-K_EMFILE));
 assert(setrlimit(RLIMIT_NOFILE,&previous)==0);KNativeSocketObject valid(2,1,6);assert(valid.error==0 && valid.nativeSocket>=0);close(valid.nativeSocket);
 assert(translateNativeSocketError(-1,ENOMEM)==-K_ENOMEM);
 assert(translateNativeSocketError(-1,ENFILE)==-K_ENFILE);
}
`);
});

test('native recvmsg preserves contiguous scatter positions for large vectors and MSG_PEEK', () => {
  const start = source.indexOf('U32 KNativeSocketObject::recvmsg(');
  const end = source.indexOf('U32 KNativeSocketObject::sendto(', start);
  run(`
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
using U32=uint32_t;using U8=uint8_t;using U16=uint16_t;using S32=int32_t;
constexpr U32 K_MSG_PEEK=2,K_PAGE_SIZE=4096,K_SOCK_STREAM=1,K_EMSGSIZE=90,K_EFAULT=14,K_EAGAIN=11;
#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c)
#define LOG_SOCK(...)
static bool isGuestLoopbackAddress(U32 address){return address==0 || (address>>24)==127;}
#define kwarn_fmt(...)
struct MsgHdr{U32 msg_iov=256,msg_iovlen=2,msg_name=0,msg_namelen=0;};static MsgHdr fixture;
struct KMemory{U32 lengths[2]={8192,4};std::array<U8,16384> bytes{};
 U32 readd(U32 address){U32 slot=(address-256)/8;return (address-256)%8 ? lengths[slot] : (slot ? 8192 : 0);}
 bool canWrite(U32 address,U32 size) const {return address<=bytes.size() && size<=bytes.size()-address;}
 void writed(U32,U32){}void memcpy(U32 address,const void* buffer,U32 size){assert(address+size<=bytes.size());std::memcpy(bytes.data()+address,buffer,size);}
};
struct KThread{KMemory* memory;};using KFileDescriptorPtr=int;
static void readMsgHdr(KThread*,U32,MsgHdr* out){*out=fixture;}
static void writeSockAddrIn(sockaddr_in*,KMemory*,U32){}
static std::vector<U8> stream(5000,'x');static size_t cursor=0;
static int receive(int,char* buffer,size_t length,int flags,sockaddr*,socklen_t*){size_t count=std::min(length,stream.size()-cursor);std::memcpy(buffer,stream.data()+cursor,count);if(!(flags&MSG_PEEK))cursor+=count;return count;}
#define recvfrom receive
struct KNativeSocketObject:std::enable_shared_from_this<KNativeSocketObject>{struct LoopbackPacket{std::vector<U8> bytes;U32 sourceAddress=0;U16 sourcePort=0;};virtual ~KNativeSocketObject()=default;U32 type=K_SOCK_STREAM,error=0;int nativeSocket=3;bool loopbackDatagram=false,loopbackStream=false,loopbackStreamConnected=false;U16 loopbackPort=0;std::mutex loopbackStreamMutex;U32 recvLoopbackStream(U8*,U32,U32){return 0;}U32 takeLoopbackPacket(LoopbackPacket&,U32){return U32(-K_EAGAIN);}U32 recvmsg(KThread*,const KFileDescriptorPtr&,U32,U32);};
static U32 handleNativeSocketError(const std::shared_ptr<KNativeSocketObject>&,bool){return U32(-1);}
${source.slice(start,end)}
int main(){auto socket=std::make_shared<KNativeSocketObject>();KMemory memory;KThread thread{&memory};
 assert(socket->recvmsg(&thread,0,0,0)==4096);assert(cursor==4096);assert(memory.bytes[4095]=='x' && memory.bytes[4096]==0 && memory.bytes[8192]==0);
 memory=KMemory{};memory.lengths[0]=4;cursor=0;assert(socket->recvmsg(&thread,0,0,K_MSG_PEEK)==4);assert(cursor==0 && memory.bytes[3]=='x' && memory.bytes[8192]==0);
 memory=KMemory{};memory.lengths[0]=0;cursor=0;assert(socket->recvmsg(&thread,0,0,0)==4);assert(cursor==4 && memory.bytes[8192]=='x');
}
`);
});

test('browser TCP distinguishes pending nonblocking connects from blocking completion', () => {
  const start = source.indexOf('U32 KNativeSocketObject::connect(');
  const end = source.indexOf('U32 KNativeSocketObject::listen(', start);
  const method = source.slice(start,end).replace('::connect(this->nativeSocket', '::nativeConnect(this->nativeSocket').replace('::poll(', '::nativePoll(').replaceAll('::getsockopt(', '::nativeGetOption(');
  run(`
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <deque>
#include <mutex>
#include <algorithm>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
using U32=uint32_t;using U16=uint16_t;using S32=int32_t;
#define __EMSCRIPTEN__ 1
#define BOXEDWINE_MULTI_THREADED 1
#define LOG_SOCK(...)
constexpr U32 K_AF_INET=2,K_EINVAL=22,K_EAFNOSUPPORT=97,K_EHOSTUNREACH=113,K_EISCONN=106,K_ECONNREFUSED=111,K_EWOULDBLOCK=11,K_EINPROGRESS=115;
static bool isGuestLoopbackAddress(U32 address){return address==0 || (address>>24)==127;}
struct Condition{std::mutex m;};
struct KNativeSocketObject;
static std::mutex g_loopbackTcpMutex;
static std::map<U16,std::weak_ptr<KNativeSocketObject>> g_loopbackTcpSockets;
#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c)
#define BOXEDWINE_CONDITION_SIGNAL_ALL(c)
struct KMemory{U16 readw(U32){return K_AF_INET;}unsigned char readb(U32){return 0;}void memcpy(char* buffer,U32,U32 len){std::memset(buffer,0,len);}};
struct KThread{KMemory* memory;};using KFileDescriptorPtr=int;
struct KNativeSocketObject:std::enable_shared_from_this<KNativeSocketObject>{virtual ~KNativeSocketObject()=default;U32 domain=AF_INET,type=SOCK_STREAM,protocol=0,error=0;int nativeSocket=3;bool connected=false,connecting=false,blocking=false,loopbackDatagram=false,loopbackStream=false,loopbackStreamConnected=false,listening=false;U32 loopbackAddress=0,loopbackPeerAddress=0,loopbackStreamBacklog=0;U16 loopbackPort=0,loopbackPeerPort=0;std::weak_ptr<KNativeSocketObject> loopbackStreamPeer;std::deque<std::shared_ptr<KNativeSocketObject>> loopbackAcceptQueue;std::mutex loopbackStreamMutex;std::shared_ptr<Condition> readingCond=std::make_shared<Condition>();KNativeSocketObject()=default;KNativeSocketObject(U32 d,U32 t,U32 p):domain(d),type(t),protocol(p){}U32 bindLoopbackStream(U32,U16){return 0;}U32 ensureLoopbackBound(){return 0;}U32 connect(KThread*,const KFileDescriptorPtr&,U32,U32);};
static void closesocket(int fd){::close(fd);}
static void readSockAddrIn(sockaddr_in* address,KMemory*,U32){address->sin_family=AF_INET;}
static int nativeError=0,polls=0;
static int nativeConnect(int,const sockaddr*,socklen_t){return 0;}
static int nativePoll(pollfd* descriptor,unsigned long,int timeout){assert(timeout==-1);polls++;descriptor->revents=nativeError?POLLERR:POLLOUT;return 1;}
static int nativeGetOption(int,int,int,void* value,socklen_t*){*(int*)value=nativeError;return 0;}
static S32 translateNativeSocketError(S32,int error){return -error;}
static U32 handleNativeSocketError(const std::shared_ptr<KNativeSocketObject>&,bool){return U32(-1);}
${method}
int main(){KMemory memory;KThread thread{&memory};auto pending=std::make_shared<KNativeSocketObject>();
 assert(pending->connect(&thread,0,0,16)==U32(-K_EINPROGRESS));assert(pending->connecting && !pending->connected && polls==0);
 auto blocking=std::make_shared<KNativeSocketObject>();blocking->blocking=true;assert(blocking->connect(&thread,0,0,16)==0);assert(blocking->connected && !blocking->connecting && polls==1);
 auto failed=std::make_shared<KNativeSocketObject>();failed->blocking=true;nativeError=K_ECONNREFUSED;assert(failed->connect(&thread,0,0,16)==U32(-K_ECONNREFUSED));assert(!failed->connected && !failed->connecting && polls==2);
}
`);
});
