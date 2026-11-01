#include "ControllerInput.h"
#include "CursorMode.h"
#include "DocumentRegistry.h"
#include "GameThreadDispatcher.h"
#include "PackageHostStartup.h"
#include "PauseHold.h"
#include "RenderHook.h"
#include "Stage0Diagnostics.h"

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <string>

#ifndef UIREPO_COMMONLIBF4_PIN
#define UIREPO_COMMONLIBF4_PIN "unknown"
#endif

#ifndef UIREPO_BUILD_SHA
#define UIREPO_BUILD_SHA "local"
#endif

namespace {

std::once_flag g_f4seInitOnce;
bool g_f4seInitialized = false;
std::once_flag g_listenerOnce;
bool g_listenerRegistered = false;

void cancelRegistryOwner(
    uirepo::fo4::DocumentRegistry::OwnerHandle owner,
    void*) noexcept {
    uirepo::fo4::GameThreadDispatcher::cancelOwner(owner);
}

void onF4SEMessage(F4SE::MessagingInterface::Message* message) {
    if (message == nullptr) {
        return;
    }

    uirepo::fo4::Stage0Diagnostics::logSessionEvent(message->type, message->data);

    if (message->type == F4SE::MessagingInterface::kGameDataReady) {
        if (message->data == nullptr) {
            return;
        }

        auto* rendererData = RE::BSGraphics::GetRendererData();
        if (rendererData != nullptr && rendererData->renderWindow[0].hwnd != nullptr) {
            const auto hwnd = rendererData->renderWindow[0].hwnd;

            // SetWindowSubclass (dispatcher wake + lifecycle sink) is only valid on the thread that
            // owns the window -- the Fallout render thread. kGameDataReady is not guaranteed to
            // arrive on that thread, so defer window bring-up to the first render-hook frame, which
            // always runs on it. See docs/in-game-acceptance.md (dispatcher thread-capture finding).
            uirepo::fo4::GameThreadDispatcher::queueGameThreadBootstrap([hwnd]() {
                uirepo::fo4::GameThreadDispatcher::captureCurrentThread();

                if (!uirepo::fo4::GameThreadDispatcher::attachWindow(hwnd)) {
                    spdlog::critical(
                        "UIRepo_F4 Stage0: verified game-thread dispatcher is unavailable; future engine/UI callbacks will fail closed");
                } else {
                    spdlog::info(
                        "UIRepo_F4 Stage0: Fallout render window {} attached to the game-thread dispatcher",
                        static_cast<const void*>(hwnd));
                }

                if (!uirepo::fo4::Stage0Diagnostics::installWindowLifecycleSink(
                        reinterpret_cast<void*>(hwnd))) {
                    spdlog::warn("UIRepo_F4 Stage0: window lifecycle sink could not be installed");
                } else {
                    spdlog::info("UIRepo_F4 Stage0: window lifecycle sink installed");
                }
            });

            if (rendererData->renderWindow[0].swapChain != nullptr) {
                uirepo::fo4::Stage0Diagnostics::logSwapChain(
                    reinterpret_cast<void*>(rendererData->renderWindow[0].swapChain));
            } else {
                spdlog::critical("UIRepo_F4 Stage0: Fallout swap chain is unavailable");
            }
        } else {
            spdlog::critical(
                "UIRepo_F4 Stage0: Fallout render window is unavailable; game-thread dispatcher not attached");
        }

        if (!uirepo::fo4::ControllerInput::install()) {
            spdlog::warn("UIRepo_F4: controller navigation handler is unavailable");
        }

        if (auto* ui = RE::UI::GetSingleton(); ui != nullptr) {
            ui->RegisterMenu(
                uirepo::fo4::PauseHold::kMenuName.data(),
                uirepo::fo4::PauseHold::creator);
            spdlog::info("UIRepo_F4: registered isolated pause-hold menu");

            ui->RegisterMenu(
                uirepo::fo4::CursorMode::kMenuName.data(),
                uirepo::fo4::CursorMode::creator);
            spdlog::info("UIRepo_F4: registered cursor-mode menu (kUsesCursor)");

            if (!uirepo::fo4::Stage0Diagnostics::installMainMenuSink()) {
                spdlog::warn("UIRepo_F4 Stage0: MainMenu lifecycle sink could not be installed");
            } else {
                spdlog::info("UIRepo_F4 Stage0: MainMenu lifecycle sink installed");
            }
        } else {
            spdlog::warn("UIRepo_F4: UI singleton unavailable; pause-hold menu not registered");
        }

        uirepo::fo4::PackageHostStartup::run();

        if (!uirepo::fo4::RenderHook::install()) {
            spdlog::critical("UIRepo_F4 Stage0: native renderer hooks failed to install");
            return;
        }
        spdlog::info("UIRepo_F4 Stage0: native renderer probe is active");
    }
}

bool initializeF4SE(const F4SE::LoadInterface* interface) noexcept {
    if (interface == nullptr) {
        return false;
    }

    try {
        std::call_once(g_f4seInitOnce, [interface]() {
            F4SE::Init(interface, F4SE::InitInfo{
                .logName = "UIRepo_F4",
                // Rotate on open (keep the newest 10 launches) so each run's log is preserved
                // instead of clobbering the previous run's in-game acceptance evidence. The
                // current run always stays at UIRepo_F4.log; prior runs become UIRepo_F4.1.log..
                .logRotate = 10,
                .trampoline = true,
                .trampolineSize = 1 << 10,
            });
            g_f4seInitialized = true;
        });
    } catch (...) {
        return false;
    }

    return g_f4seInitialized;
}

bool registerMessagingListener() noexcept {
    if (!g_f4seInitialized) {
        return false;
    }

    const auto* messaging = F4SE::GetMessagingInterface();
    if (messaging == nullptr) {
        spdlog::critical("UIRepo_F4: F4SE messaging interface is unavailable");
        return false;
    }

    try {
        std::call_once(g_listenerOnce, [messaging]() {
            g_listenerRegistered = messaging->RegisterListener(onF4SEMessage);
        });
    } catch (...) {
        return false;
    }

    if (!g_listenerRegistered) {
        spdlog::critical("UIRepo_F4: failed to register F4SE message listener");
    }
    return g_listenerRegistered;
}

} // namespace

F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info) {
    if (f4se == nullptr || info == nullptr) {
        return false;
    }

    if (const auto* data = F4SE::PluginVersionData::GetSingleton(); data != nullptr) {
        info->infoVersion = F4SE::PluginInfo::kVersion;
        info->name = data->GetPluginName().data();
        info->version = data->GetPluginVersion().pack();
    } else {
        return false;
    }

    if (f4se->IsEditor()) {
        return false;
    }

    return f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

F4SE_PLUGIN_PRELOAD(const F4SE::LoadInterface* interface) {
    return initializeF4SE(interface);
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* interface) {
    if (!initializeF4SE(interface)) {
        return false;
    }

    uirepo::fo4::DocumentRegistry::instance().setOwnerCancelHook(&cancelRegistryOwner);

    if (!registerMessagingListener()) {
        return false;
    }

    spdlog::info(
        "UIRepo_F4 Stage0: native bootstrap loaded (build {}, runtime {}, F4SE {}, commonlib_NonVR {})",
        UIREPO_BUILD_SHA,
        interface->RuntimeVersion().string(),
        F4SE::GetF4SEVersion().string(),
        UIREPO_COMMONLIBF4_PIN);
    return true;
}
