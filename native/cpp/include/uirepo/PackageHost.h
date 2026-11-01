#pragma once

#include "uirepo/Core.h"
#include "uirepo/DeclarativeUi.h"
#include "uirepo/Package.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo::pkghost {

inline constexpr std::string_view kCapabilityStyles = "styles";
inline constexpr std::string_view kCapabilityBindings = "bindings";

// Codes are stable: never renumber, only append.
enum class HostError : std::uint16_t {
    Ok = 0,
    InvalidOptions = 1,
    Container = 2,
    RequiredFormatUnsupported = 3,
    DocumentRefused = 4,
    LimitExceeded = 5,
    BadAssetReference = 6,
    MissingAsset = 7,
    NotAnAsset = 8,
    OutOfMemory = 9,
    Internal = 10,
};

[[nodiscard]] const char* describe(HostError error) noexcept;

struct HostStatus {
    HostError code{HostError::Ok};
    pkg::PackageError container{pkg::PackageError::Ok};
    declarative::ErrorCode documentError{};
    std::uint64_t offset{};
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return code == HostError::Ok; }
};

struct HostLimits {
    pkg::PackageLimits package;
    declarative::Limits document;
    std::uint32_t maxDocuments = 256;
    std::uint32_t maxTotalNodes = 65536;
    std::uint32_t maxAssetReferences = 4096;
};

struct HostOptions {
    std::uint32_t hostApiVersion = 0;
    std::span<const std::string_view> capabilities = pkg::builtinCapabilities();
    HostLimits limits;
};

struct EntryView {
    std::string path;
    std::span<const std::uint8_t> data;
};

struct AssetReference {
    std::string nodeId;
    std::string reference;
    std::string assetPath;
    bool video{};
};

struct HostedDocument {
    std::string path;
    std::string id;
    std::unique_ptr<Document> document;
    std::vector<AssetReference> assets;
};

// Spans in styles, bindings and assets point into storage; the struct is move-only so they stay valid.
struct LoadedPackage {
    pkg::Manifest manifest;
    pkg::Digest digest{};
    bool newerMinor{};
    std::size_t entryIndex{};
    std::vector<HostedDocument> documents;
    std::vector<EntryView> styles;
    std::vector<EntryView> bindings;
    std::vector<EntryView> assets;
    std::vector<std::uint8_t> storage;

    [[nodiscard]] HostedDocument& entry() noexcept { return documents[entryIndex]; }
    [[nodiscard]] const HostedDocument& entry() const noexcept { return documents[entryIndex]; }
    [[nodiscard]] const HostedDocument* findDocument(std::string_view path) const noexcept;
    [[nodiscard]] const EntryView* findAsset(std::string_view path) const noexcept;
};

struct LoadResult {
    HostStatus status;
    LoadedPackage package;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
};

// Never throws. All-or-nothing: on any failure the package is empty and nothing was exposed.
[[nodiscard]] LoadResult loadPackage(std::span<const std::uint8_t> bytes, const HostOptions& options) noexcept;
[[nodiscard]] LoadResult loadPackage(std::vector<std::uint8_t>&& bytes, const HostOptions& options) noexcept;

[[nodiscard]] std::string assetUrl(std::string_view packageId, std::string_view assetPath);

std::size_t bindAssetUrls(std::string_view packageId, HostedDocument& document);

[[nodiscard]] std::vector<std::string_view> capabilityNamesFromAbiMask(std::uint64_t mask);

} // namespace uirepo::pkghost
