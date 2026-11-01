/* Consumer #3: the v2 entries, built against the SDK headers only. */

#include "AbiHarness.h"
#include "uirepo/NativeApiLoader.h"

namespace {

constexpr int kQueuedCallbacks = 4;

const UIRepoNativeApiV2* gApi = nullptr;
int gRan = 0;
int gOrder[kQueuedCallbacks] = {};
bool gAllOnGameThread = true;

struct Seen {
    UIRepoDocumentHandle documents[4];
    UIRepoDocumentStateV1 states[4];
    int count;
};

void collect(UIRepoDocumentHandle document, const UIRepoDocumentStateV1* state, void* userData) {
    auto* seen = static_cast<Seen*>(userData);
    if (seen->count < 4) {
        seen->documents[seen->count] = document;
        seen->states[seen->count] = *state;
    }
    ++seen->count;
}

void onGameThread(void* userData) {
    const int index = *static_cast<int*>(userData);
    if (gRan < kQueuedCallbacks) {
        gOrder[gRan] = index;
    }
    ++gRan;
    if (gApi == nullptr || gApi->isGameThread() == 0) {
        gAllOnGameThread = false;
    }
}

int indices[kQueuedCallbacks] = {1, 2, 3, 4};

} // namespace

ABI_HARNESS_EXPORT int UIRepoConsumer_Run(UIRepoResolverFn resolve) {
    if (resolve == nullptr) {
        return 1;
    }

    const auto api = uirepo::sdk::Api::adoptV2(resolve(UIREPO_NATIVE_API_VERSION_V2));
    if (api.status() != uirepo::sdk::LoadStatus::Ok) {
        return 2;
    }
    if (!api.has(UIREPO_CAP_DOCUMENT_STATE) || !api.has(UIREPO_CAP_DOCUMENT_ENUMERATION) ||
        !api.has(UIREPO_CAP_GAME_THREAD_DISPATCH)) {
        return 3;
    }
    const UIRepoNativeApiV2* table = api.tableV2();
    if (table == nullptr || table->getDocumentState == nullptr || table->enumerateDocuments == nullptr ||
        table->isGameThread == nullptr || table->dispatchToGameThread == nullptr) {
        return 4;
    }
    gApi = table;
    gRan = 0;
    gAllOnGameThread = true;

    /* The V1 prefix of the same table still answers V1 calls. */
    if (table->v1.createDocument("consumer-state") != abi_harness::kDocument) {
        return 5;
    }

    UIRepoDocumentStateV1 state{};
    state.size = sizeof(state);
    if (table->getDocumentState(abi_harness::kDocument, &state) == 0) {
        return 6;
    }
    if (state.visible != 1 || state.inputEnabled != 0 || state.focused != 0 ||
        state.health != UIREPO_DOCUMENT_HEALTH_LIVE || state.role != UIREPO_DOCUMENT_ROLE_PANEL ||
        state.order != 7 || state.ownerId != 1234567u || state.generation != 3u) {
        return 7;
    }
    if (table->v1.getLastResult() != UIREPO_RESULT_OK) {
        return 8;
    }

    UIRepoDocumentStateV1 wrongSize = state;
    wrongSize.size = sizeof(wrongSize) - 1;
    if (table->getDocumentState(abi_harness::kDocument, &wrongSize) != 0 ||
        table->v1.getLastResult() != UIREPO_RESULT_INVALID_STRUCT_SIZE) {
        return 9;
    }
    if (table->getDocumentState(0xDEADu, &state) != 0 ||
        table->v1.getLastResult() != UIREPO_RESULT_INVALID_HANDLE) {
        return 10;
    }
    if (table->getDocumentState(abi_harness::kDocument, nullptr) != 0 ||
        table->v1.getLastResult() != UIREPO_RESULT_INVALID_ARGUMENT) {
        return 11;
    }

    Seen seen{};
    if (table->enumerateDocuments(&collect, &seen) == 0 || seen.count != 2) {
        return 12;
    }
    if (seen.documents[0] != abi_harness::kDocument || seen.documents[1] != abi_harness::kDocument + 1) {
        return 13;
    }
    if (seen.states[1].order != -3 || seen.states[1].health != UIREPO_DOCUMENT_HEALTH_HIDDEN ||
        seen.states[1].inputEnabled != 1 || seen.states[1].focused != 1 || seen.states[1].size != sizeof(state)) {
        return 14;
    }
    if (table->enumerateDocuments(nullptr, nullptr) != 0 ||
        table->v1.getLastResult() != UIREPO_RESULT_INVALID_ARGUMENT) {
        return 15;
    }

    /* The result a failed call left behind survives a pure query. */
    if (table->isGameThread() != 0 || table->v1.getLastResult() != UIREPO_RESULT_INVALID_ARGUMENT) {
        return 16;
    }

    for (int i = 0; i < kQueuedCallbacks; ++i) {
        if (table->dispatchToGameThread(&onGameThread, &indices[i]) == 0) {
            return 17;
        }
    }
    if (gRan != 0) {
        return 18;
    }
    if (table->dispatchToGameThread(&onGameThread, &indices[0]) != 0 ||
        table->v1.getLastResult() != UIREPO_RESULT_NOT_READY) {
        return 19;
    }
    if (table->dispatchToGameThread(nullptr, nullptr) != 0 ||
        table->v1.getLastResult() != UIREPO_RESULT_INVALID_ARGUMENT) {
        return 20;
    }
    return 0;
}

/* Called after the driver has run the provider's game-thread queue. */
ABI_HARNESS_EXPORT int UIRepoConsumer_VerifyDispatched() {
    if (gRan != kQueuedCallbacks) {
        return 1;
    }
    for (int i = 0; i < kQueuedCallbacks; ++i) {
        if (gOrder[i] != i + 1) {
            return 2;
        }
    }
    return gAllOnGameThread ? 0 : 3;
}

/* Against a v1-only provider the v2 request must be refused, not served. */
ABI_HARNESS_EXPORT int UIRepoConsumer_ExpectRefused(UIRepoResolverFn resolve) {
    if (resolve == nullptr) {
        return 1;
    }
    if (uirepo::sdk::Api::adoptV2(resolve(UIREPO_NATIVE_API_VERSION_V2)).status() !=
        uirepo::sdk::LoadStatus::VersionRefused) {
        return 2;
    }
    /* A v1 table handed over anyway is refused as well. */
    const auto v1 = uirepo::sdk::Api::adoptV2(resolve(UIREPO_NATIVE_API_VERSION_V1));
    if (v1.status() != uirepo::sdk::LoadStatus::VersionRefused || static_cast<bool>(v1)) {
        return 3;
    }
    return 0;
}
