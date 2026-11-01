#pragma once

#include "uirepo/Controls.h"
#include "uirepo/Core.h"
#include "uirepo/InputRouter.h"
#include "uirepo/NativeApi.h"
#include "uirepo/RegistryTypes.h"
#include "uirepo/TextInput.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uirepo {
class D3D11Renderer;
}

namespace uirepo::fo4 {

class DocumentRegistry final {
public:
    using DocumentHandle = registry::DocumentHandle;
    using SurfaceHandle = registry::SurfaceHandle;
    using OwnerHandle = registry::OwnerHandle;
    using DocumentRole = registry::DocumentRole;
    using DocumentHealth = registry::DocumentHealth;
    using DocumentState = registry::DocumentState;
    using NodeHandle = std::uint64_t;
    using OwnerCancelHook = void (*)(OwnerHandle owner, void* userData) noexcept;
    using SurfaceReleaseHook = void (*)(SurfaceHandle surface, void* userData) noexcept;

    static DocumentRegistry& instance() noexcept;

    DocumentRegistry() = default;
    DocumentRegistry(const DocumentRegistry&) = delete;
    DocumentRegistry& operator=(const DocumentRegistry&) = delete;

    [[nodiscard]] DocumentHandle createDocument(std::string rootId);
    [[nodiscard]] DocumentHandle createOwnedDocument(
        std::string rootId,
        OwnerHandle owner,
        DocumentRole role = DocumentRole::Panel);
    [[nodiscard]] DocumentHandle adoptOwnedDocument(
        std::unique_ptr<Document> document,
        OwnerHandle owner,
        DocumentRole role = DocumentRole::Panel,
        bool visible = true);
    [[nodiscard]] bool destroyDocument(DocumentHandle document) noexcept;
    [[nodiscard]] bool destroyDocumentManaged(DocumentHandle document) noexcept;
    [[nodiscard]] std::size_t destroyOwnerDocuments(OwnerHandle owner) noexcept;
    [[nodiscard]] bool setDocumentVisible(DocumentHandle document, bool visible) noexcept;
    [[nodiscard]] bool setDocumentOrder(DocumentHandle document, std::int32_t order) noexcept;
    [[nodiscard]] bool setDocumentInputEnabled(DocumentHandle document, bool enabled) noexcept;
    [[nodiscard]] bool setDocumentRole(DocumentHandle document, DocumentRole role) noexcept;
    [[nodiscard]] bool faultDocument(DocumentHandle document) noexcept;
    [[nodiscard]] bool recoverDocument(DocumentHandle document) noexcept;
    [[nodiscard]] bool documentState(DocumentHandle document, DocumentState& state) const noexcept;
    [[nodiscard]] std::vector<DocumentState> enumerateDocuments() const;
    [[nodiscard]] std::uint64_t documentGeneration(DocumentHandle document) const noexcept;
    [[nodiscard]] bool acceptsGeneration(DocumentHandle document, std::uint64_t generation) const noexcept;
    [[nodiscard]] OwnerHandle dispatchOwner(DocumentHandle document) const noexcept;
    void setOwnerCancelHook(OwnerCancelHook hook, void* userData = nullptr) noexcept;
    void setSurfaceReleaseHook(SurfaceReleaseHook hook, void* userData = nullptr) noexcept;

    [[nodiscard]] SurfaceHandle attachSurface(DocumentHandle document) noexcept;
    [[nodiscard]] bool detachSurface(SurfaceHandle surface) noexcept;
    [[nodiscard]] DocumentHandle surfaceDocument(SurfaceHandle surface) const noexcept;
    [[nodiscard]] bool snapshotDisplayList(
        DocumentHandle document,
        Size viewport,
        std::vector<DrawCommand>& commands) noexcept;

    [[nodiscard]] NodeHandle rootNode(DocumentHandle document) const noexcept;
    [[nodiscard]] NodeHandle findNodeById(DocumentHandle document, std::string_view id) const noexcept;

