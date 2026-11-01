#include "uirepo/Core.h"

#include "Layout.h"
#include "StyleValidation.h"

namespace uirepo {

namespace {

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}


[[nodiscard]] float clampLength(float value, float minimum, float maximum) {
    if (!finiteNonNegative(value)) {
        value = 0.0f;
    }
    if (!finiteNonNegative(minimum)) {
        minimum = 0.0f;
    }
    if (maximum <= 0.0f) {
        maximum = INFINITY;
    }
    if (std::isnan(maximum) || maximum < minimum) {
        maximum = minimum;
    }
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] Size maxSize(Size left, Size right) noexcept {
    return {std::max(left.width, right.width), std::max(left.height, right.height)};
}

[[nodiscard]] float resolveRootLength(Length length, float viewport) {
    switch (length.kind) {
    case LengthKind::Auto:
        return viewport;
    case LengthKind::Pixels:
        return length.value;
    case LengthKind::Percent:
        return viewport * length.value;
    }
    throw std::invalid_argument("Unknown length kind");
}

[[nodiscard]] float resolveLength(Length length, float available, float intrinsic) {
    switch (length.kind) {
    case LengthKind::Auto:
        return intrinsic;
    case LengthKind::Pixels:
        return length.value;
    case LengthKind::Percent:
        return available * length.value;
    }
    throw std::invalid_argument("Unknown length kind");
}

[[nodiscard]] float resolveChildAvailable(
    Length length,
    float available,
    float minimum,
    float maximum) {
    if (length.kind == LengthKind::Auto) {
        return clampLength(available, minimum, maximum);
    }
    return clampLength(resolveLength(length, available, available), minimum, maximum);
}

[[nodiscard]] float alignCross(float origin, float available, float size, Align align) noexcept {
    switch (align) {
    case Align::Center:
        return origin + (available - size) * 0.5f;
    case Align::End:
        return origin + available - size;
    default:
        return origin;
    }
}

[[nodiscard]] float textMeasureWidth(const Style& style, Size available) noexcept {
    float outerWidth = available.width;
    switch (style.width.kind) {
    case LengthKind::Pixels:
        outerWidth = style.width.value;
        break;
    case LengthKind::Percent:
        outerWidth = available.width * style.width.value;
        break;
    case LengthKind::Auto:
    default:
        break;
    }
    if (!std::isfinite(outerWidth)) {
        return INFINITY;
    }
    return std::max(0.0f, outerWidth - style.padding.horizontal());
}

}

Size TextNode::measureContent(const TextMeasurer& measurer, Size available) const noexcept {
    const float maxWidth = textLayout_.wrapping == TextWrapping::Wrap
        ? textMeasureWidth(style(), available)
        : INFINITY;
    return measurer.measure(text_, fontSize_, maxWidth, textLayout_.wrapping);
}

Size ButtonNode::measureContent(const TextMeasurer& measurer, Size available) const noexcept {
    const float maxWidth = textLayout_.wrapping == TextWrapping::Wrap
        ? textMeasureWidth(style(), available)
        : INFINITY;
    return measurer.measure(text_, fontSize_, maxWidth, textLayout_.wrapping);
}

Size ApproximateTextMeasurer::measure(std::string_view text, float fontSize) const noexcept {
    return measure(text, fontSize, INFINITY, TextWrapping::NoWrap);
}

Size ApproximateTextMeasurer::measure(
    std::string_view text,
    float fontSize,
    float maxWidth,
    TextWrapping wrapping) const noexcept {
    if (!std::isfinite(fontSize) || fontSize <= 0.0f) {
        return Size::Zero();
    }

    const float characterWidth = fontSize * 0.55f;
    const float lineHeight = fontSize * 1.25f;
    std::size_t visualLines = 0;
    std::size_t longestCharacters = 0;
    const bool shouldWrap =
        wrapping == TextWrapping::Wrap && std::isfinite(maxWidth) && maxWidth >= 0.0f;
    const std::size_t charactersPerLine = shouldWrap && characterWidth > 0.0f
        ? std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(maxWidth / characterWidth)))
        : static_cast<std::size_t>(-1);

    std::size_t lineStart = 0;
    for (std::size_t index = 0; index <= text.size(); ++index) {
        if (index != text.size() && text[index] != '\n') {
            continue;
        }

        const std::size_t lineLength = index - lineStart;
        if (shouldWrap) {
            const std::size_t wrappedLines = std::max<std::size_t>(
                1, (lineLength + charactersPerLine - 1) / charactersPerLine);
            visualLines += wrappedLines;
            longestCharacters = std::max(
                longestCharacters,
                std::min(lineLength, charactersPerLine));
        } else {
            ++visualLines;
            longestCharacters = std::max(longestCharacters, lineLength);
        }
        lineStart = index + 1;
    }

    if (visualLines == 0) {
        visualLines = 1;
    }

    const float measuredWidth = static_cast<float>(longestCharacters) * characterWidth;
    return {
        shouldWrap ? std::min(maxWidth, measuredWidth) : measuredWidth,
        static_cast<float>(visualLines) * lineHeight,
    };
}

