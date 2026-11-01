#include "PackageHostRuntime.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>
#include <utility>

namespace uirepo::fo4 {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxNameBytes = 128;
constexpr std::size_t kMaxCandidates = 1024;
constexpr std::string_view kExtension = ".uipkg";

struct FileName {
    std::string text;
    bool ascii{true};
};

[[nodiscard]] FileName narrowName(const fs::path& path) {
    FileName name;
    for (const auto unit : path.filename().native()) {
        const auto code = static_cast<std::make_unsigned_t<decltype(unit)>>(unit);
        if (code >= 0x20 && code < 0x7F) {
            name.text.push_back(static_cast<char>(code));
        } else {
            name.text.push_back('?');
            name.ascii = false;
        }
    }
    return name;
}

[[nodiscard]] bool hasPackageExtension(std::string_view text) noexcept {
    if (text.size() <= kExtension.size()) {
        return false;
    }
    const std::string_view tail = text.substr(text.size() - kExtension.size());
    return std::equal(tail.begin(), tail.end(), kExtension.begin(), [](char left, char right) {
        const char lower = left >= 'A' && left <= 'Z' ? static_cast<char>(left - 'A' + 'a') : left;
        return lower == right;
    });
}

[[nodiscard]] bool nameAllowed(const FileName& name) noexcept {
    if (!name.ascii || name.text.empty() || name.text.size() > kMaxNameBytes || name.text.front() == '.') {
        return false;
    }
    return std::all_of(name.text.begin(), name.text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
               c == '_' || c == '-' || c == ' ';
    });
}

} // namespace

std::size_t PackageHostRuntime::ScanReport::loadedCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(files.begin(), files.end(), [](const FileResult& file) {
        return file.loaded();
    }));
}

PackageHostRuntime::PackageHostRuntime(DocumentRegistry& registry, Config config)
    : registry_(registry), config_(std::move(config)) {}

PackageHostRuntime::ScanReport PackageHostRuntime::scan() noexcept {
    ScanReport report;
    try {
        std::scoped_lock lock(mutex_);

        std::error_code error;
        const fs::file_status status = fs::symlink_status(config_.packagesDirectory, error);
        if (status.type() == fs::file_type::not_found) {
            return report;
        }
        if (error) {
            report.directoryProblem = "cannot inspect the packages directory: " + error.message();
            return report;
        }
        if (status.type() != fs::file_type::directory) {
            report.directoryProblem = "the packages path is not a plain directory";
            return report;
        }
        report.directoryFound = true;

        struct Candidate {
            FileName name;
            fs::path path;
        };
        std::vector<Candidate> candidates;
        fs::directory_iterator entries(config_.packagesDirectory, error);
        for (; !error && entries != fs::directory_iterator(); entries.increment(error)) {
            FileName name = narrowName(entries->path());
            if (!hasPackageExtension(name.text)) {
                continue;
            }
            if (candidates.size() == kMaxCandidates) {
                report.directoryProblem = "more than " + std::to_string(kMaxCandidates) + " package files; the rest are ignored";
                break;
            }
            candidates.push_back({std::move(name), entries->path()});
        }
        if (error) {
            report.directoryProblem = "cannot list the packages directory: " + error.message();
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.name.text < right.name.text;
        });

        for (const Candidate& candidate : candidates) {
            FileResult result;
            result.file = candidate.name.text;
            if (!nameAllowed(candidate.name)) {
                result.failure = FileFailure::BadName;
                result.detail = "file name is not 1 to 128 characters of letters, digits, '.', '_', '-' and ' ' without a leading '.'";
                report.files.push_back(std::move(result));
                continue;
            }
            try {
                result = loadFile(candidate.path, candidate.name.text);
            } catch (const std::exception& exception) {
                result = FileResult{};
                result.file = candidate.name.text;
                result.failure = FileFailure::Internal;
                result.detail = exception.what();
            } catch (...) {
                result = FileResult{};
                result.file = candidate.name.text;
                result.failure = FileFailure::Internal;
                result.detail = "unexpected exception";
            }
            report.files.push_back(std::move(result));
        }
    } catch (...) {
        report.directoryProblem = "package scan failed with an unexpected exception";
    }
    return report;
}

