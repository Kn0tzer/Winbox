#ifndef __KPIDFD_H__
#define __KPIDFD_H__

#include "kevent.h"
#include "kerror.h"

// A pidfd: an event object that becomes readable when the target
// process terminates. Moved here from syscall64.cpp (kernel round 3C)
// so the 32-bit clone/clone3 paths can create pidfds too; the class is
// unchanged.
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

// Create a pidfd for processId in the calling thread's process fd
// table. Verbatim body of syscall64.cpp's createPidFdForClone64.
inline U32 createPidFdForProcess(KThread* thread, U32 processId) {
    if (!thread || !thread->process) return (U32)-K_ESRCH;
    KProcessPtr target = KSystem::getProcess(processId);
    if (!target) return (U32)-K_ESRCH;
    std::shared_ptr<KPidFd> object = std::make_shared<KPidFd>(target, false);
    KFileDescriptorPtr result = thread->process->allocFileDescriptor(object, K_O_RDWR, FD_CLOEXEC, -1, 0);
    return result ? result->handle : (U32)-K_EMFILE;
}

#endif
