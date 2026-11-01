#pragma once

/* Header-only loader for the UIRepo native API.

   This is convenience, not contract. Everything here is written purely against
   the public C ABI in NativeApi.h, and a consumer that would rather resolve the
   entry point itself loses nothing by ignoring this file.

   It exists because three mistakes are easy to make by hand and all three fail
   confusingly at runtime rather than at load: calling LoadLibrary on a module
   the host has already loaded, assuming a non-null table means a feature is
   present, and forgetting that a newer framework returns a larger table.

   Requires C++17. Windows only - the framework is a Fallout 4 plugin.

   INCLUDE ORDER: this header includes <windows.h>. CommonLibF4 refuses to be
   included after the Windows API, so a plugin using it must include CommonLib
   first. NativeApi.h on its own has no such constraint - it pulls in nothing
   but stdint.h and stddef.h - so resolving the entry point by hand avoids the
   question entirely. */

#include "uirepo/NativeApi.h"

#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#endif

namespace uirepo::sdk {

enum class LoadStatus {
    Ok,
    /* The framework plugin is not loaded in this process. Usually means the
       consumer ran before UIRepo_F4.dll, or it is not installed. */
    FrameworkNotLoaded,
    /* The module is present but does not export UIRepo_GetNativeApi. Almost
       always a different DLL of the same name. */
    EntryPointMissing,
    /* The framework is older than this consumer was built against and refused
       the requested version. Nothing to negotiate: rebuild or ship a fallback. */
    VersionRefused,
    /* The table is smaller than the one this consumer compiled against, so
       entries it knows about may not exist. Refused rather than risked. */
    TableTooSmall,
};

/* Provider presence and renderer readiness are deliberately separate.

   Unavailable means no compatible API table was accepted. Degraded means the
   provider is present but its renderer is not currently ready; this is normal
   during startup and renderer/device recovery. Ready means renderer-dependent
   work can proceed now.

   A degraded provider is not an absent provider. Retained document/node
   handles remain owned by UIRepo across renderer recovery and should not be
   destroyed/rebuilt merely because this state is Degraded. */
enum class ProviderHealth {
    Unavailable,
    Degraded,
    Ready,
};

class Api final {
public:
    Api() noexcept = default;

    [[nodiscard]] const UIRepoNativeApiV1* table() const noexcept { return table_; }
    [[nodiscard]] LoadStatus status() const noexcept { return status_; }
    [[nodiscard]] explicit operator bool() const noexcept { return table_ != nullptr; }
    [[nodiscard]] const UIRepoNativeApiV1* operator->() const noexcept { return table_; }

    /* Feature presence is advertised by capability bits, never inferred from
       the version number: a capability can be compiled out or unavailable on a
       given runtime while the version stays the same. Always test before use. */
    [[nodiscard]] bool has(UIRepoNativeCapabilityV1 capability) const noexcept {
        return table_ != nullptr &&
               (table_->capabilities & static_cast<uint64_t>(capability)) != 0u;
    }

    /* Provider lifecycle state derived entirely from the existing V1 contract.

       This intentionally does not add another ABI entry. If the table exists,
       the provider exists. isRendererReady() then answers whether renderer-
       dependent work can run at this instant.

       isRendererReady() is itself an API call, so like any call it may replace
       getLastResult() on the current thread. Capture a failure result before
       asking for health. */
    [[nodiscard]] ProviderHealth health() const noexcept {
        if (table_ == nullptr) {
            return ProviderHealth::Unavailable;
        }
        if (table_->isRendererReady == nullptr || table_->isRendererReady() == 0) {
            return ProviderHealth::Degraded;
        }
        return ProviderHealth::Ready;
    }

    /* The framework this consumer is actually talking to, which may be newer
       than UIREPO_NATIVE_API_VERSION. Useful for logging; branch on has(). */
    [[nodiscard]] uint32_t providerVersion() const noexcept {
        return table_ != nullptr ? table_->abiVersion : 0u;
    }

    /* Null for a V1-only table; still test the capability bit before each V2 entry. */
    [[nodiscard]] const UIRepoNativeApiV2* tableV2() const noexcept {
        if (table_ == nullptr ||
            table_->abiVersion < UIREPO_NATIVE_API_VERSION_V2 ||
            table_->size < sizeof(UIRepoNativeApiV2)) {
            return nullptr;
        }
        return reinterpret_cast<const UIRepoNativeApiV2*>(table_);
    }

