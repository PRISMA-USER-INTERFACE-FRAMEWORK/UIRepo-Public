#include "DocumentRegistry.h"
#include "GameThreadDispatcher.h"
#include "NativeApiTable.h"

#include "uirepo/NativeApi.h"
#include "uirepo/NativeApiBridge.h"

#include "ApiResult.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace uirepo::fo4 {
namespace {

constexpr std::uint64_t kV2Capabilities =
    UIREPO_CAP_DOCUMENT_STATE |
    UIREPO_CAP_DOCUMENT_ENUMERATION |
    UIREPO_CAP_GAME_THREAD_DISPATCH;

[[nodiscard]] std::uint8_t apiGetDocumentState(
    UIRepoDocumentHandle document,
    UIRepoDocumentStateV1* out) noexcept {
    if (out == nullptr) {
        return api::refuse(UIREPO_RESULT_INVALID_ARGUMENT);
    }
    if (out->size != sizeof(UIRepoDocumentStateV1)) {
        return api::refuse(UIREPO_RESULT_INVALID_STRUCT_SIZE);
    }
    registry::DocumentState state;
    if (!DocumentRegistry::instance().documentState(document, state)) {
        return api::refuse(UIREPO_RESULT_INVALID_HANDLE);
    }
    *out = native_bridge::makeDocumentState(state);
    return api::succeed();
}

[[nodiscard]] std::uint8_t apiEnumerateDocuments(
    UIRepoDocumentEnumerateCallback callback,
    void* userData) noexcept {
    try {
        if (!native_bridge::enumerateSnapshot(
                [] { return DocumentRegistry::instance().enumerateDocuments(); },
                callback,
                userData)) {
            return api::refuse(UIREPO_RESULT_INVALID_ARGUMENT);
        }
        return api::succeed();
    } catch (...) {
        return api::refuse(native_bridge::resultForActiveException());
    }
}

[[nodiscard]] std::uint8_t apiIsGameThread() noexcept {
    return GameThreadDispatcher::isGameThread() ? 1u : 0u;
}

[[nodiscard]] std::uint8_t apiDispatchToGameThread(
    UIRepoGameThreadCallback callback,
    void* userData) noexcept {
    try {
        switch (native_bridge::bridgeDispatch(
            callback,
            userData,
            [](std::function<void()> task) { return GameThreadDispatcher::dispatch(std::move(task)); })) {
        case native_bridge::DispatchStatus::Queued:
            return api::succeed();
        case native_bridge::DispatchStatus::NullCallback:
            return api::refuse(UIREPO_RESULT_INVALID_ARGUMENT);
        case native_bridge::DispatchStatus::Refused:
            break;
        }
        return api::refuse(UIREPO_RESULT_NOT_READY);
    } catch (...) {
        return api::refuse(native_bridge::resultForActiveException());
    }
}

[[nodiscard]] UIRepoNativeApiV2 makeApiV2() noexcept {
    UIRepoNativeApiV2 api{};
    api.v1 = nativeApiV1();
    api.v1.size = sizeof(UIRepoNativeApiV2);
    api.v1.abiVersion = UIREPO_NATIVE_API_VERSION_V2;
    api.v1.capabilities |= kV2Capabilities;
    api.getDocumentState = &apiGetDocumentState;
    api.enumerateDocuments = &apiEnumerateDocuments;
    api.isGameThread = &apiIsGameThread;
    api.dispatchToGameThread = &apiDispatchToGameThread;
    return api;
}

} // namespace

const UIRepoNativeApiV2& nativeApiV2() noexcept {
    static const UIRepoNativeApiV2 table = makeApiV2();
    return table;
}

} // namespace uirepo::fo4
