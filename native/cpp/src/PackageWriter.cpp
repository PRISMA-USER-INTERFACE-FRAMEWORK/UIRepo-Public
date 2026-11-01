#include "PackageInternal.h"

#include <algorithm>

namespace uirepo::pkg {

namespace {

void padTo(std::vector<std::uint8_t>& out, std::uint64_t size) {
    out.resize(static_cast<std::size_t>(size), 0);
}

}  // namespace

PackageBuilder::PackageBuilder(Manifest manifest, PackageLimits limits)
    : manifest_(std::move(manifest)),
      limits_(limits) {
    std::sort(manifest_.requiredCapabilities.begin(), manifest_.requiredCapabilities.end());
    std::sort(manifest_.optionalCapabilities.begin(), manifest_.optionalCapabilities.end());
    std::stable_sort(manifest_.dependencies.begin(), manifest_.dependencies.end(), [](const Dependency& a, const Dependency& b) {
        return a.id < b.id;
    });
}

Status PackageBuilder::add(EntryKind kind, std::string_view path, std::span<const std::uint8_t> data) {
    const std::string_view directory = kindDirectory(kind);
    if (directory.empty()) {
        return detail::fail(PackageError::UnknownEntryKind, 0, "entry kind is not one of document, style, binding, asset");
    }
    if (const char* reason = pathViolation(path, limits_)) {
        return detail::fail(PackageError::BadPath, 0, "path " + detail::quoted(path) + ": " + reason);
    }
    if (!path.starts_with(directory)) {
        return detail::fail(PackageError::KindPathMismatch, 0, "path '" + std::string(path) + "' must start with " + std::string(directory));
    }
    if (data.size() > limits_.maxEntrySize) {
        return detail::fail(PackageError::LimitExceeded, 0, "entry '" + std::string(path) + "' exceeds the entry size limit");
    }
    if (items_.find(path) != items_.end()) {
        return detail::fail(PackageError::DuplicatePath, 0, "duplicate path '" + std::string(path) + "'");
    }
    const std::string prefix = std::string(path) + '/';
    const auto below = items_.lower_bound(prefix);
    const std::string_view above =
        detail::findAncestorEntry(path, [this](std::string_view ancestor) { return items_.find(ancestor) != items_.end(); });
    if (!above.empty() || (below != items_.end() && below->first.starts_with(prefix))) {
        return detail::fail(
            PackageError::FileDirectoryConflict, 0, "path '" + std::string(path) + "' is both an entry and a directory of another entry");
    }
    if (items_.size() >= limits_.maxEntries) {
        return detail::fail(PackageError::LimitExceeded, 0, "too many entries");
    }
    items_.emplace(std::string(path), Item{kind, std::vector<std::uint8_t>(data.begin(), data.end())});
    return {};
}

Status PackageBuilder::build(std::vector<std::uint8_t>& out) const {
    using namespace detail;
    out.clear();

    if (Status status = validateManifest(manifest_, limits_); !status.ok()) {
        return status;
    }
    const auto document = items_.find(manifest_.entryDocument);
    if (document == items_.end() || document->second.kind != EntryKind::Document) {
        return fail(PackageError::EntryDocumentMissing, 0, "no document entry at " + manifest_.entryDocument);
    }
    const std::vector<std::uint8_t> manifest = encodeManifest(manifest_);
    if (manifest.size() > limits_.maxManifestSize) {
        return fail(PackageError::LimitExceeded, 0, "manifest exceeds the manifest size limit");
    }

    std::vector<std::uint8_t> pathBlob;
    for (const auto& [path, item] : items_) {
        pathBlob.insert(pathBlob.end(), path.begin(), path.end());
    }

    const std::uint64_t tableOffset = align8(kHeaderSize + manifest.size());
    const std::uint64_t pathOffset = tableOffset + items_.size() * kEntryRecordSize;
    const std::uint64_t dataOffset = align8(pathOffset + pathBlob.size());
    std::uint64_t cursor = dataOffset;
    for (const auto& [path, item] : items_) {
        cursor = align8(cursor + item.data.size());
    }
    const std::uint64_t total = cursor + kFooterSize;
    if (total > limits_.maxTotalSize) {
        return fail(PackageError::LimitExceeded, 0, "package exceeds the total size limit");
    }

    out.reserve(static_cast<std::size_t>(total));
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    put16(out, kFormatMajor);
    put16(out, kFormatMinor);
    put32(out, 0);
    put64(out, total);
    put32(out, static_cast<std::uint32_t>(manifest.size()));
    put32(out, static_cast<std::uint32_t>(items_.size()));
    put32(out, static_cast<std::uint32_t>(pathBlob.size()));
    put32(out, crc32(manifest));
    put32(out, 0);
    put32(out, crc32(std::span<const std::uint8_t>(out.data(), out.size())));

    out.insert(out.end(), manifest.begin(), manifest.end());
    padTo(out, tableOffset);

    std::uint32_t nextPath = 0;
    std::uint64_t dataCursor = dataOffset;
    for (const auto& [path, item] : items_) {
        put32(out, nextPath);
        put32(out, static_cast<std::uint32_t>(path.size()));
        out.push_back(static_cast<std::uint8_t>(item.kind));
        out.insert(out.end(), 3, 0);
        put32(out, crc32(item.data));
        put64(out, dataCursor);
        put64(out, item.data.size());
        nextPath += static_cast<std::uint32_t>(path.size());
        dataCursor = align8(dataCursor + item.data.size());
    }

    out.insert(out.end(), pathBlob.begin(), pathBlob.end());
    padTo(out, dataOffset);
    for (const auto& [path, item] : items_) {
        out.insert(out.end(), item.data.begin(), item.data.end());
        padTo(out, align8(out.size()));
    }

    const Digest digest = sha256(out);
    out.insert(out.end(), digest.begin(), digest.end());
    return {};
}

}  // namespace uirepo::pkg
