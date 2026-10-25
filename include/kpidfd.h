#ifndef __KPIDFD_H__
#define __KPIDFD_H__

#include "kevent.h"
#include "kerror.h"
#include "kpoll.h"

// pidfd descriptor object (kernel round 3C: moved out of syscall64.cpp so
// the 32-bit clone path can create pidfds too). A pidfd is a pollable
// handle on a process: it becomes readable once the target terminates.
class KPidFd final : public KEvent {
public:
    explicit KPidFd(const KProcessPtr& process, bool nonblock) : target(process) { blocking = !nonblock; }
    KProcessPtr process() const { return target.lock(); }
    bool isReadReady() override {
        KProcessPtr p = target.lock();
        return !p || p->isTerminated();
    }
    bool isWriteReady() override { return false; }
    void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override {
        KProcessPtr p = target.lock();
        if (p && (events & K_POLLIN)) BOXEDWINE_CONDITION_ADD_PARENT(p->exitOrExecCond, parentCondition);
    }
    U32 readNative(U8*, U32) override { return (U32)-K_EINVAL; }
    U32 writeNative(U8*, U32) override { return (U32)-K_EINVAL; }
private:
    KProcessWeakPtr target;
};

// Allocate a pidfd for processId on the calling thread's process
// (CLONE_PIDFD in clone/clone3, both widths). Returns the fd handle in
// the caller's table or a negative errno value.
inline U32 createPidFdForProcess(KThread* thread, U32 processId) {
    if (!thread || !thread->process) return (U32)-K_ESRCH;
    KProcessPtr target = KSystem::getProcess(processId);
    if (!target) return (U32)-K_ESRCH;
    std::shared_ptr<KPidFd> object = std::make_shared<KPidFd>(target, false);
    KFileDescriptorPtr result = thread->process->allocFileDescriptor(object, K_O_RDWR, FD_CLOEXEC, -1, 0);
    return result ? result->handle : (U32)-K_EMFILE;
}

#endif
