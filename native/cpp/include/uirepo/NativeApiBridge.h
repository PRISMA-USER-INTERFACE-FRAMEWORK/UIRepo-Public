#pragma once

#include "uirepo/NativeApi.h"
#include "uirepo/RegistryTypes.h"

#include <cstdint>
#include <functional>
#include <new>
#include <vector>

namespace uirepo::native_bridge {

using Health = registry::DocumentHealth;
using Role = registry::DocumentRole;

// The ABI enum values are the registry enum values; a registry reorder must fail the build.
static_assert(UIREPO_DOCUMENT_HEALTH_CREATING == static_cast<std::uint32_t>(Health::Creating));
static_assert(UIREPO_DOCUMENT_HEALTH_READY == static_cast<std::uint32_t>(Health::Ready));
static_assert(UIREPO_DOCUMENT_HEALTH_LIVE == static_cast<std::uint32_t>(Health::Live));
static_assert(UIREPO_DOCUMENT_HEALTH_HIDDEN == static_cast<std::uint32_t>(Health::Hidden));
static_assert(UIREPO_DOCUMENT_HEALTH_FAULTED == static_cast<std::uint32_t>(Health::Faulted));
static_assert(UIREPO_DOCUMENT_HEALTH_DESTROYING == static_cast<std::uint32_t>(Health::Destroying));
static_assert(UIREPO_DOCUMENT_HEALTH_DESTROYED == static_cast<std::uint32_t>(Health::Destroyed));
static_assert(UIREPO_DOCUMENT_ROLE_HUD == static_cast<std::uint32_t>(Role::HUD));
static_assert(UIREPO_DOCUMENT_ROLE_PANEL == static_cast<std::uint32_t>(Role::Panel));
static_assert(UIREPO_DOCUMENT_ROLE_OVERLAY == static_cast<std::uint32_t>(Role::Overlay));
static_assert(UIREPO_DOCUMENT_ROLE_WORLD_SURFACE == static_cast<std::uint32_t>(Role::WorldSurface));
static_assert(UIREPO_DOCUMENT_ROLE_DIAGNOSTIC == static_cast<std::uint32_t>(Role::Diagnostic));

[[nodiscard]] constexpr UIRepoDocumentStateV1 makeDocumentState(
    const registry::DocumentState& state) noexcept {
    UIRepoDocumentStateV1 out{};
    out.size = sizeof(UIRepoDocumentStateV1);
    out.visible = state.visible ? 1u : 0u;
    out.inputEnabled = state.inputEnabled ? 1u : 0u;
    out.focused = state.focused ? 1u : 0u;
    out.health = static_cast<std::uint32_t>(state.health);
    out.role = static_cast<std::uint32_t>(state.role);
    out.order = state.order;
    out.ownerId = state.owner;
    out.generation = state.generation;
    return out;
}

// `snapshot` returns the states by value and must take its own lock; callbacks run after it returns.
template <class Snapshot>
[[nodiscard]] bool enumerateSnapshot(
    Snapshot&& snapshot,
    UIRepoDocumentEnumerateCallback callback,
    void* userData) {
    if (callback == nullptr) {
        return false;
    }
    const std::vector<registry::DocumentState> states = snapshot();
    for (const registry::DocumentState& state : states) {
        const UIRepoDocumentStateV1 abi = makeDocumentState(state);
        callback(state.handle, &abi, userData);
    }
    return true;
}

enum class DispatchStatus : std::uint8_t {
    Queued,
    NullCallback,
    Refused,
};

// `dispatch` takes a std::function<void()> and returns whether it was queued; it must never run it inline.
template <class Dispatch>
[[nodiscard]] DispatchStatus bridgeDispatch(
    UIRepoGameThreadCallback callback,
    void* userData,
    Dispatch&& dispatch) {
    if (callback == nullptr) {
        return DispatchStatus::NullCallback;
    }
    return dispatch(std::function<void()>([callback, userData] { callback(userData); }))
        ? DispatchStatus::Queued
        : DispatchStatus::Refused;
}

// Call only from inside a catch handler.
[[nodiscard]] inline UIRepoResultV1 resultForActiveException() noexcept {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return UIREPO_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return UIREPO_RESULT_UNKNOWN;
    }
}

} // namespace uirepo::native_bridge
