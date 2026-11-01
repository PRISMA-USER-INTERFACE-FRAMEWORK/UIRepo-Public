#include "PackageHostStartup.h"

#include "DocumentRegistry.h"
#include "FocusLeaseManager.h"
#include "NativeApiTable.h"
#include "PackageHostRuntime.h"

#include <spdlog/spdlog.h>

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo::fo4::PackageHostStartup {
namespace {

using FileFailure = PackageHostRuntime::FileFailure;

std::once_flag g_once;
std::unique_ptr<PackageHostRuntime> g_runtime;

[[nodiscard]] std::filesystem::path pluginDirectory() {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&pluginDirectory),
            &module) || module == nullptr) {
        return {};
    }

    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || buffer.size() > 32768) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
}

[[nodiscard]] std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

[[nodiscard]] const char* failureName(FileFailure failure) noexcept {
    switch (failure) {
    case FileFailure::None:
        return "loaded";
    case FileFailure::BadName:
        return "bad file name";
    case FileFailure::NotRegularFile:
        return "not a regular file";
    case FileFailure::TooLarge:
        return "file too large";
    case FileFailure::Unreadable:
        return "unreadable";
    case FileFailure::TooManyPackages:
        return "too many packages";
    case FileFailure::DuplicatePackage:
        return "duplicate package id";
    case FileFailure::Host:
        return "package refused";
    case FileFailure::Registry:
        return "registry refused";
    case FileFailure::Internal:
        return "internal error";
    }
    return "unknown";
}

[[nodiscard]] std::string digestPrefix(const pkg::Digest& digest) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string text;
    for (std::size_t index = 0; index < 8; ++index) {
        text.push_back(kHex[digest[index] >> 4]);
        text.push_back(kHex[digest[index] & 15]);
    }
    return text;
}

void logReport(
    const std::filesystem::path& directory,
    const PackageHostRuntime::ScanReport& report,
    const std::vector<PackageHostRuntime::PackageRecord>& packages) {
    if (!report.directoryFound && report.directoryProblem.empty()) {
        spdlog::info("UIRepo_F4 PackageHost: no packages folder at {}", utf8(directory));
        return;
    }
    if (!report.directoryProblem.empty()) {
        spdlog::warn("UIRepo_F4 PackageHost: packages folder {}: {}", utf8(directory), report.directoryProblem);
    }

    for (const PackageHostRuntime::FileResult& file : report.files) {
        if (file.loaded()) {
            continue;
        }
        const char* detail = file.host.ok() ? "" : pkghost::describe(file.host.code);
        spdlog::warn(
            "UIRepo_F4 PackageHost: refused {} ({}{}{}): {}",
            file.file,
            failureName(file.failure),
            file.host.ok() ? "" : ", ",
            detail,
            file.detail);
    }

    for (const PackageHostRuntime::PackageRecord& package : packages) {
        const auto& entry = package.documents[package.entryIndex];
        spdlog::info(
            "UIRepo_F4 PackageHost: loaded {} {}.{}.{} from {} (digest {}): {} documents hidden under owner 0x{:X}, entry document handle {}, {} style and {} binding entries carried unused",
            package.id,
            package.version.major,
            package.version.minor,
            package.version.patch,
            package.file,
            digestPrefix(package.digest),
            package.documents.size(),
            package.owner,
            entry.handle,
            package.styleEntries,
            package.bindingEntries);
        if (package.assetReferences != 0) {
            spdlog::warn(
                "UIRepo_F4 PackageHost: {} has {} image or video references that cannot be drawn yet: the renderer image path loads files only and has no memory source",
                package.id,
                package.assetReferences);
        }
    }
    spdlog::info(
        "UIRepo_F4 PackageHost: {} of {} package files loaded",
        report.loadedCount(),
        report.files.size());
}

void runOnce() {
    const std::filesystem::path pluginFolder = pluginDirectory();
    if (pluginFolder.empty()) {
        spdlog::warn("UIRepo_F4 PackageHost: the plugin folder could not be determined; packages are not loaded");
        return;
    }
    const std::filesystem::path directory = pluginFolder / "UIRepo" / "packages";

    static const std::vector<std::string_view> capabilities =
        pkghost::capabilityNamesFromAbiMask(nativeApiV2().v1.capabilities);

    PackageHostRuntime::Config config;
    config.packagesDirectory = directory;
    config.host.hostApiVersion = UIREPO_NATIVE_API_VERSION;
    config.host.capabilities = capabilities;
    config.releaseDocument = &FocusLeaseManager::clearRequirements;
    g_runtime = std::make_unique<PackageHostRuntime>(DocumentRegistry::instance(), std::move(config));

    const PackageHostRuntime::ScanReport report = g_runtime->scan();
    logReport(directory, report, g_runtime->packages());
}

} // namespace

void run() noexcept {
    try {
        std::call_once(g_once, runOnce);
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 PackageHost: startup scan failed: {}", exception.what());
    } catch (...) {
        spdlog::error("UIRepo_F4 PackageHost: startup scan failed with an unknown exception");
    }
}

} // namespace uirepo::fo4::PackageHostStartup
