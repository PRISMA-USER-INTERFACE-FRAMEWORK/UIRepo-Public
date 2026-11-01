#include "DocumentRegistry.h"
#include "FocusLeaseManager.h"
#include "NativeApiConvert.h"
#include "NativeApiEntries.h"
#include "NativeApiTable.h"
#include "Runtime.h"

#include "uirepo/NativeApi.h"

#include "ApiResult.h"

#include <cstddef>
#include <cstdint>

namespace uirepo::fo4 {
namespace {

using namespace native_api;

constexpr std::uint64_t kCapabilities =
    UIREPO_CAP_RETAINED_DOCUMENTS |
    UIREPO_CAP_NATIVE_D3D11 |
    UIREPO_CAP_DIRECTWRITE_TEXT |
    UIREPO_CAP_ENGINE_COMPOSITION |
    UIREPO_CAP_RENDERER_RECOVERY |
    UIREPO_CAP_DXGI_FALLBACK |
    UIREPO_CAP_POINTER_INPUT |
    UIREPO_CAP_KEYBOARD_INPUT |
    UIREPO_CAP_SCROLL_CONTAINERS |
    UIREPO_CAP_POINTER_EVENTS |
    UIREPO_CAP_NATIVE_SETTINGS_CONTROLS |
    UIREPO_CAP_FOCUS_LEASE |
    UIREPO_CAP_DOCUMENT_KEY_EVENTS |
    UIREPO_CAP_TEXT_LAYOUT |
    UIREPO_CAP_OFFSCREEN_SURFACES |
    UIREPO_CAP_RESULT_CODES;

[[nodiscard]] std::uint8_t apiRendererReady() noexcept {
    return result(Runtime::instance().rendererReady());
}

[[nodiscard]] UIRepoDocumentHandle apiCreateDocument(const char* rootId) noexcept {
    if (rootId == nullptr || *rootId == '\0') {
        return 0;
    }
    try {
        return DocumentRegistry::instance().createOwnedDocument(
            rootId,
            0,
            DocumentRegistry::DocumentRole::Panel);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiDestroyDocument(UIRepoDocumentHandle document) noexcept {
    try {
        FocusLeaseManager::clearRequirements(document);
        return result(DocumentRegistry::instance().destroyDocumentManaged(document));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetDocumentVisible(
    UIRepoDocumentHandle document,
    std::uint8_t visible) noexcept {
    try {
        return result(DocumentRegistry::instance().setDocumentVisible(document, visible != 0));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetDocumentOrder(
    UIRepoDocumentHandle document,
    std::int32_t order) noexcept {
    try {
        return result(DocumentRegistry::instance().setDocumentOrder(document, order));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetDocumentInputEnabled(
    UIRepoDocumentHandle document,
    std::uint8_t enabled) noexcept {
    try {
        return result(DocumentRegistry::instance().setDocumentInputEnabled(document, enabled != 0));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] UIRepoNodeHandle apiGetRootNode(UIRepoDocumentHandle document) noexcept {
    try {
        return DocumentRegistry::instance().rootNode(document);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] constexpr UIRepoNativeApiV1 makeApiV1() noexcept {
    UIRepoNativeApiV1 api{};
    api.size = sizeof(UIRepoNativeApiV1);
    api.abiVersion = UIREPO_NATIVE_API_VERSION;
    api.capabilities = kCapabilities;
    api.isRendererReady = &apiRendererReady;
    api.createDocument = &apiCreateDocument;
    api.destroyDocument = &apiDestroyDocument;
    api.setDocumentVisible = &apiSetDocumentVisible;
    api.setDocumentOrder = &apiSetDocumentOrder;
    api.setDocumentInputEnabled = &apiSetDocumentInputEnabled;
    api.getRootNode = &apiGetRootNode;
    api.createPanel = &apiCreatePanel;
    api.createStack = &apiCreateStack;
    api.createText = &apiCreateText;
    api.createButton = &apiCreateButton;
    api.createImage = &apiCreateImage;
    api.createVideo = &apiCreateVideo;
    api.setNodeVisible = &apiSetNodeVisible;
    api.setNodeEnabled = &apiSetNodeEnabled;
    api.setNodeStyle = &apiSetNodeStyle;
    api.setNodeEventHandler = &apiSetNodeEventHandler;
    api.focusNode = &apiFocusNode;
    api.clearDocumentFocus = &apiClearDocumentFocus;
    api.setText = &apiSetText;
    api.setTextFontSize = &apiSetTextFontSize;
    api.setTextColor = &apiSetTextColor;
    api.setMediaSource = &apiSetMediaSource;
    api.setVideoPlayback = &apiSetVideoPlayback;
    api.routePointerEvent = &apiRoutePointerEvent;
    api.routeKeyEvent = &apiRouteKeyEvent;
    api.setStackScrollable = &apiSetStackScrollable;
    api.setNodePointerEventHandler = &apiSetNodePointerEventHandler;
    api.createToggle = &apiCreateToggle;
    api.createSlider = &apiCreateSlider;
    api.setToggleValue = &apiSetToggleValue;
    api.getToggleValue = &apiGetToggleValue;
    api.setSliderValue = &apiSetSliderValue;
    api.getSliderValue = &apiGetSliderValue;
    api.createStepper = &apiCreateStepper;
    api.setStepperIndex = &apiSetStepperIndex;
    api.getStepperIndex = &apiGetStepperIndex;
    api.createTextInput = &apiCreateTextInput;
    api.setTextInputValue = &apiSetTextInputValue;
    api.getTextInputValue = &apiGetTextInputValue;
    api.setDocumentKeyEventHandler = &apiSetDocumentKeyEventHandler;
    api.setDocumentFocusRequirements = &apiSetDocumentFocusRequirements;
    api.setTextLayout = &apiSetTextLayout;
    api.createOffscreenSurface = &apiCreateOffscreenSurface;
    api.destroyOffscreenSurface = &apiDestroyOffscreenSurface;
    api.resizeOffscreenSurface = &apiResizeOffscreenSurface;
    api.setOffscreenSurfaceClearColor = &apiSetOffscreenSurfaceClearColor;
    api.renderDocumentToSurface = &apiRenderDocumentToSurface;
    api.getOffscreenSurfaceInfo = &apiGetOffscreenSurfaceInfo;
    api.getOffscreenSurfaceStats = &apiGetOffscreenSurfaceStats;
    api.getLastResult = &apiGetLastResult;
    api.describeResult = &apiDescribeResult;
    return api;
}

constexpr UIRepoNativeApiV1 kApiV1 = makeApiV1();

static_assert(kApiV1.getLastResult != nullptr);
static_assert(kApiV1.describeResult != nullptr);
static_assert(kApiV1.createOffscreenSurface != nullptr);
static_assert(kApiV1.destroyOffscreenSurface != nullptr);
static_assert(kApiV1.resizeOffscreenSurface != nullptr);
static_assert(kApiV1.setOffscreenSurfaceClearColor != nullptr);
static_assert(kApiV1.renderDocumentToSurface != nullptr);
static_assert(kApiV1.getOffscreenSurfaceInfo != nullptr);
static_assert(kApiV1.getOffscreenSurfaceStats != nullptr);
static_assert(kApiV1.setDocumentInputEnabled != nullptr);
static_assert(kApiV1.setNodeEventHandler != nullptr);
static_assert(kApiV1.focusNode != nullptr);
static_assert(kApiV1.clearDocumentFocus != nullptr);
static_assert(kApiV1.routePointerEvent != nullptr);
static_assert(kApiV1.routeKeyEvent != nullptr);
static_assert(kApiV1.setStackScrollable != nullptr);
static_assert(kApiV1.setNodePointerEventHandler != nullptr);
static_assert(kApiV1.createToggle != nullptr);
static_assert(kApiV1.createSlider != nullptr);
static_assert(kApiV1.setToggleValue != nullptr);
static_assert(kApiV1.getToggleValue != nullptr);
static_assert(kApiV1.setSliderValue != nullptr);
static_assert(kApiV1.getSliderValue != nullptr);
static_assert(kApiV1.createStepper != nullptr);
static_assert(kApiV1.setStepperIndex != nullptr);
static_assert(kApiV1.getStepperIndex != nullptr);
static_assert(kApiV1.createTextInput != nullptr);
static_assert(kApiV1.setTextInputValue != nullptr);
static_assert(kApiV1.getTextInputValue != nullptr);
static_assert(kApiV1.setDocumentKeyEventHandler != nullptr);
static_assert(kApiV1.setDocumentFocusRequirements != nullptr);
static_assert(kApiV1.setTextLayout != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_POINTER_INPUT) == 0 ||
    kApiV1.routePointerEvent != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_KEYBOARD_INPUT) == 0 ||
    kApiV1.routeKeyEvent != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_SCROLL_CONTAINERS) == 0 ||
    kApiV1.setStackScrollable != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_POINTER_EVENTS) == 0 ||
    kApiV1.setNodePointerEventHandler != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_NATIVE_SETTINGS_CONTROLS) == 0 ||
    (kApiV1.createToggle != nullptr &&
     kApiV1.createSlider != nullptr &&
     kApiV1.createStepper != nullptr &&
     kApiV1.createTextInput != nullptr &&
     kApiV1.setToggleValue != nullptr &&
     kApiV1.getToggleValue != nullptr &&
     kApiV1.setSliderValue != nullptr &&
     kApiV1.getSliderValue != nullptr &&
     kApiV1.setStepperIndex != nullptr &&
     kApiV1.getStepperIndex != nullptr &&
     kApiV1.setTextInputValue != nullptr &&
     kApiV1.getTextInputValue != nullptr));
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_DOCUMENT_KEY_EVENTS) == 0 ||
    kApiV1.setDocumentKeyEventHandler != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_FOCUS_LEASE) == 0 ||
    kApiV1.setDocumentFocusRequirements != nullptr);
static_assert(
    (kApiV1.capabilities & UIREPO_CAP_TEXT_LAYOUT) == 0 ||
    kApiV1.setTextLayout != nullptr);

} // namespace

const UIRepoNativeApiV1& nativeApiV1() noexcept {
    return kApiV1;
}

} // namespace uirepo::fo4

extern "C" __declspec(dllexport) const UIRepoNativeApiV1* UIRepo_GetNativeApi(
    std::uint32_t requestedVersion) noexcept {
    // A consumer built against an older ABI must keep working against a newer
    // framework: the table only ever grows at the end, so its known prefix
    // stays valid. A consumer asking for a newer ABI than this build provides
    // is refused, because it would read entries that do not exist.
    if (requestedVersion == 0 || requestedVersion > UIREPO_NATIVE_API_VERSION) {
        return nullptr;
    }
    return &uirepo::fo4::nativeApiV2().v1;
}
