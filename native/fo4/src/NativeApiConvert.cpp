#include "NativeApiConvert.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace uirepo::fo4::native_api {

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

[[nodiscard]] bool finiteOrPositiveInfinity(float value) noexcept {
    return finiteNonNegative(value) || (std::isinf(value) && value > 0.0f);
}

[[nodiscard]] bool makeLength(const UIRepoLengthV1& source, Length& destination) noexcept {
    if (source.kind == UIREPO_LENGTH_AUTO) {
        destination = Length::Auto();
        return true;
    }
    if (!finiteNonNegative(source.value)) {
        return false;
    }
    if (source.kind == UIREPO_LENGTH_PIXELS) {
        destination = Length::Pixels(source.value);
        return true;
    }
    if (source.kind == UIREPO_LENGTH_PERCENT) {
        destination = Length::Percent(source.value);
        return true;
    }
    return false;
}

[[nodiscard]] bool makeThickness(const UIRepoThicknessV1& source, Thickness& destination) noexcept {
    if (!finiteNonNegative(source.left) || !finiteNonNegative(source.top) ||
        !finiteNonNegative(source.right) || !finiteNonNegative(source.bottom)) {
        return false;
    }
    destination = Thickness{source.left, source.top, source.right, source.bottom};
    return true;
}

