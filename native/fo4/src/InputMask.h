#pragma once

namespace uirepo::fo4::InputMask {

void apply() noexcept;
void release() noexcept;
[[nodiscard]] bool isApplied() noexcept;

} // namespace uirepo::fo4::InputMask
