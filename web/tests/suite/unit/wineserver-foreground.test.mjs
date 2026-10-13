import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('bare Wine64 server boots in the foreground and explicit control commands stay intact', () => {
  const source = readFileSync('source/kernel/syscall64.cpp', 'utf8');
  const block = source.match(/    if \(args\.size\(\) == 1 &&\n[\s\S]*?\n    }/)?.[0];
  assert.ok(block);
  const dir = mkdtempSync(join(tmpdir(), 'wineserver-args-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <string>
#include <vector>
#include <cassert>
struct BString:std::string{using std::string::string;bool endsWith(const char* s)const{std::string e(s);return size()>=e.size() && compare(size()-e.size(),e.size(),e)==0;}};
#define B(s) BString(s)
void klog_fmt(const char*,...){}
void prepare(BString path,std::vector<BString>& args){${block}}
int main(){
 for(const char* path:{"/usr/lib/wine/wineserver64","/usr/lib/wine/wineserver"}){
  std::vector<BString>a{B(path)};prepare(B(path),a);
  assert(a.size()==3 && a[1]=="-f" && a[2]=="-p");
  std::vector<BString>k{B(path),B("-k")};prepare(B(path),k);assert(k.size()==2 && k[1]=="-k");
 }
 std::vector<BString>a{B("app")};prepare(B("/app"),a);assert(a.size()==1);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive:true, force:true }); }
});
