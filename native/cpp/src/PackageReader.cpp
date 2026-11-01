#include "PackageInternal.h"

#include <algorithm>

namespace uirepo::pkg {

namespace {

using namespace detail;

struct Layout {
    std::uint32_t manifestSize{};
    std::uint32_t entryCount{};
    std::uint32_t pathBlobSize{};
    std::uint64_t manifestEnd{};
    std::uint64_t tableOffset{};
    std::uint64_t pathOffset{};
    std::uint64_t dataOffset{};
    std::uint64_t footerOffset{};
};

Status checkPadding(std::span<const std::uint8_t> in, std::uint64_t begin, std::uint64_t end) {
    for (std::uint64_t i = begin; i < end; ++i) {
        if (in[static_cast<std::size_t>(i)] != 0) {
            return fail(PackageError::NonZeroPadding, i, "padding byte is not zero");
        }
    }
    return {};
}

Status parseHeader(std::span<const std::uint8_t> in, const ReadOptions& options, Layout& layout, Package& pkg) {
    const PackageLimits& limits = options.limits;
    const std::uint8_t* h = in.data();

    if (in.size() >= kMagic.size() && !std::equal(kMagic.begin(), kMagic.end(), h)) {
        return fail(PackageError::BadMagic, 0, "magic does not match");
    }
    if (in.size() < kHeaderSize + kFooterSize) {
        return fail(PackageError::Truncated, in.size(), "shorter than the minimum package size");
    }

    pkg.formatMajor = get16(h + 8);
    pkg.formatMinor = get16(h + 10);
    if (pkg.formatMajor == 0) {
        return fail(PackageError::BadVersion, 8, "major version 0 is invalid");
    }
    if (pkg.formatMajor != kFormatMajor) {
        return fail(
            PackageError::UnsupportedMajor,
            8,
            "package major " + std::to_string(pkg.formatMajor) + ", reader supports " + std::to_string(kFormatMajor));
    }
    if (crc32(in.first(44)) != get32(h + 44)) {
        return fail(PackageError::HeaderChecksum, 44, "header checksum does not match");
    }
    pkg.newerMinor = pkg.formatMinor > options.supportedMinor;

    if (get32(h + 12) != 0) {
        return fail(PackageError::UnsupportedFlags, 12, "header flags are not zero");
    }
    if (get32(h + 40) != 0) {
        return fail(PackageError::ReservedNonZero, 40, "reserved header field is not zero");
    }

    const std::uint64_t total = get64(h + 16);
    if (in.size() < total) {
        return fail(PackageError::Truncated, in.size(), "package is shorter than its declared size");
    }
    if (in.size() > total) {
        return fail(PackageError::TrailingData, total, "data follows the declared end of the package");
    }
    if (total > limits.maxTotalSize) {
        return fail(PackageError::LimitExceeded, 16, "package exceeds the total size limit");
    }

    layout.manifestSize = get32(h + 24);
    layout.entryCount = get32(h + 28);
    layout.pathBlobSize = get32(h + 32);
    if (layout.manifestSize > limits.maxManifestSize) {
        return fail(PackageError::LimitExceeded, 24, "manifest exceeds the manifest size limit");
    }
    if (layout.entryCount > limits.maxEntries) {
        return fail(PackageError::LimitExceeded, 28, "too many entries");
    }
    if (layout.pathBlobSize > std::uint64_t{layout.entryCount} * limits.maxPathLength) {
        return fail(PackageError::LimitExceeded, 32, "path table exceeds the path length limit");
    }

    layout.manifestEnd = kHeaderSize + std::uint64_t{layout.manifestSize};
    layout.tableOffset = align8(layout.manifestEnd);
    layout.pathOffset = layout.tableOffset + std::uint64_t{layout.entryCount} * kEntryRecordSize;
    layout.dataOffset = align8(layout.pathOffset + layout.pathBlobSize);
    layout.footerOffset = total - kFooterSize;
    if (layout.dataOffset > layout.footerOffset) {
        return fail(PackageError::SectionOutOfRange, 16, "sections do not fit in the declared package size");
    }
    if (Status padding = checkPadding(in, layout.manifestEnd, layout.tableOffset); !padding.ok()) {
        return padding;
    }
    return checkPadding(in, layout.pathOffset + layout.pathBlobSize, layout.dataOffset);
}

Status checkHost(const Manifest& manifest, const ReadOptions& options) {
    if (options.hostApiVersion != 0 && manifest.minApiVersion > options.hostApiVersion) {
        return fail(
            PackageError::IncompatibleHost,
            kHeaderSize,
            "package needs API version " + std::to_string(manifest.minApiVersion) + ", host provides " +
                std::to_string(options.hostApiVersion));
    }
    for (const std::string& capability : manifest.requiredCapabilities) {
        if (std::find(options.hostCapabilities.begin(), options.hostCapabilities.end(), capability) ==
            options.hostCapabilities.end()) {
            return fail(PackageError::UnsupportedCapability, kHeaderSize, "required capability not provided: " + capability);
        }
    }
    return {};
}

Status parseEntries(std::span<const std::uint8_t> in, const Layout& layout, const PackageLimits& limits, Package& pkg) {
    const std::uint8_t* table = in.data() + layout.tableOffset;
    const std::uint8_t* paths = in.data() + layout.pathOffset;
    pkg.entries.reserve(layout.entryCount);

    std::uint64_t pathCursor = 0;
    std::uint64_t previousEnd = layout.dataOffset;
    std::uint64_t expectedOffset = layout.dataOffset;

    for (std::uint32_t i = 0; i < layout.entryCount; ++i) {
        const std::uint8_t* record = table + std::size_t{i} * kEntryRecordSize;
        const std::uint64_t at = layout.tableOffset + std::uint64_t{i} * kEntryRecordSize;
        const std::uint32_t pathOffset = get32(record);
        const std::uint32_t pathLength = get32(record + 4);
        const std::uint8_t kindByte = record[8];
        const std::uint32_t crc = get32(record + 12);
        const std::uint64_t offset = get64(record + 16);
        const std::uint64_t size = get64(record + 24);

        if ((record[9] | record[10] | record[11]) != 0) {
            return fail(PackageError::ReservedNonZero, at + 9, "reserved entry bytes are not zero");
        }
        if (kindByte < static_cast<std::uint8_t>(EntryKind::Document) || kindByte > static_cast<std::uint8_t>(EntryKind::Asset)) {
            return fail(PackageError::UnknownEntryKind, at + 8, "unknown entry kind " + std::to_string(kindByte));
        }
        const auto kind = static_cast<EntryKind>(kindByte);

        if (pathOffset != pathCursor || pathLength > layout.pathBlobSize - pathCursor) {
            return fail(PackageError::PathBlobMismatch, at, "path is not the next slice of the path table");
        }
        std::string path(reinterpret_cast<const char*>(paths + pathOffset), pathLength);
        pathCursor += pathLength;
        if (const char* reason = pathViolation(path, limits)) {
            return fail(PackageError::BadPath, at, "path " + quoted(path) + ": " + reason);
        }
        if (i > 0) {
            const int order = path.compare(pkg.entries.back().path);
            if (order == 0) {
                return fail(PackageError::DuplicatePath, at, "duplicate path '" + path + "'");
            }
            if (order < 0) {
                return fail(PackageError::UnsortedEntries, at, "path '" + path + "' sorts before the previous entry");
            }
        }
        if (!path.starts_with(kindDirectory(kind))) {
            return fail(PackageError::KindPathMismatch, at, "path '" + path + "' is outside " + std::string(kindDirectory(kind)));
        }
        const std::string_view conflict =
            findAncestorEntry(path, [&pkg](std::string_view ancestor) { return pkg.find(ancestor) != nullptr; });
        if (!conflict.empty()) {
            return fail(
                PackageError::FileDirectoryConflict,
                at,
                "'" + std::string(conflict) + "' is an entry and also a directory of '" + path + "'");
        }

        if (size > limits.maxEntrySize) {
            return fail(PackageError::LimitExceeded, at + 24, "entry exceeds the entry size limit");
        }
        if (size > layout.footerOffset || offset > layout.footerOffset - size) {
            return fail(PackageError::EntryOutOfRange, at + 16, "entry data lies outside the package");
        }
        if (offset < expectedOffset) {
            return fail(PackageError::EntryOverlap, at + 16, "entry data starts inside earlier data");
        }
        if (offset > expectedOffset) {
            return fail(PackageError::EntryGap, at + 16, "entry data does not start at the next aligned offset");
        }
        if (Status padding = checkPadding(in, previousEnd, offset); !padding.ok()) {
            return padding;
        }

        const std::span<const std::uint8_t> data = in.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
        if (crc32(data) != crc) {
            return fail(PackageError::EntryChecksum, at + 12, "checksum mismatch for '" + path + "'");
        }
        previousEnd = offset + size;
        expectedOffset = align8(previousEnd);
        pkg.entries.push_back(Entry{kind, std::move(path), data, crc});
    }

    if (pathCursor != layout.pathBlobSize) {
        return fail(PackageError::PathBlobMismatch, layout.pathOffset, "path table has unreferenced bytes");
    }
    if (expectedOffset != layout.footerOffset) {
        return fail(PackageError::EntryGap, layout.footerOffset, "unaccounted bytes before the footer");
    }
    return checkPadding(in, previousEnd, layout.footerOffset);
}

Status parse(std::span<const std::uint8_t> in, const ReadOptions& options, Package& pkg) {
    pkg.limits = options.limits;
    Layout layout;
    if (Status status = parseHeader(in, options, layout, pkg); !status.ok()) {
        return status;
    }

    const std::span<const std::uint8_t> manifest = in.subspan(kHeaderSize, layout.manifestSize);
    if (crc32(manifest) != get32(in.data() + 36)) {
        return fail(PackageError::ManifestChecksum, 36, "manifest checksum does not match");
    }
    if (Status status = decodeManifest(manifest, kHeaderSize, options.limits, pkg.newerMinor, pkg.manifest); !status.ok()) {
        return status;
    }
    if (Status status = checkHost(pkg.manifest, options); !status.ok()) {
        return status;
    }
    if (Status status = parseEntries(in, layout, options.limits, pkg); !status.ok()) {
        return status;
    }

    const Entry* document = pkg.find(pkg.manifest.entryDocument);
    if (document == nullptr || document->kind != EntryKind::Document) {
        return fail(PackageError::EntryDocumentMissing, kHeaderSize, "no document entry at " + pkg.manifest.entryDocument);
    }

    pkg.digest = sha256(in.first(static_cast<std::size_t>(layout.footerOffset)));
    if (!std::equal(pkg.digest.begin(), pkg.digest.end(), in.begin() + static_cast<std::ptrdiff_t>(layout.footerOffset))) {
        return fail(PackageError::PackageDigest, layout.footerOffset, "package digest does not match");
    }
    return {};
}

}  // namespace

ReadResult readPackage(std::span<const std::uint8_t> bytes, const ReadOptions& options) {
    ReadResult result;
    result.status = parse(bytes, options, result.package);
    if (!result.status.ok()) {
        result.package = Package{};
    }
    return result;
}

Status validatePackage(std::span<const std::uint8_t> bytes, const ReadOptions& options) {
    return readPackage(bytes, options).status;
}

}  // namespace uirepo::pkg
