#include "uirepo/NativeApi.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using PointerRouteFn = std::uint8_t (*)(const UIRepoPointerEventV1*);
using KeyRouteFn = std::uint8_t (*)(const UIRepoKeyEventV1*);
using InputEnabledFn = std::uint8_t (*)(UIRepoDocumentHandle, std::uint8_t);
using ScrollableFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, std::uint8_t);
using EventHandlerFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, UIRepoNodeEventCallback, void*);
using PointerEventHandlerFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, UIRepoNodePointerEventCallback, void*);
using DocumentKeyEventHandlerFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoDocumentKeyEventCallback, void*);
using FocusRequirementsFn = std::uint8_t (*)(UIRepoDocumentHandle, const UIRepoFocusRequirementsV1*);
using TextLayoutFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, const UIRepoTextLayoutV1*);
using FocusNodeFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle);
using ClearFocusFn = std::uint8_t (*)(UIRepoDocumentHandle);
using CreateToggleFn = UIRepoNodeHandle (*)(UIRepoDocumentHandle, UIRepoNodeHandle, const char*, std::uint8_t);
using CreateSliderFn = UIRepoNodeHandle (*)(UIRepoDocumentHandle, UIRepoNodeHandle, const char*, float, float, float, float);
using SetToggleValueFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, std::uint8_t);
using GetToggleValueFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, std::uint8_t*);
using SetSliderValueFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, float);
using GetSliderValueFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, float*);
using CreateStepperFn = UIRepoNodeHandle (*)(UIRepoDocumentHandle, UIRepoNodeHandle, const char*, const char* const*, std::uint32_t, std::uint32_t);
using SetStepperIndexFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, std::uint32_t);
using GetStepperIndexFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, std::uint32_t*);
using CreateTextInputFn = UIRepoNodeHandle (*)(UIRepoDocumentHandle, UIRepoNodeHandle, const char*, const char*, std::uint32_t);
using SetTextInputValueFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, const char*);
using GetTextInputValueFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoNodeHandle, char*, std::uint32_t, std::uint32_t*);
using CreateOffscreenSurfaceFn = UIRepoSurfaceHandle (*)(UIRepoDocumentHandle, const UIRepoSurfaceDescV1*);
using DestroyOffscreenSurfaceFn = std::uint8_t (*)(UIRepoSurfaceHandle);
using ResizeOffscreenSurfaceFn = std::uint8_t (*)(UIRepoSurfaceHandle, std::uint32_t, std::uint32_t);
using SetSurfaceClearColorFn = std::uint8_t (*)(UIRepoSurfaceHandle, UIRepoColorV1);
using RenderDocumentToSurfaceFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoSurfaceHandle);
using GetSurfaceInfoFn = std::uint8_t (*)(UIRepoSurfaceHandle, UIRepoSurfaceInfoV1*);
using GetSurfaceStatsFn = std::uint8_t (*)(UIRepoSurfaceStatsV1*);
using GetLastResultFn = std::uint32_t (*)();
using DescribeResultFn = const char* (*)(std::uint32_t);

static_assert(sizeof(UIRepoDocumentHandle) == 8);
static_assert(sizeof(UIRepoNodeHandle) == 8);
static_assert(std::is_standard_layout_v<UIRepoPointerEventV1>);
static_assert(std::is_trivially_copyable_v<UIRepoPointerEventV1>);
static_assert(std::is_standard_layout_v<UIRepoNodePointerEventV1>);
static_assert(std::is_trivially_copyable_v<UIRepoNodePointerEventV1>);
static_assert(std::is_standard_layout_v<UIRepoKeyEventV1>);
static_assert(std::is_trivially_copyable_v<UIRepoKeyEventV1>);
static_assert(std::is_standard_layout_v<UIRepoFocusRequirementsV1>);
static_assert(std::is_trivially_copyable_v<UIRepoFocusRequirementsV1>);
static_assert(sizeof(UIRepoFocusRequirementsV1) == 8);
static_assert(std::is_standard_layout_v<UIRepoTextLayoutV1>);
static_assert(std::is_trivially_copyable_v<UIRepoTextLayoutV1>);
static_assert(sizeof(UIRepoTextLayoutV1) == 16);
/* Offscreen surfaces are named by handle only: nothing in this ABI may carry
   a device, context, texture, or view pointer across the boundary. */