    [[nodiscard]] NodeHandle createPanel(DocumentHandle document, NodeHandle parent, std::string id);
    [[nodiscard]] NodeHandle createStack(DocumentHandle document, NodeHandle parent, std::string id, Axis axis);
    [[nodiscard]] NodeHandle createText(DocumentHandle document, NodeHandle parent, std::string id, std::string text);
    [[nodiscard]] NodeHandle createButton(DocumentHandle document, NodeHandle parent, std::string id, std::string text);
    [[nodiscard]] NodeHandle createToggle(DocumentHandle document, NodeHandle parent, std::string id, bool value);
    [[nodiscard]] NodeHandle createSlider(
        DocumentHandle document,
        NodeHandle parent,
        std::string id,
        float value,
        float minimum,
        float maximum,
        float step);
    [[nodiscard]] NodeHandle createStepper(
        DocumentHandle document,
        NodeHandle parent,
        std::string id,
        std::vector<std::string> options,
        std::size_t index);
    [[nodiscard]] NodeHandle createTextInput(
        DocumentHandle document,
        NodeHandle parent,
        std::string id,
        std::string value,
        std::size_t maxBytes);
    [[nodiscard]] NodeHandle createImage(
        DocumentHandle document,
        NodeHandle parent,
        std::string id,
        std::string source,
        Size naturalSize);
    [[nodiscard]] NodeHandle createVideo(
        DocumentHandle document,
        NodeHandle parent,
        std::string id,
        std::string source,
        Size naturalSize);
    [[nodiscard]] bool destroyNode(DocumentHandle document, NodeHandle node) noexcept;

    [[nodiscard]] bool setNodeVisible(DocumentHandle document, NodeHandle node, bool visible) noexcept;
    [[nodiscard]] bool setNodeEnabled(DocumentHandle document, NodeHandle node, bool enabled) noexcept;
    [[nodiscard]] bool setNodeStyle(DocumentHandle document, NodeHandle node, const Style& style) noexcept;
    [[nodiscard]] bool setStackScrollable(DocumentHandle document, NodeHandle node, bool scrollable) noexcept;
    [[nodiscard]] bool setToggleValue(DocumentHandle document, NodeHandle node, bool value) noexcept;
    [[nodiscard]] bool getToggleValue(DocumentHandle document, NodeHandle node, bool& value) const noexcept;
    [[nodiscard]] bool setSliderValue(DocumentHandle document, NodeHandle node, float value) noexcept;
    [[nodiscard]] bool getSliderValue(DocumentHandle document, NodeHandle node, float& value) const noexcept;
    [[nodiscard]] bool setStepperIndex(DocumentHandle document, NodeHandle node, std::size_t index) noexcept;
    [[nodiscard]] bool getStepperIndex(DocumentHandle document, NodeHandle node, std::size_t& index) const noexcept;
    [[nodiscard]] bool setTextInputValue(DocumentHandle document, NodeHandle node, std::string value) noexcept;
    [[nodiscard]] bool getTextInputValue(DocumentHandle document, NodeHandle node, std::string& value) const noexcept;
    [[nodiscard]] bool setNodeEventHandler(
        DocumentHandle document,
        NodeHandle node,
        UIRepoNodeEventCallback callback,
        void* userData) noexcept;
    [[nodiscard]] bool setNodePointerEventHandler(
        DocumentHandle document,
        NodeHandle node,
        UIRepoNodePointerEventCallback callback,
        void* userData) noexcept;
    [[nodiscard]] bool setDocumentKeyEventHandler(
        DocumentHandle document,
        UIRepoDocumentKeyEventCallback callback,
        void* userData) noexcept;
    [[nodiscard]] bool hasDocumentKeyEventHandler(DocumentHandle document) const noexcept;
    [[nodiscard]] bool focusNode(DocumentHandle document, NodeHandle node) noexcept;
    [[nodiscard]] bool clearDocumentFocus(DocumentHandle document) noexcept;
    [[nodiscard]] bool setText(DocumentHandle document, NodeHandle node, std::string text) noexcept;
    [[nodiscard]] bool setTextFontSize(DocumentHandle document, NodeHandle node, float fontSize) noexcept;
    [[nodiscard]] bool setTextColor(DocumentHandle document, NodeHandle node, Color color) noexcept;
    [[nodiscard]] bool setTextLayout(DocumentHandle document, NodeHandle node, TextLayoutOptions layout) noexcept;
    [[nodiscard]] bool setMediaSource(DocumentHandle document, NodeHandle node, std::string source) noexcept;
    [[nodiscard]] bool setVideoPlayback(
        DocumentHandle document,
        NodeHandle node,
        VideoPlaybackState state,
        bool loop,
        float volume) noexcept;

