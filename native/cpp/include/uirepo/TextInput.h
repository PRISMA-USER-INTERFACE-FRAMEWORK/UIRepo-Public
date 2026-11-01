#pragma once

#include <cstdint>
#include <optional>

namespace uirepo::input {

inline constexpr std::uint32_t KeyBackspace = 0x08u;

struct TextEvent {
    char32_t codepoint{};
};

class Utf16TextDecoder final {
public:
    [[nodiscard]] std::optional<TextEvent> push(char16_t codeUnit) noexcept {
        if (codeUnit >= 0xD800u && codeUnit <= 0xDBFFu) {
            pendingHighSurrogate_ = codeUnit;
            return std::nullopt;
        }

        if (codeUnit >= 0xDC00u && codeUnit <= 0xDFFFu) {
            if (pendingHighSurrogate_ == 0) {
                return std::nullopt;
            }
            const char32_t high = static_cast<char32_t>(pendingHighSurrogate_ - 0xD800u);
            const char32_t low = static_cast<char32_t>(codeUnit - 0xDC00u);
            pendingHighSurrogate_ = 0;
            return TextEvent{0x10000u + (high << 10u) + low};
        }

        pendingHighSurrogate_ = 0;
        return TextEvent{static_cast<char32_t>(codeUnit)};
    }

    void clear() noexcept { pendingHighSurrogate_ = 0; }

private:
    char16_t pendingHighSurrogate_{};
};

} // namespace uirepo::input