static_assert(sizeof(UIRepoSurfaceHandle) == 8);
static_assert(std::is_standard_layout_v<UIRepoSurfaceDescV1>);
static_assert(std::is_trivially_copyable_v<UIRepoSurfaceDescV1>);
static_assert(sizeof(UIRepoSurfaceDescV1) == 16);
static_assert(std::is_standard_layout_v<UIRepoSurfaceInfoV1>);
static_assert(std::is_trivially_copyable_v<UIRepoSurfaceInfoV1>);
static_assert(sizeof(UIRepoSurfaceInfoV1) == 32);
static_assert(offsetof(UIRepoSurfaceInfoV1, generation) == 16);
static_assert(std::is_standard_layout_v<UIRepoSurfaceStatsV1>);
static_assert(std::is_trivially_copyable_v<UIRepoSurfaceStatsV1>);
static_assert(sizeof(UIRepoSurfaceStatsV1) == 88);
static_assert(std::is_standard_layout_v<UIRepoNativeApiV1>);
static_assert(std::is_trivially_copyable_v<UIRepoNativeApiV1>);

/* ---------------------------------------------------------------------------
   Frozen ABI layout.

   The compatibility model is that a published struct never changes. A future
   revision adds a new struct and a new table entry rather than growing an
   existing one, which is why every entry point can require an exact `size`
   and why the table can only ever be appended to.

   These assertions are the enforcement. If one fails, the change under way is
   a compatibility break: give the new shape a new name instead of editing the
   published one.
   --------------------------------------------------------------------------- */

static_assert(sizeof(UIRepoLengthV1) == 8);
static_assert(sizeof(UIRepoThicknessV1) == 16);
static_assert(sizeof(UIRepoColorV1) == 4);
static_assert(sizeof(UIRepoStyleV1) == 100);
static_assert(sizeof(UIRepoTextLayoutV1) == 16);
static_assert(sizeof(UIRepoPointerEventV1) == 28);
static_assert(sizeof(UIRepoNodePointerEventV1) == 28);
static_assert(sizeof(UIRepoKeyEventV1) == 20);
static_assert(sizeof(UIRepoFocusRequirementsV1) == 8);
static_assert(sizeof(UIRepoSurfaceDescV1) == 16);
static_assert(sizeof(UIRepoSurfaceInfoV1) == 32);
static_assert(sizeof(UIRepoSurfaceStatsV1) == 88);

/* The table may only grow at the end: every existing entry keeps its offset,
   so an older consumer reading its known prefix stays correct against a newer
   provider. */
