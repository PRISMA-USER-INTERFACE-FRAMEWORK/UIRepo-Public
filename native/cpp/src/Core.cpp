#include "uirepo/Core.h"

#include "StyleValidation.h"

namespace uirepo {
Node::Node(std::string id) : id_(std::move(id)) {
    if (id_.empty()) {
        throw std::invalid_argument("A node ID is required");
    }
}

Style& Node::style() noexcept {
    if (document_ != nullptr) {
        document_->cancelAnimationsFor(*this);
    }
    markLayoutChanged();
    return style_;
}

void Node::setOpacity(float value) {
    style_validation::validateOpacity(value);
    if (document_ != nullptr) {
        document_->cancelAnimationFor(*this, AnimatedProperty::Opacity);
    }
    style_.opacity = value;
}

void Node::setBackground(Color value) noexcept {
    if (document_ != nullptr) {
        document_->cancelAnimationFor(*this, AnimatedProperty::Background);
    }
    style_.background = value;
}

void Node::setCornerRadius(float value) {
    style_validation::validateCornerRadius(value);
    if (document_ != nullptr) {
        document_->cancelAnimationFor(*this, AnimatedProperty::CornerRadius);
    }
    style_.cornerRadius = value;
}

void Node::setVisible(bool value) {
    if (visible_ == value) {
        return;
    }
    visible_ = value;
    markLayoutChanged();
    if (document_ != nullptr) {
        if (!value) {
            document_->cancelAnimationsInSubtree(*this);
        }
        document_->refreshFocus();
    }
}

void Node::setEnabled(bool value) {
    if (enabled_ == value) {
        return;
    }
    enabled_ = value;
    if (document_ != nullptr) {
        document_->refreshFocus();
    }
}

Node& Node::addChild(std::unique_ptr<Node> child) {
    if (!child) {
        throw std::invalid_argument("Child cannot be null");
    }
    if (child.get() == this) {
        throw std::invalid_argument("A node cannot parent itself");
    }
    if (child->isDocumentRoot_) {
        throw std::invalid_argument("A document root cannot be attached as a child");
    }
    if (child->parent_ != nullptr) {
        throw std::invalid_argument("Node already has a parent");
    }
    if (child->isAncestorOf(*this)) {
        throw std::invalid_argument("Adding this child would create a cycle");
    }

    child->parent_ = this;
    child->attachToDocument(document_);
    Node& result = *child;
    children_.push_back(std::move(child));
    markTreeChanged();
    return result;
}

std::unique_ptr<Node> Node::removeChild(Node& child) {
    const auto it = std::find_if(children_.begin(), children_.end(), [&child](const auto& candidate) {
        return candidate.get() == &child;
    });
    if (it == children_.end()) {
        return {};
    }

    if (document_ != nullptr) {
        document_->onSubtreeDetached(child);
    }

    std::unique_ptr<Node> removed = std::move(*it);
    children_.erase(it);
    removed->parent_ = nullptr;
    removed->attachToDocument(nullptr);
    markTreeChanged();
    return removed;
}

bool Node::isAncestorOf(const Node& node) const noexcept {
    for (const Node* current = node.parent_; current != nullptr; current = current->parent_) {
        if (current == this) {
            return true;
        }
    }
    return false;
}

void Node::attachToDocument(Document* document) {
    document_ = document;
    for (auto& child : children_) {
        child->attachToDocument(document);
    }
}

void Node::markTreeChanged() {
    if (document_ != nullptr) {
        document_->markTreeDirty();
    }
}

void Node::markLayoutChanged() {
    if (document_ != nullptr) {
        document_->markLayoutDirty();
    }
}

void StackNode::setAxis(Axis value) {
    if (axis_ == value) {
        return;
    }
    axis_ = value;
    markLayoutChanged();
}

void StackNode::setScrollable(bool value) {
    if (scrollable_ == value) {
        return;
    }
    scrollable_ = value;
    if (!scrollable_) {
        scrollOffset_ = 0.0f;
        scrollRange_ = 0.0f;
    }
    markLayoutChanged();
}

bool StackNode::setScrollOffset(float value) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    const float next = std::clamp(value, 0.0f, scrollRange_);
    if (next == scrollOffset_) {
        return false;
    }
    scrollOffset_ = next;
    markLayoutChanged();
    return true;
}

bool StackNode::scrollBy(float delta) noexcept {
    if (!scrollable_ || !std::isfinite(delta) || delta == 0.0f) {
        return false;
    }
    return setScrollOffset(scrollOffset_ + delta);
}

TextNode::TextNode(std::string id, std::string text)
    : Node(std::move(id)), text_(std::move(text)) {}

void TextNode::setText(std::string value) {
    text_ = std::move(value);
    markLayoutChanged();
}

void TextNode::setFontSize(float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        throw std::invalid_argument("Font size must be finite and positive");
    }
    fontSize_ = value;
    markLayoutChanged();
}

void TextNode::setTextLayout(TextLayoutOptions value) {
    if (textLayout_ == value) {
        return;
    }
    textLayout_ = value;
    markLayoutChanged();
}

ButtonNode::ButtonNode(std::string id, std::string text)
    : Node(std::move(id)), text_(std::move(text)) {
    setFocusable(true);
    style().padding = Thickness{12.0f, 8.0f, 12.0f, 8.0f};
    style().background = Color{40, 44, 52, 255};
}

void ButtonNode::setText(std::string value) {
    text_ = std::move(value);
    markLayoutChanged();
}

void ButtonNode::setFontSize(float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        throw std::invalid_argument("Font size must be finite and positive");
    }
    fontSize_ = value;
    markLayoutChanged();
}

void ButtonNode::setTextLayout(TextLayoutOptions value) {
    if (textLayout_ == value) {
        return;
    }
    textLayout_ = value;
    markLayoutChanged();
}

ImageNode::ImageNode(std::string id, std::string source, Size naturalSize)
    : Node(std::move(id)), source_(std::move(source)), naturalSize_(naturalSize) {}

void ImageNode::setSource(std::string value) {
    source_ = std::move(value);
}

void ImageNode::setNaturalSize(Size value) {
    naturalSize_ = value;
    markLayoutChanged();
}

VideoNode::VideoNode(std::string id, std::string source, Size naturalSize)
    : Node(std::move(id)), source_(std::move(source)), naturalSize_(naturalSize) {}

void VideoNode::setSource(std::string value) {
    source_ = std::move(value);
}

void VideoNode::setNaturalSize(Size value) {
    naturalSize_ = value;
    markLayoutChanged();
}

void VideoNode::setVolume(float value) noexcept {
    volume_ = std::clamp(value, 0.0f, 1.0f);
}

} // namespace uirepo