    [[nodiscard]] bool routePointerEvent(const input::PointerEvent& event) noexcept;
    [[nodiscard]] bool routeHorizontalWheelEvent(Point point, float wheelDelta) noexcept;
    bool handlePointerLeave() noexcept;
    [[nodiscard]] bool routeKeyEvent(const input::KeyEvent& event) noexcept;
    [[nodiscard]] bool routeDocumentKeyEvent(const input::KeyEvent& event) noexcept;
    [[nodiscard]] bool routeTextEvent(const input::TextEvent& event) noexcept;
    [[nodiscard]] bool routeNavigation(input::NavigationCommand command) noexcept;
    [[nodiscard]] bool hasPointerCapture() const noexcept;
    [[nodiscard]] Size inputViewportSize() const noexcept;
    [[nodiscard]] bool focusedTextInputCaret(Rect& caret) const noexcept;
    void cancelPointerCapture() noexcept;

    void setTextMeasurer(const TextMeasurer* textMeasurer) noexcept;
    void layoutVisible(Size viewport) noexcept;
    void renderVisible(D3D11Renderer& renderer, Size viewport) noexcept;
    [[nodiscard]] std::size_t visibleCount() const noexcept;

private:
    struct EventHandler {
        UIRepoNodeEventCallback callback{};
        void* userData{};
    };

    struct PointerHandler {
        UIRepoNodePointerEventCallback callback{};
        void* userData{};
    };

    struct KeyHandler {
        UIRepoDocumentKeyEventCallback callback{};
        void* userData{};
    };

    struct CallbackSnapshot {
        UIRepoNodeEventCallback callback{};
        void* userData{};
        DocumentHandle document{};
        NodeHandle node{};
        std::uint32_t eventType{};

        [[nodiscard]] explicit operator bool() const noexcept { return callback != nullptr; }
    };

    struct PointerCallbackSnapshot {
        UIRepoNodePointerEventCallback callback{};
        void* userData{};
        DocumentHandle document{};
        NodeHandle node{};
        UIRepoNodePointerEventV1 event{};

        [[nodiscard]] explicit operator bool() const noexcept { return callback != nullptr; }
    };

    struct KeyCallbackSnapshot {
        UIRepoDocumentKeyEventCallback callback{};
        void* userData{};
        DocumentHandle document{};
        UIRepoKeyEventV1 event{};

        [[nodiscard]] explicit operator bool() const noexcept { return callback != nullptr; }
    };

    struct InputTarget {
        DocumentHandle document{};
        NodeHandle node{};
    };

    struct FocusTransitionResult {
        bool focused{};
        bool handled{};
    };

    struct DocumentEntry {
        std::unique_ptr<Document> document;
        std::unordered_map<NodeHandle, Node*> nodes;
        std::unordered_map<NodeHandle, EventHandler> handlers;
        std::unordered_map<NodeHandle, PointerHandler> pointerHandlers;
        std::unordered_set<SurfaceHandle> surfaces;
        KeyHandler keyHandler{};
        NodeHandle rootHandle{};
        OwnerHandle owner{};
        OwnerHandle dispatchOwner{};
        DocumentRole role{DocumentRole::Panel};
        DocumentHealth health{DocumentHealth::Live};
        std::uint64_t generation{1};
        bool visible{true};
        bool inputEnabled{};
        std::int32_t order{};
        std::uint64_t sequence{};
    };

    [[nodiscard]] DocumentEntry* findDocument(DocumentHandle document) noexcept;
    [[nodiscard]] const DocumentEntry* findDocument(DocumentHandle document) const noexcept;
    [[nodiscard]] Node* findNode(DocumentEntry& document, NodeHandle node) noexcept;
    [[nodiscard]] const Node* findNode(const DocumentEntry& document, NodeHandle node) const noexcept;
    [[nodiscard]] NodeHandle findNodeHandle(const DocumentEntry& document, const Node* node) const noexcept;
    [[nodiscard]] NodeHandle registerNode(DocumentEntry& document, Node& node) noexcept;
    [[nodiscard]] bool idAvailable(DocumentEntry& document, const std::string& id) noexcept;
    [[nodiscard]] DocumentHandle nextDocumentHandle() noexcept;
    [[nodiscard]] NodeHandle nextNodeHandle() noexcept;
    [[nodiscard]] SurfaceHandle nextSurfaceHandle() noexcept;

