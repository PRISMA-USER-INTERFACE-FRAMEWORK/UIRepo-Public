/* Stub ABI v2 provider; dispatched work only runs inside UIRepoStub_RunGameThreadQueue. */

#include "StubProvider.h"

#include <cstddef>
#include <initializer_list>

namespace {

constexpr UIRepoDocumentHandle kSecondDocument = abi_harness::kDocument + 1;
constexpr std::size_t kQueueLimit = 4;

struct Queued {
    UIRepoGameThreadCallback callback;
    void* userData;
};

Queued gQueue[kQueueLimit];
std::size_t gQueued = 0;
thread_local bool tlsOnGameThread = false;

UIRepoDocumentStateV1 stateFor(UIRepoDocumentHandle document) noexcept {
    UIRepoDocumentStateV1 state{};
    state.size = sizeof(UIRepoDocumentStateV1);
    const bool first = document == abi_harness::kDocument;
    state.visible = first ? 1 : 0;
    state.inputEnabled = first ? 0 : 1;
    state.focused = first ? 0 : 1;
    state.health = first ? UIREPO_DOCUMENT_HEALTH_LIVE : UIREPO_DOCUMENT_HEALTH_HIDDEN;
    state.role = UIREPO_DOCUMENT_ROLE_PANEL;
    state.order = first ? 7 : -3;
    state.ownerId = first ? 1234567u : 0u;
    state.generation = first ? 3u : 9u;
    return state;
}

uint8_t getDocumentState(UIRepoDocumentHandle document, UIRepoDocumentStateV1* out) noexcept {
    namespace stub = abi_harness::stub;
    if (out == nullptr) {
        stub::tlsLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    if (out->size != sizeof(UIRepoDocumentStateV1)) {
        stub::tlsLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    if (document != abi_harness::kDocument && document != kSecondDocument) {
        stub::tlsLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    *out = stateFor(document);
    stub::tlsLastResult = UIREPO_RESULT_OK;
    return 1;
}

uint8_t enumerateDocuments(UIRepoDocumentEnumerateCallback callback, void* userData) noexcept {
    namespace stub = abi_harness::stub;
    if (callback == nullptr) {
        stub::tlsLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    for (const UIRepoDocumentHandle document : {abi_harness::kDocument, kSecondDocument}) {
        const UIRepoDocumentStateV1 state = stateFor(document);
        callback(document, &state, userData);
    }
    stub::tlsLastResult = UIREPO_RESULT_OK;
    return 1;
}

uint8_t isGameThread() noexcept {
    return tlsOnGameThread ? 1 : 0;
}

uint8_t dispatchToGameThread(UIRepoGameThreadCallback callback, void* userData) noexcept {
    namespace stub = abi_harness::stub;
    if (callback == nullptr) {
        stub::tlsLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    if (gQueued == kQueueLimit) {
        stub::tlsLastResult = UIREPO_RESULT_NOT_READY;
        return 0;
    }
    gQueue[gQueued++] = Queued{callback, userData};
    stub::tlsLastResult = UIREPO_RESULT_OK;
    return 1;
}

UIRepoNativeApiV2 makeTable() noexcept {
    UIRepoNativeApiV2 api{};
    abi_harness::stub::fillV1(api.v1);
    api.v1.abiVersion = UIREPO_NATIVE_API_VERSION_V2;
    api.v1.size = sizeof(UIRepoNativeApiV2);
    api.v1.capabilities |= UIREPO_CAP_DOCUMENT_STATE | UIREPO_CAP_DOCUMENT_ENUMERATION |
                           UIREPO_CAP_GAME_THREAD_DISPATCH;
    api.getDocumentState = &getDocumentState;
    api.enumerateDocuments = &enumerateDocuments;
    api.isGameThread = &isGameThread;
    api.dispatchToGameThread = &dispatchToGameThread;
    return api;
}

const UIRepoNativeApiV2 kTable = makeTable();

static_assert(sizeof(UIRepoNativeApiV2) > sizeof(UIRepoNativeApiV1));

} // namespace

/* Runs everything dispatched so far, in order, on the calling thread. */
ABI_HARNESS_EXPORT int UIRepoStub_RunGameThreadQueue() {
    tlsOnGameThread = true;
    const std::size_t count = gQueued;
    gQueued = 0;
    for (std::size_t i = 0; i < count; ++i) {
        gQueue[i].callback(gQueue[i].userData);
    }
    tlsOnGameThread = false;
    return static_cast<int>(count);
}

/* No explicit dllexport: NativeApi.h already declares this symbol exported under UIREPO_NATIVE_API_BUILD. */
extern "C" const UIRepoNativeApiV1* UIRepo_GetNativeApi(uint32_t requestedVersion) noexcept {
    if (requestedVersion == 0 || requestedVersion > UIREPO_NATIVE_API_VERSION) {
        return nullptr;
    }
    return &kTable.v1;
}
