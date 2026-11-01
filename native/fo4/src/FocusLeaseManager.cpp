#include "FocusLeaseManager.h"

#include "CursorMode.h"
#include "DocumentRegistry.h"
#include "GameThreadDispatcher.h"
#include "InputMask.h"
#include "PauseHold.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace uirepo::fo4::FocusLeaseManager {
namespace {

std::mutex g_mutex;
input::FocusLeaseState g_state;
std::unordered_map<DocumentHandle, input::FocusRequirements> g_requirements;
std::atomic<bool> g_wantInputMask{};
std::atomic<bool> g_wantCursor{};
std::atomic<bool> g_wantEscape{};
std::atomic<bool> g_wantPause{};

[[nodiscard]] constexpr input::FocusRequirements inactiveRequirements() noexcept {
    return {false, false, false, false};
}

void publishRequirements(input::FocusRequirements requirements) noexcept {
    g_wantInputMask.store(requirements.inputMask, std::memory_order_release);
    g_wantCursor.store(requirements.cursor, std::memory_order_release);
    g_wantEscape.store(requirements.escape, std::memory_order_release);
    g_wantPause.store(requirements.pause, std::memory_order_release);
}

void applyDesiredResources() noexcept {
    const bool wantInputMask = g_wantInputMask.load(std::memory_order_acquire);
    const bool wantCursor = g_wantCursor.load(std::memory_order_acquire);
    const bool wantPause = g_wantPause.load(std::memory_order_acquire);

    if (wantInputMask) {
        InputMask::apply();
    } else {
        InputMask::release();
    }
    GameThreadDispatcher::applyCursorLease(wantCursor);
    CursorMode::setEnabled(wantCursor);
    PauseHold::setPaused(wantPause);
}

void scheduleResourceApply() noexcept {
    if (GameThreadDispatcher::dispatchSafety([] {
            applyDesiredResources();
        })) {
        return;
    }

    spdlog::warn(
        "UIRepo_F4 focus lease: verified dispatcher unavailable; desired lease resources will be retried on the next transition");
}

void publishAndSchedule(
    const input::FocusLeaseTransition& transition,
    input::FocusRequirements requirements) noexcept {
    publishRequirements(requirements);
    if (transition.changed()) {
        scheduleResourceApply();
    }
}

[[nodiscard]] input::FocusRequirements requirementsForLocked(DocumentHandle document) noexcept {
    const auto found = g_requirements.find(document);
    return found == g_requirements.end() ? input::FocusRequirements{} : found->second;
}

} // namespace

void setRequirements(DocumentHandle document, input::FocusRequirements requirements) noexcept {
    if (document == 0) {
        return;
    }

    input::FocusLeaseTransition transition{};
    input::FocusRequirements desired{};
    {
        std::scoped_lock lock(g_mutex);
        g_requirements[document] = requirements;
        if (g_state.focusedDocument() == document) {
            transition = g_state.focus(document, requirements);
        }
        desired = g_state.active() ? g_state.requirements() : inactiveRequirements();
    }
    publishAndSchedule(transition, desired);
}

void clearRequirements(DocumentHandle document) noexcept {
    if (document == 0) {
        return;
    }

    input::FocusLeaseTransition transition{};
    input::FocusRequirements desired{};
    {
        std::scoped_lock lock(g_mutex);
        g_requirements.erase(document);
        if (g_state.focusedDocument() == document) {
            transition = g_state.focus(document, input::FocusRequirements{});
        }
        desired = g_state.active() ? g_state.requirements() : inactiveRequirements();
    }
    publishAndSchedule(transition, desired);
}

void sync(DocumentHandle document, input::FocusRequirements requirements) noexcept {
    input::FocusLeaseTransition transition{};
    input::FocusRequirements desired{};
    {
        std::scoped_lock lock(g_mutex);
        transition = g_state.focus(document, requirements);
        desired = g_state.active() ? g_state.requirements() : inactiveRequirements();
    }
    publishAndSchedule(transition, desired);
}

void syncFromRegistry() noexcept {
    const auto states = DocumentRegistry::instance().enumerateDocuments();

    DocumentHandle focused{};
    std::unordered_set<DocumentHandle> live;
    live.reserve(states.size());
    for (const auto& state : states) {
        live.insert(state.handle);
        if (state.focused) {
            focused = state.handle;
        }
    }

    input::FocusLeaseTransition transition{};
    input::FocusRequirements desired{};
    {
        std::scoped_lock lock(g_mutex);
        for (auto it = g_requirements.begin(); it != g_requirements.end();) {
            if (!live.contains(it->first)) {
                it = g_requirements.erase(it);
            } else {
                ++it;
            }
        }

        if (focused != 0) {
            transition = g_state.focus(focused, requirementsForLocked(focused));
        } else {
            transition = g_state.release();
        }
        desired = g_state.active() ? g_state.requirements() : inactiveRequirements();
    }
    publishAndSchedule(transition, desired);
}

void release(DocumentHandle document) noexcept {
    input::FocusLeaseTransition transition{};
    input::FocusRequirements desired{};
    {
        std::scoped_lock lock(g_mutex);
        transition = g_state.release(document);
        desired = g_state.active() ? g_state.requirements() : inactiveRequirements();
    }
    publishAndSchedule(transition, desired);
}

void releaseAll() noexcept {
    release(0);
}

DocumentHandle focusedDocument() noexcept {
    std::scoped_lock lock(g_mutex);
    return g_state.focusedDocument();
}

std::uint64_t episodeGeneration() noexcept {
    std::scoped_lock lock(g_mutex);
    return g_state.episodeGeneration();
}

bool ownsEscape() noexcept {
    return g_wantEscape.load(std::memory_order_acquire);
}

bool ownsCursor() noexcept {
    return g_wantCursor.load(std::memory_order_acquire);
}

bool pauseRequested() noexcept {
    return g_wantPause.load(std::memory_order_acquire);
}

bool inputMaskRequested() noexcept {
    return g_wantInputMask.load(std::memory_order_acquire);
}

void markPresented() noexcept {
    std::scoped_lock lock(g_mutex);
    g_state.markPresented();
}

void advancePresentationFrame(
    bool presentationExpected,
    bool framePresented,
    bool suspended) noexcept {
    input::FocusLeaseTransition transition{};
    bool releaseRequired = false;
    {
        std::scoped_lock lock(g_mutex);
        if (g_state.advancePresentationFrame(
                presentationExpected,
                framePresented,
                suspended) == input::FocusRecoveryDecision::Release) {
            transition = g_state.release();
            releaseRequired = transition.changed();
        }
    }

    if (!releaseRequired) {
        return;
    }

    publishRequirements(inactiveRequirements());
    scheduleResourceApply();
    spdlog::warn("UIRepo_F4 focus lease: stale presentation exceeded recovery grace; lease released");
}

} // namespace uirepo::fo4::FocusLeaseManager
