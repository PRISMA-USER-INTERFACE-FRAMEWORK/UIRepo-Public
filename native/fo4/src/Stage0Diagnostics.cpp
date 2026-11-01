#include "Stage0Diagnostics.h"

#include "SwapChainCompat.h"

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#include <spdlog/spdlog.h>

#include <Windows.h>
#include <CommCtrl.h>
#include <dxgi.h>

#include <atomic>
#include <iterator>
#include <mutex>
#include <string>

namespace uirepo::fo4::Stage0Diagnostics {
namespace {

constexpr UINT_PTR kWindowLifecycleSubclassId = 0x53544730u;

HWND g_windowLifecycleWindow{};
bool g_appActiveKnown{};
bool g_appActive{};
std::uint32_t g_appFocusTransition{};
std::uint32_t g_windowStyleTransition{};
std::atomic<std::uint32_t> g_pendingFocusRecoveryTransition{};
std::atomic<std::uint32_t> g_pendingWindowStyleRecoveryTransition{};

[[nodiscard]] std::string moduleNameForAddress(void* address) noexcept {
    if (address == nullptr) {
        return "<null>";
    }

    HMODULE module{};
    const DWORD flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(address), &module) || module == nullptr) {
        return "<unknown>";
    }

    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(module, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) {
        return "<unknown>";
    }

    std::string result(path, length);
    const auto slash = result.find_last_of("\\/");
    if (slash != std::string::npos) {
        result.erase(0, slash + 1);
    }
    return result;
}

LRESULT CALLBACK windowLifecycleProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR) {
    if (message == WM_ACTIVATEAPP) {
        const bool active = wParam != FALSE;
        const bool returningFromInactive = active && g_appActiveKnown && !g_appActive;
        if (!g_appActiveKnown || active != g_appActive) {
            g_appActiveKnown = true;
            g_appActive = active;
            ++g_appFocusTransition;
            spdlog::info(
                "UIRepo_F4 Stage0: app focus {} (transition {})",
                active ? "active" : "inactive",
                g_appFocusTransition);

            if (returningFromInactive) {
                g_pendingFocusRecoveryTransition.store(
                    g_appFocusTransition,
                    std::memory_order_release);
            }
        }
    } else if (message == WM_STYLECHANGED && lParam != 0) {
        const int index = static_cast<int>(wParam);
        const auto* styles = reinterpret_cast<const STYLESTRUCT*>(lParam);
        if ((index == GWL_STYLE || index == GWL_EXSTYLE) && styles->styleOld != styles->styleNew) {
            ++g_windowStyleTransition;
            spdlog::info(
                "UIRepo_F4 Stage0: window style changed (transition {}, index {}, old 0x{:08X}, new 0x{:08X})",
                g_windowStyleTransition,
                index,
                static_cast<std::uint32_t>(styles->styleOld),
                static_cast<std::uint32_t>(styles->styleNew));
            g_pendingWindowStyleRecoveryTransition.store(
                g_windowStyleTransition,
                std::memory_order_release);
        }
    }

    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, windowLifecycleProc, kWindowLifecycleSubclassId);
        if (g_windowLifecycleWindow == hwnd) {
            g_windowLifecycleWindow = nullptr;
            g_appActiveKnown = false;
            g_pendingFocusRecoveryTransition.store(0, std::memory_order_release);
            g_pendingWindowStyleRecoveryTransition.store(0, std::memory_order_release);
        }
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

class MainMenuLifecycleSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent& event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        if (event.menuName == RE::MainMenu::MENU_NAME) {
            spdlog::info(
                "UIRepo_F4 Stage0: main-menu {}",
                event.opening ? "opened" : "closed");
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

MainMenuLifecycleSink g_mainMenuSink;
std::once_flag g_mainMenuSinkOnce;
bool g_mainMenuSinkInstalled = false;

} // namespace