static_assert(sizeof(UIRepoNativeApiV1) == 432);
static_assert(offsetof(UIRepoNativeApiV1, size) == 0);
static_assert(offsetof(UIRepoNativeApiV1, abiVersion) == 4);
static_assert(offsetof(UIRepoNativeApiV1, capabilities) == 8);
static_assert(offsetof(UIRepoNativeApiV1, isRendererReady) == 16);
static_assert(offsetof(UIRepoNativeApiV1, createDocument) == 24);
static_assert(offsetof(UIRepoNativeApiV1, destroyDocument) == 32);
static_assert(offsetof(UIRepoNativeApiV1, setDocumentVisible) == 40);
static_assert(offsetof(UIRepoNativeApiV1, setDocumentOrder) == 48);
static_assert(offsetof(UIRepoNativeApiV1, setDocumentInputEnabled) == 56);
static_assert(offsetof(UIRepoNativeApiV1, getRootNode) == 64);
static_assert(offsetof(UIRepoNativeApiV1, createPanel) == 72);
static_assert(offsetof(UIRepoNativeApiV1, createStack) == 80);
static_assert(offsetof(UIRepoNativeApiV1, createText) == 88);
static_assert(offsetof(UIRepoNativeApiV1, createButton) == 96);
static_assert(offsetof(UIRepoNativeApiV1, createImage) == 104);
static_assert(offsetof(UIRepoNativeApiV1, createVideo) == 112);
static_assert(offsetof(UIRepoNativeApiV1, setNodeVisible) == 120);
static_assert(offsetof(UIRepoNativeApiV1, setNodeEnabled) == 128);
static_assert(offsetof(UIRepoNativeApiV1, setNodeStyle) == 136);
static_assert(offsetof(UIRepoNativeApiV1, setNodeEventHandler) == 144);
static_assert(offsetof(UIRepoNativeApiV1, focusNode) == 152);
static_assert(offsetof(UIRepoNativeApiV1, clearDocumentFocus) == 160);
static_assert(offsetof(UIRepoNativeApiV1, setText) == 168);
static_assert(offsetof(UIRepoNativeApiV1, setTextFontSize) == 176);
static_assert(offsetof(UIRepoNativeApiV1, setTextColor) == 184);
static_assert(offsetof(UIRepoNativeApiV1, setMediaSource) == 192);
static_assert(offsetof(UIRepoNativeApiV1, setVideoPlayback) == 200);
static_assert(offsetof(UIRepoNativeApiV1, routePointerEvent) == 208);
static_assert(offsetof(UIRepoNativeApiV1, routeKeyEvent) == 216);
static_assert(offsetof(UIRepoNativeApiV1, setStackScrollable) == 224);
static_assert(offsetof(UIRepoNativeApiV1, setNodePointerEventHandler) == 232);
static_assert(offsetof(UIRepoNativeApiV1, createToggle) == 240);
static_assert(offsetof(UIRepoNativeApiV1, createSlider) == 248);
static_assert(offsetof(UIRepoNativeApiV1, setToggleValue) == 256);
static_assert(offsetof(UIRepoNativeApiV1, getToggleValue) == 264);
static_assert(offsetof(UIRepoNativeApiV1, setSliderValue) == 272);
static_assert(offsetof(UIRepoNativeApiV1, getSliderValue) == 280);
static_assert(offsetof(UIRepoNativeApiV1, createStepper) == 288);
static_assert(offsetof(UIRepoNativeApiV1, setStepperIndex) == 296);
static_assert(offsetof(UIRepoNativeApiV1, getStepperIndex) == 304);
static_assert(offsetof(UIRepoNativeApiV1, createTextInput) == 312);
static_assert(offsetof(UIRepoNativeApiV1, setTextInputValue) == 320);
static_assert(offsetof(UIRepoNativeApiV1, getTextInputValue) == 328);
static_assert(offsetof(UIRepoNativeApiV1, setDocumentKeyEventHandler) == 336);
static_assert(offsetof(UIRepoNativeApiV1, setDocumentFocusRequirements) == 344);
static_assert(offsetof(UIRepoNativeApiV1, setTextLayout) == 352);
static_assert(offsetof(UIRepoNativeApiV1, createOffscreenSurface) == 360);
static_assert(offsetof(UIRepoNativeApiV1, destroyOffscreenSurface) == 368);
static_assert(offsetof(UIRepoNativeApiV1, resizeOffscreenSurface) == 376);
static_assert(offsetof(UIRepoNativeApiV1, setOffscreenSurfaceClearColor) == 384);
static_assert(offsetof(UIRepoNativeApiV1, renderDocumentToSurface) == 392);
static_assert(offsetof(UIRepoNativeApiV1, getOffscreenSurfaceInfo) == 400);
static_assert(offsetof(UIRepoNativeApiV1, getOffscreenSurfaceStats) == 408);
static_assert(offsetof(UIRepoNativeApiV1, getLastResult) == 416);
static_assert(offsetof(UIRepoNativeApiV1, describeResult) == 424);

static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::createOffscreenSurface), CreateOffscreenSurfaceFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::destroyOffscreenSurface), DestroyOffscreenSurfaceFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::resizeOffscreenSurface), ResizeOffscreenSurfaceFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setOffscreenSurfaceClearColor), SetSurfaceClearColorFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::renderDocumentToSurface), RenderDocumentToSurfaceFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getOffscreenSurfaceInfo), GetSurfaceInfoFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getOffscreenSurfaceStats), GetSurfaceStatsFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getLastResult), GetLastResultFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::describeResult), DescribeResultFn>);
static_assert(UIREPO_CAP_OFFSCREEN_SURFACES == (UINT64_C(1) << 14));
static_assert(UIREPO_CAP_RESULT_CODES == (UINT64_C(1) << 15));