class LayoutEngine {
public:
    static void layout(Node& root, Size viewport, const TextMeasurer& textMeasurer) {
        style_validation::validateTree(root);
        const float width = clampLength(
            resolveRootLength(root.style_.width, viewport.width),
            root.style_.minWidth,
            root.style_.maxWidth);
        const float height = clampLength(
            resolveRootLength(root.style_.height, viewport.height),
            root.style_.minHeight,
            root.style_.maxHeight);
        measure(root, viewport, textMeasurer);
        arrange(root, {0.0f, 0.0f, width, height});
    }

private:
    static Size measure(Node& node, Size available, const TextMeasurer& textMeasurer) {

        const Size childAvailable{
            resolveChildAvailable(
                node.style_.width, available.width, node.style_.minWidth, node.style_.maxWidth),
            resolveChildAvailable(
                node.style_.height, available.height, node.style_.minHeight, node.style_.maxHeight),
        };
        Size content = node.measureContent(textMeasurer, childAvailable);
        const Rect contentBox{0.0f, 0.0f, childAvailable.width, childAvailable.height};
        const Rect inner = contentBox.deflate(node.style_.padding);

        if (!node.children_.empty()) {
            if (auto* stack = dynamic_cast<StackNode*>(&node)) {
                float main = 0.0f;
                float cross = 0.0f;
                bool first = true;

                for (auto& childPtr : node.children_) {
                    Node& child = *childPtr;
                    const Size measured = measure(child, {inner.width, inner.height}, textMeasurer);
                    const Thickness margin = child.style_.margin;
                    const float childMain = stack->axis() == Axis::Column
                        ? measured.height + margin.vertical()
                        : measured.width + margin.horizontal();
                    const float childCross = stack->axis() == Axis::Column
                        ? measured.width + margin.horizontal()
                        : measured.height + margin.vertical();

                    if (!first) {
                        main += node.style_.gap;
                    }
                    first = false;
                    main += childMain;
                    cross = std::max(cross, childCross);
                }

                content = maxSize(
                    content,
                    stack->axis() == Axis::Column ? Size{cross, main} : Size{main, cross});
            } else {
                float width = 0.0f;
                float height = 0.0f;
                for (auto& childPtr : node.children_) {
                    Node& child = *childPtr;
                    const Size measured = measure(child, {inner.width, inner.height}, textMeasurer);
                    width = std::max(width, measured.width + child.style_.margin.horizontal());
                    height = std::max(height, measured.height + child.style_.margin.vertical());
                }
                content = maxSize(content, {width, height});
            }
        }

        const float intrinsicWidth = content.width + node.style_.padding.horizontal();
        const float intrinsicHeight = content.height + node.style_.padding.vertical();
        const float resolvedWidth = resolveLength(node.style_.width, available.width, intrinsicWidth);
        const float resolvedHeight = resolveLength(node.style_.height, available.height, intrinsicHeight);

        node.measured_ = {
            clampLength(resolvedWidth, node.style_.minWidth, node.style_.maxWidth),
            clampLength(resolvedHeight, node.style_.minHeight, node.style_.maxHeight),
        };
        return node.measured_;
    }

    static void arrange(Node& node, Rect bounds) {
        node.bounds_ = bounds;
        if (node.children_.empty()) {
            return;
        }

        const Rect content = bounds.deflate(node.style_.padding);
        if (auto* stack = dynamic_cast<StackNode*>(&node)) {
            arrangeStack(*stack, content);
        } else {
            arrangeOverlay(node, content);
        }
    }

