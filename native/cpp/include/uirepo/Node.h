#pragma once

#include "uirepo/Core.h"

namespace uirepo {

class Document;

class Node {
public:
    explicit Node(std::string id);
    virtual ~Node() = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] Style& style() noexcept;
    [[nodiscard]] const Style& style() const noexcept { return style_; }
    [[nodiscard]] float opacity() const noexcept { return style_.opacity; }
    void setOpacity(float value);
    [[nodiscard]] Color background() const noexcept { return style_.background; }
    void setBackground(Color value) noexcept;
    [[nodiscard]] float cornerRadius() const noexcept { return style_.cornerRadius; }
    void setCornerRadius(float value);
    [[nodiscard]] Node* parent() const noexcept { return parent_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Node>>& children() const noexcept { return children_; }
    [[nodiscard]] Rect bounds() const noexcept { return bounds_; }

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool value);
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool value);
    [[nodiscard]] bool focusable() const noexcept { return focusable_; }
    [[nodiscard]] bool focused() const noexcept { return focused_; }

    template <typename T, typename... Args>
    T& emplaceChild(Args&&... args) {
        static_assert(std::is_base_of_v<Node, T>);
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *child;
        addChild(std::move(child));
        return result;
    }

    Node& addChild(std::unique_ptr<Node> child);
    [[nodiscard]] std::unique_ptr<Node> removeChild(Node& child);

protected:
    void setFocusable(bool value) noexcept { focusable_ = value; }
    void markLayoutChanged();
    [[nodiscard]] virtual Size measureContent(
        const TextMeasurer& measurer,
        Size available) const noexcept {
        (void)measurer;
        (void)available;
        return Size::Zero();
    }
    virtual void appendDrawCommands(std::vector<DrawCommand>& commands) const;

private:
    friend class Document;
    friend class LayoutEngine;

    [[nodiscard]] bool isAncestorOf(const Node& node) const noexcept;
    void attachToDocument(Document* document);
    void markTreeChanged();

    std::string id_;
    Style style_{};
    Node* parent_{};
    Document* document_{};
    std::vector<std::unique_ptr<Node>> children_;
    Rect bounds_{};
    Size measured_{};
    bool isDocumentRoot_{};
    bool visible_{true};
    bool enabled_{true};
    bool focusable_{};
    bool focused_{};
};

class PanelNode final : public Node {
public:
    using Node::Node;
};

class StackNode final : public Node {
public:
    StackNode(std::string id, Axis axis) : Node(std::move(id)), axis_(axis) {}

    [[nodiscard]] Axis axis() const noexcept { return axis_; }
    void setAxis(Axis value);

    [[nodiscard]] bool scrollable() const noexcept { return scrollable_; }
    void setScrollable(bool value);
    [[nodiscard]] float scrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] float scrollRange() const noexcept { return scrollRange_; }
    [[nodiscard]] bool setScrollOffset(float value) noexcept;
    [[nodiscard]] bool scrollBy(float delta) noexcept;
    [[nodiscard]] Rect scrollViewport() const noexcept { return bounds().deflate(style().padding); }

private:
    friend class LayoutEngine;
    friend class Document;

    Axis axis_;
    bool scrollable_{};
    float scrollOffset_{};
    float scrollRange_{};
};

class TextNode final : public Node {
public:
    TextNode(std::string id, std::string text);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void setText(std::string value);
    [[nodiscard]] float fontSize() const noexcept { return fontSize_; }
    void setFontSize(float value);
    [[nodiscard]] Color color() const noexcept { return color_; }
    void setColor(Color value) noexcept { color_ = value; }
    [[nodiscard]] TextLayoutOptions textLayout() const noexcept { return textLayout_; }
    void setTextLayout(TextLayoutOptions value);

protected:
    [[nodiscard]] Size measureContent(
        const TextMeasurer& measurer,
        Size available) const noexcept override;
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    std::string text_;
    float fontSize_{18.0f};
    Color color_{Color::White()};
    TextLayoutOptions textLayout_{};
};

class ButtonNode final : public Node {
public:
    ButtonNode(std::string id, std::string text);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void setText(std::string value);
    [[nodiscard]] float fontSize() const noexcept { return fontSize_; }
    void setFontSize(float value);
    [[nodiscard]] Color textColor() const noexcept { return textColor_; }
    void setTextColor(Color value) noexcept { textColor_ = value; }
    [[nodiscard]] Color focusedBackground() const noexcept { return focusedBackground_; }
    void setFocusedBackground(Color value) noexcept { focusedBackground_ = value; }
    [[nodiscard]] TextLayoutOptions textLayout() const noexcept { return textLayout_; }
    void setTextLayout(TextLayoutOptions value);

protected:
    [[nodiscard]] Size measureContent(
        const TextMeasurer& measurer,
        Size available) const noexcept override;
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    std::string text_;
    float fontSize_{18.0f};
    Color textColor_{Color::White()};
    Color focusedBackground_{75, 88, 112, 255};
    TextLayoutOptions textLayout_{};
};

class ImageNode final : public Node {
public:
    ImageNode(std::string id, std::string source, Size naturalSize);

    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    void setSource(std::string value);
    [[nodiscard]] Size naturalSize() const noexcept { return naturalSize_; }
    void setNaturalSize(Size value);

protected:
    [[nodiscard]] Size measureContent(const TextMeasurer&, Size) const noexcept override { return naturalSize_; }
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    std::string source_;
    Size naturalSize_{};
};

class VideoNode final : public Node {
public:
    VideoNode(std::string id, std::string source, Size naturalSize);

    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    void setSource(std::string value);
    [[nodiscard]] Size naturalSize() const noexcept { return naturalSize_; }
    void setNaturalSize(Size value);
    [[nodiscard]] VideoPlaybackState playbackState() const noexcept { return playbackState_; }
    void setPlaybackState(VideoPlaybackState value) noexcept { playbackState_ = value; }
    [[nodiscard]] bool loop() const noexcept { return loop_; }
    void setLoop(bool value) noexcept { loop_ = value; }
    [[nodiscard]] float volume() const noexcept { return volume_; }
    void setVolume(float value) noexcept;

protected:
    [[nodiscard]] Size measureContent(const TextMeasurer&, Size) const noexcept override { return naturalSize_; }
    void appendDrawCommands(std::vector<DrawCommand>& commands) const override;

private:
    std::string source_;
    Size naturalSize_{};
    VideoPlaybackState playbackState_{VideoPlaybackState::Stopped};
    bool loop_{};
    float volume_{1.0f};
};

} // namespace uirepo
