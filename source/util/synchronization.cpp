/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#include <cstdlib>
#include <cstring>
#ifdef BOXEDWINE_MULTI_THREADED

namespace {
enum WakeProfileCategory : U8 {
    WakeProfileOther = 0,
    WakeProfileSocket = 1,
    WakeProfilePoll = 2,
    WakeProfileFutex = 3,
    WakeProfileCategoryCount = 4
};

struct WakeProfileStats {
    std::atomic<U64> count{0};
    std::atomic<U64> totalMicroseconds{0};
    std::atomic<U64> maxMicroseconds{0};
    std::atomic<U64> overOneMillisecond{0};
    std::atomic<U64> overTenMilliseconds{0};
};

WakeProfileStats g_wakeProfileStats[WakeProfileCategoryCount];
std::atomic<U64> g_wakeProfileSamples{0};

bool wakeProfileEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("BW_WAKE_PROFILE");
        return value && value[0] == '1';
    }();
    return enabled;
}

U8 wakeProfileCategory(const BString& name) {
    const char* value = name.c_str();
    if (std::strcmp(value, "KUnixSocketObject::lockCond") == 0) return WakeProfileSocket;
    if (std::strcmp(value, "KThread::pollCond") == 0) return WakeProfilePoll;
    if (std::strcmp(value, "futex") == 0) return WakeProfileFutex;
    return WakeProfileOther;
}

void reportWakeProfile() {
    static const char* names[WakeProfileCategoryCount] = {"other", "socket", "poll", "futex"};
    for (U8 i = 1; i < WakeProfileCategoryCount; ++i) {
        const WakeProfileStats& stats = g_wakeProfileStats[i];
        const U64 count = stats.count.load(std::memory_order_relaxed);
        if (!count) continue;
        klog_fmt("WAKEPROFILE category=%s count=%llu mean_us=%llu max_us=%llu over_1ms=%llu over_10ms=%llu",
                 names[i], (unsigned long long)count,
                 (unsigned long long)(stats.totalMicroseconds.load(std::memory_order_relaxed) / count),
                 (unsigned long long)stats.maxMicroseconds.load(std::memory_order_relaxed),
                 (unsigned long long)stats.overOneMillisecond.load(std::memory_order_relaxed),
                 (unsigned long long)stats.overTenMilliseconds.load(std::memory_order_relaxed));
    }
}

void recordWakeLatency(U8 category, U64 latencyMicroseconds) {
    if (category == WakeProfileOther || category >= WakeProfileCategoryCount) return;
    WakeProfileStats& stats = g_wakeProfileStats[category];
    stats.count.fetch_add(1, std::memory_order_relaxed);
    stats.totalMicroseconds.fetch_add(latencyMicroseconds, std::memory_order_relaxed);
    if (latencyMicroseconds >= 1000) stats.overOneMillisecond.fetch_add(1, std::memory_order_relaxed);
    if (latencyMicroseconds >= 10000) stats.overTenMilliseconds.fetch_add(1, std::memory_order_relaxed);
    U64 previousMax = stats.maxMicroseconds.load(std::memory_order_relaxed);
    while (previousMax < latencyMicroseconds &&
           !stats.maxMicroseconds.compare_exchange_weak(previousMax, latencyMicroseconds,
                                                        std::memory_order_relaxed)) {
    }
    if ((g_wakeProfileSamples.fetch_add(1, std::memory_order_relaxed) + 1) % 256 == 0) {
        reportWakeProfile();
    }
}
}

BoxedWineCriticalSectionCond::BoxedWineCriticalSectionCond(const std::shared_ptr<BoxedWineCondition>& cond) {
    this->cond = cond;
    this->cond->lock();
}
BoxedWineCriticalSectionCond::~BoxedWineCriticalSectionCond() {
    this->cond->unlock();
}

BoxedWineCondition::BoxedWineCondition(BString name) : name(name), wakeProfileCategory(::wakeProfileCategory(name)) {
}

BoxedWineCondition::~BoxedWineCondition() {
    m.lock(); // race condition when all threads are shuting down, just make sure no one has the lock when we destroy it
    m.unlock();
}

