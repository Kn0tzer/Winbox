import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('concurrent O_CREAT opens publish one shared inode', () => {
  const source = readFileSync(join(repoRoot, 'source/kernel/kprocess.cpp'), 'utf8');
  const start = source.indexOf('U32 KProcess::openFileDescriptor(');
  const end = source.indexOf('U32 KProcess::openFile(', start);
  assert.ok(start >= 0 && end > start);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-file-create-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <barrier>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
using U32=unsigned;using S32=int;
constexpr int K_O_CREAT=64,K_O_TMPFILE=0x400000,K_O_ACCMODE=3,K_O_WRONLY=1,K_O_RDWR=2,K_O_RDONLY=0;
constexpr int K_ENOENT=2,K_EINVAL=22,K_EISDIR=21;
#define B(s) BString(s)
#define EXT_MIXED ".mixed"
#define kwarn(...) ((void)0)
#define klog_fmt(...) ((void)0)
#define BOXEDWINE_CRITICAL_SECTION static std::recursive_mutex cs; std::lock_guard<std::recursive_mutex> lock(cs)
struct BString:std::string{
 using std::string::string;BString(std::string s):std::string(s){}
 bool contains(const char* s)const{return find(s)!=npos;}
};
struct FsNode;
struct FsOpenNode{std::shared_ptr<FsNode> node;BString openedPath;};
struct KObject{virtual ~KObject()=default;};
struct KFile:KObject{FsOpenNode* openFile;KFile(FsOpenNode* f):openFile(f){}~KFile(){delete openFile;}};
struct Descriptor{std::shared_ptr<KObject> kobject;};
using KFileDescriptorPtr=std::shared_ptr<Descriptor>;
std::mutex treeMutex;
std::shared_ptr<FsNode> child;
struct FsNode:std::enable_shared_from_this<FsNode>{
 BString path="/tmp";unsigned id;
 std::weak_ptr<KObject> kobject;
 bool isDirectory(){return path=="/tmp";}
 std::shared_ptr<FsNode> getChildByNameIgnoreCase(const BString&){return {};}
 FsOpenNode* open(U32){return new FsOpenNode{shared_from_this()};}
};
struct FsFileNode{static BString getLocalTmpPath(){return "/tmp/temporary";}};
struct Fs{
 static inline std::atomic<int> lookups{0},ids{1};
 static inline std::barrier firstMisses{2};
 static inline auto parent=std::make_shared<FsNode>();
 static std::shared_ptr<FsNode> getNodeFromLocalPath(const BString&,const BString& path,bool){
  if(path=="/tmp")return parent;
  if(lookups.fetch_add(1)<2){firstMisses.arrive_and_wait();return {};}
  std::lock_guard guard(treeMutex);return child;
 }
 static BString getFullPath(const BString& cwd,const BString& path){return cwd+"/"+path;}
 static BString getParentPath(const BString&){return "/tmp";}
 static BString getFileNameFromPath(const BString&){return "lock";}
 static BString getNativePathFromParentAndLocalFilename(std::shared_ptr<FsNode>,const BString&){return "/tmp/lock";}
 static bool doesNativePathExist(const BString&){return false;}
 static std::shared_ptr<FsNode> addFileNode(const BString& path,const BString&,const BString&,bool,std::shared_ptr<FsNode>){
  auto node=std::make_shared<FsNode>();node->path=path;node->id=ids++;
  std::lock_guard guard(treeMutex);child=node;return node;
 }
 static void makeLocalDirs(const BString&){}
};
struct KProcess{
 U32 id;
 U32 translateOpenError(){return K_ENOENT;}
 KFileDescriptorPtr allocFileDescriptor(std::shared_ptr<KObject> o,U32,U32,S32,U32){return std::make_shared<Descriptor>(Descriptor{o});}
 U32 openFileDescriptor(BString,BString,U32,U32,S32,U32,KFileDescriptorPtr&);
};
${extractSourceRange(source, start,end)}
int main(){
 KProcess first{1},second{2};KFileDescriptorPtr a,b;
 std::thread one([&]{assert(first.openFileDescriptor("/tmp","lock",K_O_CREAT|K_O_RDWR,0,-1,0,a)==0);});
 std::thread two([&]{assert(second.openFileDescriptor("/tmp","lock",K_O_CREAT|K_O_RDWR,0,-1,0,b)==0);});
 one.join();two.join();
 auto fa=std::dynamic_pointer_cast<KFile>(a->kobject),fb=std::dynamic_pointer_cast<KFile>(b->kobject);
 assert(fa->openFile->node==fb->openFile->node && "O_CREAT split one path across independent inodes");
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000 });
    assert.equal(run.status, 0, run.stderr || String(run.signal || run.error));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
