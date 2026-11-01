#pragma once

#include "uirepo/NativeApi.h"

#include <cstdint>

namespace uirepo::fo4::native_api {

[[nodiscard]] std::uint8_t apiRendererReady() noexcept;

[[nodiscard]] UIRepoDocumentHandle apiCreateDocument(const char* rootId) noexcept;

[[nodiscard]] std::uint8_t apiDestroyDocument(UIRepoDocumentHandle document) noexcept;

[[nodiscard]] std::uint8_t apiSetDocumentVisible(
    UIRepoDocumentHandle document,
    std::uint8_t visible) noexcept;

[[nodiscard]] std::uint8_t apiSetDocumentOrder(
    UIRepoDocumentHandle document,
    std::int32_t order) noexcept;

[[nodiscard]] std::uint8_t apiSetDocumentInputEnabled(
    UIRepoDocumentHandle document,
    std::uint8_t enabled) noexcept;

[[nodiscard]] UIRepoNodeHandle apiGetRootNode(UIRepoDocumentHandle document) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreatePanel(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateStack(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    std::uint32_t axisValue) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateText(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* text) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateButton(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* text) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateToggle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    std::uint8_t value) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateSlider(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    float value,
    float minimum,
    float maximum,
    float step) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateStepper(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* const* options,
    std::uint32_t optionCount,
    std::uint32_t index) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateTextInput(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* value,
    std::uint32_t maxBytes) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateImage(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* source,
    float naturalWidth,
    float naturalHeight) noexcept;

[[nodiscard]] UIRepoNodeHandle apiCreateVideo(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id,
    const char* source,
    float naturalWidth,
    float naturalHeight) noexcept;

[[nodiscard]] std::uint8_t apiSetNodeVisible(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t visible) noexcept;

[[nodiscard]] std::uint8_t apiSetNodeEnabled(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t enabled) noexcept;

[[nodiscard]] std::uint8_t apiSetNodeStyle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoStyleV1* source) noexcept;

[[nodiscard]] std::uint8_t apiSetStackScrollable(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t scrollable) noexcept;

[[nodiscard]] std::uint8_t apiSetToggleValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t value) noexcept;

[[nodiscard]] std::uint8_t apiGetToggleValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint8_t* value) noexcept;

[[nodiscard]] std::uint8_t apiSetSliderValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    float value) noexcept;

[[nodiscard]] std::uint8_t apiGetSliderValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    float* value) noexcept;

[[nodiscard]] std::uint8_t apiSetStepperIndex(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint32_t index) noexcept;

[[nodiscard]] std::uint8_t apiGetStepperIndex(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint32_t* index) noexcept;

[[nodiscard]] std::uint8_t apiSetTextInputValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const char* value) noexcept;

[[nodiscard]] std::uint8_t apiGetTextInputValue(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    char* buffer,
    std::uint32_t capacity,
    std::uint32_t* required) noexcept;

[[nodiscard]] std::uint8_t apiSetNodeEventHandler(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    UIRepoNodeEventCallback callback,
    void* userData) noexcept;

[[nodiscard]] std::uint8_t apiSetNodePointerEventHandler(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    UIRepoNodePointerEventCallback callback,
    void* userData) noexcept;

[[nodiscard]] std::uint8_t apiSetDocumentKeyEventHandler(
    UIRepoDocumentHandle document,
    UIRepoDocumentKeyEventCallback callback,
    void* userData) noexcept;

[[nodiscard]] std::uint8_t apiSetDocumentFocusRequirements(
    UIRepoDocumentHandle document,
    const UIRepoFocusRequirementsV1* source) noexcept;

[[nodiscard]] std::uint8_t apiFocusNode(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node) noexcept;

[[nodiscard]] std::uint8_t apiClearDocumentFocus(UIRepoDocumentHandle document) noexcept;

[[nodiscard]] std::uint8_t apiSetText(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const char* text) noexcept;

[[nodiscard]] std::uint8_t apiSetTextFontSize(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    float fontSize) noexcept;

[[nodiscard]] std::uint8_t apiSetTextColor(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    UIRepoColorV1 color) noexcept;

[[nodiscard]] std::uint8_t apiSetTextLayout(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoTextLayoutV1* source) noexcept;

[[nodiscard]] std::uint8_t apiSetMediaSource(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const char* source) noexcept;

[[nodiscard]] std::uint8_t apiSetVideoPlayback(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    std::uint32_t stateValue,
    std::uint8_t loop,
    float volume) noexcept;

[[nodiscard]] std::uint8_t apiRoutePointerEvent(const UIRepoPointerEventV1* source) noexcept;

[[nodiscard]] std::uint8_t apiRouteKeyEvent(const UIRepoKeyEventV1* source) noexcept;

[[nodiscard]] UIRepoSurfaceHandle apiCreateOffscreenSurface(
    UIRepoDocumentHandle document,
    const UIRepoSurfaceDescV1* desc) noexcept;

[[nodiscard]] std::uint8_t apiDestroyOffscreenSurface(UIRepoSurfaceHandle surface) noexcept;

[[nodiscard]] std::uint8_t apiResizeOffscreenSurface(
    UIRepoSurfaceHandle surface,
    std::uint32_t width,
    std::uint32_t height) noexcept;

[[nodiscard]] std::uint8_t apiSetOffscreenSurfaceClearColor(
    UIRepoSurfaceHandle surface,
    UIRepoColorV1 color) noexcept;

[[nodiscard]] std::uint8_t apiRenderDocumentToSurface(
    UIRepoDocumentHandle document,
    UIRepoSurfaceHandle surface) noexcept;

[[nodiscard]] std::uint8_t apiGetOffscreenSurfaceInfo(
    UIRepoSurfaceHandle surface,
    UIRepoSurfaceInfoV1* out) noexcept;

[[nodiscard]] std::uint8_t apiGetOffscreenSurfaceStats(UIRepoSurfaceStatsV1* out) noexcept;

[[nodiscard]] std::uint32_t apiGetLastResult() noexcept;

[[nodiscard]] const char* apiDescribeResult(std::uint32_t code) noexcept;

} // namespace uirepo::fo4::native_api
