#pragma once

/* Optional C++ convenience layer over the public UIRepo C ABI.

   This header deliberately has no access to framework internals and includes no
   Windows headers. It only calls through UIRepoNativeApiV1, so anything done
   here can also be done directly through NativeApi.h.

   The wrapper is intentionally exception-free in its API surface. Operations
   return Result<T>, versioned structs have their size field repaired before
   crossing the ABI, document/surface ownership is RAII, and raw handle kinds
   are distinct C++ types so they cannot be mixed accidentally. */

#include "uirepo/NativeApi.h"
#include "uirepo/NativeApiCppTypes.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace uirepo::sdk::native {

class Document final {
public:
    Document() noexcept = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Document(Document&& other) noexcept
        : api_(std::exchange(other.api_, nullptr)),
          handle_(std::exchange(other.handle_, DocumentHandle{})) {}

    Document& operator=(Document&& other) noexcept {
        if (this != &other) {
            reset();
            api_ = std::exchange(other.api_, nullptr);
            handle_ = std::exchange(other.handle_, DocumentHandle{});
        }
        return *this;
    }

    ~Document() { reset(); }

    [[nodiscard]] DocumentHandle handle() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    void reset() noexcept {
        if (api_ != nullptr && handle_ && api_->destroyDocument != nullptr) {
            (void)api_->destroyDocument(handle_.value);
        }
        api_ = nullptr;
        handle_ = {};
    }

    [[nodiscard]] DocumentHandle release() noexcept {
        api_ = nullptr;
        return std::exchange(handle_, DocumentHandle{});
    }

private:
    friend class Client;

    Document(const UIRepoNativeApiV1* api, DocumentHandle handle) noexcept
        : api_(api), handle_(handle) {}

    const UIRepoNativeApiV1* api_{};
    DocumentHandle handle_{};
};

class Surface final {
public:
    Surface() noexcept = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    Surface(Surface&& other) noexcept
        : api_(std::exchange(other.api_, nullptr)),
          handle_(std::exchange(other.handle_, SurfaceHandle{})) {}

    Surface& operator=(Surface&& other) noexcept {
        if (this != &other) {
            reset();
            api_ = std::exchange(other.api_, nullptr);
            handle_ = std::exchange(other.handle_, SurfaceHandle{});
        }
        return *this;
    }

    ~Surface() { reset(); }

    [[nodiscard]] SurfaceHandle handle() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    void reset() noexcept {
        if (api_ != nullptr && handle_ && api_->destroyOffscreenSurface != nullptr) {
            (void)api_->destroyOffscreenSurface(handle_.value);
        }
        api_ = nullptr;
        handle_ = {};
    }

    [[nodiscard]] SurfaceHandle release() noexcept {
        api_ = nullptr;
        return std::exchange(handle_, SurfaceHandle{});
    }

private:
    friend class Client;

    Surface(const UIRepoNativeApiV1* api, SurfaceHandle handle) noexcept
        : api_(api), handle_(handle) {}

    const UIRepoNativeApiV1* api_{};
    SurfaceHandle handle_{};
};

class Client final {
public:
    Client() noexcept = default;

    explicit Client(const UIRepoNativeApiV1* api) noexcept {
        if (api != nullptr &&
            api->size >= sizeof(UIRepoNativeApiV1) &&
            api->abiVersion >= UIREPO_NATIVE_API_VERSION_V1) {
            api_ = api;
            if (api->abiVersion >= UIREPO_NATIVE_API_VERSION_V2 && api->size >= sizeof(UIRepoNativeApiV2)) {
                v2_ = reinterpret_cast<const UIRepoNativeApiV2*>(api);
            }
        }
    }

    [[nodiscard]] bool hasV2() const noexcept { return v2_ != nullptr; }

    [[nodiscard]] const UIRepoNativeApiV1* table() const noexcept { return api_; }
    [[nodiscard]] explicit operator bool() const noexcept { return api_ != nullptr; }

    [[nodiscard]] bool has(UIRepoNativeCapabilityV1 capability) const noexcept {
        return api_ != nullptr &&
               (api_->capabilities & static_cast<std::uint64_t>(capability)) != 0;
    }

    [[nodiscard]] bool rendererReady() const noexcept {
        return api_ != nullptr && api_->isRendererReady != nullptr && api_->isRendererReady() != 0;
    }

    [[nodiscard]] const char* describe(std::uint32_t code) const noexcept {
        if (api_ != nullptr && has(UIREPO_CAP_RESULT_CODES) && api_->describeResult != nullptr) {
            return api_->describeResult(code);
        }
        return "UIRepo call failed; provider did not expose result descriptions";
    }