void BoxedWineCondition::lock() {
    this->m.lock();
    if (KThread::currentThread()) {
        this->lockOwner = KThread::currentThread()->id;
    } else {
        this->lockOwner = 0xFFFFFFFF;
    }
}
 
bool BoxedWineCondition::tryLock() {
    if (this->m.try_lock()) {
        if (KThread::currentThread()) {
            this->lockOwner = KThread::currentThread()->id;
        } else {
            this->lockOwner = 0xFFFFFFFF;
        }
        return true;
    }
    return false;
}

void BoxedWineCondition::signal() {
    for (const auto& parent : parentSnapshot()) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(parent);
        parent->signal();
        break;
    }
    if (wakeProfileEnabled()) {
        wakeSignalMicroseconds.store(KSystem::getMicroCounter(), std::memory_order_relaxed);
        wakeSequence.fetch_add(1, std::memory_order_release);
    }
    this->c.notify_one();
}

void BoxedWineCondition::signalAll() {
    for (const auto& parent : parentSnapshot()) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(parent);
        parent->signalAll();
    }
    if (wakeProfileEnabled()) {
        wakeSignalMicroseconds.store(KSystem::getMicroCounter(), std::memory_order_relaxed);
        wakeSequence.fetch_add(1, std::memory_order_release);
    }
    this->c.notify_all();
}

void BoxedWineCondition::wait(std::unique_lock<std::mutex>& lock) {
    KThread* thread = KThread::currentThread();
    const bool profileWake = wakeProfileEnabled();
    const U64 signalSequence = profileWake ? wakeSequence.load(std::memory_order_acquire) : 0;
    if (thread) {
        thread->waitingCond = shared_from_this();
    }
    this->c.wait(lock);
    if (profileWake && wakeSequence.load(std::memory_order_acquire) != signalSequence) {
        const U64 signaledAt = wakeSignalMicroseconds.load(std::memory_order_relaxed);
        const U64 resumedAt = KSystem::getMicroCounter();
        if (resumedAt >= signaledAt) recordWakeLatency(wakeProfileCategory, resumedAt - signaledAt);
    }
    if (thread) {
        thread->waitingCond = nullptr;
    }
    for (const auto& parent : parentSnapshot()) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(parent);
        parent->signalAll();
    }
}

void BoxedWineCondition::waitWithTimeout(std::unique_lock<std::mutex>& lock, U32 ms) {    
    KThread* thread = KThread::currentThread();
    const bool profileWake = wakeProfileEnabled();
    const U64 signalSequence = profileWake ? wakeSequence.load(std::memory_order_acquire) : 0;
    if (thread) {
        thread->waitingCond = shared_from_this();
    }
    const std::cv_status status = this->c.wait_for(lock, std::chrono::milliseconds(KSystem::emulatedMilliesToHost(ms)));
    if (profileWake && status == std::cv_status::no_timeout &&
        wakeSequence.load(std::memory_order_acquire) != signalSequence) {
        const U64 signaledAt = wakeSignalMicroseconds.load(std::memory_order_relaxed);
        const U64 resumedAt = KSystem::getMicroCounter();
        if (resumedAt >= signaledAt) recordWakeLatency(wakeProfileCategory, resumedAt - signaledAt);
    }
    if (thread) {
        thread->waitingCond = nullptr;
    }    
    for (const auto& parent : parentSnapshot()) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(parent);
        parent->signalAll();
    }
}

void BoxedWineCondition::unlock() {
    this->lockOwner = 0;
    this->m.unlock();
}

void BoxedWineCondition::addParentCondition(const std::shared_ptr<BoxedWineCondition>& parent) {
    const std::lock_guard<std::mutex> lock(parentsMutex);
    if (!parent) return;
    for (const auto& entry : parents) {
        if (entry.lock() == parent) return;
    }
    for (auto& entry : parents) {
        if (entry.expired()) { entry = parent; return; }
    }
    parents.push_back(parent);
}

