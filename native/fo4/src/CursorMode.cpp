#include "CursorMode.h"

#include "Runtime.h"
#include "uirepo/PausePolicy.h"

#include <F4SE/F4SE.h>
#include <spdlog/spdlog.h>

#include <Windows.h>

namespace uirepo::fo4 {
namespace {

pause::State g_policy;
bool g_vanillaCursorHidden{};

void setVanillaCursorVisible(bool visible) noexcept {
    auto* ui = RE::UI::GetSingleton();
    if (ui == nullptr) {
        return;
    }
    auto menu = ui->GetMenu(RE::CursorMenu::MENU_NAME);
    if (menu && menu->uiMovie) {
        menu->uiMovie->SetVisible(visible);
    }
}

void logMenuCursorState(const char* context) noexcept {
    auto* cursor = RE::MenuCursor::GetSingleton();
    if (cursor == nullptr) {
        spdlog::warn("UIRepo_F4 CursorMode: MenuCursor singleton unavailable ({})", context);
        return;
    }
    spdlog::info(
        "UIRepo_F4 CursorMode: {} MenuCursor registered={} pos=({},{}) forceOSCursorPos={}",
        context,
        cursor->registeredCursors,
        cursor->cursorPosX,
        cursor->cursorPosY,
        cursor->forceOSCursorPos);
}

void queueMessageForCurrentTarget() noexcept {
    const auto* tasks = F4SE::GetTaskInterface();
    if (tasks == nullptr) {
        spdlog::warn("UIRepo_F4 CursorMode: F4SE task interface unavailable");
        return;
    }

    tasks->AddUITask([] {
        bool wantEnabled = false;
        if (!g_policy.shouldSendMessage(wantEnabled)) {
            return;
        }

        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (queue == nullptr) {
            spdlog::warn("UIRepo_F4 CursorMode: UIMessageQueue unavailable");
            return;
        }

        queue->AddMessage(
            CursorMode::kMenuName,
            wantEnabled ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide);
        g_policy.markMessageSent(wantEnabled);

        if (auto* ui = RE::UI::GetSingleton(); ui != nullptr) {
            ui->RefreshCursor();
        }
        logMenuCursorState(wantEnabled ? "requested" : "released");
        spdlog::info("UIRepo_F4 CursorMode: sent {} message", wantEnabled ? "kShow" : "kHide");
    });
}

}

RE::IMenu* CursorMode::creator(const RE::UIMessage&) {
    try {
        auto* menu = new CursorMode();
        if (!menu->valid()) {
            delete menu;
            return nullptr;
        }
        return menu;
    } catch (...) {
        spdlog::error("UIRepo_F4 CursorMode: menu construction failed");
        return nullptr;
    }
}

CursorMode::CursorMode() {
    using MenuFlag = RE::UI_MENU_FLAGS;

    auto* scaleform = RE::BSScaleformManager::GetSingleton();
    if (scaleform == nullptr) {
        spdlog::error("UIRepo_F4 CursorMode: BSScaleformManager unavailable");
        return;
    }

    if (!scaleform->LoadMovieEx(*this, "Interface/CursorMenu.swf") || uiMovie == nullptr) {
        spdlog::error("UIRepo_F4 CursorMode: failed to load Interface/CursorMenu.swf");
        return;
    }
    uiMovie->SetVisible(false);

    menuFlags.set(MenuFlag::kUsesCursor);
}

void CursorMode::AdvanceMovie(float, std::uint64_t) {}

RE::UI_MESSAGE_RESULTS CursorMode::ProcessMessage(RE::UIMessage& message) {
    if (message.menu == kMenuName) {
        return RE::UI_MESSAGE_RESULTS::kHandled;
    }
    return RE::UI_MESSAGE_RESULTS::kPassOn;
}

void CursorMode::setEnabled(bool enabled) noexcept {
    g_policy.setTarget(enabled);
    queueMessageForCurrentTarget();
}

bool CursorMode::isOpen() noexcept {
    auto* ui = RE::UI::GetSingleton();
    return ui != nullptr && ui->GetMenuOpen(kMenuName);
}

bool CursorMode::isRequested() noexcept {
    return g_policy.targetPaused();
}

void CursorMode::tick() noexcept {
    if (isRequested()) {
        ClipCursor(nullptr);
    }

    const bool suppressVanilla = isRequested() && Runtime::instance().customCursorAvailable();
    if (suppressVanilla) {
        setVanillaCursorVisible(false);
        g_vanillaCursorHidden = true;
    } else if (g_vanillaCursorHidden) {
        setVanillaCursorVisible(true);
        g_vanillaCursorHidden = false;
    }

    if (!g_policy.advancePresentedFrame()) {
        return;
    }

    const auto* tasks = F4SE::GetTaskInterface();
    if (tasks == nullptr) {
        spdlog::warn("UIRepo_F4 CursorMode: cannot verify state without F4SE task interface");
        return;
    }

    tasks->AddUITask([] {
        const bool wantEnabled = g_policy.targetPaused();
        const bool open = CursorMode::isOpen();
        if (open == wantEnabled) {
            spdlog::info("UIRepo_F4 CursorMode: settled at enabled={}", wantEnabled);
        } else {
            spdlog::warn(
                "UIRepo_F4 CursorMode: requested enabled={} but menu is {}",
                wantEnabled,
                open ? "open" : "closed");
        }
        logMenuCursorState("settle");
    });
}

}
