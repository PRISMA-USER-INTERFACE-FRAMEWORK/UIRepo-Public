#pragma once

#include "uirepo/Core.h"
#include "uirepo/InputPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uirepo::input {

enum class PointerEventType : std::uint8_t {
    Move,
    ButtonDown,
    ButtonUp,
    Wheel,
    HorizontalWheel,
};

enum class PointerButton : std::uint8_t {
    None,
    Left,
    Right,
    Middle,
    X1,
    X2,
};

enum Modifier : std::uint32_t {
    ModifierNone = 0,
    ModifierShift = 1u << 0,
    ModifierControl = 1u << 1,
    ModifierAlt = 1u << 2,
};

struct PointerEvent {
    PointerEventType type{PointerEventType::Move};
    PointerButton button{PointerButton::None};
    float x{};
    float y{};
    float wheelDelta{};
    std::uint32_t modifiers{};
};

enum class KeyEventType : std::uint8_t {
    Down,
    Up,
};

struct KeyEvent {
    KeyEventType type{KeyEventType::Down};
    std::uint32_t keyCode{};
    std::uint32_t modifiers{};
    bool repeat{};
};

inline constexpr std::uint32_t KeyTab = 0x09u;
inline constexpr std::uint32_t KeyEnter = 0x0Du;
inline constexpr std::uint32_t KeyEscape = 0x1Bu;
inline constexpr std::uint32_t KeySpace = 0x20u;
inline constexpr std::uint32_t KeyLeft = 0x25u;
inline constexpr std::uint32_t KeyUp = 0x26u;
inline constexpr std::uint32_t KeyRight = 0x27u;
inline constexpr std::uint32_t KeyDown = 0x28u;
inline constexpr std::uint32_t KeyF4 = 0x73u;
inline constexpr float WheelDeltaPerNotch = 120.0f;
inline constexpr float WheelScrollPixelsPerNotch = 48.0f;

[[nodiscard]] constexpr NavigationCommand navigationForKey(
    std::uint32_t keyCode,
    std::uint32_t modifiers) noexcept {
    switch (keyCode) {
    case KeyTab:
        return (modifiers & ModifierShift) != 0
            ? NavigationCommand::Previous
            : NavigationCommand::Next;
    case KeyLeft:
        return NavigationCommand::Left;
    case KeyUp:
        return NavigationCommand::Up;
    case KeyRight:
        return NavigationCommand::Right;
    case KeyDown:
        return NavigationCommand::Down;
    case KeyEnter:
    case KeySpace:
        return NavigationCommand::Accept;
    default:
        return NavigationCommand::None;
    }
}

[[nodiscard]] constexpr bool isActivationKey(std::uint32_t keyCode) noexcept {
    return keyCode == KeyEnter || keyCode == KeySpace;
}

enum class SequenceOwner : std::uint8_t {
    None,
    Fallout,
    UIRepo,
};

class PointerSequenceState final {
public:
    void begin(PointerButton button, bool handledByUIRepo) noexcept {
        const std::size_t index = buttonIndex(button);
        if (index >= owners_.size()) {
            return;
        }
        owners_[index] = handledByUIRepo ? SequenceOwner::UIRepo : SequenceOwner::Fallout;
    }

    [[nodiscard]] SequenceOwner owner(PointerButton button) const noexcept {
        const std::size_t index = buttonIndex(button);
        return index < owners_.size() ? owners_[index] : SequenceOwner::None;
    }

    [[nodiscard]] bool end(PointerButton button) noexcept {
        const std::size_t index = buttonIndex(button);
        if (index >= owners_.size()) {
            return false;
        }
        const bool ownedByUIRepo = owners_[index] == SequenceOwner::UIRepo;
        owners_[index] = SequenceOwner::None;
        return ownedByUIRepo;
    }

    [[nodiscard]] bool anyOwned() const noexcept {
        return std::any_of(owners_.begin(), owners_.end(), [](SequenceOwner owner) {
            return owner == SequenceOwner::UIRepo;
        });
    }

    void clear() noexcept {
        owners_.fill(SequenceOwner::None);
    }

private:
    [[nodiscard]] static constexpr std::size_t buttonIndex(PointerButton button) noexcept {
        switch (button) {
        case PointerButton::Left:
            return 0;
        case PointerButton::Right:
            return 1;
        case PointerButton::Middle:
            return 2;
        case PointerButton::X1:
            return 3;
        case PointerButton::X2:
            return 4;
        case PointerButton::None:
        default:
            return 5;
        }
    }

