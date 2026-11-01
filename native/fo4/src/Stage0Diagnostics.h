#pragma once

#include <cstdint>

namespace uirepo::fo4::Stage0Diagnostics {

void logSwapChain(void* swapChain) noexcept;
void logSessionEvent(std::uint32_t type, const void* data) noexcept;
[[nodiscard]] bool installWindowLifecycleSink(void* window) noexcept;
[[nodiscard]] bool installMainMenuSink() noexcept;
[[nodiscard]] std::uint32_t consumeFocusRecoveryTransition() noexcept;
[[nodiscard]] std::uint32_t consumeWindowStyleRecoveryTransition() noexcept;

} // namespace uirepo::fo4::Stage0Diagnostics
