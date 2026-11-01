#include "DocumentRegistry.h"

#ifdef _WIN32
#include "uirepo/D3D11Renderer.h"
#include "uirepo/TelemetryPolicy.h"
#include <spdlog/spdlog.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <utility>
#include <vector>

namespace uirepo::fo4 {
namespace {

constexpr float kDragThresholdPixels = 4.0f;

[[nodiscard]] constexpr std::uint32_t pointerButtonValue(input::PointerButton button) noexcept {
    return static_cast<std::uint32_t>(button);
}

} // namespace

DocumentRegistry& DocumentRegistry::instance() noexcept {
    static DocumentRegistry registry;
    return registry;
}

DocumentRegistry::DocumentHandle DocumentRegistry::nextDocumentHandle() noexcept {
    if (nextDocument_ == 0) {
        return 0;
    }
    return nextDocument_++;
}

DocumentRegistry::NodeHandle DocumentRegistry::nextNodeHandle() noexcept {
    if (nextNode_ == 0) {
        return 0;
    }
    return nextNode_++;
}

DocumentRegistry::DocumentEntry* DocumentRegistry::findDocument(DocumentHandle document) noexcept {
    const auto it = documents_.find(document);
    return it == documents_.end() ? nullptr : &it->second;
}

const DocumentRegistry::DocumentEntry* DocumentRegistry::findDocument(DocumentHandle document) const noexcept {
    const auto it = documents_.find(document);
    return it == documents_.end() ? nullptr : &it->second;
}

Node* DocumentRegistry::findNode(DocumentEntry& document, NodeHandle node) noexcept {
    const auto it = document.nodes.find(node);
    return it == document.nodes.end() ? nullptr : it->second;
}

const Node* DocumentRegistry::findNode(const DocumentEntry& document, NodeHandle node) const noexcept {
    const auto it = document.nodes.find(node);
    return it == document.nodes.end() ? nullptr : it->second;
}

DocumentRegistry::NodeHandle DocumentRegistry::findNodeHandle(
    const DocumentEntry& document,
    const Node* node) const noexcept {
    if (node == nullptr) {
        return 0;
    }
    for (const auto& [handle, candidate] : document.nodes) {
        if (candidate == node) {
            return handle;
        }
    }
    return 0;
}

DocumentRegistry::NodeHandle DocumentRegistry::registerNode(
    DocumentEntry& document,
    Node& node) noexcept {
    const NodeHandle handle = nextNodeHandle();
    if (handle == 0) {
        return 0;
    }
    try {
        document.nodes.emplace(handle, &node);
        return handle;
    } catch (...) {
        return 0;
    }
}

bool DocumentRegistry::idAvailable(DocumentEntry& document, const std::string& id) noexcept {
    if (id.empty() || !document.document) {
        return false;
    }
    try {
        return document.document->find(id) == nullptr;
    } catch (...) {
        return false;
    }
}

bool DocumentRegistry::inputEligible(const DocumentEntry& entry) const noexcept {
    return entry.document != nullptr && entry.visible && entry.inputEnabled;
}

bool DocumentRegistry::nodeInteractive(const Node* node) const noexcept {
    if (node == nullptr) {
        return false;
    }
    for (const Node* current = node; current != nullptr; current = current->parent()) {
        if (!current->visible() || !current->enabled()) {
            return false;
        }
    }
    return true;
}

bool DocumentRegistry::subtreeContains(const Node* root, const Node* node) const noexcept {
    if (root == nullptr || node == nullptr) {
        return false;
    }
    for (const Node* current = node; current != nullptr; current = current->parent()) {
        if (current == root) {
            return true;
        }
    }
    return false;
}

DocumentRegistry::NodeHandle DocumentRegistry::focusableHit(
    DocumentEntry& entry,
    Point point) const noexcept {
    if (!inputEligible(entry)) {
        return 0;
    }
    Node* target = entry.document->hitTest(point);
    while (target != nullptr && !target->focusable()) {
        target = target->parent();
    }
    if (!nodeInteractive(target)) {
        return 0;
    }
    return findNodeHandle(entry, target);
}

DocumentRegistry::NodeHandle DocumentRegistry::pointerHandlerHit(
    DocumentEntry& entry,
    Point point) const noexcept {
    if (!inputEligible(entry)) {
        return 0;
    }
    Node* target = entry.document->hitTest(point);
    while (target != nullptr) {
        const NodeHandle handle = findNodeHandle(entry, target);
        const auto handler = entry.pointerHandlers.find(handle);
        if (handler != entry.pointerHandlers.end() && handler->second.callback != nullptr && nodeInteractive(target)) {
            return handle;
        }
        target = target->parent();
    }
    return 0;
}

DocumentRegistry::InputTarget DocumentRegistry::pointerTarget(
    Point point,
    bool requirePointerHandler) noexcept {
    InputTarget best{};
    const DocumentEntry* bestEntry{};
    for (auto& [handle, entry] : documents_) {
        const NodeHandle node = requirePointerHandler
            ? pointerHandlerHit(entry, point)
            : focusableHit(entry, point);
        if (node == 0) {
            continue;
        }
        if (bestEntry == nullptr || entry.order > bestEntry->order ||
            (entry.order == bestEntry->order && entry.sequence > bestEntry->sequence)) {
            best = InputTarget{handle, node};
            bestEntry = &entry;
        }
    }
    return best;
}

DocumentRegistry::DocumentEntry* DocumentRegistry::topmostHitDocument(Point point) noexcept {
    DocumentEntry* best{};
    for (auto& [handle, entry] : documents_) {
        (void)handle;
        if (!inputEligible(entry) || entry.document->hitTest(point) == nullptr) {
            continue;
        }
        if (best == nullptr || entry.order > best->order ||
            (entry.order == best->order && entry.sequence > best->sequence)) {
            best = &entry;
        }
    }
    return best;
}

DocumentRegistry::CallbackSnapshot DocumentRegistry::snapshotHandler(
    DocumentHandle document,
    const DocumentEntry& entry,
    NodeHandle node,
    std::uint32_t eventType) const noexcept {
    const auto handler = entry.handlers.find(node);
    if (handler == entry.handlers.end() || handler->second.callback == nullptr) {
        return {};
    }
    return {
        .callback = handler->second.callback,
        .userData = handler->second.userData,
        .document = document,
        .node = node,
        .eventType = eventType,
    };
}

DocumentRegistry::PointerCallbackSnapshot DocumentRegistry::snapshotPointerHandler(
    DocumentHandle document,
    const DocumentEntry& entry,
    NodeHandle node,
    std::uint32_t eventType,
    const input::PointerEvent& source) const noexcept {
    const auto handler = entry.pointerHandlers.find(node);
    if (handler == entry.pointerHandlers.end() || handler->second.callback == nullptr) {
        return {};
    }
    return {
        .callback = handler->second.callback,
        .userData = handler->second.userData,
        .document = document,
        .node = node,
        .event = UIRepoNodePointerEventV1{
            .size = sizeof(UIRepoNodePointerEventV1),
            .type = eventType,
            .button = pointerButtonValue(source.button),
            .modifiers = source.modifiers,
            .x = source.x,
            .y = source.y,
            .wheelDelta = source.wheelDelta,
        },
    };
}

void DocumentRegistry::invoke(CallbackSnapshot snapshot) noexcept {
    if (!snapshot) {
        return;
    }
    try {
        snapshot.callback(snapshot.document, snapshot.node, snapshot.eventType, snapshot.userData);
#ifdef _WIN32
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 SDK: node event callback threw: {}", exception.what());
#else
    } catch (const std::exception&) {
#endif
    } catch (...) {
#ifdef _WIN32
        spdlog::error("UIRepo_F4 SDK: node event callback threw an unknown exception");
#endif
    }
}

void DocumentRegistry::invoke(PointerCallbackSnapshot snapshot) noexcept {
    if (!snapshot) {
        return;
    }
    try {
        snapshot.callback(snapshot.document, snapshot.node, &snapshot.event, snapshot.userData);
#ifdef _WIN32
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 SDK: pointer callback threw: {}", exception.what());
#else
    } catch (const std::exception&) {
#endif
    } catch (...) {
#ifdef _WIN32
        spdlog::error("UIRepo_F4 SDK: pointer callback threw an unknown exception");
#endif
    }
}

void DocumentRegistry::invoke(std::vector<PointerCallbackSnapshot> snapshots) noexcept {
    for (auto& snapshot : snapshots) {
        invoke(snapshot);
    }
}

std::vector<DocumentRegistry::PointerCallbackSnapshot> DocumentRegistry::clearHoverLocked(
    DocumentHandle document,
    Node* subtree) noexcept {
    std::vector<PointerCallbackSnapshot> callbacks;
    if (hoveredDocument_ == 0 || (document != 0 && hoveredDocument_ != document)) {
        return callbacks;
    }

    auto* entry = findDocument(hoveredDocument_);
    Node* hovered = entry ? findNode(*entry, hoveredNode_) : nullptr;
    if (subtree != nullptr && !subtreeContains(subtree, hovered)) {
        return callbacks;
    }

    if (entry != nullptr && hovered != nullptr) {
        const input::PointerEvent source{
            .type = input::PointerEventType::Move,
            .x = hovered->bounds().x,
            .y = hovered->bounds().y,
        };
        if (auto leave = snapshotPointerHandler(
                hoveredDocument_, *entry, hoveredNode_, UIREPO_NODE_POINTER_LEAVE, source)) {
            callbacks.push_back(leave);
        }
    }
    hoveredDocument_ = 0;
    hoveredNode_ = 0;
    return callbacks;
}

std::vector<DocumentRegistry::PointerCallbackSnapshot> DocumentRegistry::clearGestureLocked(
    DocumentHandle document,
    Node* subtree) noexcept {
    std::vector<PointerCallbackSnapshot> callbacks;
    if (captureDocument_ == 0 || (document != 0 && captureDocument_ != document)) {
        return callbacks;
    }

    auto* entry = findDocument(captureDocument_);
    Node* captured = entry ? findNode(*entry, captureNode_) : nullptr;
    if (subtree != nullptr && !subtreeContains(subtree, captured)) {
        return callbacks;
    }

    if (entry != nullptr && captured != nullptr) {
        const input::PointerEvent source{
            .type = input::PointerEventType::Move,
            .button = pressedButton_,
            .x = pressPoint_.x,
            .y = pressPoint_.y,
        };
        if (auto cancel = snapshotPointerHandler(
                captureDocument_, *entry, captureNode_, UIREPO_NODE_POINTER_CANCEL, source)) {
            callbacks.push_back(cancel);
        }
    }

    pressedDocument_ = 0;
    pressedNode_ = 0;
    pressedButton_ = input::PointerButton::None;
    pressPoint_ = {};
    dragging_ = false;
    captureDocument_ = 0;
    captureNode_ = 0;
    return callbacks;
}

DocumentRegistry::CallbackSnapshot DocumentRegistry::clearFocusLocked(
    DocumentHandle document) noexcept {
    if (document == 0 || focusedDocument_ != document) {
        return {};
    }

    CallbackSnapshot lost{};
    if (auto* entry = findDocument(document)) {
        lost = snapshotHandler(document, *entry, focusedNode_, UIREPO_EVENT_FOCUS_LOST);
        if (entry->document) {
            entry->document->clearFocus();
        }
    }
    focusedDocument_ = 0;
    focusedNode_ = 0;
    return lost;
}

DocumentRegistry::DocumentHandle DocumentRegistry::createDocument(std::string rootId) {
    if (rootId.empty()) {
        return 0;
    }

    std::scoped_lock lock(mutex_);
    auto root = std::make_unique<PanelNode>(std::move(rootId));
    auto document = std::make_unique<Document>(std::move(root), textMeasurer_);

    DocumentEntry entry{};
    entry.document = std::move(document);
    entry.sequence = nextSequence_++;
    entry.rootHandle = registerNode(entry, entry.document->root());
    if (entry.rootHandle == 0) {
        return 0;
    }

    const DocumentHandle handle = nextDocumentHandle();
    if (handle == 0) {
        return 0;
    }
    documents_.emplace(handle, std::move(entry));
    return handle;
}

bool DocumentRegistry::destroyDocument(DocumentHandle document) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        if (!entry) {
            return false;
        }
        if (focusedDocument_ == document) {
            lost = clearFocusLocked(document);
        }
        auto hover = clearHoverLocked(document);
        auto gesture = clearGestureLocked(document);
        pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
        pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
        documents_.erase(document);
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::setDocumentVisible(DocumentHandle document, bool visible) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        if (!entry) {
            return false;
        }
        entry->visible = visible;
        if (!visible) {
            lost = clearFocusLocked(document);
            auto hover = clearHoverLocked(document);
            auto gesture = clearGestureLocked(document);
            pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
            pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
        }
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::setDocumentOrder(DocumentHandle document, std::int32_t order) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry) {
        return false;
    }
    entry->order = order;
    return true;
}

