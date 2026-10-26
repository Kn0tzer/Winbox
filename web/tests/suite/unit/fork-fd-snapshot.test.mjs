import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
test('fork snapshots the complete fd table after a sibling descriptor update finishes',()=>{
 const src=readFileSync(new URL('../../../../source/kernel/kprocess.cpp',import.meta.url),'utf8');
 const start=src.indexOf('void KProcess::clone(const KProcessPtr& from, U32 shareFlags)');assert.ok(start>=0);
 let end=src.indexOf('{',start),depth=1;
 for(end++;depth&&end<src.length;end++){if(src[end]==='{')depth++;if(src[end]==='}')depth--;}
 const dir=mkdtempSync(join(tmpdir(),'bw64-fork-fds-'));
 try{
 const setup=String.raw`
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <future>
#include <chrono>
#include <cstdint>
#include <array>
using U32=uint32_t;
#define MAX_SIG_ACTIONS 4
#define LDT_ENTRIES 16
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::mutex> lock(m)
struct BString:std::string{using std::string::string;static BString valueOf(U32 n){auto s=std::to_string(n);return BString(s.c_str());}};
struct KObject{int version;BString selfFd(){return "fixture";}};
struct Descriptor{std::shared_ptr<KObject> kobject;U32 accessFlags=2,descriptorFlags=0;};
using KFileDescriptorPtr=std::shared_ptr<Descriptor>;
struct Entry{U32 key;KFileDescriptorPtr value;};
struct Table{std::vector<Entry> values;std::mutex mutex;auto begin(){return values.begin();}auto end(){return values.end();}void set(U32 key,KFileDescriptorPtr value){for(auto& e:values)if(e.key==key){e.value=value;return;}values.push_back({key,value});}};
constexpr U32 KPROCESS_CLONE_SHARE_FS=0x200,KPROCESS_CLONE_SHARE_FILES=0x400,KPROCESS_CLONE_SHARE_SIGHAND=0x800;
struct KFdTable{std::shared_ptr<Table> table=std::make_shared<Table>();auto begin(){return table->values.begin();}auto end(){return table->values.end();}void set(U32 k,KFileDescriptorPtr v){table->set(k,v);}void shareFrom(const KFdTable& o){table=o.table;}std::mutex& mutex(){return table->mutex;}};
struct KSigActionTable{std::shared_ptr<std::array<int,MAX_SIG_ACTIONS>> impl=std::make_shared<std::array<int,MAX_SIG_ACTIONS>>();int* begin(){return impl->data();}int* end(){return impl->data()+MAX_SIG_ACTIONS;}void shareFrom(const KSigActionTable& o){impl=o.impl;}};
struct KFsContext{BString currentDirectory;U32 umaskValue=0;};
struct FsNode{BString path;};
struct Fs{template<class F>static void addDynamicLinkFile(std::string,int,std::shared_ptr<FsNode>,bool,F){}};
int k_mdev(int,int){return 0;}
struct AttachedSHM{int shm;AttachedSHM(int s,U32,U32):shm(s){}};
struct ShmEntry{U32 key;std::shared_ptr<AttachedSHM> value;};
struct Shms:std::vector<ShmEntry>{void set(U32 k,std::shared_ptr<AttachedSHM> v){push_back({k,v});}};
struct KProcess;using KProcessPtr=std::shared_ptr<KProcess>;
struct KProcess{
 U32 id=1,parentId=0,groupId=1,userId=0,effectiveUserId=0,effectiveGroupId=0,brkEnd=0,loaderBaseAddress=0,phdr=0,phnum=0,entry=0;
 std::shared_ptr<KFsContext> fsContext=std::make_shared<KFsContext>();
 BString path,commandLine,exe,name;bool hasSetStackMask=false,systemProcess=false;int ldt[LDT_ENTRIES]={};bool hasSetSeg[6]={};KSigActionTable sigActions;
 unsigned long long memLockedBytes=0,rlimMemlockCur=~0ULL,rlimMemlockMax=~0ULL;bool memLockAllFuture=false;
 KFdTable fds;std::mutex& fdsMutex(){return fds.mutex();}std::mutex mappedFilesMutex;std::vector<int> mappedFiles,privateShm;Shms attachedShm;
 std::shared_ptr<FsNode> fdNode=std::make_shared<FsNode>();
 KFileDescriptorPtr allocFileDescriptor(std::shared_ptr<KObject> o,U32 a,U32 d,U32 h,U32){auto f=std::make_shared<Descriptor>();f->kobject=o;f->accessFlags=a;f->descriptorFlags=d;fds.set(h,f);return f;}
 void setupCommandlineNode(){}void clone(const KProcessPtr& from,U32 shareFlags);
};
`;
 const main=String.raw`
int main(){
 auto parent=std::make_shared<KProcess>();auto child=std::make_shared<KProcess>();
 auto old=std::make_shared<KObject>();old->version=1;
 auto updated=std::make_shared<KObject>();updated->version=2;
 parent->allocFileDescriptor(old,2,0,3,0);parent->allocFileDescriptor(old,2,0,4,0);
 std::promise<void> halfway;
 std::thread writer([&]{std::lock_guard<std::mutex> lock(parent->fdsMutex());
  for(auto& e:parent->fds)if(e.key==3)e.value->kobject=updated;halfway.set_value();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  for(auto& e:parent->fds)if(e.key==4)e.value->kobject=updated;
 });
 halfway.get_future().wait();child->clone(parent,0);writer.join();
 U32 nfds=0;for(auto& fd:child->fds){nfds++;if(fd.value->kobject->version!=2)return 7;}
 if(nfds!=2)return 6;
 return 0;
}
`;
 writeFileSync(join(dir,'check.cpp'),setup+extractSourceRange(src, start,end)+main);
 execFileSync('c++',['-std=c++17','-pthread',join(dir,'check.cpp'),'-o',join(dir,'check')],{stdio:'pipe'});
 execFileSync(join(dir,'check'),[],{timeout:2000,stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