/* Result codes are a published contract: a consumer switches on these values,
   so an existing one may never be renumbered. New codes append. */
static_assert(UIREPO_RESULT_OK == 0);
static_assert(UIREPO_RESULT_UNKNOWN == 1);
static_assert(UIREPO_RESULT_INVALID_ARGUMENT == 2);
static_assert(UIREPO_RESULT_INVALID_STRUCT_SIZE == 3);
static_assert(UIREPO_RESULT_INVALID_HANDLE == 4);
static_assert(UIREPO_RESULT_WRONG_THREAD == 5);
static_assert(UIREPO_RESULT_NOT_READY == 6);
static_assert(UIREPO_RESULT_UNSUPPORTED == 7);
static_assert(UIREPO_RESULT_BUDGET_EXCEEDED == 8);
static_assert(UIREPO_RESULT_RESOURCE_FAILED == 9);
static_assert(UIREPO_RESULT_STATE_CONFLICT == 10);
static_assert(UIREPO_RESULT_OUT_OF_MEMORY == 11);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setDocumentInputEnabled), InputEnabledFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setNodeEventHandler), EventHandlerFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::focusNode), FocusNodeFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::clearDocumentFocus), ClearFocusFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::routePointerEvent), PointerRouteFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::routeKeyEvent), KeyRouteFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setStackScrollable), ScrollableFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setNodePointerEventHandler), PointerEventHandlerFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::createToggle), CreateToggleFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::createSlider), CreateSliderFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setToggleValue), SetToggleValueFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getToggleValue), GetToggleValueFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setSliderValue), SetSliderValueFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getSliderValue), GetSliderValueFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::createStepper), CreateStepperFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setStepperIndex), SetStepperIndexFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getStepperIndex), GetStepperIndexFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::createTextInput), CreateTextInputFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setTextInputValue), SetTextInputValueFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::getTextInputValue), GetTextInputValueFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setDocumentKeyEventHandler), DocumentKeyEventHandlerFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setDocumentFocusRequirements), FocusRequirementsFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV1::setTextLayout), TextLayoutFn>);