bool DocumentRegistry::setDocumentInputEnabled(DocumentHandle document, bool enabled) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        if (!entry) {
            return false;
        }
        entry->inputEnabled = enabled;
        if (!enabled) {
            lost = clearFocusLocked(document);
            auto hover = clearHoverLocked(document);
            auto gesture = clearGestureLocked(document);
            pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
            pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
        }
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

DocumentRegistry::NodeHandle DocumentRegistry::rootNode(DocumentHandle document) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    return entry ? entry->rootHandle : 0;
}

DocumentRegistry::NodeHandle DocumentRegistry::createPanel(
    DocumentHandle document,
    NodeHandle parent,
    std::string id) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }
    auto& node = parentNode->emplaceChild<PanelNode>(std::move(id));
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createStack(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    Axis axis) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }
    auto& node = parentNode->emplaceChild<StackNode>(std::move(id), axis);
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createText(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    std::string text) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }
    auto& node = parentNode->emplaceChild<TextNode>(std::move(id), std::move(text));
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createButton(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    std::string text) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }
    auto& node = parentNode->emplaceChild<ButtonNode>(std::move(id), std::move(text));
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createImage(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    std::string source,
    Size naturalSize) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }
    auto& node = parentNode->emplaceChild<ImageNode>(std::move(id), std::move(source), naturalSize);
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createVideo(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    std::string source,
    Size naturalSize) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }
    auto& node = parentNode->emplaceChild<VideoNode>(std::move(id), std::move(source), naturalSize);
    return registerNode(*entry, node);
}

