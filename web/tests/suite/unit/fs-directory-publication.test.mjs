import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('a concurrent directory lookup waits until native children are published', () => {
  const source = readFileSync('source/io/fsnode.cpp', 'utf8');
  const start = source.indexOf('void FsNode::loadChildren()');
  const end = source.indexOf('std::shared_ptr<FsNode> FsNode::getChildByName(', start);
  assert.ok(start >= 0 && end > start);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-fs-publish-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using U8=unsigned char; using U32=unsigned;
#define B(s) BString(s)
#define EXT_MIXED ".mixed"
#define EXT_DOSATTRIB ".user.DOSATTRIB"
#define EXT_LINK ".link"
#define MAX_FILEPATH_LEN 4096
#define klog_fmt(...) ((void)0)
#define kwarn_fmt(...) ((void)0)
#define BOXEDWINE_CRITICAL_SECTION static std::recursive_mutex cs; std::lock_guard<std::recursive_mutex> lock(cs)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::recursive_mutex> lock(m)
struct BString:std::string {
 using std::string::string;
 BString(std::string value):std::string(value){}
 static BString copy(const char* s){return s;}
 bool contains(const char* s)const{return find(s)!=npos;}
 bool endsWith(const char* s)const{return std::string::ends_with(s);}
 bool endsWith(char c)const{return back()==c;}
 void remove(int length){resize(length);}
 BString stringByApppendingPath(const BString& s)const{return *this+"/"+s;}
};
std::atomic<bool> listing{false},releaseListing{false};
struct Platform {
 struct ListNodeResult {BString name;bool isDirectory;};
 static void listNodes(const BString&,std::vector<ListNodeResult>& out){
  listing=true;
  while(!releaseListing)std::this_thread::yield();
  out.push_back({"lock",false});
 }
};
struct FsNode:std::enable_shared_from_this<FsNode>{
 std::recursive_mutex childrenByNameMutex;
 bool hasLoadedChildrenFromFileSystem=false;
 BString path="/tmp/server",nativePath="/tmp/server";
 std::vector<BString> children;
 void loadChildren();
};
struct Fs {
 static void remoteNameToLocal(BString&){}
 static U32 readNativeFile(const BString&,U8*,U32){return 0;}
 static void addFileNode(const BString& path,const BString&,const BString&,bool,std::shared_ptr<FsNode> parent){parent->children.push_back(path);}
};
${source.slice(start, end)}
int main(){
 auto node=std::make_shared<FsNode>();
 std::thread loader([&]{node->loadChildren();});
 while(!listing)std::this_thread::yield();
 std::atomic<bool> readerStarted{false};
 auto reader=std::async(std::launch::async,[&]{readerStarted=true;node->loadChildren();return node->children.size();});
 while(!readerStarted)std::this_thread::yield();
 bool returnedEarly=reader.wait_for(std::chrono::milliseconds(50))==std::future_status::ready;
 releaseListing=true;
 loader.join();
 auto count=reader.get();
 assert(!returnedEarly && "lookup observed a partially loaded directory");
 assert(count==1 && node->children.size()==1);
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000 });
    assert.equal(run.status, 0, run.stderr || String(run.signal || run.error));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
