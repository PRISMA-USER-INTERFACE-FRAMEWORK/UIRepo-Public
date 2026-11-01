#include "RenderHook.h"

#include "CursorMode.h"
#include "FocusLeaseManager.h"
#include "GameThreadDispatcher.h"
#include "PauseHold.h"
#include "Runtime.h"

#include <MinHook.h>
#include <RE/Fallout.h>
#include <spdlog/spdlog.h>

#include <Windows.h>
#include <dxgi.h>

#include <cstdint>
#include <exception>
#include <iterator>
#include <string>

namespace uirepo::fo4 {
namespace {

constexpr std::size_t kPresentVtableIndex = 8;
constexpr std::size_t kResizeBuffersVtableIndex = 13;

constexpr std::uint64_t kUIEndFrameOG = 137303;
constexpr std::uint64_t kUIEndFrameNG = 2284763;
constexpr std::uint64_t kUIEndFrameAE = 2284763;

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

// A hook invoked from the engine's Present/UIEndFrame path must never let an exception escape:
// unwinding into DXGI/the engine crashes with no useful stack. Log the first occurrence and keep
// the frame alive; suppress further logs so a persistent fault does not flood the log.
void logRenderExceptionOnce(const char* what) noexcept {
    static bool logged = false;
    if (logged) {
        return;
    }
    logged = true;
    if (what != nullptr) {
        spdlog::error(
            "UIRepo_F4: render frame threw ({}); suppressing further render-frame exception logs",
            what);
    } else {
        spdlog::error(
            "UIRepo_F4: render frame threw an unknown exception; suppressing further render-frame exception logs");
    }
}

void renderFocusAwareEngineFrame() noexcept {
    GameThreadDispatcher::runGameThreadBootstrap();
    try {
        FocusLeaseManager::syncFromRegistry();
        const bool presentationExpected = FocusLeaseManager::focusedDocument() != 0;
        PauseHold::tick();
        CursorMode::tick();
        Runtime::instance().renderEngineFrame();
        FocusLeaseManager::advancePresentationFrame(presentationExpected, true, false);
    } catch (const std::exception& exception) {
        logRenderExceptionOnce(exception.what());
    } catch (...) {
        logRenderExceptionOnce(nullptr);
    }
}

void renderFocusAwareFallbackFrame(IDXGISwapChain* swapChain) noexcept {
    GameThreadDispatcher::runGameThreadBootstrap();
    try {
        FocusLeaseManager::syncFromRegistry();
        const bool presentationExpected = FocusLeaseManager::focusedDocument() != 0;
        PauseHold::tick();
        CursorMode::tick();
        Runtime::instance().render(swapChain);
        FocusLeaseManager::advancePresentationFrame(presentationExpected, true, false);
    } catch (const std::exception& exception) {
        logRenderExceptionOnce(exception.what());
    } catch (...) {
        logRenderExceptionOnce(nullptr);
    }
}

} // namespace

void RenderHook::uiEndFrame(void* ui) noexcept {
    auto original = originalUIEndFrame_;
    if (original == nullptr) {
        return;
    }

    original(ui);
    renderFocusAwareEngineFrame();
}

HRESULT APIENTRY RenderHook::present(
    REX::W32::IDXGISwapChain* swapChain,
    UINT syncInterval,
    UINT flags) {
    renderFocusAwareFallbackFrame(reinterpret_cast<IDXGISwapChain*>(swapChain));

    if (originalPresent_ == nullptr) {
        return E_FAIL;
    }
    return originalPresent_(swapChain, syncInterval, flags);
}

HRESULT APIENTRY RenderHook::resizeBuffers(
    REX::W32::IDXGISwapChain* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    REX::W32::DXGI_FORMAT newFormat,
    UINT swapChainFlags) {
    Runtime::instance().beforeResize();

    if (originalResizeBuffers_ == nullptr) {
        return E_FAIL;
    }

    const HRESULT result = originalResizeBuffers_(
        swapChain,
        bufferCount,
        width,
        height,
        newFormat,
        swapChainFlags);
    spdlog::info(
        "UIRepo_F4 Stage0: ResizeBuffers completed (buffers {}, requested {}x{}, format {}, flags 0x{:X}, HRESULT 0x{:08X})",
        bufferCount,
        width,
        height,
        static_cast<unsigned>(newFormat),
        swapChainFlags,
        static_cast<std::uint32_t>(result));
    return result;
}

bool RenderHook::install() noexcept {
    if (installed_) {
        return true;
    }

    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED) {
        spdlog::error("UIRepo_F4: MH_Initialize failed ({})", static_cast<int>(initializeStatus));
        return false;
    }

