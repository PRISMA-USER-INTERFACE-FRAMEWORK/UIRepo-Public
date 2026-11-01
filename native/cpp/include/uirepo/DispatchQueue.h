#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>

namespace uirepo::dispatch {

inline constexpr std::size_t kMaxQueue = 1024;
inline constexpr std::size_t kMaxTasksPerWake = 64;

using OwnerHandle = std::uint64_t;

class Queue final {
public:
    struct EnqueueResult {
        bool accepted{};
        bool needsWake{};
    };

    struct Task {
        std::uint64_t generation{};
        OwnerHandle owner{};
        bool safety{};
        std::function<void()> function;
    };

    [[nodiscard]] bool ready() const {
        std::lock_guard lock(mutex_);
        return ready_ && !failed_;
    }

    [[nodiscard]] bool failed() const {
        std::lock_guard lock(mutex_);
        return failed_;
    }

    [[nodiscard]] std::uint64_t generation() const {
        std::lock_guard lock(mutex_);
        return generation_;
    }

    [[nodiscard]] bool isExecuting(OwnerHandle owner) const {
        std::lock_guard lock(mutex_);
        return executing_ && executingOwner_ == owner;
    }

    [[nodiscard]] bool isCancelled(OwnerHandle owner) const {
        std::lock_guard lock(mutex_);
        return owner != 0 && cancelled_.contains(owner);
    }

    [[nodiscard]] bool draining() const {
        std::lock_guard lock(mutex_);
        return draining_;
    }

    void attach() {
        std::lock_guard lock(mutex_);
        if (failed_) {
            return;
        }
        ++generation_;
        ready_ = true;
        wakePending_ = false;
        draining_ = false;
        queue_.clear();
    }

    void detach() {
        std::lock_guard lock(mutex_);
        ++generation_;
        ready_ = false;
        wakePending_ = false;
        draining_ = false;
        queue_.clear();
        condition_.notify_all();
    }

    void failClosed() {
        std::lock_guard lock(mutex_);
        ++generation_;
        ready_ = false;
        failed_ = true;
        wakePending_ = false;
        draining_ = false;
        queue_.clear();
        condition_.notify_all();
    }

    [[nodiscard]] EnqueueResult tryEnqueue(
        std::function<void()> function,
        OwnerHandle owner = 0,
        bool safety = false) {
        if (!function) {
            return {};
        }

        std::lock_guard lock(mutex_);
        if (failed_ || !ready_) {
            return {};
        }
        if (owner != 0 && cancelled_.contains(owner)) {
            return {};
        }
        if (queue_.size() >= kMaxQueue && (!safety || hasSafetyLocked())) {
            return {};
        }

        const bool needsWake = !wakePending_ && !draining_;
        if (needsWake) {
            wakePending_ = true;
        }
        queue_.push_back(Task{generation_, owner, safety, std::move(function)});
        return {.accepted = true, .needsWake = needsWake};
    }

    void cancelOwner(OwnerHandle owner) {
        if (owner == 0) {
            return;
        }
        std::lock_guard lock(mutex_);
        cancelled_.insert(owner);
        std::erase_if(queue_, [owner](const Task& task) {
            return task.owner == owner;
        });
    }

    void beginDrain() {
        std::lock_guard lock(mutex_);
        draining_ = true;
        wakePending_ = false;
    }

    void endDrain() {
        std::lock_guard lock(mutex_);
        draining_ = false;
    }

    [[nodiscard]] std::optional<Task> takeNext(
        const std::function<bool(OwnerHandle)>& ownerLive = {}) {
        std::lock_guard lock(mutex_);
        if (executing_) {
            return std::nullopt;
        }

        while (!queue_.empty()) {
            Task task = std::move(queue_.front());
            queue_.pop_front();
            if (!ready_ || failed_ || task.generation != generation_) {
                continue;
            }
            if (task.owner != 0 && cancelled_.contains(task.owner)) {
                continue;
            }
            if (task.owner != 0 && ownerLive && !ownerLive(task.owner)) {
                continue;
            }

            executing_ = true;
            executingOwner_ = task.owner;
            return task;
        }
        return std::nullopt;
    }

    void finishExecuting() {
        std::lock_guard lock(mutex_);
        executing_ = false;
        executingOwner_ = 0;
        condition_.notify_all();
    }

    [[nodiscard]] bool waitUntilNotExecutingFor(
        OwnerHandle owner,
        std::chrono::milliseconds timeout) noexcept {
        if (owner == 0) {
            return true;
        }
        try {
            std::unique_lock lock(mutex_);
            return condition_.wait_for(lock, timeout, [&] {
                return !executing_ || executingOwner_ != owner;
            });
        } catch (...) {
            return false;
        }
    }

    void waitUntilNotExecuting(OwnerHandle owner) {
        while (!waitUntilNotExecutingFor(owner, std::chrono::seconds(5))) {
        }
    }

    [[nodiscard]] bool takeWakeRearm() {
        std::lock_guard lock(mutex_);
        if (draining_ || !ready_ || failed_ || queue_.empty() || wakePending_) {
            return false;
        }
        wakePending_ = true;
        return true;
    }

private:
    [[nodiscard]] bool hasSafetyLocked() const {
        for (const Task& task : queue_) {
            if (task.safety) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_set<OwnerHandle> cancelled_;
    std::uint64_t generation_{};
    OwnerHandle executingOwner_{};
    bool ready_{};
    bool failed_{};
    bool wakePending_{};
    bool draining_{};
    bool executing_{};
    std::deque<Task> queue_;
};

} // namespace uirepo::dispatch
