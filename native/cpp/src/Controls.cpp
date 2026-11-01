#include "uirepo/Controls.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace uirepo {
namespace {

constexpr Size kToggleSize{46.0f, 24.0f};
constexpr Size kSliderSize{140.0f, 20.0f};
constexpr Size kStepperSize{152.0f, 26.0f};
constexpr Size kTextInputSize{160.0f, 30.0f};
constexpr float kToggleInset = 2.0f;
constexpr float kSliderTrackHeight = 6.0f;
constexpr float kSliderThumbSize = 14.0f;
constexpr float kStepperButtonWidth = 26.0f;
constexpr float kStepperFontSize = 12.0f;

[[nodiscard]] bool finite(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool appendUtf8(std::string& destination, char32_t codepoint, std::size_t maxBytes) noexcept {
    if (codepoint == 0 || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint < 0x20 || codepoint == 0x7F) {
        return false;
    }

    char encoded[4]{};
    std::size_t count{};
    if (codepoint <= 0x7F) {
        encoded[0] = static_cast<char>(codepoint);
        count = 1;
    } else if (codepoint <= 0x7FF) {
        encoded[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        encoded[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        count = 2;
    } else if (codepoint <= 0xFFFF) {
        encoded[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        encoded[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        count = 3;
    } else {
        encoded[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        encoded[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        count = 4;
    }

    if (destination.size() > maxBytes || count > maxBytes - destination.size()) {
        return false;
    }
    destination.append(encoded, count);
    return true;
}

} // namespace

ToggleNode::ToggleNode(std::string id, bool value)
    : Node(std::move(id)), value_(value) {
    setFocusable(true);
    style().width = Length::Pixels(kToggleSize.width);
    style().height = Length::Pixels(kToggleSize.height);
}

bool ToggleNode::setValue(bool value) noexcept {
    if (value_ == value) {
        return false;
    }
    value_ = value;
    return true;
}

bool ToggleNode::toggle() noexcept {
    value_ = !value_;
    return true;
}

Size ToggleNode::measureContent(const TextMeasurer&, Size) const noexcept {
    return kToggleSize;
}

void ToggleNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    Node::appendDrawCommands(commands);

    const Rect track = bounds().deflate(style().padding);
    if (track.empty()) {
        return;
    }

    const Color trackColor = value_
        ? (focused() ? Color{42, 190, 222, 255} : Color{18, 128, 154, 255})
        : (focused() ? Color{66, 78, 88, 255} : Color{26, 31, 36, 255});
    commands.emplace_back(FillRectCommand{
        id(), track, style().opacity, style().zIndex, trackColor, track.height * 0.5f});

    const float thumbSize = std::max(0.0f, track.height - kToggleInset * 2.0f);
    const float thumbX = value_
        ? std::max(track.x + kToggleInset, track.right() - kToggleInset - thumbSize)
        : track.x + kToggleInset;
    const Rect thumb{
        thumbX,
        track.y + (track.height - thumbSize) * 0.5f,
        thumbSize,
        thumbSize,
    };
    commands.emplace_back(FillRectCommand{
        id(), thumb, style().opacity, style().zIndex + 1,
        value_ ? Color{225, 250, 255, 255} : Color{105, 124, 134, 255},
        thumbSize * 0.5f});
}

SliderNode::SliderNode(
    std::string id,
    float value,
    float minimum,
    float maximum,
    float step)
    : Node(std::move(id)), minimum_(minimum), maximum_(maximum), step_(step) {
    if (!finite(minimum_) || !finite(maximum_) || !finite(step_) ||
        maximum_ <= minimum_ || step_ <= 0.0f) {
        throw std::invalid_argument("Slider range and step must be finite, ordered, and positive");
    }

    value_ = normalize(value);
    setFocusable(true);
    style().width = Length::Pixels(kSliderSize.width);
    style().height = Length::Pixels(kSliderSize.height);
}

float SliderNode::normalizedValue() const noexcept {
    return (value_ - minimum_) / (maximum_ - minimum_);
}

bool SliderNode::setValue(float value) noexcept {
    if (!finite(value)) {
        return false;
    }
    const float next = normalize(value);
    if (next == value_) {
        return false;
    }
    value_ = next;
    return true;
}

bool SliderNode::adjustSteps(int steps) noexcept {
    if (steps == 0) {
        return false;
    }
    return setValue(value_ + step_ * static_cast<float>(steps));
}

bool SliderNode::setValueFromPointer(Point point) noexcept {
    const Rect content = bounds().deflate(style().padding);
    if (content.width <= 0.0f) {
        return false;
    }
    const float fraction = std::clamp((point.x - content.x) / content.width, 0.0f, 1.0f);
    return setValue(minimum_ + fraction * (maximum_ - minimum_));
}

Size SliderNode::measureContent(const TextMeasurer&, Size) const noexcept {
    return kSliderSize;
}

void SliderNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    Node::appendDrawCommands(commands);

    const Rect content = bounds().deflate(style().padding);
    if (content.empty()) {
        return;
    }

    const float trackHeight = std::min(kSliderTrackHeight, content.height);
    const Rect track{
        content.x,
        content.y + (content.height - trackHeight) * 0.5f,
        content.width,
        trackHeight,
    };
    commands.emplace_back(FillRectCommand{
        id(), track, style().opacity, style().zIndex, Color{20, 25, 30, 255}, trackHeight * 0.5f});

    const float progress = std::clamp(normalizedValue(), 0.0f, 1.0f);
    const Rect filled{track.x, track.y, track.width * progress, track.height};
    if (!filled.empty()) {
        commands.emplace_back(FillRectCommand{
            id(), filled, style().opacity, style().zIndex + 1, Color{22, 164, 196, 255}, trackHeight * 0.5f});
    }

    const float thumbSize = std::min(kSliderThumbSize, content.height);
    const float centerX = track.x + track.width * progress;
    const Rect thumb{
        std::clamp(centerX - thumbSize * 0.5f, content.x, std::max(content.x, content.right() - thumbSize)),
        content.y + (content.height - thumbSize) * 0.5f,
        thumbSize,
        thumbSize,
    };
    commands.emplace_back(FillRectCommand{
        id(), thumb, style().opacity, style().zIndex + 2,
        focused() ? Color{225, 250, 255, 255} : Color{65, 205, 230, 255},
        thumbSize * 0.5f});
}

float SliderNode::normalize(float value) const noexcept {
    if (!finite(value)) {
        return minimum_;
    }
    const float clamped = std::clamp(value, minimum_, maximum_);
    const float stepCount = std::round((clamped - minimum_) / step_);
    return std::clamp(minimum_ + stepCount * step_, minimum_, maximum_);
}

StepperNode::StepperNode(
    std::string id,
    std::vector<std::string> options,
    std::size_t index)
    : Node(std::move(id)), options_(std::move(options)) {
    if (!options_.empty()) {
        index_ = std::min(index, options_.size() - 1);
    }
    setFocusable(true);
    style().width = Length::Pixels(kStepperSize.width);
    style().height = Length::Pixels(kStepperSize.height);
}

std::string_view StepperNode::value() const noexcept {
    if (options_.empty()) {
        return {};
    }
    return options_[index_];
}

bool StepperNode::setIndex(std::size_t index) noexcept {
    if (options_.empty()) {
        return false;
    }
    const std::size_t next = std::min(index, options_.size() - 1);
    const bool changed = next != index_;
    index_ = next;
    return changed || index != next;
}

bool StepperNode::stepBy(int delta) noexcept {
    if (options_.empty() || delta == 0) {
        return false;
    }

    const auto count = static_cast<long long>(options_.size());
    const auto current = static_cast<long long>(index_);
    const auto amount = static_cast<long long>(delta);
    const auto wrapped = ((current + amount) % count + count) % count;
    const auto next = static_cast<std::size_t>(wrapped);
    if (next == index_) {
        return false;
    }
    index_ = next;
    return true;
}

Size StepperNode::measureContent(const TextMeasurer&, Size) const noexcept {
    return kStepperSize;
}

void StepperNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    Node::appendDrawCommands(commands);

    const Rect content = bounds().deflate(style().padding);
    if (content.empty()) {
        return;
    }

    const float buttonWidth = std::min(kStepperButtonWidth, content.width * 0.5f);
    const Rect left{content.x, content.y, buttonWidth, content.height};
    const Rect right{content.right() - buttonWidth, content.y, buttonWidth, content.height};
    const Rect center{
        left.right(), content.y,
        std::max(0.0f, content.width - buttonWidth * 2.0f),
        content.height,
    };

    const Color edgeColor = focused() ? Color{32, 95, 112, 255} : Color{18, 28, 34, 255};
    const Color centerColor = focused() ? Color{20, 54, 64, 255} : Color{12, 20, 24, 255};
    commands.emplace_back(FillRectCommand{id(), left, style().opacity, style().zIndex, edgeColor, 0.0f});
    commands.emplace_back(FillRectCommand{id(), center, style().opacity, style().zIndex, centerColor, 0.0f});
    commands.emplace_back(FillRectCommand{id(), right, style().opacity, style().zIndex, edgeColor, 0.0f});

    const Color textColor = focused() ? Color{225, 250, 255, 255} : Color{72, 210, 236, 255};
    commands.emplace_back(DrawTextCommand{
        id(), left, style().opacity, style().zIndex + 1, "<", kStepperFontSize, textColor});
    commands.emplace_back(DrawTextCommand{
        id(), center, style().opacity, style().zIndex + 1, value(), kStepperFontSize, textColor});
    commands.emplace_back(DrawTextCommand{
        id(), right, style().opacity, style().zIndex + 1, ">", kStepperFontSize, textColor});
}

TextInputNode::TextInputNode(std::string id, std::string value, std::size_t maxBytes)
    : Node(std::move(id)), maxBytes_(maxBytes) {
    if (maxBytes_ == 0 || value.size() > maxBytes_) {
        throw std::invalid_argument("Text input byte budget must be positive and contain its initial value");
    }
    value_ = std::move(value);
    setFocusable(true);
    style().width = Length::Pixels(kTextInputSize.width);
    style().height = Length::Pixels(kTextInputSize.height);
    style().padding = Thickness{8.0f, 5.0f, 8.0f, 5.0f};
}

bool TextInputNode::setValue(std::string value) noexcept {
    if (value.size() > maxBytes_ || value == value_) {
        return false;
    }
    value_ = std::move(value);
    return true;
}

bool TextInputNode::appendCodepoint(char32_t codepoint) noexcept {
    return appendUtf8(value_, codepoint, maxBytes_);
}

bool TextInputNode::backspace() noexcept {
    if (value_.empty()) {
        return false;
    }

    std::size_t index = value_.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(value_[index]) & 0xC0u) == 0x80u) {
        --index;
    }
    value_.erase(index);
    return true;
}

Size TextInputNode::measureContent(const TextMeasurer&, Size) const noexcept {
    return kTextInputSize;
}

void TextInputNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    const Rect boundsValue = bounds();
    const Color background = focused() ? Color{22, 50, 58, 255} : Color{12, 20, 24, 255};
    commands.emplace_back(FillRectCommand{
        id(), boundsValue, style().opacity, style().zIndex, background, style().cornerRadius});
    commands.emplace_back(DrawTextCommand{
        id(),
        boundsValue.deflate(style().padding),
        style().opacity,
        style().zIndex + 1,
        value_,
        fontSize_,
        focused() ? Color{225, 250, 255, 255} : Color{200, 225, 232, 255}});
}

} // namespace uirepo
