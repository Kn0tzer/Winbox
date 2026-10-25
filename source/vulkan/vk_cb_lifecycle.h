#ifndef VK_CB_LIFECYCLE_H
#define VK_CB_LIFECYCLE_H

// Recording lifecycle of one command buffer, tracked for the queue-submit
// fallback in vk64bridge.cpp.
//
// The fallback exists because DXVK can record draws into command buffers
// that the matching vkQueueSubmit batch does not reference (observed during
// D3D9 bring-up, 2026-10-05: draws recorded to 3 CBs while the submits
// referenced empty ones). The original scan picked up every live CmdBuf
// with a non-empty command list, which is not safe: a buffer another guest
// thread is still recording has a command vector that mutates under the
// scan (observed 2026-10-25 in the D3D9 Pong workload probe: garbage
// command counts in the billions, dropped commands, a WASM trap), and a
// buffer that was already submitted replays stale work.
//
// The fallback may therefore only pick up a buffer that
//   - reached vkEndCommandBuffer since its last begin/reset, and
//   - has not been submitted since: explicitly, through the fallback, or
//     by being executed into a primary via vkCmdExecuteCommands (its
//     commands then travel with the primary).
// An ended buffer's command list is stable until the next begin/reset —
// the guest may not record into it meanwhile — so the submission thread can
// read it without extra locking beyond the guest's own ordering.
namespace vkcb {
enum class CbState : unsigned char { Initial, Recording, Ended };
struct CbLifecycle {
    CbState state = CbState::Initial;
    bool submittedSinceEnd = false;
    // vkBeginCommandBuffer (the bridge resets the buffer first, so a begin
    // always lands here from Initial/Ended with a clean command list).
    void onBegin() { state = CbState::Recording; submittedSinceEnd = false; }
    // vkEndCommandBuffer. Only a recording buffer can end; an end in any
    // other state is a guest error and leaves the state untouched.
    void onEnd() {
        if (state != CbState::Recording) return;
        state = CbState::Ended;
        submittedSinceEnd = false;
    }
    // vkResetCommandBuffer / vkResetCommandPool / the reset inside begin.
    void onReset() { state = CbState::Initial; submittedSinceEnd = false; }
    // The buffer's commands were submitted (any path). Only meaningful
    // for an ended buffer; a mid-record or never-ended buffer has nothing
    // to consume.
    void onSubmitted() { if (state == CbState::Ended) submittedSinceEnd = true; }
    // Queue-submit fallback gate: ended, not yet submitted, has commands.
    bool fallbackEligible(bool hasCommands) const {
        return state == CbState::Ended && !submittedSinceEnd && hasCommands;
    }
};
} // namespace vkcb
#endif