    auto* rendererData = RE::BSGraphics::GetRendererData();
    if (rendererData == nullptr || rendererData->renderWindow[0].swapChain == nullptr) {
        spdlog::error("UIRepo_F4: Fallout renderer swap chain is unavailable");
        return false;
    }

    auto* swapChain = rendererData->renderWindow[0].swapChain;
    auto** vtable = *reinterpret_cast<void***>(swapChain);
    if (vtable == nullptr || vtable[kResizeBuffersVtableIndex] == nullptr) {
        spdlog::error("UIRepo_F4: invalid swap-chain ResizeBuffers target");
        return false;
    }
    resizeTarget_ = vtable[kResizeBuffersVtableIndex];

    // Record who currently owns the swap-chain vtable slots before we hook them. If another
    // overlay/capture layer (ReShade, RTSS, RenderDoc, another UI mod) already chained these,
    // this line makes "our hook was displaced" diagnosable straight from a user log.
    spdlog::info(
        "UIRepo_F4: pre-hook swap-chain vtable owners Present '{}' ResizeBuffers '{}'",
        moduleNameForAddress(vtable[kPresentVtableIndex]),
        moduleNameForAddress(vtable[kResizeBuffersVtableIndex]));

    try {
        const REL::ID uiEndFrameId{kUIEndFrameOG, kUIEndFrameNG, kUIEndFrameAE};
        uiEndFrameTarget_ = reinterpret_cast<void*>(uiEndFrameId.address());
    } catch (const std::exception& exception) {
        spdlog::warn(
            "UIRepo_F4: could not resolve UIEndFrame from Address Library: {}; falling back to DXGI",
            exception.what());
        uiEndFrameTarget_ = nullptr;
    } catch (...) {
        spdlog::warn(
            "UIRepo_F4: could not resolve UIEndFrame from Address Library; falling back to DXGI");
        uiEndFrameTarget_ = nullptr;
    }

    if (uiEndFrameTarget_ != nullptr) {
        const MH_STATUS createFrame = MH_CreateHook(
            uiEndFrameTarget_,
            reinterpret_cast<void*>(&RenderHook::uiEndFrame),
            reinterpret_cast<void**>(&originalUIEndFrame_));
        if (createFrame == MH_OK) {
            const MH_STATUS createResize = MH_CreateHook(
                resizeTarget_,
                reinterpret_cast<void*>(&RenderHook::resizeBuffers),
                reinterpret_cast<void**>(&originalResizeBuffers_));
            if (createResize == MH_OK) {
                const MH_STATUS enableFrame = MH_EnableHook(uiEndFrameTarget_);
                const MH_STATUS enableResize =
                    enableFrame == MH_OK ? MH_EnableHook(resizeTarget_) : MH_ERROR_NOT_CREATED;
                if (enableFrame == MH_OK && enableResize == MH_OK) {
                    uiEndFrameInstalled_ = true;
                    resizeInstalled_ = true;
                    installed_ = true;
                    spdlog::info(
                        "UIRepo_F4: engine UIEndFrame compositor installed at {} ({}) with ResizeBuffers lifecycle hook {} ({})",
                        uiEndFrameTarget_,
                        moduleNameForAddress(uiEndFrameTarget_),
                        resizeTarget_,
                        moduleNameForAddress(resizeTarget_));
                    return true;
                }

                spdlog::warn(
                    "UIRepo_F4: engine compositor hooks could not be enabled ({}, {}); falling back to DXGI",
                    static_cast<int>(enableFrame),
                    static_cast<int>(enableResize));
                MH_DisableHook(resizeTarget_);
                MH_DisableHook(uiEndFrameTarget_);
                MH_RemoveHook(resizeTarget_);
            } else {
                spdlog::warn(
                    "UIRepo_F4: ResizeBuffers lifecycle hook could not be created ({}); falling back to DXGI",
                    static_cast<int>(createResize));
            }
            MH_RemoveHook(uiEndFrameTarget_);
            originalUIEndFrame_ = nullptr;
            originalResizeBuffers_ = nullptr;
        } else {
            spdlog::warn(
                "UIRepo_F4: UIEndFrame hook could not be created ({}); falling back to DXGI",
                static_cast<int>(createFrame));
        }
        uiEndFrameTarget_ = nullptr;
    }

    if (vtable[kPresentVtableIndex] == nullptr) {
        spdlog::error("UIRepo_F4: invalid fallback Present target");
        resizeTarget_ = nullptr;
        return false;
    }

