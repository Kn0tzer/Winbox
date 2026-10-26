import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

// Compile the production cache, path walker, and child-removal methods together.
// The small in-memory directory adapter avoids linking the whole emulator.
test('dynamic fd link removal stays coherent without scanning unrelated cached paths', () => {
  const fs = readFileSync(join(repoRoot, 'source/io/fs.cpp'), 'utf8');
  const node = readFileSync(join(repoRoot, 'source/io/fsnode.cpp'), 'utf8');
  const header = readFileSync(join(repoRoot, 'source/io/fs.h'), 'utf8');
  const section = (source, begin, end) => {
    const start = source.indexOf(begin);
    const finish = source.indexOf(end, start);
    assert.ok(start >= 0 && finish > start, `missing source section ${begin}`);
    return extractSourceRange(source, start, finish);
  };
  const dir = mkdtempSync(join(tmpdir(), 'bw64-dynamic-cache-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
using U32=unsigned; using U64=unsigned long long;
#define B(s) BString(s)
#define BOXEDWINE_MUTEX std::recursive_mutex
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::recursive_mutex> lock(m)
#define klog_fmt(...) ((void)0)
#define FS_PATH_CACHE_MAX 4096
#define FS_PATH_CACHE_STATS_INTERVAL 50000
static size_t prefixChecks=0;
struct BString:std::string {
 using std::string::string;
 BString(std::string s):std::string(std::move(s)){}
 bool startsWith(const BString& s)const{++prefixChecks;return starts_with(s);}
};
namespace std {template<>struct hash<BString>{size_t operator()(const BString& s)const{return hash<string>()(s);}};}
struct FsNode;
struct Children {
 std::unordered_map<BString,std::shared_ptr<FsNode>> entries;
 std::shared_ptr<FsNode> operator[](const BString& name){auto it=entries.find(name);return it==entries.end()?nullptr:it->second;}
 void set(const BString& name,std::shared_ptr<FsNode> value){entries[name]=std::move(value);}
 void remove(const BString& name){entries.erase(name);}
};
struct FsNode:std::enable_shared_from_this<FsNode>{
 BString path,name,link;
 bool dynamic=false;
 std::atomic<bool> removedFromParent{false};
 std::atomic<U32> childrenVersion{0};
 std::recursive_mutex childrenByNameMutex;
 Children childrenByName;
 FsNode(BString path):path(path),name(path.substr(path.rfind('/')+1)){}
 void loadChildren(){}
 bool isDynamicLink(){return dynamic;}
 bool isLink(){return dynamic||!link.empty();}
 BString getLink(){return link;}
 std::shared_ptr<FsNode> getChildByName(const BString& name){return childrenByName[name];}
 void addChild(std::shared_ptr<FsNode> node);
 void removeChildByName(BString name);
};
struct Fs {
 ${section(header, '    struct FsPathCacheKey {', 'private:')}
 static std::unordered_map<FsPathCacheKey,FsPathCacheEntry,FsPathCacheKeyHash> pathCache;
 static std::list<FsPathCacheKey> pathCacheLru;
 static BOXEDWINE_MUTEX pathCacheMutex;
 static std::atomic<U64> pathCacheHits,pathCacheMisses;
 static std::shared_ptr<FsNode> rootNode;
 static bool isPathCacheEnabled();
 static bool lookupPathCache(const BString&,bool,bool,bool*,std::shared_ptr<FsNode>&);
 static void storePositivePathCache(const BString&,bool,const std::shared_ptr<FsNode>&,const std::vector<FsPathCacheLinkHop>&,bool);
 static void storeNegativePathCache(const BString&,bool,const std::shared_ptr<FsNode>&,U32);
 static BString getFullPath(const BString&,const BString& path){return path;}
 static void splitPath(const BString& path,std::vector<BString>& parts){
  size_t start=0;
  while(start<=path.size()){
   auto end=path.find('/',start);if(end==std::string::npos)end=path.size();
   parts.emplace_back(path.substr(start,end-start));start=end+1;
  }
 }
 static std::shared_ptr<FsNode> getNodeFromLocalPath(const BString&,const BString&,std::shared_ptr<FsNode>*,std::vector<BString>*,bool,bool*);
 static std::shared_ptr<FsNode> get(const BString& path,bool follow=true){return getNodeFromLocalPath("",path,nullptr,nullptr,follow,nullptr);}
};
std::shared_ptr<FsNode> Fs::rootNode;
${section(fs, 'std::unordered_map<Fs::FsPathCacheKey', 'void Fs::shutDown()')}
${section(fs, 'bool Fs::isPathCacheEnabled()', 'BString Fs::getFullPath(')}
${section(fs, 'bool cleanPath(', 'std::shared_ptr<FsFileNode> Fs::addFileNode(')}
${section(node, 'void FsNode::addChild(', 'void FsNode::getAllChildren(')}
static auto add(const std::shared_ptr<FsNode>& parent,const BString& path,const BString& target="",bool dynamic=false){
 auto n=std::make_shared<FsNode>(path);n->link=target;n->dynamic=dynamic;parent->addChild(n);return n;
}
static void coherent(){
 Fs::clearPathCache();
 auto root=Fs::rootNode=std::make_shared<FsNode>("/");
 auto fds=add(root,"/fd");auto a=add(root,"/a");auto b=add(root,"/b");
 auto x=add(a,"/a/x");auto y=add(b,"/b/x");
 // Misses before descriptor creation must expire on the parent's version bump.
 assert(!Fs::get("/fd/7"));assert(!Fs::get("/fd/7/x"));
 auto old=add(fds,"/fd/7","/a",true);
 assert(Fs::get("/fd/7",false)==old);
 assert(Fs::get("/fd/7")==a && Fs::get("/fd/7/x")==x);
 assert(Fs::get("/fd/7/x",false)==x);
 // Positive and negative descendants through a dynamic link never enter cache.
 assert(!Fs::pathCache.contains({"/fd/7",true}));
 assert(!Fs::pathCache.contains({"/fd/7/x",true}));
 assert(!Fs::get("/fd/7/missing"));
 assert(!Fs::pathCache.contains({"/fd/7/missing",true}));
 old->link="/b";
 assert(Fs::get("/fd/7/x")==y);
 auto alias=add(root,"/alias","/fd/7");
 assert(Fs::get("/alias",false)==alias);
 assert(Fs::get("/alias/x")==y);
 // Keep old alive: weak_ptr expiry alone cannot hide stale lstat cache entries.
 fds->removeChildByName("7");
 assert(old->removedFromParent);
 assert(!Fs::get("/fd/7",false));assert(!Fs::get("/fd/7/x"));assert(!Fs::get("/alias/x"));
 auto replacement=add(fds,"/fd/7","/a",true);
 assert(Fs::get("/fd/7",false)==replacement);
 assert(Fs::get("/fd/7/x")==x && Fs::get("/alias/x")==x);
 // Directory/static-link deletion must still invalidate cached descendants.
 assert(Fs::get("/a/x")==x);
 root->removeChildByName("a");assert(!Fs::get("/a/x"));
 auto fixed=add(root,"/fixed","/b");
 assert(Fs::get("/fixed/x")==y);assert(!Fs::get("/fixed/missing"));
 root->removeChildByName("fixed");assert(!Fs::get("/fixed/x"));
 auto c=add(root,"/c");auto present=add(c,"/c/missing");
 add(root,"/fixed","/c");assert(Fs::get("/fixed/missing")==present);
}
static void boundedRemoval(){
 Fs::clearPathCache();
 auto root=Fs::rootNode=std::make_shared<FsNode>("/");
 auto fds=add(root,"/fd");std::vector<std::shared_ptr<FsNode>> keep;
 for(unsigned i=0;i<2000;++i){
  auto n=add(root,BString("/unrelated")+std::to_string(i));keep.push_back(n);
  Fs::storePositivePathCache(n->path,true,n,{},false);
 }
 auto target=add(root,"/target");
 // Include an unfollowed cached link, and retain the node after closing it.
 auto old=add(fds,"/fd/7","/target",true);
 assert(Fs::get("/fd/7",false)==old);
 prefixChecks=0;
 for(unsigned i=0;i<100;++i){
  if(i)add(fds,"/fd/7","/target",true);
  fds->removeChildByName("7");
 }
 printf("unrelated-prefix-comparisons=%zu\\n",prefixChecks);fflush(stdout);
 assert(prefixChecks<100 && "fd closes scanned the unrelated path cache");
 assert(Fs::get("/unrelated1999")==keep.back());
 assert(!Fs::get("/fd/7",false));
}
int main(){unsetenv("BW64_NOPATHCACHE");coherent();boundedRemoval();Fs::clearPathCache();}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', '-O2', join(dir, 'check.cpp'), '-o', join(dir, 'check')],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 5000 });
    assert.equal(run.status, 0, run.stdout + run.stderr || String(run.signal || run.error));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
