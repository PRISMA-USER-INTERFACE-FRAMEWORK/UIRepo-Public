#pragma once

#include <dxgi.h>

#include <string>
#include <string_view>

namespace uirepo::fo4::SwapChainCompat {

struct BackbufferPolicy {
    bool borrowed{};
    std::string getBufferOwner;
};

[[nodiscard]] bool isKnownFrameGenerationModule(std::string_view moduleName) noexcept;
[[nodiscard]] BackbufferPolicy classifyBackbuffer(IDXGISwapChain* swapChain) noexcept;

} // namespace uirepo::fo4::SwapChainCompat