    std::array<SequenceOwner, 5> owners_{};
};

class KeySequenceState final {
public:
    static constexpr std::size_t kVirtualKeyCount = 256;

    void begin(std::uint32_t keyCode, bool handledByUIRepo) noexcept {
        if (keyCode >= kVirtualKeyCount) {
            return;
        }
        owners_[keyCode] = handledByUIRepo ? SequenceOwner::UIRepo : SequenceOwner::Fallout;
    }

    [[nodiscard]] SequenceOwner owner(std::uint32_t keyCode) const noexcept {
        return keyCode < kVirtualKeyCount ? owners_[keyCode] : SequenceOwner::None;
    }

    [[nodiscard]] bool end(std::uint32_t keyCode) noexcept {
        if (keyCode >= kVirtualKeyCount) {
            return false;
        }
        const bool ownedByUIRepo = owners_[keyCode] == SequenceOwner::UIRepo;
        owners_[keyCode] = SequenceOwner::None;
        return ownedByUIRepo;
    }

    void clear() noexcept {
        owners_.fill(SequenceOwner::None);
    }

private:
    std::array<SequenceOwner, kVirtualKeyCount> owners_{};
};

[[nodiscard]] constexpr bool consumePointerRelease(
    bool pressOwnedByUIRepo,
    bool releaseHandledByUIRepo) noexcept {
    return pressOwnedByUIRepo || releaseHandledByUIRepo;
}

[[nodiscard]] constexpr Point mapClientToViewport(
    Point point,
    Size clientSize,
    Size viewportSize) noexcept {
    if (clientSize.width <= 0.0f || clientSize.height <= 0.0f ||
        viewportSize.width <= 0.0f || viewportSize.height <= 0.0f) {
        return point;
    }
    return {
        point.x * viewportSize.width / clientSize.width,
        point.y * viewportSize.height / clientSize.height,
    };
}

[[nodiscard]] constexpr bool pointInsideClient(Point point, Size clientSize) noexcept {
    return point.x >= 0.0f && point.y >= 0.0f &&
           point.x < clientSize.width && point.y < clientSize.height;
}

inline void collectFocusable(Node& node, bool ancestorsInteractive, std::vector<Node*>& out) {
    const bool interactive = ancestorsInteractive && node.visible() && node.enabled();
    if (!interactive) {
        return;
    }
    if (node.focusable()) {
        out.push_back(&node);
    }
    for (const auto& child : node.children()) {
        if (child) {
            collectFocusable(*child, interactive, out);
        }
    }
}

[[nodiscard]] inline bool clipsChildren(const Node& node) noexcept {
    if (node.style().clipToBounds) {
        return true;
    }
    const auto* stack = dynamic_cast<const StackNode*>(&node);
    return stack != nullptr && stack->scrollable();
}

[[nodiscard]] inline Rect nodeClipBounds(const Node& node) noexcept {
    const auto* stack = dynamic_cast<const StackNode*>(&node);
    if (stack != nullptr && stack->scrollable()) {
        return stack->scrollViewport();
    }
    return node.bounds();
}

[[nodiscard]] inline Rect visibleBoundsThroughClips(const Node& node) noexcept {
    Rect visible = node.bounds();
    for (const Node* ancestor = node.parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        if (clipsChildren(*ancestor)) {
            visible = visible.intersection(nodeClipBounds(*ancestor));
            if (visible.empty()) {
                return {};
            }
        }
    }
    return visible;
}

[[nodiscard]] inline bool visibleForNavigation(const Node& node) noexcept {
    const Rect bounds = node.bounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return false;
    }
    return !visibleBoundsThroughClips(node).empty();
}

