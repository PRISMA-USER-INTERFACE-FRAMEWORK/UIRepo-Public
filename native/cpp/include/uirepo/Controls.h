#pragma once

#include "uirepo/Core.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo {

class DirectionalValueControl {
public:
    virtual ~DirectionalValueControl() = default;
    [[nodiscard]] virtual bool adjustDirectional(int steps) noexcept = 0;
    [[nodiscard]] virtual bool setValueFromPointer(Point point) noexcept {
        (void)point;
        return false;
    }
};

class ToggleNode final : public Node {
public:
    explicit ToggleNode(std::string id, bool value = false);

    [[nodiscard]] bool value() const noexcept { return value_; }
    [[nodiscard]] bool setValue(bool value) noexcept;
    [[nodiscard]] bool toggle() noexcept;

protected:
    [[nodiscard]] Size measureContent(const TextMeasurer&, Size) const noexcept override;
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    bool value_{};
};

class SliderNode final : public Node, public DirectionalValueControl {
public:
    SliderNode(std::string id, float value, float minimum, float maximum, float step = 1.0f);

    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] float minimum() const noexcept { return minimum_; }
    [[nodiscard]] float maximum() const noexcept { return maximum_; }
    [[nodiscard]] float step() const noexcept { return step_; }
    [[nodiscard]] float normalizedValue() const noexcept;

    [[nodiscard]] bool setValue(float value) noexcept;
    [[nodiscard]] bool adjustSteps(int steps) noexcept;
    [[nodiscard]] bool adjustDirectional(int steps) noexcept override { return adjustSteps(steps); }
    [[nodiscard]] bool setValueFromPointer(Point point) noexcept override;

protected:
    [[nodiscard]] Size measureContent(const TextMeasurer&, Size) const noexcept override;
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    [[nodiscard]] float normalize(float value) const noexcept;

    float value_{};
    float minimum_{};
    float maximum_{};
    float step_{1.0f};
};

class StepperNode final : public Node, public DirectionalValueControl {
public:
    StepperNode(std::string id, std::vector<std::string> options, std::size_t index = 0);

    [[nodiscard]] const std::vector<std::string>& options() const noexcept { return options_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] std::string_view value() const noexcept;

    [[nodiscard]] bool setIndex(std::size_t index) noexcept;
    [[nodiscard]] bool stepBy(int delta) noexcept;
    [[nodiscard]] bool adjustDirectional(int steps) noexcept override { return stepBy(steps); }

protected:
    [[nodiscard]] Size measureContent(const TextMeasurer&, Size) const noexcept override;
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    std::vector<std::string> options_;
    std::size_t index_{};
};

class TextInputNode final : public Node {
public:
    TextInputNode(std::string id, std::string value = {}, std::size_t maxBytes = 4096);

    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] std::size_t maxBytes() const noexcept { return maxBytes_; }
    [[nodiscard]] float fontSize() const noexcept { return fontSize_; }
    [[nodiscard]] bool setValue(std::string value) noexcept;
    [[nodiscard]] bool appendCodepoint(char32_t codepoint) noexcept;
    [[nodiscard]] bool backspace() noexcept;
    [[nodiscard]] Rect endCaretRect(const TextMeasurer& measurer) const noexcept {
        const Rect content = bounds().deflate(style().padding);
        if (content.empty()) {
            return {};
        }

        const Size measured = measurer.measure(value_, fontSize_);
        const float caretWidth = std::min(1.0f, content.width);
        const float maxAdvance = std::max(0.0f, content.width - caretWidth);
        const float textWidth = std::isfinite(measured.width)
            ? std::clamp(measured.width, 0.0f, maxAdvance)
            : 0.0f;
        const float measuredHeight = std::isfinite(measured.height) && measured.height > 0.0f
            ? measured.height
            : fontSize_;
        const float caretHeight = std::min(content.height, std::max(1.0f, measuredHeight));
        return {content.x + textWidth, content.y, caretWidth, caretHeight};
    }

protected:
    [[nodiscard]] Size measureContent(const TextMeasurer&, Size) const noexcept override;
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    std::string value_;
    std::size_t maxBytes_{4096};
    float fontSize_{14.0f};
};

} // namespace uirepo