bool DocumentRegistry::destroyNode(DocumentHandle document, NodeHandle node) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    std::unique_ptr<Node> detached;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        Node* target = entry ? findNode(*entry, node) : nullptr;
        if (!entry || !target || node == entry->rootHandle || target->parent() == nullptr) {
            return false;
        }

        Node* focused = focusedDocument_ == document ? findNode(*entry, focusedNode_) : nullptr;
        if (subtreeContains(target, focused)) {
            lost = clearFocusLocked(document);
        }
        auto hover = clearHoverLocked(document, target);
        auto gesture = clearGestureLocked(document, target);
        pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
        pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());

        std::vector<NodeHandle> removed;
        for (const auto& [handle, candidate] : entry->nodes) {
            if (subtreeContains(target, candidate)) {
                removed.push_back(handle);
            }
        }

        try {
            detached = target->parent()->removeChild(*target);
        } catch (...) {
            return false;
        }
        if (!detached) {
            return false;
        }

        for (NodeHandle handle : removed) {
            entry->handlers.erase(handle);
            entry->pointerHandlers.erase(handle);
            entry->nodes.erase(handle);
        }
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::setNodeVisible(
    DocumentHandle document,
    NodeHandle node,
    bool visible) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        auto* target = entry ? findNode(*entry, node) : nullptr;
        if (!target) {
            return false;
        }
        try {
            target->setVisible(visible);
        } catch (...) {
            return false;
        }
        if (!visible) {
            Node* focused = focusedDocument_ == document ? findNode(*entry, focusedNode_) : nullptr;
            if (subtreeContains(target, focused)) {
                lost = clearFocusLocked(document);
            }
            auto hover = clearHoverLocked(document, target);
            auto gesture = clearGestureLocked(document, target);
            pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
            pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
        }
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::setNodeEnabled(
    DocumentHandle document,
    NodeHandle node,
    bool enabled) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        auto* target = entry ? findNode(*entry, node) : nullptr;
        if (!target) {
            return false;
        }
        try {
            target->setEnabled(enabled);
        } catch (...) {
            return false;
        }
        if (!enabled) {
            Node* focused = focusedDocument_ == document ? findNode(*entry, focusedNode_) : nullptr;
            if (subtreeContains(target, focused)) {
                lost = clearFocusLocked(document);
            }
            auto hover = clearHoverLocked(document, target);
            auto gesture = clearGestureLocked(document, target);
            pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
            pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
        }
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::setNodeStyle(
    DocumentHandle document,
    NodeHandle node,
    const Style& style) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    if (!target) {
        return false;
    }
    try {
        target->style() = style;
        return true;
    } catch (...) {
        return false;
    }
}

