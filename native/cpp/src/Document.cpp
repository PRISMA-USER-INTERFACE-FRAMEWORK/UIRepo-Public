#include "uirepo/Core.h"

#include "uirepo/Animation.h"
#include "Layout.h"
#include "StyleValidation.h"

namespace uirepo {
namespace {
[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}
}

struct Document::AnimationTrack {
    AnimationId id;
    Node* node;
    AnimatedProperty property;
    std::variant<anim::Tween<float>, anim::Tween<Color>> tween;
};

const ApproximateTextMeasurer& Document::fallbackTextMeasurer() noexcept {
    static const ApproximateTextMeasurer instance;
    return instance;
}

Document::~Document() = default;

AnimationId Document::allocateAnimationId() noexcept {
    const AnimationId id = nextAnimationId_;
    ++nextAnimationId_;
    if (nextAnimationId_ == 0) {
        nextAnimationId_ = 1;
    }
    return id;
}

AnimationId Document::animateOpacity(Node& node, float target, anim::Playback playback) {
    if (node.document_ != this) {
        throw std::invalid_argument("Animation node does not belong to this document");
    }
    style_validation::validateOpacity(node.style_.opacity);
    style_validation::validateOpacity(target);
    cancelAnimationFor(node, AnimatedProperty::Opacity);

    const AnimationId id = allocateAnimationId();
    animations_.push_back(std::make_unique<AnimationTrack>(AnimationTrack{
        id,
        &node,
        AnimatedProperty::Opacity,
        anim::Tween<float>{node.style_.opacity, target, std::move(playback)},
    }));
    return id;
}

AnimationId Document::animateBackground(Node& node, Color target, anim::Playback playback) {
    if (node.document_ != this) {
        throw std::invalid_argument("Animation node does not belong to this document");
    }
    cancelAnimationFor(node, AnimatedProperty::Background);

    const AnimationId id = allocateAnimationId();
    animations_.push_back(std::make_unique<AnimationTrack>(AnimationTrack{
        id,
        &node,
        AnimatedProperty::Background,
        anim::Tween<Color>{node.style_.background, target, std::move(playback)},
    }));
    return id;
}

AnimationId Document::animateCornerRadius(Node& node, float target, anim::Playback playback) {
    if (node.document_ != this) {
        throw std::invalid_argument("Animation node does not belong to this document");
    }
    style_validation::validateCornerRadius(node.style_.cornerRadius);
    style_validation::validateCornerRadius(target);
    cancelAnimationFor(node, AnimatedProperty::CornerRadius);

    const AnimationId id = allocateAnimationId();
    animations_.push_back(std::make_unique<AnimationTrack>(AnimationTrack{
        id,
        &node,
        AnimatedProperty::CornerRadius,
        anim::Tween<float>{node.style_.cornerRadius, target, std::move(playback)},
    }));
    return id;
}

bool Document::cancelAnimation(AnimationId id) noexcept {
    const auto it = std::find_if(animations_.begin(), animations_.end(), [id](const auto& track) {
        return track->id == id;
    });
    if (it == animations_.end()) {
        return false;
    }
    animations_.erase(it);
    return true;
}

void Document::advanceAnimations(float dt) noexcept {
    for (auto it = animations_.begin(); it != animations_.end();) {
        AnimationTrack& track = **it;
        if (track.node == nullptr || track.node->document_ != this || !track.node->visible_) {
            it = animations_.erase(it);
            continue;
        }

        bool finished = false;
        switch (track.property) {
        case AnimatedProperty::Opacity: {
            auto& tween = std::get<anim::Tween<float>>(track.tween);
            tween.advance(dt);
            track.node->style_.opacity = tween.value();
            finished = tween.finished();
            break;
        }
        case AnimatedProperty::Background: {
            auto& tween = std::get<anim::Tween<Color>>(track.tween);
            tween.advance(dt);
            track.node->style_.background = tween.value();
            finished = tween.finished();
            break;
        }
        case AnimatedProperty::CornerRadius: {
            auto& tween = std::get<anim::Tween<float>>(track.tween);
            tween.advance(dt);
            track.node->style_.cornerRadius = tween.value();
            finished = tween.finished();
            break;
        }
        }

        if (finished) {
            it = animations_.erase(it);
        } else {
            ++it;
        }
    }
}

void Document::cancelAnimationsFor(Node& node) noexcept {
    std::erase_if(animations_, [&node](const auto& track) {
        return track->node == &node;
    });
}

void Document::cancelAnimationFor(Node& node, AnimatedProperty property) noexcept {
    std::erase_if(animations_, [&node, property](const auto& track) {
        return track->node == &node && track->property == property;
    });
}

void Document::cancelAnimationsInSubtree(const Node& subtree) noexcept {
    std::erase_if(animations_, [this, &subtree](const auto& track) {
        return track->node == nullptr || subtreeContains(subtree, track->node);
    });
}

Document::Document(std::unique_ptr<Node> root, const TextMeasurer* textMeasurer)
    : root_(std::move(root)),
      textMeasurer_(textMeasurer != nullptr ? textMeasurer : &fallbackTextMeasurer()) {
    if (!root_) {
        throw std::invalid_argument("Document root cannot be null");
    }
    if (root_->parent_ != nullptr) {
        throw std::invalid_argument("Document root must be unparented");
    }

    root_->isDocumentRoot_ = true;
    root_->attachToDocument(this);
    displayList_.reserve(128);
    ensureIndex();
}

void Document::setTextMeasurer(const TextMeasurer* textMeasurer) noexcept {
    const TextMeasurer* next = textMeasurer != nullptr ? textMeasurer : &fallbackTextMeasurer();
    if (textMeasurer_ == next) {
        return;
    }
    textMeasurer_ = next;
    markLayoutDirty();
}

void Document::layout(Size viewport) {
    if (!finiteNonNegative(viewport.width) || !finiteNonNegative(viewport.height)) {
        throw std::invalid_argument("Viewport dimensions must be finite and non-negative");
    }

    const bool viewportChanged =
        viewport.width != lastViewport_.width || viewport.height != lastViewport_.height;
    if (!layoutDirty_ && !viewportChanged) {
        refreshFocus();
        return;
    }

    layout_engine::perform(*root_, viewport, *textMeasurer_);
    lastViewport_ = viewport;
    layoutDirty_ = false;
    refreshFocus();
}

std::span<const DrawCommand> Document::buildDisplayList() {
    style_validation::validateTree(*root_);
    displayList_.clear();
    appendNode(*root_);
    return displayList_;
}

Node* Document::find(std::string_view id) {
    ensureIndex();
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : it->second;
}

const Node* Document::find(std::string_view id) const {
    ensureIndex();
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : it->second;
}

Node* Document::hitTest(Point point) noexcept {
    return hitTestNode(*root_, point);
}

bool Document::focus(Node& node) {
    if (!node.focusable_ || !isReachableAndInteractive(node)) {
        return false;
    }

    if (focused_ == &node) {
        ensureNodeVisible(node);
        return true;
    }

    clearFocus();
    focused_ = &node;
    node.focused_ = true;
    ensureNodeVisible(node);
    return true;
}

void Document::clearFocus() noexcept {
    if (focused_ != nullptr) {
        focused_->focused_ = false;
        focused_ = nullptr;
    }
}

void Document::markTreeDirty() noexcept {
    indexDirty_ = true;
    layoutDirty_ = true;
}

void Document::markLayoutDirty() noexcept {
    layoutDirty_ = true;
}

void Document::onSubtreeDetached(Node& subtree) noexcept {
    cancelAnimationsInSubtree(subtree);
    if (subtreeContains(subtree, focused_)) {
        clearFocus();
    }
}

void Document::ensureIndex() const {
    if (!indexDirty_) {
        return;
    }

    index_.clear();
    indexNode(*root_);
    indexDirty_ = false;
}

void Document::indexNode(Node& node) const {
    const auto [_, inserted] = index_.emplace(node.id_, &node);
    if (!inserted) {
        throw std::invalid_argument("Duplicate node ID: " + node.id_);
    }

    for (auto& child : node.children_) {
        indexNode(*child);
    }
}

bool Document::isReachableAndInteractive(const Node& node) const noexcept {
    const Node* current = &node;
    while (current != nullptr) {
        if (!current->visible_ || !current->enabled_) {
            return false;
        }
        if (current == root_.get()) {
            return true;
        }
        current = current->parent_;
    }
    return false;
}

bool Document::subtreeContains(const Node& subtree, const Node* needle) const noexcept {
    if (needle == nullptr) {
        return false;
    }
    if (&subtree == needle) {
        return true;
    }
    for (const auto& child : subtree.children_) {
        if (subtreeContains(*child, needle)) {
            return true;
        }
    }
    return false;
}

void Document::refreshFocus() noexcept {
    if (focused_ != nullptr && (!focused_->focusable_ || !isReachableAndInteractive(*focused_))) {
        clearFocus();
    }
}

void Document::ensureNodeVisible(Node& node) {
    if (!hasLayout()) {
        return;
    }

    for (Node* ancestor = node.parent_; ancestor != nullptr; ancestor = ancestor->parent_) {
        auto* stack = dynamic_cast<StackNode*>(ancestor);
        if (stack == nullptr || !stack->scrollable_) {
            continue;
        }

        const Rect viewport = stack->scrollViewport();
        const Rect target = node.bounds_;
        float desired = stack->scrollOffset_;

        if (stack->axis_ == Axis::Column) {
            if (target.y < viewport.y) {
                desired += target.y - viewport.y;
            } else if (target.bottom() > viewport.bottom()) {
                desired += target.bottom() - viewport.bottom();
            }
        } else {
            if (target.x < viewport.x) {
                desired += target.x - viewport.x;
            } else if (target.right() > viewport.right()) {
                desired += target.right() - viewport.right();
            }
        }

        if (stack->setScrollOffset(desired)) {
            layout(lastViewport_);
        }
    }
}

void Document::appendNode(const Node& node) {
    if (!node.visible_) {
        return;
    }

    const auto* stack = dynamic_cast<const StackNode*>(&node);
    const bool scrollClip = stack != nullptr && stack->scrollable_;
    const bool clipped = node.style_.clipToBounds || scrollClip;
    const Rect clipBounds = scrollClip ? stack->scrollViewport() : node.bounds_;

    if (clipped) {
        displayList_.emplace_back(PushClipCommand{
            node.id_, clipBounds, node.style_.opacity, node.style_.zIndex});
    }

    node.appendDrawCommands(displayList_);
    for (const auto& child : node.children_) {
        appendNode(*child);
    }

    if (clipped) {
        displayList_.emplace_back(PopClipCommand{
            node.id_, clipBounds, node.style_.opacity, node.style_.zIndex});
    }
}

Node* Document::hitTestNode(Node& node, Point point) noexcept {
    if (!node.visible_ || !node.enabled_) {
        return nullptr;
    }

    const bool insideNode = node.bounds_.contains(point);
    bool testChildren = true;
    if (auto* stack = dynamic_cast<StackNode*>(&node); stack != nullptr && stack->scrollable_) {
        testChildren = stack->scrollViewport().contains(point);
    } else if (node.style_.clipToBounds) {
        testChildren = insideNode;
    }

    if (testChildren) {
        for (auto it = node.children_.rbegin(); it != node.children_.rend(); ++it) {
            if (Node* hit = hitTestNode(**it, point); hit != nullptr) {
                return hit;
            }
        }
    }

    return insideNode ? &node : nullptr;
}

}
