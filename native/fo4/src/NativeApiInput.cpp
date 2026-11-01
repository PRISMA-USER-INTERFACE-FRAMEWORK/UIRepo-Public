#include "DocumentRegistry.h"
#include "FocusLeaseManager.h"
#include "NativeApiConvert.h"
#include "NativeApiEntries.h"

#include "uirepo/NativeApi.h"

#include "ApiResult.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace uirepo::fo4::native_api {

[[nodiscard]] std::uint8_t apiSetNodeEventHandler(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    UIRepoNodeEventCallback callback,
    void* userData) noexcept {
    try {
        return result(DocumentRegistry::instance().setNodeEventHandler(document, node, callback, userData));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetNodePointerEventHandler(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    UIRepoNodePointerEventCallback callback,
    void* userData) noexcept {
    try {
        return result(DocumentRegistry::instance().setNodePointerEventHandler(
            document, node, callback, userData));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetDocumentKeyEventHandler(
    UIRepoDocumentHandle document,
    UIRepoDocumentKeyEventCallback callback,
    void* userData) noexcept {
    try {
        auto& registry = DocumentRegistry::instance();
        if (!registry.setDocumentKeyEventHandler(document, callback, userData)) {
            return 0;
        }
        if (callback == nullptr) {
            FocusLeaseManager::clearRequirements(document);
        }
        return 1u;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetDocumentFocusRequirements(
    UIRepoDocumentHandle document,
    const UIRepoFocusRequirementsV1* source) noexcept {
    if (source == nullptr) {
        return api::refuse(UIREPO_RESULT_INVALID_ARGUMENT);
    }
    if (source->size != sizeof(UIRepoFocusRequirementsV1)) {
        return api::refuse(UIREPO_RESULT_INVALID_STRUCT_SIZE);
    }

    try {
        auto& registry = DocumentRegistry::instance();
        DocumentRegistry::DocumentState state{};
        if (!registry.documentState(document, state)) {
            return 0;
        }
        if (source->escape != 0 && !registry.hasDocumentKeyEventHandler(document)) {
            return 0;
        }

        FocusLeaseManager::setRequirements(
            document,
            input::FocusRequirements{
                .inputMask = source->inputMask != 0,
                .cursor = source->cursor != 0,
                .escape = source->escape != 0,
                .pause = source->pause != 0,
            });
        return 1u;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiFocusNode(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node) noexcept {
    try {
        return result(DocumentRegistry::instance().focusNode(document, node));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiClearDocumentFocus(UIRepoDocumentHandle document) noexcept {
    try {
        return result(DocumentRegistry::instance().clearDocumentFocus(document));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiRoutePointerEvent(const UIRepoPointerEventV1* source) noexcept {
    input::PointerEvent event{};
    if (!makePointerEvent(source, event)) {
        return 0;
    }
    try {
        auto& registry = DocumentRegistry::instance();
        if (event.type == input::PointerEventType::HorizontalWheel) {
            return result(registry.routeHorizontalWheelEvent(Point{event.x, event.y}, event.wheelDelta));
        }
        return result(registry.routePointerEvent(event));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiRouteKeyEvent(const UIRepoKeyEventV1* source) noexcept {
    input::KeyEvent event{};
    if (!makeKeyEvent(source, event)) {
        return 0;
    }
    try {
        auto& registry = DocumentRegistry::instance();
        return result(registry.routeDocumentKeyEvent(event) || registry.routeKeyEvent(event));
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

} // namespace uirepo::fo4::native_api