bool DocumentRegistry::setStackScrollable(
    DocumentHandle document,
    NodeHandle node,
    bool scrollable) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    auto* stack = target ? dynamic_cast<StackNode*>(target) : nullptr;
    if (!stack) {
        return false;
    }
    try {
        stack->setScrollable(scrollable);
        return true;
    } catch (...) {
        return false;
    }
}

bool DocumentRegistry::setNodeEventHandler(
    DocumentHandle document,
    NodeHandle node,
    UIRepoNodeEventCallback callback,
    void* userData) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !findNode(*entry, node)) {
        return false;
    }
    try {
        if (callback == nullptr) {
            entry->handlers.erase(node);
        } else {
            entry->handlers[node] = EventHandler{callback, userData};
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool DocumentRegistry::setNodePointerEventHandler(
    DocumentHandle document,
    NodeHandle node,
    UIRepoNodePointerEventCallback callback,
    void* userData) noexcept {
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        Node* target = entry ? findNode(*entry, node) : nullptr;
        if (!entry || !target) {
            return false;
        }
        try {
            if (callback == nullptr) {
                auto hover = clearHoverLocked(document, target);
                auto gesture = clearGestureLocked(document, target);
                pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
                pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
                entry->pointerHandlers.erase(node);
            } else {
                entry->pointerHandlers[node] = PointerHandler{callback, userData};
            }
        } catch (...) {
            return false;
        }
    }
    invoke(std::move(pointerCallbacks));
    return true;
}

DocumentRegistry::FocusTransitionResult DocumentRegistry::focusNodeInternal(
    DocumentHandle document,
    NodeHandle node) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    bool transitionStarted = false;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        Node* target = entry ? findNode(*entry, node) : nullptr;
        if (!entry || !inputEligible(*entry) || !target || !target->focusable() || !nodeInteractive(target)) {
            return {};
        }
        if (focusedDocument_ == document && focusedNode_ == node) {
            return {.focused = true, .handled = true};
        }
        if (focusedDocument_ != 0) {
            transitionStarted = true;
            const DocumentHandle previous = focusedDocument_;
            lost = clearFocusLocked(previous);
            auto gesture = clearGestureLocked(previous);
            pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
        }
    }

    invoke(lost);
    invoke(std::move(pointerCallbacks));

    CallbackSnapshot gained{};
    {
        std::unique_lock lock(mutex_);
        if (focusedDocument_ != 0) {
            return {.focused = false, .handled = true};
        }
        auto* entry = findDocument(document);
        Node* target = entry ? findNode(*entry, node) : nullptr;
        if (!entry || !inputEligible(*entry) || !target || !target->focusable() || !nodeInteractive(target)) {
            return {.focused = false, .handled = transitionStarted};
        }
        try {
            if (!entry->document->focus(*target)) {
                return {.focused = false, .handled = transitionStarted};
            }
        } catch (...) {
            return {.focused = false, .handled = transitionStarted};
        }
        focusedDocument_ = document;
        focusedNode_ = node;
        gained = snapshotHandler(document, *entry, node, UIREPO_EVENT_FOCUS_GAINED);
    }
    invoke(gained);

    std::scoped_lock lock(mutex_);
    const bool authoritative = focusedDocument_ == document && focusedNode_ == node;
    return {.focused = authoritative, .handled = true};
}

