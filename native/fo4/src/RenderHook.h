#pragma once

#include <Windows.h>
#include <REX/W32/DXGI.h>

namespace uirepo::fo4 {

class RenderHook final {
public:
    static bool install() noexcept;
    static void uninstall() noexcept;

private:
    using UIEndFrame = void (*)(void*) noexcept;
    using Present = HRESULT(APIENTRY*)(REX::W32::IDXGISwapChain*, UINT, UINT);
    using ResizeBuffers = HRESULT(APIENTRY*)(
        REX::W32::IDXGISwapChain*,
        UINT,
        UINT,
        UINT,
        REX::W32::DXGI_FORMAT,
        UINT);

    static void uiEndFrame(void* ui) noexcept;
    static HRESULT APIENTRY present(
        REX::W32::IDXGISwapChain* swapChain,
        UINT syncInterval,
        UINT flags);
    static HRESULT APIENTRY resizeBuffers(
        REX::W32::IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        REX::W32::DXGI_FORMAT newFormat,
        UINT swapChainFlags);

    static inline UIEndFrame originalUIEndFrame_{};
    static inline Present originalPresent_{};
    static inline ResizeBuffers originalResizeBuffers_{};
    static inline void* uiEndFrameTarget_{};
    static inline void* presentTarget_{};
    static inline void* resizeTarget_{};
    static inline bool uiEndFrameInstalled_{};
    static inline bool presentInstalled_{};
    static inline bool resizeInstalled_{};
    static inline bool installed_{};
};

} // namespace uirepo::fo4
