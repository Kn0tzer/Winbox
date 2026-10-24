import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import { extractSourceSlice } from '../lib/source-slice.mjs';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
for(const multi of [true,false])test(`condition parents grow, deduplicate, expire and notify (${multi?'threaded':'cooperative'})`,()=>{
 const header=readFileSync(join(root,'source/util/synchronization.h'),'utf8');
 const source=readFileSync(join(root,'source/util/synchronization.cpp'),'utf8');
 assert.ok(!header.includes('MAX_PARENTS'),'parent waiters must not have a fixed two-slot cap');
 const sourceName='source/util/synchronization.cpp';
 const branch=multi
  ?extractSourceSlice(source,'BoxedWineCriticalSectionCond::','bool BoxedWineConditionTimer::run()', {sourceName})
  :extractSourceRange(source, source.indexOf('bool BoxedWineConditionTimer::run()'));
 if (!multi && !source.includes('bool BoxedWineConditionTimer::run()')) throw new Error(`source slice marker not found in ${sourceName}: bool BoxedWineConditionTimer::run()`);
 const methods=['addParentCondition','removeParentCondition','parentsCount','parentSnapshot','signalAll'].map(name=>branch.match(new RegExp('(?:void|U32|std::vector<std::shared_ptr<BoxedWineCondition>>) BoxedWineCondition::'+name+'\\([^]*?\\n\\}'))?.[0]);
 assert.ok(methods.every(Boolean));
 const dir=mkdtempSync(join(tmpdir(),'condition-parents-'));
 try{
  const cls=`class BoxedWineCondition:public std::enable_shared_from_this<BoxedWineCondition>{public:std::mutex m;std::condition_variable c;void addParentCondition(const std::shared_ptr<BoxedWineCondition>&);void removeParentCondition(const std::shared_ptr<BoxedWineCondition>&);U32 parentsCount();std::vector<std::shared_ptr<BoxedWineCondition>> parentSnapshot();void signalAll();void signalThread(bool){c.notify_all();}std::atomic<uint64_t> wakeSequence{0};std::atomic<uint64_t> wakeSignalMicroseconds{0};private:std::vector<std::weak_ptr<BoxedWineCondition>> parents;std::mutex parentsMutex;};`;
  writeFileSync(join(dir,'check.cpp'),`#include <cstdint>\n#include <memory>\n#include <vector>\n#include <mutex>\n#include <thread>\n#include <condition_variable>\n#include <atomic>\n#include <chrono>\n#include <cassert>\nusing U32=uint32_t;\nstatic bool wakeProfileEnabled(){return false;}\nstruct KSystem{static uint64_t getMicroCounter(){return 0;}};\n#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(p) std::unique_lock<std::mutex> guard((p)->m)\n${cls}\n${methods.join('\n')}\nint main(){auto child=std::make_shared<BoxedWineCondition>();std::vector<std::shared_ptr<BoxedWineCondition>> ps;for(int i=0;i<16;i++){ps.push_back(std::make_shared<BoxedWineCondition>());child->addParentCondition(ps.back());child->addParentCondition(ps.back());}assert(child->parentsCount()==16);child->removeParentCondition(ps[3]);assert(child->parentsCount()==15);ps[7].reset();assert(child->parentsCount()==14);auto extra=std::make_shared<BoxedWineCondition>();child->addParentCondition(extra);assert(child->parentsCount()==15);${multi?`std::atomic<int> woke{0},ready{0};std::vector<std::thread> ts;for(int i=0;i<3;i++)ts.emplace_back([&,i]{std::unique_lock<std::mutex> lock(ps[i]->m);ready++;if(ps[i]->c.wait_for(lock,std::chrono::seconds(2))==std::cv_status::no_timeout)woke++;});while(ready!=3)std::this_thread::yield();child->signalAll();for(auto& t:ts)t.join();assert(woke==3);ts.clear();for(int i=0;i<4;i++)ts.emplace_back([&,i]{for(int n=0;n<1000;n++){child->addParentCondition(ps[i]);child->parentsCount();child->removeParentCondition(ps[i]);}});for(auto& t:ts)t.join();`:''}ps.clear();extra.reset();assert(child->parentsCount()==0);}\n`);
  execFileSync('c++',['-std=c++17','-pthread',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'),[],{timeout:10000});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