void logSwapChain(void* swapChainValue) noexcept {
    auto* swapChain = static_cast<IDXGISwapChain*>(swapChainValue);
    if (swapChain == nullptr) {
        spdlog::critical("UIRepo_F4 Stage0: swap chain is unavailable");
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(swapChain);
    if (vtable == nullptr || vtable[8] == nullptr || vtable[9] == nullptr || vtable[13] == nullptr) {
        spdlog::critical("UIRepo_F4 Stage0: swap-chain diagnostic vtable is invalid");
        return;
    }

    const auto policy = SwapChainCompat::classifyBackbuffer(swapChain);
    spdlog::info(
        "UIRepo_F4 Stage0: swap chain {} owners Present '{}' ResizeBuffers '{}' GetBuffer '{}' ({})",
        static_cast<const void*>(swapChain),
        moduleNameForAddress(vtable[8]),
        moduleNameForAddress(vtable[13]),
        policy.getBufferOwner,
        policy.borrowed ? "known frame-generation proxy / borrowed" : "normal COM ownership");
}

void logSessionEvent(std::uint32_t type, const void* data) noexcept {
    const char* name = nullptr;
    switch (type) {
    case F4SE::MessagingInterface::kPreLoadGame:
        name = "pre-load-game";
        break;
    case F4SE::MessagingInterface::kPostLoadGame:
        spdlog::info(
            "UIRepo_F4 Stage0: session event post-load-game success {}",
            data != nullptr ? "true" : "false");
        return;
    case F4SE::MessagingInterface::kPreSaveGame:
        name = "pre-save-game";
        break;
    case F4SE::MessagingInterface::kPostSaveGame:
        name = "post-save-game";
        break;
    case F4SE::MessagingInterface::kInputLoaded:
        name = "input-loaded";
        break;
    case F4SE::MessagingInterface::kNewGame:
        name = "new-game";
        break;
    case F4SE::MessagingInterface::kGameLoaded:
        name = "game-loaded";
        break;
    case F4SE::MessagingInterface::kGameDataReady:
        name = data != nullptr ? "game-data-ready" : "game-data-not-ready";
        break;
    default:
        return;
    }

    spdlog::info("UIRepo_F4 Stage0: session event {}", name);
}

bool installWindowLifecycleSink(void* windowValue) noexcept {
    auto* window = static_cast<HWND>(windowValue);
    if (window == nullptr) {
        return false;
    }
    if (g_windowLifecycleWindow == window) {
        return true;
    }

    if (g_windowLifecycleWindow != nullptr) {
        RemoveWindowSubclass(
            g_windowLifecycleWindow,
            windowLifecycleProc,
            kWindowLifecycleSubclassId);
        g_windowLifecycleWindow = nullptr;
        g_appActiveKnown = false;
        g_pendingFocusRecoveryTransition.store(0, std::memory_order_release);
        g_pendingWindowStyleRecoveryTransition.store(0, std::memory_order_release);
    }

    if (!SetWindowSubclass(window, windowLifecycleProc, kWindowLifecycleSubclassId, 0)) {
        return false;
    }

    g_windowLifecycleWindow = window;
    g_appActiveKnown = false;
    g_appFocusTransition = 0;
    g_windowStyleTransition = 0;
    g_pendingFocusRecoveryTransition.store(0, std::memory_order_release);
    g_pendingWindowStyleRecoveryTransition.store(0, std::memory_order_release);
    return true;
}

bool installMainMenuSink() noexcept {
    auto* ui = RE::UI::GetSingleton();
    if (ui == nullptr) {
        return false;
    }

    try {
        std::call_once(g_mainMenuSinkOnce, [ui]() {
            ui->RegisterSink<RE::MenuOpenCloseEvent>(&g_mainMenuSink);
            g_mainMenuSinkInstalled = true;
        });
    } catch (...) {
        return false;
    }

    return g_mainMenuSinkInstalled;
}

std::uint32_t consumeFocusRecoveryTransition() noexcept {
    return g_pendingFocusRecoveryTransition.exchange(0, std::memory_order_acq_rel);
}

std::uint32_t consumeWindowStyleRecoveryTransition() noexcept {
    return g_pendingWindowStyleRecoveryTransition.exchange(0, std::memory_order_acq_rel);
}

} // namespace uirepo::fo4::Stage0Diagnostics
