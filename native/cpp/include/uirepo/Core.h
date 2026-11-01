#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace uirepo::anim {
class Playback;
}

namespace uirepo {

struct Point {
    float x{};
    float y{};
};

struct Size {
    float width{};
    float height{};

    [[nodiscard]] static constexpr Size Zero() noexcept { return {}; }
};

struct Thickness {
    float left{};
    float top{};
    float right{};
    float bottom{};

    constexpr Thickness() noexcept = default;
    constexpr explicit Thickness(float uniform) noexcept
        : left(uniform), top(uniform), right(uniform), bottom(uniform) {}
    constexpr Thickness(float leftValue, float topValue, float rightValue, float bottomValue) noexcept
        : left(leftValue), top(topValue), right(rightValue), bottom(bottomValue) {}

    [[nodiscard]] constexpr float horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr float vertical() const noexcept { return top + bottom; }
};

struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};

    [[nodiscard]] constexpr float right() const noexcept { return x + width; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0.0f || height <= 0.0f; }

    [[nodiscard]] constexpr bool contains(Point point) const noexcept {
        return point.x >= x && point.y >= y && point.x <= right() && point.y <= bottom();
    }

    [[nodiscard]] constexpr bool intersects(Rect other) const noexcept {
        return !empty() && !other.empty() &&
               right() > other.x && other.right() > x &&
               bottom() > other.y && other.bottom() > y;
    }

    [[nodiscard]] constexpr Rect intersection(Rect other) const noexcept {
        const float left = std::max(x, other.x);
        const float top = std::max(y, other.y);
        const float rightEdge = std::min(right(), other.right());
        const float bottomEdge = std::min(bottom(), other.bottom());
        return {
            left,
            top,
            std::max(0.0f, rightEdge - left),
            std::max(0.0f, bottomEdge - top),
        };
    }

    [[nodiscard]] constexpr Rect deflate(Thickness value) const noexcept {
        const float newWidth = width > value.horizontal() ? width - value.horizontal() : 0.0f;
        const float newHeight = height > value.vertical() ? height - value.vertical() : 0.0f;
        return {x + value.left, y + value.top, newWidth, newHeight};
    }
};

struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    [[nodiscard]] static constexpr Color White() noexcept { return {255, 255, 255, 255}; }
};

enum class LengthKind : std::uint8_t {
    Auto,
    Pixels,
    Percent,
};

struct Length {
    LengthKind kind{LengthKind::Auto};
    float value{};

    [[nodiscard]] static constexpr Length Auto() noexcept { return {}; }
    [[nodiscard]] static constexpr Length Pixels(float value) noexcept {
        return {LengthKind::Pixels, value};
    }
    [[nodiscard]] static constexpr Length Percent(float value) noexcept {
        return {LengthKind::Percent, value};
    }
};

enum class Axis : std::uint8_t {
    Row,
    Column,
};

enum class Align : std::uint8_t {
    Start,
    Center,
    End,
    Stretch,
};

enum class TextWrapping : std::uint8_t {
    NoWrap,
    Wrap,
};

enum class TextAlignment : std::uint8_t {
    Start,
    Center,
    End,
};

enum class TextOverflow : std::uint8_t {
    Clip,
    Ellipsis,
};

struct TextLayoutOptions {
    TextWrapping wrapping{TextWrapping::NoWrap};
    TextAlignment alignment{TextAlignment::Start};
    TextOverflow overflow{TextOverflow::Clip};

    friend constexpr bool operator==(const TextLayoutOptions&, const TextLayoutOptions&) noexcept = default;
};

enum class VideoPlaybackState : std::uint8_t {
    Stopped,
    Playing,
    Paused,
};

struct Style {
    Length width{Length::Auto()};
    Length height{Length::Auto()};
    float minWidth{};
    float minHeight{};
    float maxWidth{INFINITY};
    float maxHeight{INFINITY};
    Thickness padding{};
    Thickness margin{};
    float gap{};
    float flexGrow{};
    Align alignItems{Align::Stretch};
    Color background{0, 0, 0, 0};
    float opacity{1.0f};
    int zIndex{};
    float cornerRadius{};
    bool clipToBounds{};
};

class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    [[nodiscard]] virtual Size measure(std::string_view text, float fontSize) const noexcept = 0;
    [[nodiscard]] virtual Size measure(
        std::string_view text,
        float fontSize,
        float maxWidth,
        TextWrapping wrapping) const noexcept {
        (void)maxWidth;
        (void)wrapping;
        return measure(text, fontSize);
    }
};