bool DocumentRegistry::focusNode(DocumentHandle document, NodeHandle node) noexcept {
    return focusNodeInternal(document, node).focused;
}

bool DocumentRegistry::clearDocumentFocus(DocumentHandle document) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        if (!findDocument(document)) {
            return false;
        }
        lost = clearFocusLocked(document);
        pointerCallbacks = clearGestureLocked(document);
    }
    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::activateNode(DocumentHandle document, NodeHandle node) noexcept {
    CallbackSnapshot valueChanged{};
    CallbackSnapshot activate{};
    bool handledByControl = false;
    {
        std::scoped_lock lock(mutex_);
        auto* entry = findDocument(document);
        Node* target = entry ? findNode(*entry, node) : nullptr;
        if (!entry || !inputEligible(*entry) || !target || !target->focusable() || !nodeInteractive(target)) {
            return false;
        }

        if (auto* toggle = dynamic_cast<ToggleNode*>(target)) {
            handledByControl = toggle->toggle();
            if (handledByControl) {
                valueChanged = snapshotHandler(document, *entry, node, UIREPO_EVENT_VALUE_CHANGED);
            }
        }
        activate = snapshotHandler(document, *entry, node, UIREPO_EVENT_ACTIVATE);
    }

    invoke(valueChanged);
    invoke(activate);
    return handledByControl || static_cast<bool>(activate);
}

bool DocumentRegistry::handlePointerLeave() noexcept {
    try {
        std::vector<PointerCallbackSnapshot> callbacks;
        {
            std::scoped_lock lock(mutex_);
            callbacks = clearHoverLocked(0);
        }
        invoke(std::move(callbacks));
        return true;
    } catch (...) {
        return false;
    }
}

