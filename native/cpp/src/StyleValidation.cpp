#include "StyleValidation.h"

namespace uirepo::style_validation {
namespace {
[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

[[nodiscard]] bool finiteNonNegative(Thickness value) noexcept {
    return finiteNonNegative(value.left) && finiteNonNegative(value.top) &&
           finiteNonNegative(value.right) && finiteNonNegative(value.bottom);
}

[[nodiscard]] bool validMaximum(float value) noexcept {
    return value == 0.0f ||
           (std::isfinite(value) && value > 0.0f) ||
           (std::isinf(value) && value > 0.0f);
}

void validateLength(Length length) {
    switch (length.kind) {
    case LengthKind::Auto:
        return;
    case LengthKind::Pixels:
    case LengthKind::Percent:
        if (!finiteNonNegative(length.value)) {
            throw std::invalid_argument("Length value must be finite and non-negative");
        }
        return;
    }
    throw std::invalid_argument("Unknown length kind");
}

void validateDimensionBounds(float minimum, float maximum) {
    if (!finiteNonNegative(minimum)) {
        throw std::invalid_argument("Minimum size must be finite and non-negative");
    }
    if (!validMaximum(maximum)) {
        throw std::invalid_argument("Maximum size must be zero, positive finite, or positive infinity");
    }
    if (maximum > 0.0f && maximum < minimum) {
        throw std::invalid_argument("Maximum size cannot be smaller than minimum size");
    }
}
}

void validateOpacity(float value) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
        throw std::invalid_argument("Opacity must be between 0 and 1");
    }
}

void validateCornerRadius(float value) {
    if (!finiteNonNegative(value)) {
        throw std::invalid_argument("Corner radius must be finite and non-negative");
    }
}

void validate(const Style& style) {
    validateLength(style.width);
    validateLength(style.height);
    validateDimensionBounds(style.minWidth, style.maxWidth);
    validateDimensionBounds(style.minHeight, style.maxHeight);
    if (!finiteNonNegative(style.padding)) {
        throw std::invalid_argument("Padding must be finite and non-negative");
    }
    if (!finiteNonNegative(style.margin)) {
        throw std::invalid_argument("Margin must be finite and non-negative");
    }
    if (!finiteNonNegative(style.gap)) {
        throw std::invalid_argument("Gap must be finite and non-negative");
    }
    if (!finiteNonNegative(style.flexGrow)) {
        throw std::invalid_argument("FlexGrow must be finite and non-negative");
    }
    validateOpacity(style.opacity);
    validateCornerRadius(style.cornerRadius);
}

void validateTree(const Node& root) {
    validate(root.style());
    for (const auto& child : root.children()) {
        validateTree(*child);
    }
}
}