    presentTarget_ = vtable[kPresentVtableIndex];
    if (presentTarget_ == resizeTarget_) {
        spdlog::error(
            "UIRepo_F4: fallback Present and ResizeBuffers unexpectedly resolve to the same target");
        presentTarget_ = nullptr;
        resizeTarget_ = nullptr;
        return false;
    }

    spdlog::info(
        "UIRepo_F4: using DXGI fallback: swap chain {} Present {} ({}) ResizeBuffers {} ({})",
        static_cast<const void*>(swapChain),
        presentTarget_,
        moduleNameForAddress(presentTarget_),
        resizeTarget_,
        moduleNameForAddress(resizeTarget_));

    const MH_STATUS presentCreate = MH_CreateHook(
        presentTarget_,
        reinterpret_cast<void*>(&RenderHook::present),
        reinterpret_cast<void**>(&originalPresent_));
    if (presentCreate != MH_OK) {
        spdlog::error(
            "UIRepo_F4: failed to create fallback Present hook ({})",
            static_cast<int>(presentCreate));
        presentTarget_ = nullptr;
        resizeTarget_ = nullptr;
        return false;
    }

    const MH_STATUS resizeCreate = MH_CreateHook(
        resizeTarget_,
        reinterpret_cast<void*>(&RenderHook::resizeBuffers),
        reinterpret_cast<void**>(&originalResizeBuffers_));
    if (resizeCreate != MH_OK) {
        spdlog::error(
            "UIRepo_F4: failed to create fallback ResizeBuffers hook ({})",
            static_cast<int>(resizeCreate));
        MH_RemoveHook(presentTarget_);
        originalPresent_ = nullptr;
        presentTarget_ = nullptr;
        resizeTarget_ = nullptr;
        return false;
    }

    const MH_STATUS presentEnable = MH_EnableHook(presentTarget_);
    if (presentEnable != MH_OK) {
        spdlog::error(
            "UIRepo_F4: failed to enable fallback Present hook ({})", static_cast<int>(presentEnable));
        MH_RemoveHook(resizeTarget_);
        MH_RemoveHook(presentTarget_);
        originalPresent_ = nullptr;
        originalResizeBuffers_ = nullptr;
        presentTarget_ = nullptr;
        resizeTarget_ = nullptr;
        return false;
    }
    presentInstalled_ = true;

    const MH_STATUS resizeEnable = MH_EnableHook(resizeTarget_);
    if (resizeEnable != MH_OK) {
        spdlog::error(
            "UIRepo_F4: failed to enable fallback ResizeBuffers hook ({})", static_cast<int>(resizeEnable));
        MH_DisableHook(presentTarget_);
        MH_RemoveHook(resizeTarget_);
        MH_RemoveHook(presentTarget_);
        Runtime::instance().shutdownOffscreenSurfaces();
        Runtime::instance().shutdown();
        originalPresent_ = nullptr;
        originalResizeBuffers_ = nullptr;
        presentTarget_ = nullptr;
        resizeTarget_ = nullptr;
        presentInstalled_ = false;
        return false;
    }
    resizeInstalled_ = true;

    installed_ = true;
    spdlog::info("UIRepo_F4: DXGI Present/ResizeBuffers fallback compositor installed");
    return true;
}

void RenderHook::uninstall() noexcept {
    if (!installed_) {
        return;
    }

    FocusLeaseManager::releaseAll();
    Runtime::instance().shutdownOffscreenSurfaces();
    Runtime::instance().shutdown();

    if (resizeInstalled_ && resizeTarget_ != nullptr) {
        MH_DisableHook(resizeTarget_);
        MH_RemoveHook(resizeTarget_);
    }
    if (presentInstalled_ && presentTarget_ != nullptr) {
        MH_DisableHook(presentTarget_);
        MH_RemoveHook(presentTarget_);
    }
    if (uiEndFrameInstalled_ && uiEndFrameTarget_ != nullptr) {
        MH_DisableHook(uiEndFrameTarget_);
        MH_RemoveHook(uiEndFrameTarget_);
    }

    originalUIEndFrame_ = nullptr;
    originalPresent_ = nullptr;
    originalResizeBuffers_ = nullptr;
    uiEndFrameTarget_ = nullptr;
    presentTarget_ = nullptr;
    resizeTarget_ = nullptr;
    uiEndFrameInstalled_ = false;
    presentInstalled_ = false;
    resizeInstalled_ = false;
    installed_ = false;

    spdlog::info("UIRepo_F4: render hooks uninstalled");
}

} // namespace uirepo::fo4
