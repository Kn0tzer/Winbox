import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const source=readFileSync(new URL('../../../../source/kernel/kunixsocket.cpp',import.meta.url),'utf8');
function implementation(name) {
 const signature=name==='~KUnixSocketObject'?'KUnixSocketObject::~KUnixSocketObject()':`U32 KUnixSocketObject::${name}(`; const start=source.indexOf(signature);assert.ok(start>=0);
 let end=source.indexOf('{',start),depth=1;
 for(end++;depth&&end<source.length;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
 return extractSourceRange(source, start,end);
}
test('a blocking retry of a pending UNIX connect completes when the listener accepts',()=>{
 const dir=mkdtempSync(join(tmpdir(),'bw64-unix-handoff-'));
 try {
  const harness=String.raw`
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <list>
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <thread>
#include <chrono>
using U32=uint32_t; using U16=uint16_t; using S32=int32_t;
constexpr int K_EINVAL=22,K_EISCONN=106,K_ENOENT=2,K_ECONNREFUSED=111,K_EINTR=4,K_CONTINUE=1000,K_EINPROGRESS=115,K_EAFNOSUPPORT=97,K_EWOULDBLOCK=11,K_ECONNABORTED=103,K_EMFILE=24;
constexpr int K_ENAMETOOLONG=36;
constexpr int K_EPROTOTYPE=71;
constexpr int K_AF_UNIX=1,K_SOCK_DGRAM=2,K_SOCK_STREAM=1,K_SOCK_SEQPACKET=5,KTYPE_UNIX_SOCKET=3,K_O_RDWR=2,K_O_NONBLOCK=2048,FD_CLOEXEC=1;
#define BOXEDWINE_MULTI_THREADED
struct Condition {std::mutex m;std::condition_variable cv;std::atomic<int> waits{0};};
#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c) std::unique_lock<std::mutex> boxedWineCriticalSection((c)->m)
#define BOXEDWINE_CONDITION_WAIT(c) do{(c)->waits++;(c)->cv.wait(boxedWineCriticalSection);}while(0)
#define BOXEDWINE_CONDITION_SIGNAL_ALL(c) (c)->cv.notify_all()
#define klog_fmt(...) ((void)0)
#define kwarn_fmt(...) ((void)0)
#define bw64RecoverEnabled() false
#define bw64RecoverLogOnce(...) ((void)0)
#define kpanic_fmt(...) std::abort()
void kpanic(const char*) {std::abort();}
struct BString:std::string {using std::string::string; static BString copy(const char* p){return BString(p);}};
struct KObject:std::enable_shared_from_this<KObject> {virtual ~KObject()=default;int type=KTYPE_UNIX_SOCKET;virtual void setBlocking(bool) {}};
struct Descriptor {std::shared_ptr<KObject> kobject;U32 handle;U32 descriptorFlags=0;};
using KFileDescriptorPtr=std::shared_ptr<Descriptor>;
struct KMemory {char bytes[200]={};U16 readw(U32 a){U16 v;std::memcpy(&v,bytes+a,2);return v;}void memcpy(void* d,U32 s,U32 n){std::memcpy(d,bytes+s,n);}};
struct FsContext {BString currentDirectory="/tmp";};
struct Process {U32 id=10;std::shared_ptr<FsContext> fsContext=std::make_shared<FsContext>();std::vector<KFileDescriptorPtr> fds;KFileDescriptorPtr allocFileDescriptor(std::shared_ptr<KObject> o,int,int,int,int){auto f=std::make_shared<Descriptor>();f->kobject=o;f->handle=fds.size()+3;fds.push_back(f);return f;}};
struct KThread {KMemory* memory;Process* process;bool terminating=false,startSignal=false;static KThread* currentThread(){return nullptr;}};
struct FsNode {std::weak_ptr<KObject> kobject;std::weak_ptr<FsNode> parent;BString name;auto getParent(){return parent;}void removeChildByName(const BString&) {}};
struct Fs {static std::shared_ptr<FsNode> node;static std::shared_ptr<FsNode> getNodeFromLocalPath(const BString&,const BString&,bool){return node;}};
std::shared_ptr<FsNode> Fs::node;
struct KUnixSocketObject;
std::mutex& abstractRegistryMutex();
std::map<std::string,std::weak_ptr<KUnixSocketObject>>& abstractRegistry();
struct KUnixSocketObject:KObject {
 KUnixSocketObject(int d,int t,int p):domain(d),protocol(p){type=t;}
 ~KUnixSocketObject();
 int domain,protocol,type;U32 pid=0;bool blocking=true,connected=false,inClosed=false,outClosed=false;
 std::string abstractName;bool abstractRegistered=false;bool oneWayPeerLink=false;bool peerCloseRefusalPending=false;bool oneWayRefusalSpent=false;
 std::shared_ptr<FsNode> node;std::queue<int> msgs;

 struct{U16 family=0;char data[108]={};}destAddress;
 std::weak_ptr<KUnixSocketObject> connection,connecting;
 std::list<std::weak_ptr<KUnixSocketObject>> pendingConnections;
 std::shared_ptr<Condition> lockCond=std::make_shared<Condition>();
 void setBlocking(bool b)override{blocking=b;}
 U32 connect(KThread*,const KFileDescriptorPtr&,U32,U32);
 U32 accept(KThread*,const KFileDescriptorPtr&,U32,U32,U32);
 std::shared_ptr<KUnixSocketObject> resolveNamedPeer(KThread*,U32,U32,U32&);
 void recordOwner(KThread*) {}
};
inline void eraseUnixSocketPriority(const KUnixSocketObject* socket) {(void)socket;}
struct XWireServer {static bool isXDisplayPath(const char*){return false;}static XWireServer& instance(){static XWireServer s;return s;}bool acceptConnection(std::shared_ptr<KUnixSocketObject>){return false;}};
std::mutex& abstractRegistryMutex(){static std::mutex m;return m;}
std::map<std::string,std::weak_ptr<KUnixSocketObject>>& abstractRegistry(){static std::map<std::string,std::weak_ptr<KUnixSocketObject>> r;return r;}
std::shared_ptr<KUnixSocketObject> KUnixSocketObject::resolveNamedPeer(KThread* thread,U32 address,U32 len,U32& err){
 err=0;KMemory* memory=thread->memory;
 if(len<=2||memory->readw(address)!=K_AF_UNIX){err=K_EINVAL;return nullptr;}
 char first=0;memory->memcpy(&first,address+2,1);
 if(first==0){
  std::string key;U32 n=len>3?len-3:0;if(n>107)n=107;key.resize(n);if(n)memory->memcpy(&key[0],address+3,n);
  std::lock_guard<std::mutex> guard(abstractRegistryMutex());
  auto it=abstractRegistry().find(key);
  if(it!=abstractRegistry().end()){
   std::shared_ptr<KUnixSocketObject> dest=it->second.lock();
   if(dest){if(dest->type!=this->type){err=K_EPROTOTYPE;return nullptr;}return dest;}
   abstractRegistry().erase(it);
  }
  err=K_ECONNREFUSED;return nullptr;
 }
 std::string name;
 for(U32 i=0;i+2<len;i++){char c=0;memory->memcpy(&c,address+2+i,1);if(c==0)break;name.push_back(c);}
 if(name.empty()){err=K_EINVAL;return nullptr;}
 std::shared_ptr<FsNode> node=Fs::getNodeFromLocalPath(thread->process->fsContext->currentDirectory,BString(name.c_str()),true);
 if(!node){err=K_ENOENT;return nullptr;}
 std::shared_ptr<KObject> kobject=node->kobject.lock();
 if(!kobject||kobject->type!=KTYPE_UNIX_SOCKET){err=K_ECONNREFUSED;return nullptr;}
 std::shared_ptr<KUnixSocketObject> dest=std::dynamic_pointer_cast<KUnixSocketObject>(kobject);
 if(!dest){err=K_ECONNREFUSED;return nullptr;}
 if(dest->type!=this->type){err=K_EPROTOTYPE;return nullptr;}
 return dest;
}
`;
  const main=String.raw`
void handoff(bool pendingRetry){
 KMemory memory;Process process;KThread thread{&memory,&process};
 const U16 family=K_AF_UNIX;std::memcpy(memory.bytes,&family,2);std::memcpy(memory.bytes+2,"socket",7);
 auto listener=std::make_shared<KUnixSocketObject>(1,1,0);Fs::node=std::make_shared<FsNode>();Fs::node->kobject=listener;
 auto client=std::make_shared<KUnixSocketObject>(1,1,0);
 auto fd=process.allocFileDescriptor(client,2,0,-1,0);
 if(pendingRetry){client->blocking=false;assert(int32_t(client->connect(&thread,fd,0,9))==-K_EINPROGRESS);}
 client->blocking=true;std::atomic<int32_t> result{-999};
 std::thread retry([&]{result=int32_t(client->connect(&thread,fd,0,9));});
 for(int n=0;!client->lockCond->waits&&n<100;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(client->lockCond->waits);
 // A notification is not acceptance. Both the initial blocking connect and
 // a blocking retry must keep waiting until the peer is actually linked.
 {std::lock_guard<std::mutex> lock(client->lockCond->m);client->lockCond->cv.notify_all();}
 for(int n=0;client->lockCond->waits<2&&n<100;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(client->lockCond->waits>=2);assert(result==-999);
 auto serverfd=process.allocFileDescriptor(listener,2,0,-1,0);
 assert(listener->accept(&thread,serverfd,0,0,0)>=3);
 retry.join();assert(result==0);assert(client->connected);assert(!client->connection.expired());assert(client->connecting.expired());
 assert(client->connect(&thread,fd,0,9)==0);
}
int main(){
 auto listener=std::make_shared<KUnixSocketObject>(1,1,0);
 auto canceled=std::make_shared<KUnixSocketObject>(1,1,0);
 canceled->connecting=listener;listener->pendingConnections.push_back(canceled);
 canceled.reset();assert(listener->pendingConnections.empty());
 handoff(true);handoff(false);
}
`;
  writeFileSync(join(dir,'check.cpp'),harness+implementation('~KUnixSocketObject')+'\n'+implementation('connect')+'\n'+implementation('accept')+main);
  execFileSync('c++',['-std=c++17','-pthread',join(dir,'check.cpp'),'-o',join(dir,'check')],{stdio:'pipe'});
  execFileSync('bash',['-c','ulimit -c 0; exec "$1"','unix-handoff-check',join(dir,'check')],{timeout:2000,stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