static_assert(offsetof(UIRepoNativeApiV1, size) == 0);
static_assert(offsetof(UIRepoNativeApiV1, abiVersion) > offsetof(UIRepoNativeApiV1, size));
static_assert(offsetof(UIRepoNativeApiV1, capabilities) > offsetof(UIRepoNativeApiV1, abiVersion));
static_assert(offsetof(UIRepoNativeApiV1, setDocumentOrder) < offsetof(UIRepoNativeApiV1, setDocumentInputEnabled));
static_assert(offsetof(UIRepoNativeApiV1, setDocumentInputEnabled) < offsetof(UIRepoNativeApiV1, getRootNode));
static_assert(offsetof(UIRepoNativeApiV1, setNodeStyle) < offsetof(UIRepoNativeApiV1, setNodeEventHandler));
static_assert(offsetof(UIRepoNativeApiV1, setNodeEventHandler) < offsetof(UIRepoNativeApiV1, focusNode));
static_assert(offsetof(UIRepoNativeApiV1, focusNode) < offsetof(UIRepoNativeApiV1, clearDocumentFocus));
static_assert(offsetof(UIRepoNativeApiV1, setVideoPlayback) < offsetof(UIRepoNativeApiV1, routePointerEvent));
static_assert(offsetof(UIRepoNativeApiV1, routePointerEvent) < offsetof(UIRepoNativeApiV1, routeKeyEvent));
static_assert(offsetof(UIRepoNativeApiV1, routeKeyEvent) < offsetof(UIRepoNativeApiV1, setStackScrollable));
static_assert(offsetof(UIRepoNativeApiV1, setStackScrollable) < offsetof(UIRepoNativeApiV1, setNodePointerEventHandler));
static_assert(offsetof(UIRepoNativeApiV1, setNodePointerEventHandler) < offsetof(UIRepoNativeApiV1, createToggle));
static_assert(offsetof(UIRepoNativeApiV1, createToggle) < offsetof(UIRepoNativeApiV1, createSlider));
static_assert(offsetof(UIRepoNativeApiV1, createSlider) < offsetof(UIRepoNativeApiV1, setToggleValue));
static_assert(offsetof(UIRepoNativeApiV1, setToggleValue) < offsetof(UIRepoNativeApiV1, getToggleValue));
static_assert(offsetof(UIRepoNativeApiV1, getToggleValue) < offsetof(UIRepoNativeApiV1, setSliderValue));
static_assert(offsetof(UIRepoNativeApiV1, setSliderValue) < offsetof(UIRepoNativeApiV1, getSliderValue));
static_assert(offsetof(UIRepoNativeApiV1, getSliderValue) < offsetof(UIRepoNativeApiV1, createStepper));
static_assert(offsetof(UIRepoNativeApiV1, createStepper) < offsetof(UIRepoNativeApiV1, setStepperIndex));
static_assert(offsetof(UIRepoNativeApiV1, setStepperIndex) < offsetof(UIRepoNativeApiV1, getStepperIndex));
static_assert(offsetof(UIRepoNativeApiV1, getStepperIndex) < offsetof(UIRepoNativeApiV1, createTextInput));
static_assert(offsetof(UIRepoNativeApiV1, createTextInput) < offsetof(UIRepoNativeApiV1, setTextInputValue));
static_assert(offsetof(UIRepoNativeApiV1, setTextInputValue) < offsetof(UIRepoNativeApiV1, getTextInputValue));
static_assert(offsetof(UIRepoNativeApiV1, getTextInputValue) < offsetof(UIRepoNativeApiV1, setDocumentKeyEventHandler));
static_assert(offsetof(UIRepoNativeApiV1, setDocumentKeyEventHandler) < offsetof(UIRepoNativeApiV1, setDocumentFocusRequirements));
static_assert(offsetof(UIRepoNativeApiV1, setDocumentFocusRequirements) < offsetof(UIRepoNativeApiV1, setTextLayout));

static_assert((UIREPO_CAP_POINTER_INPUT & UIREPO_CAP_KEYBOARD_INPUT) == 0);
static_assert((UIREPO_CAP_SCROLL_CONTAINERS & UIREPO_CAP_POINTER_EVENTS) == 0);
static_assert((UIREPO_CAP_FOCUS_LEASE & UIREPO_CAP_DOCUMENT_KEY_EVENTS) == 0);
static_assert(UIREPO_CAP_NATIVE_SETTINGS_CONTROLS != 0);
static_assert(UIREPO_CAP_FOCUS_LEASE != 0);
static_assert(UIREPO_CAP_DOCUMENT_KEY_EVENTS != 0);
static_assert(UIREPO_CAP_TEXT_LAYOUT != 0);
static_assert(UIREPO_NODE_STEPPER == 9);
static_assert(UIREPO_NODE_TEXT_INPUT == 10);
static_assert(UIREPO_NATIVE_API_VERSION_V1 == 1u);
static_assert(UIREPO_NATIVE_API_VERSION_V2 == 2u);
static_assert(UIREPO_NATIVE_API_VERSION == 2u);

/* ABI v2: UIRepoNativeApiV2 begins with the unchanged V1 table, so every V1 assertion above keeps holding. */

using GetDocumentStateFn = std::uint8_t (*)(UIRepoDocumentHandle, UIRepoDocumentStateV1*);
using EnumerateDocumentsFn = std::uint8_t (*)(UIRepoDocumentEnumerateCallback, void*);
using IsGameThreadFn = std::uint8_t (*)();
using DispatchToGameThreadFn = std::uint8_t (*)(UIRepoGameThreadCallback, void*);
using DocumentEnumerateCallbackFn = void (*)(UIRepoDocumentHandle, const UIRepoDocumentStateV1*, void*);
using GameThreadCallbackFn = void (*)(void*);

