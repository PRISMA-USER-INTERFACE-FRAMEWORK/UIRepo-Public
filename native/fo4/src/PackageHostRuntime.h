#pragma once

#include "DocumentRegistry.h"
#include "uirepo/PackageHost.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo::fo4 {

class PackageHostRuntime final {
public:
    using DocumentHandle = DocumentRegistry::DocumentHandle;
    using OwnerHandle = DocumentRegistry::OwnerHandle;

    static constexpr OwnerHandle kOwnerBase = 0x55504B4700000000ull;
    static constexpr std::uint32_t kMaxPackages = 64;
    static constexpr std::uintmax_t kMaxFileBytes = std::uintmax_t{64} << 20;

    enum class FileFailure : std::uint8_t {
        None,
        BadName,
        NotRegularFile,
        TooLarge,
        Unreadable,
        TooManyPackages,
        DuplicatePackage,
        Host,
        Registry,
        Internal,
    };

    struct Config {
        std::filesystem::path packagesDirectory;
        pkghost::HostOptions host;
        std::uint32_t maxPackages = kMaxPackages;
        std::uintmax_t maxFileBytes = kMaxFileBytes;
        void (*releaseDocument)(DocumentHandle) noexcept = nullptr;
    };

    struct DocumentRecord {
        std::string path;
        std::string id;
        DocumentHandle handle{};
    };

    struct PackageRecord {
        std::string id;
        std::string file;
        OwnerHandle owner{};
        pkg::Version version;
        pkg::Digest digest{};
        std::vector<DocumentRecord> documents;
        std::size_t entryIndex{};
        std::size_t assetReferences{};
        std::size_t styleEntries{};
        std::size_t bindingEntries{};
    };

    struct FileResult {
        std::string file;
        FileFailure failure{FileFailure::None};
        pkghost::HostStatus host;
        std::string packageId;
        std::string detail;

        [[nodiscard]] bool loaded() const noexcept { return failure == FileFailure::None; }
    };

    struct ScanReport {
        bool directoryFound{};
        std::string directoryProblem;
        std::vector<FileResult> files;

        [[nodiscard]] std::size_t loadedCount() const noexcept;
    };

    PackageHostRuntime(DocumentRegistry& registry, Config config);

    PackageHostRuntime(const PackageHostRuntime&) = delete;
    PackageHostRuntime& operator=(const PackageHostRuntime&) = delete;

    [[nodiscard]] ScanReport scan() noexcept;
    [[nodiscard]] bool unload(std::string_view packageId) noexcept;
    [[nodiscard]] std::size_t unloadAll() noexcept;
    [[nodiscard]] std::vector<PackageRecord> packages() const;

private:
    [[nodiscard]] FileResult loadFile(const std::filesystem::path& path, const std::string& name);
    [[nodiscard]] bool adopt(pkghost::LoadedPackage& package, PackageRecord& record, FileResult& result);
    [[nodiscard]] std::size_t destroyDocuments(const PackageRecord& record) noexcept;

    DocumentRegistry& registry_;
    Config config_;
    mutable std::mutex mutex_;
    std::vector<PackageRecord> packages_;
    std::uint32_t nextOwner_{1};
};

} // namespace uirepo::fo4
