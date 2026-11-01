/* Consumer #2: offscreen surfaces.

   A separate binary from consumer #1, exercising a different area of the same
   table. Both run against the same provider in one process. */

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

    /* Optional area: test the bit, never infer it from the version. */
    if (!api.has(UIREPO_CAP_OFFSCREEN_SURFACES)) {
        return 3;
    }

    const UIRepoDocumentHandle document = api->createDocument("consumer-surfaces");
    if (document != abi_harness::kDocument) {
        return 4;
    }

    UIRepoSurfaceDescV1 desc{};
    desc.size = sizeof(desc);
    desc.width = abi_harness::kSurfaceWidth;
    desc.height = abi_harness::kSurfaceHeight;
    desc.clearColor = UIRepoColorV1{0, 0, 0, 0};

    const UIRepoSurfaceHandle surface = api->createOffscreenSurface(document, &desc);
    if (surface != abi_harness::kSurface) {
        return 5;
    }

    UIRepoSurfaceDescV1 bad = desc;
    bad.size = sizeof(bad) + 4;
    if (api->createOffscreenSurface(document, &bad) != 0) {
        return 6;
    }
    /* A zero handle is ambiguous on its own; the code is what makes it
       actionable. Read across the module boundary from the provider that
       recorded it. */
    if (api.has(UIREPO_CAP_RESULT_CODES) &&
        api->getLastResult() != UIREPO_RESULT_INVALID_STRUCT_SIZE) {
        return 14;
    }

    /* A wrong-document handle is a different cause entirely. */
    if (api->createOffscreenSurface(document + 1, &desc) != 0) {
        return 15;
    }
    if (api.has(UIREPO_CAP_RESULT_CODES) &&
        api->getLastResult() != UIREPO_RESULT_INVALID_HANDLE) {
        return 16;
    }

    if (api->renderDocumentToSurface(document, surface) == 0) {
        return 7;
    }

    /* An out-parameter struct crosses the boundary in the other direction. */
    UIRepoSurfaceInfoV1 info{};
    info.size = sizeof(info);
    if (api->getOffscreenSurfaceInfo(surface, &info) == 0) {
        return 8;
    }
    if (info.width != abi_harness::kSurfaceWidth || info.height != abi_harness::kSurfaceHeight) {
        return 9;
    }
    if (info.generation != abi_harness::kSurfaceGeneration) {
        return 10;
    }
    if (info.byteSize != static_cast<uint64_t>(abi_harness::kSurfaceWidth) * abi_harness::kSurfaceHeight * 4u) {
        return 11;
    }

    if (api->destroyOffscreenSurface(surface) == 0) {
        return 12;
    }
    if (api->destroyDocument(document) == 0) {
        return 13;
    }
    return 0;
}
