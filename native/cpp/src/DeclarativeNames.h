#pragma once

#include "uirepo/DeclarativeUi.h"
#include "uirepo/NativeApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace uirepo::declarative::names {

enum class Kind : std::uint8_t {
    Panel = UIREPO_NODE_PANEL,
    Stack = UIREPO_NODE_STACK,
    Text = UIREPO_NODE_TEXT,
    Button = UIREPO_NODE_BUTTON,
    Image = UIREPO_NODE_IMAGE,
    Video = UIREPO_NODE_VIDEO,
    Toggle = UIREPO_NODE_TOGGLE,
    Slider = UIREPO_NODE_SLIDER,
    Stepper = UIREPO_NODE_STEPPER,
    TextInput = UIREPO_NODE_TEXT_INPUT,
};

template <typename E>
struct Entry {
    std::string_view name;
    E value;
};

template <typename E, std::size_t N>
using Table = std::array<Entry<E>, N>;

inline constexpr Table<Kind, 10> kKinds{{
    {"panel", Kind::Panel},
    {"stack", Kind::Stack},
    {"text", Kind::Text},
    {"button", Kind::Button},
    {"image", Kind::Image},
    {"video", Kind::Video},
    {"toggle", Kind::Toggle},
    {"slider", Kind::Slider},
    {"stepper", Kind::Stepper},
    {"textInput", Kind::TextInput},
}};

inline constexpr Table<Axis, 2> kAxes{{{"row", Axis::Row}, {"column", Axis::Column}}};

inline constexpr Table<Align, 4> kAligns{{
    {"start", Align::Start},
    {"center", Align::Center},
    {"end", Align::End},
    {"stretch", Align::Stretch},
}};

inline constexpr Table<TextWrapping, 2> kWrapping{{
    {"noWrap", TextWrapping::NoWrap},
    {"wrap", TextWrapping::Wrap},
}};

inline constexpr Table<TextAlignment, 3> kTextAlignments{{
    {"start", TextAlignment::Start},
    {"center", TextAlignment::Center},
    {"end", TextAlignment::End},
}};

inline constexpr Table<TextOverflow, 2> kOverflow{{
    {"clip", TextOverflow::Clip},
    {"ellipsis", TextOverflow::Ellipsis},
}};

inline constexpr Table<VideoPlaybackState, 3> kPlayback{{
    {"stopped", VideoPlaybackState::Stopped},
    {"playing", VideoPlaybackState::Playing},
    {"paused", VideoPlaybackState::Paused},
}};

inline constexpr std::size_t kDefaultTextInputBytes = 4096;
inline constexpr float kDefaultSliderStep = 1.0f;

static_assert(static_cast<int>(Axis::Row) == UIREPO_AXIS_ROW);
static_assert(static_cast<int>(Axis::Column) == UIREPO_AXIS_COLUMN);
static_assert(static_cast<int>(Align::Start) == UIREPO_ALIGN_START);
static_assert(static_cast<int>(Align::Stretch) == UIREPO_ALIGN_STRETCH);
static_assert(static_cast<int>(TextWrapping::Wrap) == UIREPO_TEXT_WRAP);
static_assert(static_cast<int>(TextAlignment::End) == UIREPO_TEXT_ALIGN_END);
static_assert(static_cast<int>(TextOverflow::Ellipsis) == UIREPO_TEXT_OVERFLOW_ELLIPSIS);
static_assert(static_cast<int>(VideoPlaybackState::Paused) == UIREPO_VIDEO_PAUSED);

template <typename E, std::size_t N>
[[nodiscard]] constexpr std::optional<E> fromName(const Table<E, N>& table, std::string_view name) noexcept {
    for (const auto& entry : table) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <typename E, std::size_t N>
[[nodiscard]] constexpr std::string_view toName(const Table<E, N>& table, E value) noexcept {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return {};
}

template <typename E, std::size_t N>
[[nodiscard]] std::string expectedList(const Table<E, N>& table) {
    std::string text = "expected one of ";
    for (std::size_t index = 0; index < N; ++index) {
        text += index == 0 ? "\"" : ", \"";
        text += table[index].name;
        text += '"';
    }
    return text;
}

[[nodiscard]] inline std::string pointerEscape(std::string_view key) {
    std::string out;
    out.reserve(key.size());
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

[[nodiscard]] inline bool validId(std::string_view id) noexcept {
    if (id.empty() || id.size() > kMaxIdBytes) {
        return false;
    }
    for (std::size_t index = 0; index < id.size(); ++index) {
        const char c = id[index];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool tail = (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!(letter || (index > 0 && tail))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool validAsset(std::string_view source) noexcept {
    if (source.empty() || source.size() > kMaxAssetBytes) {
        return false;
    }
    for (const char c : source) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return false;
        }
    }
    return true;
}

} // namespace uirepo::declarative::names