class ApproximateTextMeasurer final : public TextMeasurer {
public:
    [[nodiscard]] Size measure(std::string_view text, float fontSize) const noexcept override;
    [[nodiscard]] Size measure(
        std::string_view text,
        float fontSize,
        float maxWidth,
        TextWrapping wrapping) const noexcept override;
};

struct PushClipCommand {
    std::string_view nodeId;
    Rect bounds;
    float opacity;
    int zIndex;
};

struct PopClipCommand {
    std::string_view nodeId;
    Rect bounds;
    float opacity;
    int zIndex;
};

struct FillRectCommand {
    std::string_view nodeId;
    Rect bounds;
    float opacity;
    int zIndex;
    Color color;
    float cornerRadius;
};

struct DrawTextCommand {
    std::string_view nodeId;
    Rect bounds;
    float opacity;
    int zIndex;
    std::string_view text;
    float fontSize;
    Color color;
    TextLayoutOptions layout;
};

struct DrawImageCommand {
    std::string_view nodeId;
    Rect bounds;
    float opacity;
    int zIndex;
    std::string_view source;
};

struct DrawVideoCommand {
    std::string_view nodeId;
    Rect bounds;
    float opacity;
    int zIndex;
    std::string_view source;
    VideoPlaybackState playbackState;
    bool loop;
    float volume;
};

using DrawCommand = std::variant<
    PushClipCommand,
    PopClipCommand,
    FillRectCommand,
    DrawTextCommand,
    DrawImageCommand,
    DrawVideoCommand>;

} // namespace uirepo

#include "uirepo/Node.h"

namespace uirepo {
enum class AnimatedProperty : std::uint8_t {
    Opacity,
    Background,
    CornerRadius,
};

using AnimationId = std::uint64_t;

class Node;
class Document {
public:
    explicit Document(std::unique_ptr<Node> root, const TextMeasurer* textMeasurer = nullptr);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] Node& root() noexcept { return *root_; }
    [[nodiscard]] const Node& root() const noexcept { return *root_; }

    void setTextMeasurer(const TextMeasurer* textMeasurer) noexcept;
    [[nodiscard]] const TextMeasurer& textMeasurer() const noexcept { return *textMeasurer_; }

    void layout(Size viewport);
    AnimationId animateOpacity(Node& node, float target, anim::Playback playback);
    AnimationId animateBackground(Node& node, Color target, anim::Playback playback);
    AnimationId animateCornerRadius(Node& node, float target, anim::Playback playback);
    bool cancelAnimation(AnimationId id) noexcept;
    void advanceAnimations(float dt) noexcept;

    [[nodiscard]] bool hasLayout() const noexcept {
        return lastViewport_.width >= 0.0f && lastViewport_.height >= 0.0f;
    }
    [[nodiscard]] Size viewport() const noexcept { return lastViewport_; }
    [[nodiscard]] std::span<const DrawCommand> buildDisplayList();

    [[nodiscard]] Node* find(std::string_view id);
    [[nodiscard]] const Node* find(std::string_view id) const;
    [[nodiscard]] Node* hitTest(Point point) noexcept;

    [[nodiscard]] Node* focused() const noexcept { return focused_; }
    bool focus(Node& node);
    void clearFocus() noexcept;

private:
    friend class Node;
    friend class RetainedAnimationTestAccess;

    struct AnimationTrack;

    static const ApproximateTextMeasurer& fallbackTextMeasurer() noexcept;
    void markTreeDirty() noexcept;
    void markLayoutDirty() noexcept;
    AnimationId allocateAnimationId() noexcept;
    void cancelAnimationsFor(Node& node) noexcept;
    void cancelAnimationFor(Node& node, AnimatedProperty property) noexcept;
    void cancelAnimationsInSubtree(const Node& subtree) noexcept;
    void onSubtreeDetached(Node& subtree) noexcept;
    void ensureIndex() const;
    void indexNode(Node& node) const;
    [[nodiscard]] bool isReachableAndInteractive(const Node& node) const noexcept;
    [[nodiscard]] bool subtreeContains(const Node& subtree, const Node* needle) const noexcept;
    void refreshFocus() noexcept;
    void ensureNodeVisible(Node& node);
    void appendNode(const Node& node);
    [[nodiscard]] Node* hitTestNode(Node& node, Point point) noexcept;

    std::unique_ptr<Node> root_;
    std::vector<std::unique_ptr<AnimationTrack>> animations_;
    AnimationId nextAnimationId_{1};
    const TextMeasurer* textMeasurer_{};
    mutable std::unordered_map<std::string_view, Node*> index_;
    mutable bool indexDirty_{true};
    bool layoutDirty_{true};
    Size lastViewport_{-1.0f, -1.0f};
    Node* focused_{};
    std::vector<DrawCommand> displayList_;
};

} // namespace uirepo