    [[nodiscard]] Result<Document> createDocument(const char* rootId) const noexcept {
        if (api_ == nullptr || api_->createDocument == nullptr) {
            return Result<Document>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        const UIRepoDocumentHandle raw = api_->createDocument(rootId);
        if (raw == 0) {
            return Result<Document>::failure(lastFailure());
        }
        return Result<Document>::success(Document(api_, DocumentHandle{raw}));
    }

    [[nodiscard]] Result<void> setVisible(DocumentHandle document, bool visible) const noexcept {
        return call(api_ != nullptr ? api_->setDocumentVisible : nullptr,
                    document.value, visible ? 1u : 0u);
    }

    [[nodiscard]] Result<void> setOrder(DocumentHandle document, std::int32_t order) const noexcept {
        return call(api_ != nullptr ? api_->setDocumentOrder : nullptr, document.value, order);
    }

    [[nodiscard]] Result<void> setInputEnabled(DocumentHandle document, bool enabled) const noexcept {
        return call(api_ != nullptr ? api_->setDocumentInputEnabled : nullptr,
                    document.value, enabled ? 1u : 0u);
    }

    [[nodiscard]] Result<NodeHandle> root(DocumentHandle document) const noexcept {
        if (api_ == nullptr || api_->getRootNode == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->getRootNode(document.value));
    }

    [[nodiscard]] Result<NodeHandle> createPanel(
        DocumentHandle document, NodeHandle parent, const char* id) const noexcept {
        if (api_ == nullptr || api_->createPanel == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createPanel(document.value, parent.value, id));
    }

    [[nodiscard]] Result<NodeHandle> createStack(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        std::uint32_t axis) const noexcept {
        if (api_ == nullptr || api_->createStack == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createStack(document.value, parent.value, id, axis));
    }

    [[nodiscard]] Result<NodeHandle> createText(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        const char* text) const noexcept {
        if (api_ == nullptr || api_->createText == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createText(document.value, parent.value, id, text));
    }

    [[nodiscard]] Result<NodeHandle> createButton(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        const char* text) const noexcept {
        if (api_ == nullptr || api_->createButton == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createButton(document.value, parent.value, id, text));
    }

    [[nodiscard]] Result<NodeHandle> createImage(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        const char* source,
        float naturalWidth,
        float naturalHeight) const noexcept {
        if (api_ == nullptr || api_->createImage == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createImage(
            document.value, parent.value, id, source, naturalWidth, naturalHeight));
    }

    [[nodiscard]] Result<NodeHandle> createVideo(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        const char* source,
        float naturalWidth,
        float naturalHeight) const noexcept {
        if (api_ == nullptr || api_->createVideo == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createVideo(
            document.value, parent.value, id, source, naturalWidth, naturalHeight));
    }

    [[nodiscard]] Result<NodeHandle> createToggle(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        bool value) const noexcept {
        if (api_ == nullptr || api_->createToggle == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createToggle(document.value, parent.value, id, value ? 1u : 0u));
    }

    [[nodiscard]] Result<NodeHandle> createSlider(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        float value,
        float minimum,
        float maximum,
        float step) const noexcept {
        if (api_ == nullptr || api_->createSlider == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createSlider(
            document.value, parent.value, id, value, minimum, maximum, step));
    }

    [[nodiscard]] Result<NodeHandle> createStepper(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        const char* const* options,
        std::uint32_t optionCount,
        std::uint32_t index) const noexcept {
        if (api_ == nullptr || api_->createStepper == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createStepper(
            document.value, parent.value, id, options, optionCount, index));
    }

    [[nodiscard]] Result<NodeHandle> createTextInput(
        DocumentHandle document,
        NodeHandle parent,
        const char* id,
        const char* value,
        std::uint32_t maxBytes) const noexcept {
        if (api_ == nullptr || api_->createTextInput == nullptr) {
            return Result<NodeHandle>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return nodeResult(api_->createTextInput(
            document.value, parent.value, id, value, maxBytes));
    }

    [[nodiscard]] Result<void> setNodeVisible(
        DocumentHandle document, NodeHandle node, bool visible) const noexcept {
        return call(api_ != nullptr ? api_->setNodeVisible : nullptr,
                    document.value, node.value, visible ? 1u : 0u);
    }

    [[nodiscard]] Result<void> setNodeEnabled(
        DocumentHandle document, NodeHandle node, bool enabled) const noexcept {
        return call(api_ != nullptr ? api_->setNodeEnabled : nullptr,
                    document.value, node.value, enabled ? 1u : 0u);
    }

    [[nodiscard]] Result<void> setStyle(
        DocumentHandle document,
        NodeHandle node,
        UIRepoStyleV1 style) const noexcept {
        if (api_ == nullptr || api_->setNodeStyle == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        style.size = sizeof(style);
        return fromBool(api_->setNodeStyle(document.value, node.value, &style));
    }

    [[nodiscard]] Result<void> setNodeEventHandler(
        DocumentHandle document,
        NodeHandle node,
        UIRepoNodeEventCallback callback,
        void* userData) const noexcept {
        return call(api_ != nullptr ? api_->setNodeEventHandler : nullptr,
                    document.value, node.value, callback, userData);
    }

    [[nodiscard]] Result<void> setNodePointerEventHandler(
        DocumentHandle document,
        NodeHandle node,
        UIRepoNodePointerEventCallback callback,
        void* userData) const noexcept {
        return call(api_ != nullptr ? api_->setNodePointerEventHandler : nullptr,
                    document.value, node.value, callback, userData);
    }

    [[nodiscard]] Result<void> focus(DocumentHandle document, NodeHandle node) const noexcept {
        return call(api_ != nullptr ? api_->focusNode : nullptr, document.value, node.value);
    }

    [[nodiscard]] Result<void> clearFocus(DocumentHandle document) const noexcept {
        return call(api_ != nullptr ? api_->clearDocumentFocus : nullptr, document.value);
    }

    [[nodiscard]] Result<void> setText(
        DocumentHandle document, NodeHandle node, const char* text) const noexcept {
        return call(api_ != nullptr ? api_->setText : nullptr, document.value, node.value, text);
    }

    [[nodiscard]] Result<void> setTextFontSize(
        DocumentHandle document, NodeHandle node, float fontSize) const noexcept {
        return call(api_ != nullptr ? api_->setTextFontSize : nullptr,
                    document.value, node.value, fontSize);
    }

    [[nodiscard]] Result<void> setTextColor(
        DocumentHandle document, NodeHandle node, UIRepoColorV1 color) const noexcept {
        return call(api_ != nullptr ? api_->setTextColor : nullptr,
                    document.value, node.value, color);
    }

    [[nodiscard]] Result<void> setTextLayout(
        DocumentHandle document,
        NodeHandle node,
        UIRepoTextLayoutV1 layout) const noexcept {
        if (api_ == nullptr || api_->setTextLayout == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        layout.size = sizeof(layout);
        return fromBool(api_->setTextLayout(document.value, node.value, &layout));
    }

    [[nodiscard]] Result<void> setMediaSource(
        DocumentHandle document, NodeHandle node, const char* source) const noexcept {
        return call(api_ != nullptr ? api_->setMediaSource : nullptr,
                    document.value, node.value, source);
    }

    [[nodiscard]] Result<void> setVideoPlayback(
        DocumentHandle document,
        NodeHandle node,
        std::uint32_t state,
        bool loop,
        float volume) const noexcept {
        return call(api_ != nullptr ? api_->setVideoPlayback : nullptr,
                    document.value, node.value, state, loop ? 1u : 0u, volume);
    }

    [[nodiscard]] Result<void> setScrollable(
        DocumentHandle document, NodeHandle node, bool scrollable) const noexcept {
        return call(api_ != nullptr ? api_->setStackScrollable : nullptr,
                    document.value, node.value, scrollable ? 1u : 0u);
    }

    [[nodiscard]] Result<void> setToggleValue(
        DocumentHandle document, NodeHandle node, bool value) const noexcept {
        return call(api_ != nullptr ? api_->setToggleValue : nullptr,
                    document.value, node.value, value ? 1u : 0u);
    }

    [[nodiscard]] Result<bool> toggleValue(
        DocumentHandle document, NodeHandle node) const noexcept {
        if (api_ == nullptr || api_->getToggleValue == nullptr) {
            return Result<bool>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        std::uint8_t value{};
        if (api_->getToggleValue(document.value, node.value, &value) == 0) {
            return Result<bool>::failure(lastFailure());
        }
        return Result<bool>::success(value != 0);
    }

    [[nodiscard]] Result<void> setSliderValue(
        DocumentHandle document, NodeHandle node, float value) const noexcept {
        return call(api_ != nullptr ? api_->setSliderValue : nullptr,
                    document.value, node.value, value);
    }

    [[nodiscard]] Result<float> sliderValue(
        DocumentHandle document, NodeHandle node) const noexcept {
        if (api_ == nullptr || api_->getSliderValue == nullptr) {
            return Result<float>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        float value{};
        if (api_->getSliderValue(document.value, node.value, &value) == 0) {
            return Result<float>::failure(lastFailure());
        }
        return Result<float>::success(value);
    }

    [[nodiscard]] Result<void> setStepperIndex(
        DocumentHandle document, NodeHandle node, std::uint32_t index) const noexcept {
        return call(api_ != nullptr ? api_->setStepperIndex : nullptr,
                    document.value, node.value, index);
    }

    [[nodiscard]] Result<std::uint32_t> stepperIndex(
        DocumentHandle document, NodeHandle node) const noexcept {
        if (api_ == nullptr || api_->getStepperIndex == nullptr) {
            return Result<std::uint32_t>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        std::uint32_t value{};
        if (api_->getStepperIndex(document.value, node.value, &value) == 0) {
            return Result<std::uint32_t>::failure(lastFailure());
        }
        return Result<std::uint32_t>::success(value);
    }

    [[nodiscard]] Result<void> setTextInputValue(
        DocumentHandle document, NodeHandle node, const char* value) const noexcept {
        return call(api_ != nullptr ? api_->setTextInputValue : nullptr,
                    document.value, node.value, value);
    }

    /* Caller-owned buffer keeps this header allocation-free and noexcept. The
       returned value is the required byte count including the trailing NUL. A
       null buffer with capacity zero is the supported sizing query. */
    [[nodiscard]] Result<std::uint32_t> textInputValue(
        DocumentHandle document,
        NodeHandle node,
        char* buffer,
        std::uint32_t capacity) const noexcept {
        if (api_ == nullptr || api_->getTextInputValue == nullptr) {
            return Result<std::uint32_t>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        std::uint32_t required{};
        if (api_->getTextInputValue(document.value, node.value, buffer, capacity, &required) == 0) {
            return Result<std::uint32_t>::failure(lastFailure());
        }
        return Result<std::uint32_t>::success(required);
    }

    [[nodiscard]] Result<void> setDocumentKeyEventHandler(
        DocumentHandle document,
        UIRepoDocumentKeyEventCallback callback,
        void* userData) const noexcept {
        return call(api_ != nullptr ? api_->setDocumentKeyEventHandler : nullptr,
                    document.value, callback, userData);
    }

    [[nodiscard]] Result<void> setFocusRequirements(
        DocumentHandle document,
        UIRepoFocusRequirementsV1 requirements) const noexcept {
        if (api_ == nullptr || api_->setDocumentFocusRequirements == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        requirements.size = sizeof(requirements);
        return fromBool(api_->setDocumentFocusRequirements(document.value, &requirements));
    }

    [[nodiscard]] Result<void> routePointer(UIRepoPointerEventV1 event) const noexcept {
        if (api_ == nullptr || api_->routePointerEvent == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        event.size = sizeof(event);
        return fromBool(api_->routePointerEvent(&event));
    }

    [[nodiscard]] Result<void> routeKey(UIRepoKeyEventV1 event) const noexcept {
        if (api_ == nullptr || api_->routeKeyEvent == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        event.size = sizeof(event);
        return fromBool(api_->routeKeyEvent(&event));
    }

    [[nodiscard]] Result<Surface> createSurface(
        DocumentHandle document,
        std::uint32_t width,
        std::uint32_t height,
        UIRepoColorV1 clearColor = UIRepoColorV1{}) const noexcept {
        if (!has(UIREPO_CAP_OFFSCREEN_SURFACES) ||
            api_ == nullptr || api_->createOffscreenSurface == nullptr) {
            return Result<Surface>::failure(UIREPO_RESULT_UNSUPPORTED);
        }

        UIRepoSurfaceDescV1 desc = makeSurfaceDesc();
        desc.width = width;
        desc.height = height;
        desc.clearColor = clearColor;
        const UIRepoSurfaceHandle raw = api_->createOffscreenSurface(document.value, &desc);
        if (raw == 0) {
            return Result<Surface>::failure(lastFailure());
        }
        return Result<Surface>::success(Surface(api_, SurfaceHandle{raw}));
    }

    [[nodiscard]] Result<void> resizeSurface(
        SurfaceHandle surface,
        std::uint32_t width,
        std::uint32_t height) const noexcept {
        return call(api_ != nullptr ? api_->resizeOffscreenSurface : nullptr,
                    surface.value, width, height);
    }

    [[nodiscard]] Result<void> setSurfaceClearColor(
        SurfaceHandle surface, UIRepoColorV1 color) const noexcept {
        return call(api_ != nullptr ? api_->setOffscreenSurfaceClearColor : nullptr,
                    surface.value, color);
    }

    [[nodiscard]] Result<void> renderToSurface(
        DocumentHandle document,
        SurfaceHandle surface) const noexcept {
        return call(api_ != nullptr ? api_->renderDocumentToSurface : nullptr,
                    document.value, surface.value);
    }

    [[nodiscard]] Result<UIRepoSurfaceInfoV1> surfaceInfo(SurfaceHandle surface) const noexcept {
        if (api_ == nullptr || api_->getOffscreenSurfaceInfo == nullptr) {
            return Result<UIRepoSurfaceInfoV1>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        UIRepoSurfaceInfoV1 info = makeSurfaceInfo();
        if (api_->getOffscreenSurfaceInfo(surface.value, &info) == 0) {
            return Result<UIRepoSurfaceInfoV1>::failure(lastFailure());
        }
        return Result<UIRepoSurfaceInfoV1>::success(info);
    }

    [[nodiscard]] Result<UIRepoSurfaceStatsV1> surfaceStats() const noexcept {
        if (api_ == nullptr || api_->getOffscreenSurfaceStats == nullptr) {
            return Result<UIRepoSurfaceStatsV1>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        UIRepoSurfaceStatsV1 stats = makeSurfaceStats();
        if (api_->getOffscreenSurfaceStats(&stats) == 0) {
            return Result<UIRepoSurfaceStatsV1>::failure(lastFailure());
        }
        return Result<UIRepoSurfaceStatsV1>::success(stats);
    }

    [[nodiscard]] Result<UIRepoDocumentStateV1> documentState(DocumentHandle document) const noexcept {
        if (v2_ == nullptr || !has(UIREPO_CAP_DOCUMENT_STATE) || v2_->getDocumentState == nullptr) {
            return Result<UIRepoDocumentStateV1>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        UIRepoDocumentStateV1 state = makeDocumentState();
        if (v2_->getDocumentState(document.value, &state) == 0) {
            return Result<UIRepoDocumentStateV1>::failure(lastFailure());
        }
        return Result<UIRepoDocumentStateV1>::success(state);
    }

    [[nodiscard]] Result<void> enumerateDocuments(
        UIRepoDocumentEnumerateCallback callback, void* userData) const noexcept {
        if (v2_ == nullptr || !has(UIREPO_CAP_DOCUMENT_ENUMERATION) || v2_->enumerateDocuments == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return fromBool(v2_->enumerateDocuments(callback, userData));
    }

    /* A pure query: it never records a result, so it cannot disturb a pending failure code. */
    [[nodiscard]] bool isGameThread() const noexcept {
        return v2_ != nullptr && has(UIREPO_CAP_GAME_THREAD_DISPATCH) &&
               v2_->isGameThread != nullptr && v2_->isGameThread() != 0;
    }

    [[nodiscard]] Result<void> dispatchToGameThread(
        UIRepoGameThreadCallback callback, void* userData) const noexcept {
        if (v2_ == nullptr || !has(UIREPO_CAP_GAME_THREAD_DISPATCH) || v2_->dispatchToGameThread == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return fromBool(v2_->dispatchToGameThread(callback, userData));
    }

private:
    template <class Function, class... Args>
    [[nodiscard]] Result<void> call(Function function, Args... args) const noexcept {
        if (function == nullptr) {
            return Result<void>::failure(UIREPO_RESULT_UNSUPPORTED);
        }
        return fromBool(function(args...));
    }

    [[nodiscard]] Result<NodeHandle> nodeResult(UIRepoNodeHandle raw) const noexcept {
        if (raw == 0) {
            return Result<NodeHandle>::failure(lastFailure());
        }
        return Result<NodeHandle>::success(NodeHandle{raw});
    }

    [[nodiscard]] std::uint32_t lastFailure() const noexcept {
        if (api_ != nullptr && has(UIREPO_CAP_RESULT_CODES) && api_->getLastResult != nullptr) {
            const std::uint32_t code = api_->getLastResult();
            if (code == static_cast<std::uint32_t>(UIREPO_RESULT_OK)) {
                return static_cast<std::uint32_t>(UIREPO_RESULT_UNKNOWN);
            }
            return code;
        }
        return static_cast<std::uint32_t>(UIREPO_RESULT_UNKNOWN);
    }

    [[nodiscard]] Result<void> fromBool(std::uint8_t value) const noexcept {
        if (value != 0) {
            return Result<void>::success();
        }
        return Result<void>::failure(lastFailure());
    }

    const UIRepoNativeApiV1* api_{};
    const UIRepoNativeApiV2* v2_{};
};

} // namespace uirepo::sdk::native
