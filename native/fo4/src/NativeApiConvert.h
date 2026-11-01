#pragma once

#include "uirepo/Core.h"
#include "uirepo/InputRouter.h"
#include "uirepo/NativeApi.h"

#include "ApiResult.h"

#include <cstdint>

namespace uirepo::fo4::native_api {

[[nodiscard]] inline std::uint8_t result(
    bool value,
    UIRepoResultV1 failureCode = UIREPO_RESULT_UNKNOWN) noexcept {
    return api::report(value, failureCode);
}

[[nodiscard]] constexpr Color makeColor(UIRepoColorV1 source) noexcept {
    return Color{source.r, source.g, source.b, source.a};
}

[[nodiscard]] bool finiteNonNegative(float value) noexcept;
[[nodiscard]] bool finiteOrPositiveInfinity(float value) noexcept;
[[nodiscard]] bool makeLength(const UIRepoLengthV1& source, Length& destination) noexcept;
[[nodiscard]] bool makeThickness(const UIRepoThicknessV1& source, Thickness& destination) noexcept;
[[nodiscard]] bool makeAlign(std::uint32_t source, Align& destination) noexcept;
[[nodiscard]] bool makeTextLayout(const UIRepoTextLayoutV1* source, TextLayoutOptions& destination) noexcept;
[[nodiscard]] bool makeStyle(const UIRepoStyleV1* source, Style& destination) noexcept;
[[nodiscard]] bool makeAxis(std::uint32_t source, Axis& destination) noexcept;
[[nodiscard]] bool makePlayback(std::uint32_t source, VideoPlaybackState& destination) noexcept;
[[nodiscard]] bool makePointerEvent(const UIRepoPointerEventV1* source, input::PointerEvent& destination) noexcept;
[[nodiscard]] bool makeKeyEvent(const UIRepoKeyEventV1* source, input::KeyEvent& destination) noexcept;

} // namespace uirepo::fo4::native_api
