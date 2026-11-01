#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace uirepo::render {

constexpr std::array<std::string_view, 4> KnownFrameGenerationModules{
    "AAAFrameGeneration.dll",
    "amd_fidelityfx_dx12.dll",
    "nvngx_dlssg.dll",
    "dlssg_to_fsr3.dll",
};

[[nodiscard]] constexpr char asciiLower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

[[nodiscard]] constexpr bool equalAsciiNoCase(
    std::string_view left,
    std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (asciiLower(left[index]) != asciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool isKnownFrameGenerationModule(
    std::string_view moduleName) noexcept {
    for (const auto candidate : KnownFrameGenerationModules) {
        if (equalAsciiNoCase(moduleName, candidate)) {
            return true;
        }
    }
    return false;
}

} // namespace uirepo::render
