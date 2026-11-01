#pragma once

/* Result-code storage for the native ABI.

   The published entry points return 0/1 or a zero handle and always will -
   that shape is frozen. This records *why* alongside the boolean, so a
   consumer can tell "wrong thread" from "budget exceeded" from "stale handle"
   without six new entry points.

   Per-thread rather than global: a failure recorded by one thread must never
   be readable as another thread's. There is no synchronisation here and none
   is needed.

   Deliberately free of Windows and D3D11 dependencies so the semantics can be
   exercised by a portable regression. */

#include "uirepo/NativeApi.h"

#include <cstdint>

namespace uirepo::fo4::api {

inline thread_local UIRepoResultV1 tlsLastResult = UIREPO_RESULT_OK;

inline void setResult(UIRepoResultV1 code) noexcept {
    tlsLastResult = code;
}

[[nodiscard]] inline UIRepoResultV1 lastResult() noexcept {
    return tlsLastResult;
}

/* Records success and yields the ABI's true value. */
[[nodiscard]] inline std::uint8_t succeed() noexcept {
    tlsLastResult = UIREPO_RESULT_OK;
    return 1u;
}

/* Records why and yields the ABI's false value. */
[[nodiscard]] inline std::uint8_t refuse(UIRepoResultV1 code) noexcept {
    tlsLastResult = code;
    return 0u;
}

/* Same, for entry points whose failure value is a zero handle. */
[[nodiscard]] inline std::uint64_t refuseHandle(UIRepoResultV1 code) noexcept {
    tlsLastResult = code;
    return 0u;
}

/* Bridges a bool-returning internal call. `failureCode` is what gets recorded
   when the call reports false; UIREPO_RESULT_UNKNOWN is the honest answer
   where the cause is decided deeper than this layer can see. */
[[nodiscard]] inline std::uint8_t report(bool value, UIRepoResultV1 failureCode) noexcept {
    tlsLastResult = value ? UIREPO_RESULT_OK : failureCode;
    return value ? 1u : 0u;
}

[[nodiscard]] inline const char* describeResult(std::uint32_t code) noexcept {
    switch (static_cast<UIRepoResultV1>(code)) {
    case UIREPO_RESULT_OK:
        return "ok";
    case UIREPO_RESULT_UNKNOWN:
        return "refused, cause not classified";
    case UIREPO_RESULT_INVALID_ARGUMENT:
        return "invalid argument";
    case UIREPO_RESULT_INVALID_STRUCT_SIZE:
        return "struct size field does not match the published struct";
    case UIREPO_RESULT_INVALID_HANDLE:
        return "unknown, stale, or foreign handle";
    case UIREPO_RESULT_WRONG_THREAD:
        return "called off the game render thread";
    case UIREPO_RESULT_NOT_READY:
        return "renderer or device is not ready yet";
    case UIREPO_RESULT_UNSUPPORTED:
        return "capability not present in this build";
    case UIREPO_RESULT_BUDGET_EXCEEDED:
        return "a hard resource budget refused the request";
    case UIREPO_RESULT_RESOURCE_FAILED:
        return "GPU resource creation or device operation failed";
    case UIREPO_RESULT_STATE_CONFLICT:
        return "target is in a state this call forbids";
    case UIREPO_RESULT_OUT_OF_MEMORY:
        return "allocation failed";
    }
    /* A consumer built against a newer header can hand us a code this build
       has never heard of. Never return null. */
    return "unrecognised result code";
}

} // namespace uirepo::fo4::api
