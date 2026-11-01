#pragma once

#include <atomic>

namespace uirepo::pause {

// Engine-independent state behind a Fallout pause-hold menu. The adapter owns actual UI messages
// and menu inspection; this policy owns target state, duplicate suppression, and verification timing.
class State final {
public:
    static constexpr int kVerifyAfterCompleteFrames = 8;

    void setTarget(bool paused) noexcept {
        targetPaused_.store(paused, std::memory_order_release);
    }

    [[nodiscard]] bool targetPaused() const noexcept {
        return targetPaused_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool shouldSendMessage(bool& paused) const noexcept {
        paused = targetPaused();
        if (!haveSent_.load(std::memory_order_acquire)) {
            return true;
        }
        return sentValue_.load(std::memory_order_acquire) != paused;
    }

    // Call only after Fallout accepted the kShow/kHide message. Missing UIMessageQueue must not
    // create a fake sent state or suppress a later retry.
    void markMessageSent(bool paused) noexcept {
        sentValue_.store(paused, std::memory_order_relaxed);
        haveSent_.store(true, std::memory_order_release);
        verifyFrames_.store(0, std::memory_order_release);
    }

    // Called once per completed presented frame. Returns true once when engine verification is due.
    [[nodiscard]] bool advancePresentedFrame() noexcept {
        int frames = verifyFrames_.load(std::memory_order_acquire);
        for (;;) {
            if (frames < 0) {
                return false;
            }
            if (frames < kVerifyAfterCompleteFrames) {
                if (verifyFrames_.compare_exchange_weak(
                        frames,
                        frames + 1,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return false;
                }
                continue;
            }
            if (verifyFrames_.compare_exchange_weak(
                    frames,
                    -1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
    }

private:
    std::atomic<bool> targetPaused_{};
    std::atomic<bool> sentValue_{};
    std::atomic<bool> haveSent_{};
    std::atomic<int> verifyFrames_{-1};
};

} // namespace uirepo::pause