void BoxedWineCondition::removeParentCondition(const std::shared_ptr<BoxedWineCondition>& parent) {
    const std::lock_guard<std::mutex> lock(parentsMutex);
    for (auto it = parents.begin(); it != parents.end();) {
        auto p = it->lock();
        if (!p || p == parent) it = parents.erase(it);
        else ++it;
    }
}

std::vector<std::shared_ptr<BoxedWineCondition>> BoxedWineCondition::parentSnapshot() {
    const std::lock_guard<std::mutex> lock(parentsMutex);
    std::vector<std::shared_ptr<BoxedWineCondition>> live;
    for (auto it = parents.begin(); it != parents.end();) {
        if (auto p = it->lock()) { live.push_back(p); ++it; }
        else it = parents.erase(it);
    }
    return live;
}

U32 BoxedWineCondition::parentsCount() {
    return (U32)parentSnapshot().size();
}

#else 

bool BoxedWineConditionTimer::run() {
    this->cond->signalThread(true);
    return false; // signal will remove timer
}

BoxedWineCondition::BoxedWineCondition(BString name) : name(name) {
}

BoxedWineCondition::~BoxedWineCondition() {
}

void BoxedWineCondition::signalThread(bool all) {
    while (this->waitingThreads.size()) {
        KThread* thread = this->waitingThreads.front()->data;
        this->waitingThreads.front()->remove();
#ifdef _DEBUG
        if (!thread->waitingCond) {
            kpanic("shouldn't signal a thread that is not waiting");
        }
#endif
        thread->waitingCond = nullptr;
        if (thread->condTimer.active) {
            removeTimer(&thread->condTimer);
            thread->condTimer.cond = nullptr;
        }
        scheduleThread(thread);
        if (!all) {
            break;
        }
    }
}

void BoxedWineCondition::signal() {
    this->signalThread(false);

    for (const auto& parent : parentSnapshot()) {
        parent->signal();
    }
}

void BoxedWineCondition::signalAll() {
    this->signalThread(true);

    for (const auto& parent : parentSnapshot()) {
        parent->signalAll();
    }
}

U32 BoxedWineCondition::wait() {
    this->waitingThreads.addToBack(&KThread::currentThread()->waitThreadNode);
    KThread::currentThread()->waitingCond = shared_from_this();
    unscheduleThread(KThread::currentThread());
    return -K_WAIT;
}

U32 BoxedWineCondition::waitWithTimeout(U32 ms) {
    KThread* thread = KThread::currentThread();
    thread->condTimer.millies = ms + KSystem::getMilliesSinceStart();
    thread->condTimer.cond = this;
    addTimer(&thread->condTimer);
    this->waitingThreads.addToBack(&KThread::currentThread()->waitThreadNode);
    KThread::currentThread()->waitingCond = shared_from_this();
    unscheduleThread(KThread::currentThread());
    return -K_WAIT;    
}
 
U32 BoxedWineCondition::waitCount() {
    return this->waitingThreads.size() + (parentsCount() > 0 ? 1 : 0);
}

void BoxedWineCondition::addParentCondition(const std::shared_ptr<BoxedWineCondition>& parent) {
    if (!parent) return;
    for (const auto& entry : parents) {
        if (entry.lock() == parent) return;
    }
    for (auto& entry : parents) {
        if (entry.expired()) { entry = parent; return; }
    }
    parents.push_back(parent);
}

void BoxedWineCondition::removeParentCondition(const std::shared_ptr<BoxedWineCondition>& parent) {
    for (auto it = parents.begin(); it != parents.end();) {
        auto p = it->lock();
        if (!p || p == parent) it = parents.erase(it);
        else ++it;
    }
}

std::vector<std::shared_ptr<BoxedWineCondition>> BoxedWineCondition::parentSnapshot() {
    std::vector<std::shared_ptr<BoxedWineCondition>> live;
    for (auto it = parents.begin(); it != parents.end();) {
        if (auto p = it->lock()) { live.push_back(p); ++it; }
        else it = parents.erase(it);
    }
    return live;
}

U32 BoxedWineCondition::parentsCount() {
    return (U32)parentSnapshot().size();
}
#endif
