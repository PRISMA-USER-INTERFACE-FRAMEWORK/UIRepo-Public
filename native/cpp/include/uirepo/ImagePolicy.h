#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace uirepo::render {

inline constexpr std::uint32_t MaxImageDimension = 8192u;
inline constexpr std::size_t Rgba8BytesPerPixel = 4u;

struct ImageAdmission {
    bool accepted{};
    std::size_t bytes{};
};

[[nodiscard]] constexpr ImageAdmission admitRgba8Image(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t maxDimension = MaxImageDimension) noexcept {
    if (width == 0 || height == 0 || maxDimension == 0 ||
        width > maxDimension || height > maxDimension) {
        return {};
    }

    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();
    const std::size_t wideWidth = static_cast<std::size_t>(width);
    const std::size_t wideHeight = static_cast<std::size_t>(height);
    if (wideWidth > max / wideHeight) {
        return {};
    }
    const std::size_t pixels = wideWidth * wideHeight;
    if (pixels > max / Rgba8BytesPerPixel) {
        return {};
    }

    return {
        .accepted = true,
        .bytes = pixels * Rgba8BytesPerPixel,
    };
}

} // namespace uirepo::render
