#pragma once

/* The table body shared by both stub providers.

   These are not mocks of the framework's behaviour - they answer with fixed
   values. What they exercise is the boundary itself: that a separately
   compiled consumer can call through this table, pass structs by pointer, and
   get the expected value back. Every entry point that takes a versioned struct
   validates `size` exactly as the real implementation does, so a layout
   disagreement between independently built binaries shows up as a failure here
   rather than as corruption in the game. */

#include "AbiHarness.h"

namespace abi_harness::stub {

/* Declared before any entry point that records into it. Per-thread, the
   way the real implementation stores it. */
inline thread_local uint32_t tlsLastResult = UIREPO_RESULT_OK;


inline uint8_t rendererReady() noexcept {
    return 1;
}

inline UIRepoDocumentHandle createDocument(const char* rootId) noexcept {
    return rootId != nullptr ? kDocument : 0;
}

inline uint8_t destroyDocument(UIRepoDocumentHandle document) noexcept {
    return document == kDocument ? 1 : 0;
}

inline uint8_t setDocumentVisible(UIRepoDocumentHandle document, uint8_t) noexcept {
    return document == kDocument ? 1 : 0;
}

inline UIRepoNodeHandle getRootNode(UIRepoDocumentHandle document) noexcept {
    return document == kDocument ? kRootNode : 0;
}

inline UIRepoNodeHandle createPanel(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle parent,
    const char* id) noexcept {
    if (document != kDocument || parent != kRootNode || id == nullptr) {
        return 0;
    }
    return kPanelNode;
}

inline uint8_t setNodeStyle(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoStyleV1* style) noexcept {
    /* The exact-size rule, enforced across a real binary boundary. If the
       consumer and provider disagreed about UIRepoStyleV1's layout, this is
       where it would surface. */
    if (style == nullptr) {
        tlsLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    if (style->size != sizeof(UIRepoStyleV1)) {
        tlsLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    if (document != kDocument || node != kPanelNode) {
        tlsLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    tlsLastResult = UIREPO_RESULT_OK;
    /* Read a field from each end of the struct so a padding or ordering
       disagreement cannot slip through a size check alone. */
    return (style->width.kind == UIREPO_LENGTH_PERCENT && style->clipToBounds == 1) ? 1 : 0;
}

inline UIRepoSurfaceHandle createOffscreenSurface(
    UIRepoDocumentHandle document,
    const UIRepoSurfaceDescV1* desc) noexcept {
    if (desc == nullptr) {
        tlsLastResult = UIREPO_RESULT_INVALID_ARGUMENT;
        return 0;
    }
    if (desc->size != sizeof(UIRepoSurfaceDescV1)) {
        tlsLastResult = UIREPO_RESULT_INVALID_STRUCT_SIZE;
        return 0;
    }
    if (document != kDocument || desc->width != kSurfaceWidth || desc->height != kSurfaceHeight) {
        tlsLastResult = UIREPO_RESULT_INVALID_HANDLE;
        return 0;
    }
    tlsLastResult = UIREPO_RESULT_OK;
    return kSurface;
}

inline uint8_t destroyOffscreenSurface(UIRepoSurfaceHandle surface) noexcept {
    return surface == kSurface ? 1 : 0;
}

inline uint8_t renderDocumentToSurface(
    UIRepoDocumentHandle document,
    UIRepoSurfaceHandle surface) noexcept {
    return (document == kDocument && surface == kSurface) ? 1 : 0;
}

inline uint8_t getOffscreenSurfaceInfo(
    UIRepoSurfaceHandle surface,
    UIRepoSurfaceInfoV1* out) noexcept {
    if (out == nullptr || out->size != sizeof(UIRepoSurfaceInfoV1) || surface != kSurface) {
        return 0;
    }
    out->width = kSurfaceWidth;
    out->height = kSurfaceHeight;
    out->clearColor = UIRepoColorV1{0, 0, 0, 0};
    out->generation = kSurfaceGeneration;
    out->byteSize = static_cast<uint64_t>(kSurfaceWidth) * kSurfaceHeight * 4u;
    return 1;
}

/* Result codes, recorded the way the real implementation records them:
   per-thread, cleared on success, readable immediately after a failure.

   Proving this across the harness matters because the storage lives in the
   provider module while the reader lives in a separately compiled consumer -
   a arrangement no in-process test exercises. */
inline uint32_t getLastResult() noexcept {
    return tlsLastResult;
}

inline const char* describeResult(uint32_t code) noexcept {
    switch (code) {
    case UIREPO_RESULT_OK:
        return "ok";
    case UIREPO_RESULT_INVALID_ARGUMENT:
        return "invalid argument";
    case UIREPO_RESULT_INVALID_STRUCT_SIZE:
        return "struct size field does not match the published struct";
    case UIREPO_RESULT_INVALID_HANDLE:
        return "unknown, stale, or foreign handle";
    default:
        /* Never null, including for a code a newer consumer knows and this
           provider does not. */
        return "unrecognised result code";
    }
}

inline constexpr uint64_t kCapabilities =
    UIREPO_CAP_RETAINED_DOCUMENTS |
    UIREPO_CAP_NATIVE_D3D11 |
    UIREPO_CAP_OFFSCREEN_SURFACES |
    UIREPO_CAP_RESULT_CODES;

/* Fills the V1 prefix. Both providers share this; they differ only in the
   version they claim and the size of the table they hand out. */
inline void fillV1(UIRepoNativeApiV1& api) noexcept {
    api.capabilities = kCapabilities;
    api.isRendererReady = &rendererReady;
    api.createDocument = &createDocument;
    api.destroyDocument = &destroyDocument;
    api.setDocumentVisible = &setDocumentVisible;
    api.getRootNode = &getRootNode;
    api.createPanel = &createPanel;
    api.setNodeStyle = &setNodeStyle;
    api.createOffscreenSurface = &createOffscreenSurface;
    api.destroyOffscreenSurface = &destroyOffscreenSurface;
    api.renderDocumentToSurface = &renderDocumentToSurface;
    api.getOffscreenSurfaceInfo = &getOffscreenSurfaceInfo;
    api.getLastResult = &getLastResult;
    api.describeResult = &describeResult;
}

} // namespace abi_harness::stub
