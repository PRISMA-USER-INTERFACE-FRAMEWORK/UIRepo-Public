#pragma once

#include "uirepo/PackageHash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo::pkg {

inline constexpr std::uint16_t kFormatMajor = 1;
inline constexpr std::uint16_t kFormatMinor = 0;
inline constexpr std::size_t kHeaderSize = 48;
inline constexpr std::size_t kEntryRecordSize = 32;
inline constexpr std::size_t kFooterSize = 32;
inline constexpr std::array<std::uint8_t, 8> kMagic = {0x89, 'U', 'I', 'P', 'K', 'G', 0x0D, 0x0A};

enum class EntryKind : std::uint8_t {
    Document = 1,
    Style = 2,
    Binding = 3,
    Asset = 4,
};

[[nodiscard]] std::string_view kindDirectory(EntryKind kind) noexcept;

enum class PackageError : std::uint16_t {
    Ok = 0,
    Truncated = 1,
    BadMagic = 2,
    BadVersion = 3,
    UnsupportedMajor = 4,
    HeaderChecksum = 5,
    UnsupportedFlags = 6,
    ReservedNonZero = 7,
    TrailingData = 8,
    LimitExceeded = 9,
    SectionOutOfRange = 10,
    ManifestChecksum = 11,
    ManifestMalformed = 12,
    ManifestMissingField = 13,
    UnknownManifestField = 14,
    ManifestInvalidValue = 15,
    UnsupportedCapability = 16,
    IncompatibleHost = 17,
    UnknownEntryKind = 18,
    BadPath = 19,
    DuplicatePath = 20,
    UnsortedEntries = 21,
    KindPathMismatch = 22,
    PathBlobMismatch = 23,
    EntryOutOfRange = 24,
    EntryOverlap = 25,
    EntryGap = 26,
    NonZeroPadding = 27,
    EntryChecksum = 28,
    PackageDigest = 29,
    EntryDocumentMissing = 30,
    BadReference = 31,
    FileDirectoryConflict = 32,
};

[[nodiscard]] const char* describe(PackageError error) noexcept;

struct Status {
    PackageError code{PackageError::Ok};
    std::uint64_t offset{};
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return code == PackageError::Ok; }
};

struct PackageLimits {
    std::uint32_t maxEntries = 16384;
    std::uint32_t maxManifestSize = 64 * 1024;
    std::uint32_t maxPathLength = 240;
    std::uint32_t maxSegmentLength = 64;
    std::uint32_t maxDisplayNameLength = 128;
    std::uint32_t maxCapabilities = 64;
    std::uint32_t maxDependencies = 64;
    std::uint64_t maxEntrySize = std::uint64_t{256} << 20;
    std::uint64_t maxTotalSize = std::uint64_t{1} << 30;
};

inline constexpr std::size_t kMaxIdentifierLength = 64;

struct Version {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};

    friend constexpr bool operator==(const Version&, const Version&) = default;
};

struct Dependency {
    std::string id;
    Version minVersion;

    friend bool operator==(const Dependency&, const Dependency&) = default;
};

struct Manifest {
    std::string id;
    Version version;
    std::string displayName;
    std::string entryDocument;
    std::uint32_t minApiVersion = 1;
    std::vector<std::string> requiredCapabilities;
    std::vector<std::string> optionalCapabilities;
    std::vector<Dependency> dependencies;

    friend bool operator==(const Manifest&, const Manifest&) = default;
};

struct Entry {
    EntryKind kind{};
    std::string path;
    std::span<const std::uint8_t> data;
    std::uint32_t crc32{};
};

struct Package {
    std::uint16_t formatMajor{};
    std::uint16_t formatMinor{};
    bool newerMinor{};
    Manifest manifest;
    std::vector<Entry> entries;
    Digest digest{};
    PackageLimits limits;

    [[nodiscard]] const Entry* find(std::string_view path) const noexcept;
    [[nodiscard]] Status resolve(std::string_view basePath, std::string_view reference, const Entry*& out) const;
};

[[nodiscard]] std::span<const std::string_view> builtinCapabilities() noexcept;

struct ReadOptions {
    PackageLimits limits;
    std::uint16_t supportedMinor = kFormatMinor;
    std::uint32_t hostApiVersion = 0;
    std::span<const std::string_view> hostCapabilities = builtinCapabilities();
};

struct ReadResult {
    Status status;
    Package package;
};

[[nodiscard]] ReadResult readPackage(std::span<const std::uint8_t> bytes, const ReadOptions& options = {});
[[nodiscard]] Status validatePackage(std::span<const std::uint8_t> bytes, const ReadOptions& options = {});

class PackageBuilder final {
public:
    explicit PackageBuilder(Manifest manifest, PackageLimits limits = {});

    [[nodiscard]] Status add(EntryKind kind, std::string_view path, std::span<const std::uint8_t> data);
    [[nodiscard]] Status build(std::vector<std::uint8_t>& out) const;

private:
    struct Item {
        EntryKind kind{};
        std::vector<std::uint8_t> data;
    };

    Manifest manifest_;
    PackageLimits limits_;
    std::map<std::string, Item, std::less<>> items_;
};

[[nodiscard]] const char* pathViolation(std::string_view path, const PackageLimits& limits = {}) noexcept;
[[nodiscard]] bool isCanonicalPath(std::string_view path, const PackageLimits& limits = {}) noexcept;
[[nodiscard]] Status resolveReference(
    std::string_view basePath,
    std::string_view reference,
    std::string& out,
    const PackageLimits& limits = {});

}  // namespace uirepo::pkg