PackageHostRuntime::FileResult PackageHostRuntime::loadFile(const fs::path& path, const std::string& name) {
    FileResult result;
    result.file = name;
    const auto refuse = [&result](FileFailure failure, std::string detail) {
        result.failure = failure;
        result.detail = std::move(detail);
        return result;
    };

    if (packages_.size() >= config_.maxPackages) {
        return refuse(FileFailure::TooManyPackages, "already hosting " + std::to_string(config_.maxPackages) + " packages");
    }

    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || status.type() != fs::file_type::regular) {
        return refuse(FileFailure::NotRegularFile, "not a regular file (directories, links and devices are refused)");
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        return refuse(FileFailure::Unreadable, "cannot read the file size: " + error.message());
    }
    if (size > config_.maxFileBytes) {
        return refuse(
            FileFailure::TooLarge,
            std::to_string(size) + " bytes exceeds the limit of " + std::to_string(config_.maxFileBytes));
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return refuse(FileFailure::Unreadable, "cannot open the file");
    }
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<std::uintmax_t>(stream.gcount()) != size ||
        stream.peek() != std::char_traits<char>::eof()) {
        return refuse(FileFailure::Unreadable, "the file changed size while it was read");
    }

    pkghost::LoadResult loaded = pkghost::loadPackage(std::move(bytes), config_.host);
    if (!loaded.ok()) {
        result.host = loaded.status;
        return refuse(FileFailure::Host, loaded.status.detail);
    }

    pkghost::LoadedPackage& package = loaded.package;
    result.packageId = package.manifest.id;
    const bool duplicate = std::any_of(packages_.begin(), packages_.end(), [&package](const PackageRecord& record) {
        return record.id == package.manifest.id;
    });
    if (duplicate) {
        return refuse(FileFailure::DuplicatePackage, "a package with this id is already loaded");
    }

    PackageRecord record;
    record.id = package.manifest.id;
    record.file = name;
    record.version = package.manifest.version;
    record.digest = package.digest;
    record.entryIndex = package.entryIndex;
    record.styleEntries = package.styles.size();
    record.bindingEntries = package.bindings.size();
    if (!adopt(package, record, result)) {
        return result;
    }
    packages_.push_back(std::move(record));
    return result;
}

bool PackageHostRuntime::adopt(pkghost::LoadedPackage& package, PackageRecord& record, FileResult& result) {
    if (nextOwner_ == 0) {
        result.failure = FileFailure::Registry;
        result.detail = "package owner ids are exhausted";
        return false;
    }
    record.owner = kOwnerBase + nextOwner_++;

    try {
        for (pkghost::HostedDocument& hosted : package.documents) {
            record.assetReferences += pkghost::bindAssetUrls(record.id, hosted);
            const DocumentHandle handle = registry_.adoptOwnedDocument(
                std::move(hosted.document), record.owner, DocumentRegistry::DocumentRole::Panel, false);
            if (handle == 0) {
                (void)destroyDocuments(record);
                result.failure = FileFailure::Registry;
                result.detail = "the registry refused document " + hosted.path;
                return false;
            }
            record.documents.push_back({hosted.path, hosted.id, handle});
        }
    } catch (...) {
        (void)destroyDocuments(record);
        throw;
    }
    return true;
}

std::size_t PackageHostRuntime::destroyDocuments(const PackageRecord& record) noexcept {
    if (config_.releaseDocument != nullptr) {
        for (const DocumentRecord& document : record.documents) {
            config_.releaseDocument(document.handle);
        }
    }
    return registry_.destroyOwnerDocuments(record.owner);
}

bool PackageHostRuntime::unload(std::string_view packageId) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        const auto found = std::find_if(packages_.begin(), packages_.end(), [packageId](const PackageRecord& record) {
            return record.id == packageId;
        });
        if (found == packages_.end()) {
            return false;
        }
        const PackageRecord record = std::move(*found);
        packages_.erase(found);
        (void)destroyDocuments(record);
        return true;
    } catch (...) {
        return false;
    }
}

std::size_t PackageHostRuntime::unloadAll() noexcept {
    try {
        std::scoped_lock lock(mutex_);
        std::size_t destroyed = 0;
        for (const PackageRecord& record : packages_) {
            destroyed += destroyDocuments(record);
        }
        packages_.clear();
        return destroyed;
    } catch (...) {
        return 0;
    }
}

std::vector<PackageHostRuntime::PackageRecord> PackageHostRuntime::packages() const {
    std::scoped_lock lock(mutex_);
    return packages_;
}

} // namespace uirepo::fo4