[[nodiscard]] bool makeAlign(std::uint32_t source, Align& destination) noexcept {
    switch (source) {
    case UIREPO_ALIGN_START:
        destination = Align::Start;
        return true;
    case UIREPO_ALIGN_CENTER:
        destination = Align::Center;
        return true;
    case UIREPO_ALIGN_END:
        destination = Align::End;
        return true;
    case UIREPO_ALIGN_STRETCH:
        destination = Align::Stretch;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool makeTextLayout(
    const UIRepoTextLayoutV1* source,
    TextLayoutOptions& destination) noexcept {
    if (source == nullptr) {
        api::setResult(UIREPO_RESULT_INVALID_ARGUMENT);
        return false;
    }
    if (source->size != sizeof(UIRepoTextLayoutV1)) {
        api::setResult(UIREPO_RESULT_INVALID_STRUCT_SIZE);
        return false;
    }

    TextLayoutOptions layout{};
    switch (source->wrapping) {
    case UIREPO_TEXT_NO_WRAP:
        layout.wrapping = TextWrapping::NoWrap;
        break;
    case UIREPO_TEXT_WRAP:
        layout.wrapping = TextWrapping::Wrap;
        break;
    default:
        return false;
    }

    switch (source->alignment) {
    case UIREPO_TEXT_ALIGN_START:
        layout.alignment = TextAlignment::Start;
        break;
    case UIREPO_TEXT_ALIGN_CENTER:
        layout.alignment = TextAlignment::Center;
        break;
    case UIREPO_TEXT_ALIGN_END:
        layout.alignment = TextAlignment::End;
        break;
    default:
        return false;
    }

    switch (source->overflow) {
    case UIREPO_TEXT_OVERFLOW_CLIP:
        layout.overflow = TextOverflow::Clip;
        break;
    case UIREPO_TEXT_OVERFLOW_ELLIPSIS:
        layout.overflow = TextOverflow::Ellipsis;
        break;
    default:
        return false;
    }

    destination = layout;
    return true;
}

[[nodiscard]] bool makeStyle(const UIRepoStyleV1* source, Style& destination) noexcept {
    if (source == nullptr) {
        api::setResult(UIREPO_RESULT_INVALID_ARGUMENT);
        return false;
    }
    if (source->size != sizeof(UIRepoStyleV1)) {
        api::setResult(UIREPO_RESULT_INVALID_STRUCT_SIZE);
        return false;
    }

    Style style{};
    const float maxWidth = source->maxWidth > 0.0f
        ? source->maxWidth
        : std::numeric_limits<float>::infinity();
    const float maxHeight = source->maxHeight > 0.0f
        ? source->maxHeight
        : std::numeric_limits<float>::infinity();
    if (!makeLength(source->width, style.width) ||
        !makeLength(source->height, style.height) ||
        !finiteNonNegative(source->minWidth) ||
        !finiteNonNegative(source->minHeight) ||
        !finiteOrPositiveInfinity(source->maxWidth) ||
        !finiteOrPositiveInfinity(source->maxHeight) ||
        maxWidth < source->minWidth ||
        maxHeight < source->minHeight ||
        !makeThickness(source->padding, style.padding) ||
        !makeThickness(source->margin, style.margin) ||
        !finiteNonNegative(source->gap) ||
        !finiteNonNegative(source->flexGrow) ||
        !std::isfinite(source->opacity) ||
        source->opacity < 0.0f || source->opacity > 1.0f ||
        !finiteNonNegative(source->cornerRadius) ||
        !makeAlign(source->alignItems, style.alignItems)) {
        return false;
    }

    style.minWidth = source->minWidth;
    style.minHeight = source->minHeight;
    style.maxWidth = maxWidth;
    style.maxHeight = maxHeight;
    style.gap = source->gap;
    style.flexGrow = source->flexGrow;
    style.background = makeColor(source->background);
    style.opacity = source->opacity;
    style.zIndex = source->zIndex;
    style.cornerRadius = source->cornerRadius;
    style.clipToBounds = source->clipToBounds != 0;
    destination = style;
    return true;
}

[[nodiscard]] bool makeAxis(std::uint32_t source, Axis& destination) noexcept {
    if (source == UIREPO_AXIS_ROW) {
        destination = Axis::Row;
        return true;
    }
    if (source == UIREPO_AXIS_COLUMN) {
        destination = Axis::Column;
        return true;
    }
    return false;
}

[[nodiscard]] bool makePlayback(std::uint32_t source, VideoPlaybackState& destination) noexcept {
    switch (source) {
    case UIREPO_VIDEO_STOPPED:
        destination = VideoPlaybackState::Stopped;
        return true;
    case UIREPO_VIDEO_PLAYING:
        destination = VideoPlaybackState::Playing;
        return true;
    case UIREPO_VIDEO_PAUSED:
        destination = VideoPlaybackState::Paused;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool makePointerEvent(
    const UIRepoPointerEventV1* source,
    input::PointerEvent& destination) noexcept {
    if (source != nullptr && source->size != sizeof(UIRepoPointerEventV1)) {
        api::setResult(UIREPO_RESULT_INVALID_STRUCT_SIZE);
        return false;
    }
    if (source == nullptr ||
        !std::isfinite(source->x) || !std::isfinite(source->y) ||
        !std::isfinite(source->wheelDelta)) {
        api::setResult(UIREPO_RESULT_INVALID_ARGUMENT);
        return false;
    }

    switch (source->type) {
    case UIREPO_POINTER_MOVE:
        destination.type = input::PointerEventType::Move;
        break;
    case UIREPO_POINTER_BUTTON_DOWN:
        destination.type = input::PointerEventType::ButtonDown;
        break;
    case UIREPO_POINTER_BUTTON_UP:
        destination.type = input::PointerEventType::ButtonUp;
        break;
    case UIREPO_POINTER_WHEEL:
        destination.type = input::PointerEventType::Wheel;
        break;
    case UIREPO_POINTER_HORIZONTAL_WHEEL:
        destination.type = input::PointerEventType::HorizontalWheel;
        break;
    default:
        return false;
    }

    switch (source->button) {
    case UIREPO_POINTER_BUTTON_NONE:
        destination.button = input::PointerButton::None;
        break;
    case UIREPO_POINTER_BUTTON_LEFT:
        destination.button = input::PointerButton::Left;
        break;
    case UIREPO_POINTER_BUTTON_RIGHT:
        destination.button = input::PointerButton::Right;
        break;
    case UIREPO_POINTER_BUTTON_MIDDLE:
        destination.button = input::PointerButton::Middle;
        break;
    case UIREPO_POINTER_BUTTON_X1:
        destination.button = input::PointerButton::X1;
        break;
    case UIREPO_POINTER_BUTTON_X2:
        destination.button = input::PointerButton::X2;
        break;
    default:
        return false;
    }

    if ((destination.type == input::PointerEventType::ButtonDown ||
         destination.type == input::PointerEventType::ButtonUp) &&
        destination.button == input::PointerButton::None) {
        return false;
    }

    destination.x = source->x;
    destination.y = source->y;
    destination.wheelDelta = source->wheelDelta;
    destination.modifiers = source->modifiers;
    return true;
}

[[nodiscard]] bool makeKeyEvent(
    const UIRepoKeyEventV1* source,
    input::KeyEvent& destination) noexcept {
    if (source == nullptr) {
        api::setResult(UIREPO_RESULT_INVALID_ARGUMENT);
        return false;
    }
    if (source->size != sizeof(UIRepoKeyEventV1)) {
        api::setResult(UIREPO_RESULT_INVALID_STRUCT_SIZE);
        return false;
    }
    if (source->type == UIREPO_KEY_DOWN) {
        destination.type = input::KeyEventType::Down;
    } else if (source->type == UIREPO_KEY_UP) {
        destination.type = input::KeyEventType::Up;
    } else {
        return false;
    }
    destination.keyCode = source->keyCode;
    destination.modifiers = source->modifiers;
    destination.repeat = source->repeat != 0;
    return true;
}

} // namespace uirepo::fo4::native_api