[[nodiscard]] inline bool hasScrollableAncestor(const Node& node) noexcept {
    for (const Node* ancestor = node.parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        const auto* stack = dynamic_cast<const StackNode*>(ancestor);
        if (stack != nullptr && stack->scrollable()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool routeWheel(
    Document& document,
    Point point,
    float wheelDelta,
    float pixelsPerNotch = WheelScrollPixelsPerNotch) {
    if (!document.hasLayout() || !std::isfinite(wheelDelta) || wheelDelta == 0.0f ||
        !std::isfinite(pixelsPerNotch) || pixelsPerNotch <= 0.0f) {
        return false;
    }

    Node* hit = document.hitTest(point);
    if (hit == nullptr) {
        return false;
    }

    const float delta = -(wheelDelta / WheelDeltaPerNotch) * pixelsPerNotch;
    for (Node* current = hit; current != nullptr; current = current->parent()) {
        auto* stack = dynamic_cast<StackNode*>(current);
        if (stack == nullptr || !stack->scrollable()) {
            continue;
        }
        if (stack->scrollBy(delta)) {
            document.layout(document.viewport());
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool routeHorizontalWheel(
    Document& document,
    Point point,
    float wheelDelta,
    float pixelsPerNotch = WheelScrollPixelsPerNotch) {
    if (!document.hasLayout() || !std::isfinite(wheelDelta) || wheelDelta == 0.0f ||
        !std::isfinite(pixelsPerNotch) || pixelsPerNotch <= 0.0f) {
        return false;
    }

    Node* hit = document.hitTest(point);
    if (hit == nullptr) {
        return false;
    }

    // Win32 defines positive WM_MOUSEHWHEEL delta as a tilt to the right. Row
    // scroll offsets increase toward later/right-side children, so unlike the
    // vertical wheel path the sign is intentionally not inverted here.
    const float delta = (wheelDelta / WheelDeltaPerNotch) * pixelsPerNotch;
    for (Node* current = hit; current != nullptr; current = current->parent()) {
        auto* stack = dynamic_cast<StackNode*>(current);
        if (stack == nullptr || !stack->scrollable() || stack->axis() != Axis::Row) {
            continue;
        }
        if (stack->scrollBy(delta)) {
            document.layout(document.viewport());
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr float rectCenterX(Rect rect) noexcept {
    return rect.x + rect.width * 0.5f;
}

[[nodiscard]] constexpr float rectCenterY(Rect rect) noexcept {
    return rect.y + rect.height * 0.5f;
}

[[nodiscard]] constexpr float rangeGap(
    float firstStart,
    float firstEnd,
    float secondStart,
    float secondEnd) noexcept {
    if (firstEnd < secondStart) {
        return secondStart - firstEnd;
    }
    if (secondEnd < firstStart) {
        return firstStart - secondEnd;
    }
    return 0.0f;
}

[[nodiscard]] constexpr bool isSpatialCommand(NavigationCommand command) noexcept {
    return command == NavigationCommand::Up || command == NavigationCommand::Down ||
           command == NavigationCommand::Left || command == NavigationCommand::Right;
}

[[nodiscard]] constexpr bool hasSpatialBounds(Rect rect) noexcept {
    return rect.width > 0.0f || rect.height > 0.0f;
}

struct SpatialScore {
    bool outsideLane{};
    float primaryDistance{};
    float crossGap{};
    float crossCenterDistance{};
    std::size_t traversalIndex{};
};

[[nodiscard]] constexpr bool betterSpatialScore(
    const SpatialScore& candidate,
    const SpatialScore& current) noexcept {
    if (candidate.outsideLane != current.outsideLane) {
        return !candidate.outsideLane;
    }
    if (candidate.primaryDistance != current.primaryDistance) {
        return candidate.primaryDistance < current.primaryDistance;
    }
    if (candidate.crossGap != current.crossGap) {
        return candidate.crossGap < current.crossGap;
    }
    if (candidate.crossCenterDistance != current.crossCenterDistance) {
        return candidate.crossCenterDistance < current.crossCenterDistance;
    }
    return candidate.traversalIndex < current.traversalIndex;
}

[[nodiscard]] inline bool spatialScore(
    Rect current,
    Rect candidate,
    NavigationCommand command,
    std::size_t traversalIndex,
    SpatialScore& score) noexcept {
    const float currentX = rectCenterX(current);
    const float currentY = rectCenterY(current);
    const float candidateX = rectCenterX(candidate);
    const float candidateY = rectCenterY(candidate);

    float primaryDistance{};
    float crossCenterDistance{};
    float crossGap{};

    switch (command) {
    case NavigationCommand::Left:
        if (candidateX >= currentX) {
            return false;
        }
        primaryDistance = currentX - candidateX;
        crossCenterDistance = std::abs(candidateY - currentY);
        crossGap = rangeGap(current.y, current.bottom(), candidate.y, candidate.bottom());
        break;
    case NavigationCommand::Right:
        if (candidateX <= currentX) {
            return false;
        }
        primaryDistance = candidateX - currentX;
        crossCenterDistance = std::abs(candidateY - currentY);
        crossGap = rangeGap(current.y, current.bottom(), candidate.y, candidate.bottom());
        break;
    case NavigationCommand::Up:
        if (candidateY >= currentY) {
            return false;
        }
        primaryDistance = currentY - candidateY;
        crossCenterDistance = std::abs(candidateX - currentX);
        crossGap = rangeGap(current.x, current.right(), candidate.x, candidate.right());
        break;
    case NavigationCommand::Down:
        if (candidateY <= currentY) {
            return false;
        }
        primaryDistance = candidateY - currentY;
        crossCenterDistance = std::abs(candidateX - currentX);
        crossGap = rangeGap(current.x, current.right(), candidate.x, candidate.right());
        break;
    default:
        return false;
    }

    score = {
        .outsideLane = crossGap > 0.0f,
        .primaryDistance = primaryDistance,
        .crossGap = crossGap,
        .crossCenterDistance = crossCenterDistance,
        .traversalIndex = traversalIndex,
    };
    return true;
}

[[nodiscard]] inline Node* orderedNavigationTarget(
    const std::vector<Node*>& focusable,
    Node* current,
    NavigationCommand command) noexcept {
    if (command != NavigationCommand::Next && command != NavigationCommand::Previous) {
        return nullptr;
    }
    if (focusable.empty()) {
        return nullptr;
    }

    const auto currentIt = std::find(focusable.begin(), focusable.end(), current);
    if (currentIt == focusable.end()) {
        return command == NavigationCommand::Next ? focusable.front() : focusable.back();
    }
    if (focusable.size() == 1) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(std::distance(focusable.begin(), currentIt));
    if (command == NavigationCommand::Next) {
        return focusable[(index + 1) % focusable.size()];
    }
    return focusable[(index + focusable.size() - 1) % focusable.size()];
}

[[nodiscard]] inline Node* preLayoutDirectionalTarget(
    const std::vector<Node*>& focusable,
    Node* current,
    NavigationCommand command) noexcept {
    switch (command) {
    case NavigationCommand::Right:
    case NavigationCommand::Down:
        return orderedNavigationTarget(focusable, current, NavigationCommand::Next);
    case NavigationCommand::Left:
    case NavigationCommand::Up:
        return orderedNavigationTarget(focusable, current, NavigationCommand::Previous);
    default:
        return nullptr;
    }
}

[[nodiscard]] inline Node* bestSpatialCandidate(
    const std::vector<Node*>& focusable,
    Node* current,
    NavigationCommand command,
    bool requireVisible) noexcept {
    Node* best{};
    SpatialScore bestScore{};
    bool haveBest = false;

    for (std::size_t index = 0; index < focusable.size(); ++index) {
        Node* candidate = focusable[index];
        if (candidate == nullptr || candidate == current || !hasSpatialBounds(candidate->bounds())) {
            continue;
        }

        const bool visible = visibleForNavigation(*candidate);
        if (requireVisible) {
            if (!visible) {
                continue;
            }
        } else if (visible || !hasScrollableAncestor(*candidate)) {
            continue;
        }

        SpatialScore candidateScore{};
        if (!spatialScore(current->bounds(), candidate->bounds(), command, index, candidateScore)) {
            continue;
        }
        if (!haveBest || betterSpatialScore(candidateScore, bestScore)) {
            best = candidate;
            bestScore = candidateScore;
            haveBest = true;
        }
    }
    return best;
}

[[nodiscard]] inline Node* spatialNavigationTarget(
    const std::vector<Node*>& focusable,
    Node* current,
    NavigationCommand command) noexcept {
    if (!isSpatialCommand(command) || current == nullptr) {
        return nullptr;
    }
    if (!hasSpatialBounds(current->bounds())) {
        return preLayoutDirectionalTarget(focusable, current, command);
    }

    if (Node* visible = bestSpatialCandidate(focusable, current, command, true)) {
        return visible;
    }
    return bestSpatialCandidate(focusable, current, command, false);
}

[[nodiscard]] inline Node* navigationTarget(Document& document, NavigationCommand command) {
    std::vector<Node*> focusable;
    collectFocusable(document.root(), true, focusable);

    Node* current = document.focused();
    if (command == NavigationCommand::Next || command == NavigationCommand::Previous) {
        return orderedNavigationTarget(focusable, current, command);
    }
    if (isSpatialCommand(command)) {
        return spatialNavigationTarget(focusable, current, command);
    }
    return nullptr;
}

} // namespace uirepo::input