    static void arrangeOverlay(Node& node, Rect content) {
        for (auto& childPtr : node.children_) {
            Node& child = *childPtr;
            const Thickness margin = child.style_.margin;
            const Size available{
                std::max(0.0f, content.width - margin.horizontal()),
                std::max(0.0f, content.height - margin.vertical()),
            };
            const Size measured = child.measured_;
            const float proposedWidth =
                child.style_.width.kind == LengthKind::Auto && node.style_.alignItems == Align::Stretch
                    ? available.width
                    : measured.width;
            const float proposedHeight =
                child.style_.height.kind == LengthKind::Auto && node.style_.alignItems == Align::Stretch
                    ? available.height
                    : measured.height;
            const float width = clampLength(
                proposedWidth, child.style_.minWidth, child.style_.maxWidth);
            const float height = clampLength(
                proposedHeight, child.style_.minHeight, child.style_.maxHeight);
            const float x =
                alignCross(content.x + margin.left, available.width, width, node.style_.alignItems);
            const float y =
                alignCross(content.y + margin.top, available.height, height, node.style_.alignItems);
            arrange(child, {x, y, width, height});
        }
    }

    static void arrangeStack(StackNode& stack, Rect content) {
        float fixedMain = 0.0f;
        float growTotal = 0.0f;

        for (auto& childPtr : stack.children_) {
            Node& child = *childPtr;
            const Thickness margin = child.style_.margin;
            fixedMain += stack.axis() == Axis::Column
                ? child.measured_.height + margin.vertical()
                : child.measured_.width + margin.horizontal();
            growTotal += std::max(0.0f, child.style_.flexGrow);
        }

        if (stack.children_.size() > 1) {
            fixedMain += stack.style_.gap * static_cast<float>(stack.children_.size() - 1);
        }

        const float availableMain = stack.axis() == Axis::Column ? content.height : content.width;
        const float remaining = std::max(0.0f, availableMain - fixedMain);

        stack.scrollRange_ = stack.scrollable_ ? std::max(0.0f, fixedMain - availableMain) : 0.0f;
        stack.scrollOffset_ = stack.scrollable_
            ? std::clamp(stack.scrollOffset_, 0.0f, stack.scrollRange_)
            : 0.0f;

        float cursor = stack.axis() == Axis::Column ? content.y : content.x;
        if (stack.scrollable_) {
            cursor -= stack.scrollOffset_;
        }

        for (auto& childPtr : stack.children_) {
            Node& child = *childPtr;
            const Thickness margin = child.style_.margin;
            const float grow = growTotal > 0.0f
                ? remaining * std::max(0.0f, child.style_.flexGrow) / growTotal
                : 0.0f;

            const float measuredMain =
                stack.axis() == Axis::Column ? child.measured_.height : child.measured_.width;
            const float mainSize = stack.axis() == Axis::Column
                ? clampLength(
                    measuredMain + grow, child.style_.minHeight, child.style_.maxHeight)
                : clampLength(
                    measuredMain + grow, child.style_.minWidth, child.style_.maxWidth);

            const float crossAvailable = stack.axis() == Axis::Column
                ? std::max(0.0f, content.width - margin.horizontal())
                : std::max(0.0f, content.height - margin.vertical());
            const float measuredCross =
                stack.axis() == Axis::Column ? child.measured_.width : child.measured_.height;
            const Length crossLength =
                stack.axis() == Axis::Column ? child.style_.width : child.style_.height;
            const float proposedCross =
                crossLength.kind == LengthKind::Auto && stack.style_.alignItems == Align::Stretch
                    ? crossAvailable
                    : std::min(measuredCross, crossAvailable);
            const float crossSize = stack.axis() == Axis::Column
                ? clampLength(
                    proposedCross, child.style_.minWidth, child.style_.maxWidth)
                : clampLength(
                    proposedCross, child.style_.minHeight, child.style_.maxHeight);

            const float crossOrigin = stack.axis() == Axis::Column
                ? alignCross(content.x + margin.left, crossAvailable, crossSize, stack.style_.alignItems)
                : alignCross(content.y + margin.top, crossAvailable, crossSize, stack.style_.alignItems);

            if (stack.axis() == Axis::Column) {
                cursor += margin.top;
                arrange(child, {crossOrigin, cursor, crossSize, mainSize});
                cursor += mainSize + margin.bottom + stack.style_.gap;
            } else {
                cursor += margin.left;
                arrange(child, {cursor, crossOrigin, mainSize, crossSize});
                cursor += mainSize + margin.right + stack.style_.gap;
            }
        }
    }
};


namespace layout_engine {
void perform(Node& root, Size viewport, const TextMeasurer& textMeasurer) {
    LayoutEngine::layout(root, viewport, textMeasurer);
}
}
}
