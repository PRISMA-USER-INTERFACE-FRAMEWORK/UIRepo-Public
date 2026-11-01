/* Consumer #1: documents and nodes.

   Built as its own binary against the SDK headers only. It never sees the
   provider's sources, and the harness hands it a resolver rather than letting
   it look one up, so two consumers can share one process without either
   knowing the other exists. */

#include "AbiHarness.h"
#include "uirepo/NativeApiLoader.h"

ABI_HARNESS_EXPORT int UIRepoConsumer_Run(UIRepoResolverFn resolve) {
    if (resolve == nullptr) {
        return 1;
    }

    const auto api = uirepo::sdk::Api::adopt(resolve(UIREPO_NATIVE_API_VERSION_V1));
    if (!api || api.status() != uirepo::sdk::LoadStatus::Ok) {
        return 2;
    }
    if (!api.has(UIREPO_CAP_RETAINED_DOCUMENTS)) {
        return 3;
    }

    const UIRepoDocumentHandle document = api->createDocument("consumer-documents");
    if (document != abi_harness::kDocument) {
        return 4;
    }
    if (api->setDocumentVisible(document, 0) == 0) {
        return 5;
    }

    const UIRepoNodeHandle root = api->getRootNode(document);
    if (root != abi_harness::kRootNode) {
        return 6;
    }

    const UIRepoNodeHandle panel = api->createPanel(document, root, "consumer-panel");
    if (panel != abi_harness::kPanelNode) {
        return 7;
    }

    /* Passing a struct across the boundary is the part most likely to break
       between independently compiled binaries. */
    UIRepoStyleV1 style{};
    style.size = sizeof(style);
    style.width.kind = UIREPO_LENGTH_PERCENT;
    style.width.value = 100.0f;
    style.height.kind = UIREPO_LENGTH_PIXELS;
    style.height.value = 48.0f;
    style.background = UIRepoColorV1{10, 20, 30, 255};
    style.opacity = 1.0f;
    style.clipToBounds = 1;
    if (api->setNodeStyle(document, panel, &style) == 0) {
        return 8;
    }

    if (!api.has(UIREPO_CAP_RESULT_CODES)) {
        return 9;
    }
    /* Success must clear a previously recorded failure, read from a module
       that is not the one that recorded it. */
    if (api->getLastResult() != UIREPO_RESULT_OK) {
        return 10;
    }

    /* A deliberately wrong size must be refused, not tolerated - and the
       refusal must be classifiable, not just a bare zero. */
    UIRepoStyleV1 bad = style;
    bad.size = sizeof(bad) - 1;
    if (api->setNodeStyle(document, panel, &bad) != 0) {
        return 11;
    }
    if (api->getLastResult() != UIREPO_RESULT_INVALID_STRUCT_SIZE) {
        return 12;
    }

    /* A null argument is a different cause and must report as one. */
    if (api->setNodeStyle(document, panel, nullptr) != 0) {
        return 13;
    }
    if (api->getLastResult() != UIREPO_RESULT_INVALID_ARGUMENT) {
        return 14;
    }

    /* describeResult never returns null, including for a code this provider
       has never heard of. */
    const char* known = api->describeResult(UIREPO_RESULT_INVALID_STRUCT_SIZE);
    const char* unknown = api->describeResult(0xFFFFFFFFu);
    if (known == nullptr || known[0] == '\0') {
        return 15;
    }
    if (unknown == nullptr || unknown[0] == '\0') {
        return 16;
    }

    if (api->destroyDocument(document) == 0) {
        return 17;
    }
    return 0;
}
