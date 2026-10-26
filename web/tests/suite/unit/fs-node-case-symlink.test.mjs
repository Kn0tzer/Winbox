import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { extractSourceSlice } from '../lib/source-slice.mjs';

test('VFS resolves Windows case differences and Wine drive/link aliases', () => {
  const sourceName = 'source/io/fsnode.cpp';
  const source = readFileSync(new URL('../../../../source/io/fsnode.cpp', import.meta.url), 'utf8');
  const lookup = extractSourceSlice(source, 'std::shared_ptr<FsNode> FsNode::getChildByName(', 'U32 FsNode::getChildCount()', { sourceName });
  const dir = mkdtempSync(join(tmpdir(), 'bw64-fs-case-link-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
using U32=unsigned;
struct BString{std::string s;BString()=default;BString(const char*v):s(v){}BString(std::string v):s(std::move(v)){}size_t length()const{return s.length();}bool contains(const char*v)const{return s.find(v)!=std::string::npos;}bool endsWith(const char*v)const{return s.size()>=std::string(v).size()&&s.compare(s.size()-std::string(v).size(),std::string(v).size(),v)==0;}BString replace(const char*a,const char*b)const{std::string v=s;size_t p=0;while((p=v.find(a,p))!=std::string::npos){v.replace(p,std::string(a).size(),b);p+=std::string(b).size();}return BString(v);}BString substr(size_t p,size_t n=std::string::npos)const{return BString(s.substr(p,n));}int compareTo(const BString&o,bool ignoreCase)const{std::string a=s,b=o.s;if(ignoreCase){for(char&c:a)c=(char)std::tolower((unsigned char)c);for(char&c:b)c=(char)std::tolower((unsigned char)c);}return a.compare(b);}BString operator+(const char*v)const{return BString(s+v);}bool operator==(const BString&o)const{return s==o.s;}};
struct FsNode;using FsNodePtr=std::shared_ptr<FsNode>;struct Entry{BString key;FsNodePtr value;};struct BHashTable{std::vector<Entry> entries;FsNodePtr& operator[](const BString&k){for(auto&e:entries)if(e.key==k)return e.value;entries.push_back({k,{}});return entries.back().value;}auto begin(){return entries.begin();}auto end(){return entries.end();}};
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) ((void)0)
struct FsNode:std::enable_shared_from_this<FsNode>{BHashTable childrenByName;int childrenByNameMutex=0;void loadChildren(){}FsNodePtr getChildByName(BString);FsNodePtr getChildByNameIgnoreCase(BString);};
${lookup}
int main(){auto root=std::make_shared<FsNode>(),drive=std::make_shared<FsNode>(),exe=std::make_shared<FsNode>();root->childrenByName[BString("Z(_colon_)")]=drive;root->childrenByName[BString("game.EXE")]=exe;assert(root->getChildByName(BString("Z:"))==drive);assert(root->getChildByNameIgnoreCase(BString("GAME.exe"))==exe);root->childrenByName[BString("C:")]=drive;assert(root->getChildByName(BString("C:.link"))==drive);root->childrenByName[BString("dosdevices/d(_colon_).link")]=drive;assert(root->getChildByName(BString("dosdevices/d:"))==drive);root->childrenByName[BString("plain.link")]=drive;assert(root->getChildByName(BString("plain"))==drive);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