static_assert(std::is_standard_layout_v<UIRepoDocumentStateV1>);
static_assert(std::is_trivially_copyable_v<UIRepoDocumentStateV1>);
static_assert(std::is_standard_layout_v<UIRepoNativeApiV2>);
static_assert(std::is_trivially_copyable_v<UIRepoNativeApiV2>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV2::v1), UIRepoNativeApiV1>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV2::getDocumentState), GetDocumentStateFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV2::enumerateDocuments), EnumerateDocumentsFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV2::isGameThread), IsGameThreadFn>);
static_assert(std::is_same_v<decltype(UIRepoNativeApiV2::dispatchToGameThread), DispatchToGameThreadFn>);
static_assert(std::is_same_v<UIRepoDocumentEnumerateCallback, DocumentEnumerateCallbackFn>);
static_assert(std::is_same_v<UIRepoGameThreadCallback, GameThreadCallbackFn>);

static_assert(sizeof(UIRepoDocumentStateV1) == 40);
static_assert(offsetof(UIRepoDocumentStateV1, size) == 0);
static_assert(offsetof(UIRepoDocumentStateV1, visible) == 4);
static_assert(offsetof(UIRepoDocumentStateV1, inputEnabled) == 5);
static_assert(offsetof(UIRepoDocumentStateV1, focused) == 6);
static_assert(offsetof(UIRepoDocumentStateV1, reserved0) == 7);
static_assert(offsetof(UIRepoDocumentStateV1, health) == 8);
static_assert(offsetof(UIRepoDocumentStateV1, role) == 12);
static_assert(offsetof(UIRepoDocumentStateV1, order) == 16);
static_assert(offsetof(UIRepoDocumentStateV1, reserved1) == 20);
static_assert(offsetof(UIRepoDocumentStateV1, ownerId) == 24);
static_assert(offsetof(UIRepoDocumentStateV1, generation) == 32);
static_assert(offsetof(UIRepoNativeApiV2, v1) == 0);
static_assert(sizeof(UIRepoNativeApiV2) == 464);
static_assert(offsetof(UIRepoNativeApiV2, getDocumentState) == sizeof(UIRepoNativeApiV1));
static_assert(offsetof(UIRepoNativeApiV2, getDocumentState) == 432);
static_assert(offsetof(UIRepoNativeApiV2, enumerateDocuments) == 440);
static_assert(offsetof(UIRepoNativeApiV2, isGameThread) == 448);
static_assert(offsetof(UIRepoNativeApiV2, dispatchToGameThread) == 456);

static_assert(sizeof(UIRepoNativeApiV2) == sizeof(UIRepoNativeApiV1) + 4 * sizeof(void*));
static_assert(UIREPO_CAP_DOCUMENT_STATE == (UINT64_C(1) << 16));
static_assert(UIREPO_CAP_DOCUMENT_ENUMERATION == (UINT64_C(1) << 17));
static_assert(UIREPO_CAP_GAME_THREAD_DISPATCH == (UINT64_C(1) << 18));
static_assert((UIREPO_CAP_RESULT_CODES & UIREPO_CAP_DOCUMENT_STATE) == 0);
static_assert(UIREPO_DOCUMENT_HEALTH_CREATING == 0);
static_assert(UIREPO_DOCUMENT_HEALTH_READY == 1);
static_assert(UIREPO_DOCUMENT_HEALTH_LIVE == 2);
static_assert(UIREPO_DOCUMENT_HEALTH_HIDDEN == 3);
static_assert(UIREPO_DOCUMENT_HEALTH_FAULTED == 4);
static_assert(UIREPO_DOCUMENT_HEALTH_DESTROYING == 5);
static_assert(UIREPO_DOCUMENT_HEALTH_DESTROYED == 6);
static_assert(UIREPO_DOCUMENT_ROLE_HUD == 0);
static_assert(UIREPO_DOCUMENT_ROLE_PANEL == 1);
static_assert(UIREPO_DOCUMENT_ROLE_OVERLAY == 2);
static_assert(UIREPO_DOCUMENT_ROLE_WORLD_SURFACE == 3);
static_assert(UIREPO_DOCUMENT_ROLE_DIAGNOSTIC == 4);

int main() {
    return 0;
}
