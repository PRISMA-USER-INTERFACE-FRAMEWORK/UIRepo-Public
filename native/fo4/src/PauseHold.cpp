#include "PauseHold.h"

#include "uirepo/PausePolicy.h"

#include <F4SE/F4SE.h>
#include <spdlog/spdlog.h>

namespace uirepo::fo4 {
namespace {

pause::State g_policy;

void queueMessageForCurrentTarget() noexcept {
    const auto* tasks = F4SE::GetTaskInterface();
    if (tasks == nullptr) {
        spdlog::warn("UIRepo_F4 PauseHold: F4SE task interface unavailable");
        return;
    }

    tasks->AddUITask([] {
        bool wantPaused = false;
        if (!g_policy.shouldSendMessage(wantPaused)) {
            return;
        }

        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (queue == nullptr) {
            spdlog::warn("UIRepo_F4 PauseHold: UIMessageQueue unavailable");
            return;
        }

        queue->AddMessage(
            PauseHold::kMenuName,
            wantPaused ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide);
        g_policy.markMessageSent(wantPaused);
    });
}

} // namespace

RE::IMenu* PauseHold::creator(const RE::UIMessage&) {
    try {
        auto* menu = new PauseHold();
        if (!menu->valid()) {
            delete menu;
            return nullptr;
        }
        return menu;
    } catch (...) {
        spdlog::error("UIRepo_F4 PauseHold: menu construction failed");
        return nullptr;
    }
}

PauseHold::PauseHold() {
    using MenuFlag = RE::UI_MENU_FLAGS;

    auto* scaleform = RE::BSScaleformManager::GetSingleton();
    if (scaleform == nullptr) {
        spdlog::error("UIRepo_F4 PauseHold: BSScaleformManager unavailable");
        return;
    }

    // Fallout requires a valid movie-backed IMenu. Reuse the tiny vanilla cursor movie but keep it
    // invisible. UIRepo never renders through Scaleform; this instance exists only to own kPausesGame.
    if (!scaleform->LoadMovieEx(*this, "Interface/CursorMenu.swf") || uiMovie == nullptr) {
        spdlog::error("UIRepo_F4 PauseHold: failed to load Interface/CursorMenu.swf");
        return;
    }
    uiMovie->SetVisible(false);

    menuFlags.set(
        MenuFlag::kPausesGame,
        MenuFlag::kAllowSaving,
        MenuFlag::kAdvancesUnderPauseMenu,
        MenuFlag::kRendersUnderPauseMenu);
}

void PauseHold::AdvanceMovie(float, std::uint64_t) {}

RE::UI_MESSAGE_RESULTS PauseHold::ProcessMessage(RE::UIMessage& message) {
    if (message.menu == kMenuName) {
        return RE::UI_MESSAGE_RESULTS::kHandled;
    }
    return RE::UI_MESSAGE_RESULTS::kPassOn;
}

void PauseHold::setPaused(bool paused) noexcept {
    g_policy.setTarget(paused);
    queueMessageForCurrentTarget();
}

bool PauseHold::isOpen() noexcept {
    auto* ui = RE::UI::GetSingleton();
    return ui != nullptr && ui->GetMenuOpen(kMenuName);
}

bool PauseHold::isRequested() noexcept {
    return g_policy.targetPaused();
}

void PauseHold::tick() noexcept {
    if (!g_policy.advancePresentedFrame()) {
        return;
    }

    const auto* tasks = F4SE::GetTaskInterface();
    if (tasks == nullptr) {
        spdlog::warn("UIRepo_F4 PauseHold: cannot verify state without F4SE task interface");
        return;
    }

    tasks->AddUITask([] {
        const bool wantPaused = g_policy.targetPaused();
        const bool open = PauseHold::isOpen();
        if (open == wantPaused) {
            spdlog::info("UIRepo_F4 PauseHold: settled at paused={}", wantPaused);
        } else {
            spdlog::warn(
                "UIRepo_F4 PauseHold: requested paused={} but menu is {}",
                wantPaused,
                open ? "open" : "closed");
        }
    });
}

} // namespace uirepo::fo4