    [[nodiscard]] bool inputEligible(const DocumentEntry& entry) const noexcept;
    [[nodiscard]] bool nodeInteractive(const Node* node) const noexcept;
    [[nodiscard]] bool subtreeContains(const Node* root, const Node* node) const noexcept;
    [[nodiscard]] NodeHandle focusableHit(DocumentEntry& entry, Point point) const noexcept;
    [[nodiscard]] NodeHandle pointerHandlerHit(DocumentEntry& entry, Point point) const noexcept;
    [[nodiscard]] InputTarget pointerTarget(Point point, bool requirePointerHandler) noexcept;
    [[nodiscard]] DocumentEntry* topmostHitDocument(Point point) noexcept;
    [[nodiscard]] CallbackSnapshot snapshotHandler(
        DocumentHandle document,
        const DocumentEntry& entry,
        NodeHandle node,
        std::uint32_t eventType) const noexcept;
    [[nodiscard]] PointerCallbackSnapshot snapshotPointerHandler(
        DocumentHandle document,
        const DocumentEntry& entry,
        NodeHandle node,
        std::uint32_t eventType,
        const input::PointerEvent& source) const noexcept;
    [[nodiscard]] KeyCallbackSnapshot snapshotKeyHandler(
        DocumentHandle document,
        const DocumentEntry& entry,
        const input::KeyEvent& source) const noexcept;
    [[nodiscard]] CallbackSnapshot clearFocusLocked(DocumentHandle document) noexcept;
    [[nodiscard]] std::vector<PointerCallbackSnapshot> clearHoverLocked(DocumentHandle document = 0, Node* subtree = nullptr) noexcept;
    [[nodiscard]] std::vector<PointerCallbackSnapshot> clearGestureLocked(DocumentHandle document = 0, Node* subtree = nullptr) noexcept;
    [[nodiscard]] FocusTransitionResult focusNodeInternal(DocumentHandle document, NodeHandle node) noexcept;
    [[nodiscard]] bool activateNode(DocumentHandle document, NodeHandle node) noexcept;
    [[nodiscard]] DocumentHealth effectiveHealth(const DocumentEntry& entry) const noexcept;
    static void invoke(CallbackSnapshot snapshot) noexcept;
    static void invoke(PointerCallbackSnapshot snapshot) noexcept;
    static void invoke(std::vector<PointerCallbackSnapshot> snapshots) noexcept;
    [[nodiscard]] static bool invoke(KeyCallbackSnapshot snapshot) noexcept;

    mutable std::mutex mutex_;
    std::unordered_map<DocumentHandle, DocumentEntry> documents_;
    std::unordered_map<SurfaceHandle, DocumentHandle> surfaces_;
    std::unordered_set<DocumentHandle> destroyedDocuments_;
    std::unordered_set<SurfaceHandle> destroyedSurfaces_;
    const TextMeasurer* textMeasurer_{};
    Size inputViewport_{};
    DocumentHandle focusedDocument_{};
    NodeHandle focusedNode_{};
    DocumentHandle hoveredDocument_{};
    NodeHandle hoveredNode_{};
    DocumentHandle pressedDocument_{};
    NodeHandle pressedNode_{};
    input::PointerButton pressedButton_{input::PointerButton::None};
    Point pressPoint_{};
    bool dragging_{};
    DocumentHandle captureDocument_{};
    NodeHandle captureNode_{};
    DocumentHandle nextDocument_{1};
    NodeHandle nextNode_{1};
    SurfaceHandle nextSurface_{1};
    std::uint64_t nextSequence_{1};
    OwnerCancelHook ownerCancelHook_{};
    void* ownerCancelUserData_{};
    SurfaceReleaseHook surfaceReleaseHook_{};
    void* surfaceReleaseUserData_{};
};

} // namespace uirepo::fo4