bool DocumentRegistry::routePointerEvent(const input::PointerEvent& event) noexcept {
    try {
        const Point point{event.x, event.y};

        if (event.type == input::PointerEventType::Wheel) {
            std::scoped_lock lock(mutex_);
            auto* entry = topmostHitDocument(point);
            if (!entry || !entry->document) {
                return false;
            }
            return input::routeWheel(*entry->document, point, event.wheelDelta);
        }

        if (event.type == input::PointerEventType::Move) {
            PointerCallbackSnapshot callback{};
            CallbackSnapshot valueChanged{};
            bool captured = false;
            {
                std::unique_lock lock(mutex_);
                auto* entry = findDocument(captureDocument_);
                Node* capturedNode = entry ? findNode(*entry, captureNode_) : nullptr;
                if (entry && inputEligible(*entry) && capturedNode && nodeInteractive(capturedNode)) {
                    captured = true;
                    const float dx = event.x - pressPoint_.x;
                    const float dy = event.y - pressPoint_.y;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (!dragging_ && distanceSquared >= kDragThresholdPixels * kDragThresholdPixels) {
                        dragging_ = true;
                        callback = snapshotPointerHandler(
                            captureDocument_, *entry, captureNode_, UIREPO_NODE_POINTER_DRAG_BEGIN, event);
                    } else {
                        callback = snapshotPointerHandler(
                            captureDocument_,
                            *entry,
                            captureNode_,
                            dragging_ ? UIREPO_NODE_POINTER_DRAG_MOVE : UIREPO_NODE_POINTER_MOVE,
                            event);
                    }
                    if (auto* directional = dynamic_cast<DirectionalValueControl*>(capturedNode);
                        directional != nullptr && directional->setValueFromPointer(point)) {
                        valueChanged = snapshotHandler(
                            captureDocument_, *entry, captureNode_, UIREPO_EVENT_VALUE_CHANGED);
                    }
                } else if (captureDocument_ != 0) {
                    auto cancelled = clearGestureLocked();
                    lock.unlock();
                    invoke(std::move(cancelled));
                    return true;
                }
            }
            if (captured) {
                invoke(callback);
                invoke(valueChanged);
                return true;
            }

            InputTarget target{};
            PointerCallbackSnapshot leave{};
            PointerCallbackSnapshot move{};
            bool changed = false;
            {
                std::unique_lock lock(mutex_);
                target = pointerTarget(point, true);
                changed = target.document != hoveredDocument_ || target.node != hoveredNode_;
                if (changed) {
                    if (hoveredDocument_ != 0) {
                        auto* oldEntry = findDocument(hoveredDocument_);
                        if (oldEntry) {
                            leave = snapshotPointerHandler(
                                hoveredDocument_, *oldEntry, hoveredNode_, UIREPO_NODE_POINTER_LEAVE, event);
                        }
                    }
                    hoveredDocument_ = target.document;
                    hoveredNode_ = target.node;
                } else if (target.document != 0) {
                    auto* entry = findDocument(target.document);
                    if (entry) {
                        move = snapshotPointerHandler(
                            target.document, *entry, target.node, UIREPO_NODE_POINTER_MOVE, event);
                    }
                }
            }

            invoke(leave);
            if (!changed) {
                invoke(move);
                return false;
            }

            PointerCallbackSnapshot enter{};
            if (target.document != 0) {
                std::scoped_lock lock(mutex_);
                auto* entry = findDocument(target.document);
                Node* node = entry ? findNode(*entry, target.node) : nullptr;
                if (entry && inputEligible(*entry) && nodeInteractive(node) &&
                    hoveredDocument_ == target.document && hoveredNode_ == target.node) {
                    enter = snapshotPointerHandler(
                        target.document, *entry, target.node, UIREPO_NODE_POINTER_ENTER, event);
                }
            }
            invoke(enter);
            return false;
        }

        if (event.type == input::PointerEventType::ButtonDown) {
            if (event.button == input::PointerButton::None) {
                return false;
            }

            InputTarget pointerHandled{};
            {
                std::scoped_lock lock(mutex_);
                if (captureDocument_ != 0) {
                    return false;
                }
                pointerHandled = pointerTarget(point, true);
            }

            if (pointerHandled.document != 0) {
                bool focusable = false;
                {
                    std::scoped_lock lock(mutex_);
                    auto* entry = findDocument(pointerHandled.document);
                    Node* node = entry ? findNode(*entry, pointerHandled.node) : nullptr;
                    focusable = node && node->focusable();
                }
                if (focusable) {
                    (void)focusNodeInternal(pointerHandled.document, pointerHandled.node);
                }

                PointerCallbackSnapshot down{};
                {
                    std::scoped_lock lock(mutex_);
                    auto* entry = findDocument(pointerHandled.document);
                    Node* node = entry ? findNode(*entry, pointerHandled.node) : nullptr;
                    if (!entry || !inputEligible(*entry) || !nodeInteractive(node) ||
                        entry->pointerHandlers.find(pointerHandled.node) == entry->pointerHandlers.end()) {
                        return false;
                    }
                    pressedDocument_ = pointerHandled.document;
                    pressedNode_ = pointerHandled.node;
                    pressedButton_ = event.button;
                    pressPoint_ = point;
                    dragging_ = false;
                    captureDocument_ = pointerHandled.document;
                    captureNode_ = pointerHandled.node;
                    down = snapshotPointerHandler(
                        pointerHandled.document,
                        *entry,
                        pointerHandled.node,
                        UIREPO_NODE_POINTER_BUTTON_DOWN,
                        event);
                }
                invoke(down);
                return true;
            }

            if (event.button != input::PointerButton::Left) {
                return false;
            }

            InputTarget target{};
            {
                std::scoped_lock lock(mutex_);
                target = pointerTarget(point, false);
            }
            if (target.document == 0 || target.node == 0) {
                return false;
            }

            const FocusTransitionResult focusResult = focusNodeInternal(target.document, target.node);
            if (!focusResult.handled) {
                return false;
            }

            if (focusResult.focused) {
                CallbackSnapshot valueChanged{};
                {
                    std::scoped_lock lock(mutex_);
                    auto* entry = findDocument(target.document);
                    Node* node = entry ? findNode(*entry, target.node) : nullptr;
                    if (entry && inputEligible(*entry) && focusedDocument_ == target.document &&
                        focusedNode_ == target.node && nodeInteractive(node)) {
                        pressedDocument_ = target.document;
                        pressedNode_ = target.node;
                        pressedButton_ = event.button;
                        pressPoint_ = point;
                        dragging_ = false;
                        captureDocument_ = target.document;
                        captureNode_ = target.node;
                        if (auto* directional = dynamic_cast<DirectionalValueControl*>(node);
                            directional != nullptr && directional->setValueFromPointer(point)) {
                            valueChanged = snapshotHandler(
                                target.document, *entry, target.node, UIREPO_EVENT_VALUE_CHANGED);
                        }
                    }
                }
                invoke(valueChanged);
            }
            return true;
        }

        if (event.type == input::PointerEventType::ButtonUp) {
            DocumentHandle document{};
            NodeHandle pressed{};
            bool owned = false;
            bool activate = false;
            bool wasDragging = false;
            PointerCallbackSnapshot releaseCallback{};
            {
                std::scoped_lock lock(mutex_);
                document = captureDocument_;
                pressed = pressedNode_;
                auto* entry = findDocument(document);
                Node* captured = entry ? findNode(*entry, captureNode_) : nullptr;
                owned = event.button == pressedButton_ && entry && inputEligible(*entry) &&
                    captured && nodeInteractive(captured) && pressedDocument_ == document && pressed != 0;
                if (!owned) {
                    return false;
                }

                wasDragging = dragging_;
                if (event.button == input::PointerButton::Left && captured->focusable()) {
                    activate = focusableHit(*entry, point) == pressed;
                }
                releaseCallback = snapshotPointerHandler(
                    document,
                    *entry,
                    captureNode_,
                    wasDragging ? UIREPO_NODE_POINTER_DRAG_END : UIREPO_NODE_POINTER_BUTTON_UP,
                    event);

                pressedDocument_ = 0;
                pressedNode_ = 0;
                pressedButton_ = input::PointerButton::None;
                pressPoint_ = {};
                dragging_ = false;
                captureDocument_ = 0;
                captureNode_ = 0;
            }

            invoke(releaseCallback);

            if (wasDragging) {
                PointerCallbackSnapshot up{};
                {
                    std::scoped_lock lock(mutex_);
                    auto* entry = findDocument(document);
                    Node* node = entry ? findNode(*entry, pressed) : nullptr;
                    if (entry && inputEligible(*entry) && nodeInteractive(node)) {
                        up = snapshotPointerHandler(
                            document, *entry, pressed, UIREPO_NODE_POINTER_BUTTON_UP, event);
                    }
                }
                invoke(up);
            }

            if (activate) {
                (void)activateNode(document, pressed);
            }
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool DocumentRegistry::routeKeyEvent(const input::KeyEvent& event) noexcept {
    if (event.type != input::KeyEventType::Down || event.keyCode == input::KeyEscape) {
        return false;
    }
    if (event.keyCode == input::KeyF4 && (event.modifiers & input::ModifierAlt) != 0) {
        return false;
    }

    const input::NavigationCommand command = input::navigationForKey(event.keyCode, event.modifiers);
    return command != input::NavigationCommand::None && routeNavigation(command);
}

bool DocumentRegistry::routeNavigation(input::NavigationCommand command) noexcept {
    if (command == input::NavigationCommand::None || command == input::NavigationCommand::Cancel) {
        return false;
    }

    DocumentHandle document{};
    NodeHandle target{};
    CallbackSnapshot valueChanged{};
    bool controlHandled = false;
    {
        std::scoped_lock lock(mutex_);
        document = focusedDocument_;
        auto* entry = findDocument(document);
        Node* focused = entry ? findNode(*entry, focusedNode_) : nullptr;
        if (!entry || !inputEligible(*entry) || !focused || !focused->focusable() || !nodeInteractive(focused)) {
            return false;
        }

        if (auto* directional = dynamic_cast<DirectionalValueControl*>(focused);
            directional != nullptr &&
            (command == input::NavigationCommand::Left || command == input::NavigationCommand::Right)) {
            controlHandled = true;
            const int steps = command == input::NavigationCommand::Right ? 1 : -1;
            if (directional->adjustDirectional(steps)) {
                valueChanged = snapshotHandler(document, *entry, focusedNode_, UIREPO_EVENT_VALUE_CHANGED);
            }
        } else if (command == input::NavigationCommand::Accept) {
            target = focusedNode_;
        } else {
            Node* next = input::navigationTarget(*entry->document, command);
            target = findNodeHandle(*entry, next);
            if (target == 0 || target == focusedNode_) {
                return false;
            }
        }
    }

    if (controlHandled) {
        invoke(valueChanged);
        return true;
    }
    if (command == input::NavigationCommand::Accept) {
        return activateNode(document, target);
    }
    return focusNodeInternal(document, target).handled;
}

bool DocumentRegistry::hasPointerCapture() const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(captureDocument_);
    const Node* captured = entry ? findNode(*entry, captureNode_) : nullptr;
    return entry && inputEligible(*entry) && captured && nodeInteractive(captured);
}

Size DocumentRegistry::inputViewportSize() const noexcept {
    std::scoped_lock lock(mutex_);
    return inputViewport_;
}

void DocumentRegistry::cancelPointerCapture() noexcept {
    std::vector<PointerCallbackSnapshot> callbacks;
    {
        std::scoped_lock lock(mutex_);
        auto hover = clearHoverLocked();
        auto gesture = clearGestureLocked();
        callbacks.insert(callbacks.end(), hover.begin(), hover.end());
        callbacks.insert(callbacks.end(), gesture.begin(), gesture.end());
    }
    invoke(std::move(callbacks));
}

bool DocumentRegistry::setText(
    DocumentHandle document,
    NodeHandle node,
    std::string text) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    if (!target) {
        return false;
    }
    try {
        if (auto* textNode = dynamic_cast<TextNode*>(target)) {
            textNode->setText(std::move(text));
            return true;
        }
        if (auto* buttonNode = dynamic_cast<ButtonNode*>(target)) {
            buttonNode->setText(std::move(text));
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool DocumentRegistry::setTextFontSize(
    DocumentHandle document,
    NodeHandle node,
    float fontSize) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    if (!target) {
        return false;
    }
    try {
        if (auto* textNode = dynamic_cast<TextNode*>(target)) {
            textNode->setFontSize(fontSize);
            return true;
        }
        if (auto* buttonNode = dynamic_cast<ButtonNode*>(target)) {
            buttonNode->setFontSize(fontSize);
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool DocumentRegistry::setTextColor(
    DocumentHandle document,
    NodeHandle node,
    Color color) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    if (!target) {
        return false;
    }
    if (auto* textNode = dynamic_cast<TextNode*>(target)) {
        textNode->setColor(color);
        return true;
    }
    if (auto* buttonNode = dynamic_cast<ButtonNode*>(target)) {
        buttonNode->setTextColor(color);
        return true;
    }
    return false;
}

bool DocumentRegistry::setMediaSource(
    DocumentHandle document,
    NodeHandle node,
    std::string source) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    if (!target) {
        return false;
    }
    try {
        if (auto* imageNode = dynamic_cast<ImageNode*>(target)) {
            imageNode->setSource(std::move(source));
            return true;
        }
        if (auto* videoNode = dynamic_cast<VideoNode*>(target)) {
            videoNode->setSource(std::move(source));
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool DocumentRegistry::setVideoPlayback(
    DocumentHandle document,
    NodeHandle node,
    VideoPlaybackState state,
    bool loop,
    float volume) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    auto* videoNode = target ? dynamic_cast<VideoNode*>(target) : nullptr;
    if (!videoNode) {
        return false;
    }
    try {
        videoNode->setPlaybackState(state);
        videoNode->setLoop(loop);
        videoNode->setVolume(volume);
        return true;
    } catch (...) {
        return false;
    }
}

void DocumentRegistry::setTextMeasurer(const TextMeasurer* textMeasurer) noexcept {
    std::scoped_lock lock(mutex_);
    textMeasurer_ = textMeasurer;
    for (auto& [handle, entry] : documents_) {
        (void)handle;
        if (entry.document) {
            entry.document->setTextMeasurer(textMeasurer_);
        }
    }
}

void DocumentRegistry::layoutVisible(Size viewport) noexcept {
    std::scoped_lock lock(mutex_);
    inputViewport_ = viewport;
    try {
        for (auto& [handle, entry] : documents_) {
            (void)handle;
            if (entry.visible && entry.document) {
                entry.document->layout(viewport);
            }
        }
#ifdef _WIN32
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 SDK: document layout failed: {}", exception.what());
#else
    } catch (const std::exception&) {
#endif
    } catch (...) {
#ifdef _WIN32
        spdlog::error("UIRepo_F4 SDK: document layout failed with an unknown exception");
#endif
    }
}

#ifdef _WIN32
void DocumentRegistry::renderVisible(D3D11Renderer& renderer, Size viewport) noexcept {
    std::scoped_lock lock(mutex_);
    inputViewport_ = viewport;
    try {
        std::vector<DocumentEntry*> ordered;
        ordered.reserve(documents_.size());
        for (auto& [handle, entry] : documents_) {
            (void)handle;
            if (entry.visible && entry.document) {
                ordered.push_back(&entry);
            }
        }

        std::sort(ordered.begin(), ordered.end(), [](const DocumentEntry* left, const DocumentEntry* right) {
            if (left->order != right->order) {
                return left->order < right->order;
            }
            return left->sequence < right->sequence;
        });

        std::uint32_t logicalDraws = 0;
        std::uint32_t segments = 0;
        std::uint32_t rects = 0;
        std::uint32_t glyphRuns = 0;
        std::uint32_t images = 0;
        for (auto* entry : ordered) {
            entry->document->layout(viewport);
            D3D11RenderStats stats{};
            if (!renderer.render(entry->document->buildDisplayList(), &stats)) {
                spdlog::error("UIRepo_F4 SDK: renderer rejected an external document display list");
            }
            logicalDraws += stats.drawCalls;
            segments += stats.drawSegments;
            rects += stats.fillRects;
            glyphRuns += stats.glyphRuns;
            images += stats.imageCommands;
        }

        static telemetry::PeriodicGate batchGate{std::chrono::seconds(2)};
        if (segments > 0 && batchGate.due(std::chrono::steady_clock::now())) {
            spdlog::info(
                "UIRepo_F4 batch: {} logical draws -> {} DrawIndexed segments (rects {}, glyph-runs {}, images {}, docs {})",
                logicalDraws,
                segments,
                rects,
                glyphRuns,
                images,
                ordered.size());
        }
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 SDK: external document render failed: {}", exception.what());
    } catch (...) {
        spdlog::error("UIRepo_F4 SDK: external document render failed with an unknown exception");
    }
}
#endif

std::size_t DocumentRegistry::visibleCount() const noexcept {
    std::scoped_lock lock(mutex_);
    std::size_t count = 0;
    for (const auto& [handle, entry] : documents_) {
        (void)handle;
        if (entry.visible && entry.document) {
            ++count;
        }
    }
    return count;
}

} // namespace uirepo::fo4