    /* Validates a table the caller resolved itself.

       Resolving `UIRepo_GetNativeApi` by hand is a supported way to consume
       this ABI, and doing so should not mean giving up the checks. Pass the
       result here and inspect status() exactly as if loadNativeApi() had been
       used. */
    [[nodiscard]] static Api adopt(const UIRepoNativeApiV1* table) noexcept {
        Api api;
        if (table == nullptr) {
            api.status_ = LoadStatus::VersionRefused;
            return api;
        }

        /* A newer framework returns a larger table and that is fine - this
           consumer reads only the prefix it knows. A smaller one is not: it
           would mean reading entries that do not exist. */
        if (table->size < sizeof(UIRepoNativeApiV1)) {
            api.status_ = LoadStatus::TableTooSmall;
            return api;
        }

        api.table_ = table;
        api.status_ = LoadStatus::Ok;
        return api;
    }

    /* Like adopt() for a V2 consumer: a V1-only table is VersionRefused, a short V2 table TableTooSmall. */
    [[nodiscard]] static Api adoptV2(const UIRepoNativeApiV1* table) noexcept {
        Api api = adopt(table);
        if (api.table_ == nullptr) {
            return api;
        }
        if (api.table_->abiVersion < UIREPO_NATIVE_API_VERSION_V2) {
            return Api{LoadStatus::VersionRefused};
        }
        if (api.table_->size < sizeof(UIRepoNativeApiV2)) {
            return Api{LoadStatus::TableTooSmall};
        }
        return api;
    }

private:
    explicit Api(LoadStatus status) noexcept : status_(status) {}

    /* Declared unconditionally so the class does not change shape by platform.
       The function itself only exists on Windows. */
    friend Api loadNativeApi(const wchar_t* moduleName, uint32_t requestedVersion) noexcept;

    const UIRepoNativeApiV1* table_{};
    LoadStatus status_{LoadStatus::FrameworkNotLoaded};
};

#ifdef _WIN32

/* Resolves the API from a module the host has already loaded.

   Deliberately GetModuleHandleW and not LoadLibraryW: the framework is loaded
   by F4SE, and a consumer taking its own reference would change the module's
   unload ordering for everyone. If the module is not there yet, that is a
   sequencing problem to fix in the caller, not something to paper over. */
[[nodiscard]] inline Api loadNativeApi(
    const wchar_t* moduleName = L"UIRepo_F4.dll",
    uint32_t requestedVersion = UIREPO_NATIVE_API_VERSION_V1) noexcept {
    Api api;

    const HMODULE module = ::GetModuleHandleW(moduleName);
    if (module == nullptr) {
        api.status_ = LoadStatus::FrameworkNotLoaded;
        return api;
    }

    using Resolver = const UIRepoNativeApiV1* (*)(uint32_t);
    const auto resolver = reinterpret_cast<Resolver>(
        reinterpret_cast<void*>(::GetProcAddress(module, "UIRepo_GetNativeApi")));
    if (resolver == nullptr) {
        api.status_ = LoadStatus::EntryPointMissing;
        return api;
    }

    const UIRepoNativeApiV1* table = resolver(requestedVersion);
    return requestedVersion >= UIREPO_NATIVE_API_VERSION_V2 ? Api::adoptV2(table) : Api::adopt(table);
}

#endif

[[nodiscard]] inline const char* describe(LoadStatus status) noexcept {
    switch (status) {
    case LoadStatus::Ok:
        return "ok";
    case LoadStatus::FrameworkNotLoaded:
        return "UIRepo framework plugin is not loaded in this process";
    case LoadStatus::EntryPointMissing:
        return "module does not export UIRepo_GetNativeApi";
    case LoadStatus::VersionRefused:
        return "framework is older than this consumer requires";
    case LoadStatus::TableTooSmall:
        return "framework returned a smaller API table than this consumer expects";
    }
    return "unknown";
}

[[nodiscard]] inline const char* describe(ProviderHealth health) noexcept {
    switch (health) {
    case ProviderHealth::Unavailable:
        return "provider unavailable";
    case ProviderHealth::Degraded:
        return "provider loaded; renderer not ready";
    case ProviderHealth::Ready:
        return "provider ready";
    }
    return "unknown";
}

} // namespace uirepo::sdk
