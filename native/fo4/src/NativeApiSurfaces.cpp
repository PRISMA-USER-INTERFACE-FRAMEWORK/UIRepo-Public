#include "DocumentRegistry.h"
#include "GameThreadDispatcher.h"
#include "NativeApiConvert.h"
#include "NativeApiEntries.h"
#include "Runtime.h"

#include "uirepo/NativeApi.h"

#include "ApiResult.h"

#include <cstddef>
#include <cstdint>

namespace uirepo::fo4::native_api {
namespace {

/* Classifies why an offscreen call is going to be refused, for the causes this
   layer can determine on its own: the thread it was called on, whether the
   renderer is up, and whether the surface handle is known. Anything it cannot
   see - a budget refusal, a failed GPU allocation - stays unclassified and is
   reported by the caller as a resource failure. */
[[nodiscard]] UIRepoResultV1 classifyOffscreenPrecondition(
    registry::SurfaceHandle surface,
    bool requireSurface) noexcept {
    if (!GameThreadDispatcher::isGameThread()) {
        return UIREPO_RESULT_WRONG_THREAD;
    }
    if (!Runtime::instance().rendererReady()) {
        return UIREPO_RESULT_NOT_READY;
    }
    if (requireSurface && DocumentRegistry::instance().surfaceDocument(surface) == 0) {
        return UIREPO_RESULT_INVALID_HANDLE;
    }
    return UIREPO_RESULT_OK;
}

} // namespace

[[nodiscard]] UIRepoSurfaceHandle apiCreateOffscreenSurface(
    UIRepoDocumentHandle document,
    const UIRepoSurfaceDescV1* desc) noexcept {
    if (desc == nullptr) {
        return api::refuseHandle(UIREPO_RESULT_INVALID_ARGUMENT);
    }
    if (desc->size != sizeof(UIRepoSurfaceDescV1)) {
        return api::refuseHandle(UIREPO_RESULT_INVALID_STRUCT_SIZE);
    }

    const UIRepoResultV1 precondition = classifyOffscreenPrecondition(0, false);
    if (precondition != UIREPO_RESULT_OK) {
        return api::refuseHandle(precondition);
    }

    try {
        DocumentRegistry::DocumentState state{};
        if (!DocumentRegistry::instance().documentState(document, state)) {
            return api::refuseHandle(UIREPO_RESULT_INVALID_HANDLE);
        }

        const UIRepoSurfaceHandle surface = Runtime::instance().createOffscreenSurface(
            document,
            desc->width,
            desc->height,
            makeColor(desc->clearColor));
        if (surface == 0) {
            /* Preconditions held and the document exists, so the refusal came
               from admission: a dimension cap, the aggregate live budget, or a
               GPU allocation that failed. */
            return api::refuseHandle(UIREPO_RESULT_BUDGET_EXCEEDED);
        }
        api::setResult(UIREPO_RESULT_OK);
        return surface;
    } catch (...) {
        return api::refuseHandle(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiDestroyOffscreenSurface(UIRepoSurfaceHandle surface) noexcept {
    try {
        return result(
            Runtime::instance().destroyOffscreenSurface(surface),
            UIREPO_RESULT_INVALID_HANDLE);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiResizeOffscreenSurface(
    UIRepoSurfaceHandle surface,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    const UIRepoResultV1 precondition = classifyOffscreenPrecondition(surface, true);
    if (precondition != UIREPO_RESULT_OK) {
        return api::refuse(precondition);
    }

    try {
        return result(
            Runtime::instance().resizeOffscreenSurface(surface, width, height),
            UIREPO_RESULT_BUDGET_EXCEEDED);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiSetOffscreenSurfaceClearColor(
    UIRepoSurfaceHandle surface,
    UIRepoColorV1 color) noexcept {
    try {
        return result(
            Runtime::instance().setOffscreenSurfaceClearColor(surface, makeColor(color)),
            UIREPO_RESULT_INVALID_HANDLE);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiRenderDocumentToSurface(
    UIRepoDocumentHandle document,
    UIRepoSurfaceHandle surface) noexcept {
    const UIRepoResultV1 precondition = classifyOffscreenPrecondition(surface, true);
    if (precondition != UIREPO_RESULT_OK) {
        return api::refuse(precondition);
    }

    try {
        if (DocumentRegistry::instance().surfaceDocument(surface) != document) {
            return api::refuse(UIREPO_RESULT_INVALID_HANDLE);
        }

        DocumentRegistry::DocumentState state{};
        if (!DocumentRegistry::instance().documentState(document, state)) {
            return api::refuse(UIREPO_RESULT_INVALID_HANDLE);
        }
        if (state.visible || state.inputEnabled) {
            /* Offscreen layout would rewrite the hit-test geometry this
               document is using on the flat HUD. */
            return api::refuse(UIREPO_RESULT_STATE_CONFLICT);
        }

        return result(
            Runtime::instance().renderDocumentToSurface(document, surface, nullptr),
            UIREPO_RESULT_RESOURCE_FAILED);
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiGetOffscreenSurfaceInfo(
    UIRepoSurfaceHandle surface,
    UIRepoSurfaceInfoV1* out) noexcept {
    if (out == nullptr) {
        return api::refuse(UIREPO_RESULT_INVALID_ARGUMENT);
    }
    if (out->size != sizeof(UIRepoSurfaceInfoV1)) {
        return api::refuse(UIREPO_RESULT_INVALID_STRUCT_SIZE);
    }

    try {
        D3D11SurfaceInfo info{};
        if (!Runtime::instance().offscreenSurfaceInfo(surface, info)) {
            const UIRepoResultV1 precondition = classifyOffscreenPrecondition(surface, true);
            return api::refuse(
                precondition != UIREPO_RESULT_OK ? precondition : UIREPO_RESULT_INVALID_HANDLE);
        }

        out->width = info.width;
        out->height = info.height;
        out->clearColor = UIRepoColorV1{
            info.clearColor.r,
            info.clearColor.g,
            info.clearColor.b,
            info.clearColor.a,
        };
        out->generation = info.generation;
        out->byteSize = static_cast<std::uint64_t>(info.bytes);
        return 1;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint8_t apiGetOffscreenSurfaceStats(UIRepoSurfaceStatsV1* out) noexcept {
    if (out == nullptr) {
        return api::refuse(UIREPO_RESULT_INVALID_ARGUMENT);
    }
    if (out->size != sizeof(UIRepoSurfaceStatsV1)) {
        return api::refuse(UIREPO_RESULT_INVALID_STRUCT_SIZE);
    }

    try {
        const D3D11SurfaceStats stats = Runtime::instance().offscreenSurfaceStats();
        out->reserved = 0;
        out->liveCount = static_cast<std::uint64_t>(stats.liveCount);
        out->liveBytes = static_cast<std::uint64_t>(stats.liveBytes);
        out->pooledCount = static_cast<std::uint64_t>(stats.pooledCount);
        out->pooledBytes = static_cast<std::uint64_t>(stats.pooledBytes);
        out->allocations = stats.allocations;
        out->reuses = stats.reuses;
        out->resizes = stats.resizes;
        out->deviceRebuilds = stats.deviceRebuilds;
        out->rejections = stats.rejections;
        out->poolDrains = stats.poolDrains;
        return 1;
    } catch (...) {
        return api::refuse(UIREPO_RESULT_OUT_OF_MEMORY);
    }
}

[[nodiscard]] std::uint32_t apiGetLastResult() noexcept {
    return static_cast<std::uint32_t>(api::lastResult());
}

[[nodiscard]] const char* apiDescribeResult(std::uint32_t code) noexcept {
    return api::describeResult(code);
}

} // namespace uirepo::fo4::native_api
