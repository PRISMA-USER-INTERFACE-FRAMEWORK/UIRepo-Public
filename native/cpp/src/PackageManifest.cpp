#include "PackageInternal.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace uirepo::pkg {

namespace {

constexpr std::array<std::string_view, 16> kBuiltinCapabilities = {
    "retained-documents",
    "native-d3d11",
    "directwrite-text",
    "engine-composition",
    "renderer-recovery",
    "dxgi-fallback",
    "pointer-input",
    "keyboard-input",
    "scroll-containers",
    "pointer-events",
    "native-settings-controls",
    "focus-lease",
    "document-key-events",
    "text-layout",
    "offscreen-surfaces",
    "result-codes",
};

}  // namespace

std::span<const std::string_view> builtinCapabilities() noexcept {
    return kBuiltinCapabilities;
}

const char* describe(PackageError error) noexcept {
    switch (error) {
    case PackageError::Ok:
        return "ok";
    case PackageError::Truncated:
        return "package is truncated";
    case PackageError::BadMagic:
        return "not a uipkg file (bad magic)";
    case PackageError::BadVersion:
        return "invalid format version";
    case PackageError::UnsupportedMajor:
        return "unsupported format major version";
    case PackageError::HeaderChecksum:
        return "header checksum mismatch";
    case PackageError::UnsupportedFlags:
        return "unsupported header flags";
    case PackageError::ReservedNonZero:
        return "reserved field is not zero";
    case PackageError::TrailingData:
        return "data after the end of the package";
    case PackageError::LimitExceeded:
        return "package exceeds a configured limit";
    case PackageError::SectionOutOfRange:
        return "section layout is inconsistent with the package size";
    case PackageError::ManifestChecksum:
        return "manifest checksum mismatch";
    case PackageError::ManifestMalformed:
        return "manifest is malformed";
    case PackageError::ManifestMissingField:
        return "manifest is missing a required field";
    case PackageError::UnknownManifestField:
        return "manifest has an unknown field";
    case PackageError::ManifestInvalidValue:
        return "manifest field has an invalid value";
    case PackageError::UnsupportedCapability:
        return "package requires a capability the host does not provide";
    case PackageError::IncompatibleHost:
        return "package requires a newer host API";
    case PackageError::UnknownEntryKind:
        return "unknown entry kind";
    case PackageError::BadPath:
        return "entry path is not canonical";
    case PackageError::DuplicatePath:
        return "duplicate entry path";
    case PackageError::UnsortedEntries:
        return "entries are not sorted by path";
    case PackageError::KindPathMismatch:
        return "entry path is outside its kind directory";
    case PackageError::PathBlobMismatch:
        return "entry path table is inconsistent";
    case PackageError::EntryOutOfRange:
        return "entry data lies outside the data region";
    case PackageError::EntryOverlap:
        return "entry data overlaps earlier data";
    case PackageError::EntryGap:
        return "unaccounted bytes in the data region";
    case PackageError::NonZeroPadding:
        return "padding bytes are not zero";
    case PackageError::EntryChecksum:
        return "entry checksum mismatch";
    case PackageError::PackageDigest:
        return "package digest mismatch";
    case PackageError::EntryDocumentMissing:
        return "entry document is not a document entry in the package";
    case PackageError::BadReference:
        return "reference does not resolve";
    case PackageError::FileDirectoryConflict:
        return "entry path is also a directory of another entry";
    }
    return "unknown package error";
}

namespace detail {

namespace {

std::string hexTag(std::uint16_t tag) {
    char text[8];
    std::snprintf(text, sizeof(text), "0x%04X", static_cast<unsigned>(tag));
    return text;
}

std::string toString(std::span<const std::uint8_t> bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void putRecord(std::vector<std::uint8_t>& out, std::uint16_t tag, const std::vector<std::uint8_t>& value) {
    put16(out, tag);
    put32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void putText(std::vector<std::uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

std::vector<std::uint8_t> encodeList(const std::vector<std::string>& list) {
    std::vector<std::uint8_t> value;
    put32(value, static_cast<std::uint32_t>(list.size()));
    for (const std::string& item : list) {
        put16(value, static_cast<std::uint16_t>(item.size()));
        putText(value, item);
    }
    return value;
}

Status decodeList(
    std::span<const std::uint8_t> value,
    std::uint64_t at,
    std::uint32_t maxCount,
    std::vector<std::string>& out) {
    if (value.size() < 4) {
        return fail(PackageError::ManifestMalformed, at, "list shorter than its count");
    }
    const std::uint32_t count = get32(value.data());
    if (count == 0) {
        return fail(PackageError::ManifestInvalidValue, at, "empty list must be omitted");
    }
    if (count > maxCount) {
        return fail(PackageError::LimitExceeded, at, "list has too many items");
    }
    std::size_t pos = 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (value.size() - pos < 2) {
            return fail(PackageError::ManifestMalformed, at, "list item length is truncated");
        }
        const std::size_t length = get16(value.data() + pos);
        pos += 2;
        if (value.size() - pos < length) {
            return fail(PackageError::ManifestMalformed, at, "list item exceeds its record");
        }
        out.push_back(toString(value.subspan(pos, length)));
        pos += length;
    }
    if (pos != value.size()) {
        return fail(PackageError::ManifestMalformed, at, "trailing bytes after list");
    }
    return {};
}

Status decodeDependencies(
    std::span<const std::uint8_t> value,
    std::uint64_t at,
    std::uint32_t maxCount,
    std::vector<Dependency>& out) {
    if (value.size() < 4) {
        return fail(PackageError::ManifestMalformed, at, "dependency list shorter than its count");
    }
    const std::uint32_t count = get32(value.data());
    if (count == 0) {
        return fail(PackageError::ManifestInvalidValue, at, "empty list must be omitted");
    }
    if (count > maxCount) {
        return fail(PackageError::LimitExceeded, at, "too many dependencies");
    }
    std::size_t pos = 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (value.size() - pos < 2) {
            return fail(PackageError::ManifestMalformed, at, "dependency id length is truncated");
        }
        const std::size_t length = get16(value.data() + pos);
        pos += 2;
        if (value.size() - pos < length || value.size() - pos - length < 12) {
            return fail(PackageError::ManifestMalformed, at, "dependency exceeds its record");
        }
        Dependency dependency;
        dependency.id = toString(value.subspan(pos, length));
        pos += length;
        dependency.minVersion = Version{get32(value.data() + pos), get32(value.data() + pos + 4), get32(value.data() + pos + 8)};
        pos += 12;
        out.push_back(std::move(dependency));
    }
    if (pos != value.size()) {
        return fail(PackageError::ManifestMalformed, at, "trailing bytes after dependency list");
    }
    return {};
}

Status validateCapabilityList(const std::vector<std::string>& list, std::string_view name, const PackageLimits& limits) {
    if (list.size() > limits.maxCapabilities) {
        return fail(PackageError::LimitExceeded, 0, std::string(name) + " has too many items");
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!isIdentifier(list[i])) {
            return fail(PackageError::ManifestInvalidValue, 0, std::string(name) + " item is not a valid identifier");
        }
        if (i > 0 && !(list[i - 1] < list[i])) {
            return fail(PackageError::ManifestInvalidValue, 0, std::string(name) + " must be sorted and unique");
        }
    }
    return {};
}

}  // namespace

bool isIdentifier(std::string_view text) noexcept {
    if (text.empty() || text.size() > kMaxIdentifierLength) {
        return false;
    }
    const auto lowerAlnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!lowerAlnum(text.front())) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [&](char c) { return lowerAlnum(c) || c == '.' || c == '-' || c == '_'; });
}

Status validateManifest(const Manifest& manifest, const PackageLimits& limits) {
    if (!isIdentifier(manifest.id)) {
        return fail(PackageError::ManifestInvalidValue, 0, "package id must match [a-z0-9][a-z0-9._-]{0,63}");
    }
    if (manifest.displayName.size() > limits.maxDisplayNameLength ||
        !std::all_of(manifest.displayName.begin(), manifest.displayName.end(), [](char c) { return c >= 0x20 && c <= 0x7E; })) {
        return fail(PackageError::ManifestInvalidValue, 0, "display name must be printable ASCII within the length limit");
    }
    if (const char* reason = pathViolation(manifest.entryDocument, limits)) {
        return fail(PackageError::ManifestInvalidValue, 0, std::string("entry document path: ") + reason);
    }
    if (!manifest.entryDocument.starts_with(kindDirectory(EntryKind::Document))) {
        return fail(PackageError::ManifestInvalidValue, 0, "entry document must be under documents/");
    }
    if (manifest.minApiVersion == 0) {
        return fail(PackageError::ManifestInvalidValue, 0, "minimum API version must be at least 1");
    }
    if (Status status = validateCapabilityList(manifest.requiredCapabilities, "required capabilities", limits); !status.ok()) {
        return status;
    }
    if (Status status = validateCapabilityList(manifest.optionalCapabilities, "optional capabilities", limits); !status.ok()) {
        return status;
    }
    for (const std::string& capability : manifest.requiredCapabilities) {
        if (std::binary_search(manifest.optionalCapabilities.begin(), manifest.optionalCapabilities.end(), capability)) {
            return fail(PackageError::ManifestInvalidValue, 0, "capability is both required and optional: " + capability);
        }
    }
    if (manifest.dependencies.size() > limits.maxDependencies) {
        return fail(PackageError::LimitExceeded, 0, "too many dependencies");
    }
    for (std::size_t i = 0; i < manifest.dependencies.size(); ++i) {
        const std::string& id = manifest.dependencies[i].id;
        if (!isIdentifier(id) || id == manifest.id) {
            return fail(PackageError::ManifestInvalidValue, 0, "dependency id is invalid or names the package itself");
        }
        if (i > 0 && !(manifest.dependencies[i - 1].id < id)) {
            return fail(PackageError::ManifestInvalidValue, 0, "dependencies must be sorted by id and unique");
        }
    }
    return {};
}

std::vector<std::uint8_t> encodeManifest(const Manifest& manifest) {
    std::vector<std::uint8_t> out;
    const auto text = [](std::string_view value) { return std::vector<std::uint8_t>(value.begin(), value.end()); };

    if (!manifest.displayName.empty()) {
        putRecord(out, kTagDisplayName, text(manifest.displayName));
    }
    if (!manifest.optionalCapabilities.empty()) {
        putRecord(out, kTagOptionalCapabilities, encodeList(manifest.optionalCapabilities));
    }
    putRecord(out, kTagId, text(manifest.id));

    std::vector<std::uint8_t> version;
    put32(version, manifest.version.major);
    put32(version, manifest.version.minor);
    put32(version, manifest.version.patch);
    putRecord(out, kTagVersion, version);

    putRecord(out, kTagEntryDocument, text(manifest.entryDocument));

    std::vector<std::uint8_t> api;
    put32(api, manifest.minApiVersion);
    putRecord(out, kTagMinApiVersion, api);

    if (!manifest.requiredCapabilities.empty()) {
        putRecord(out, kTagRequiredCapabilities, encodeList(manifest.requiredCapabilities));
    }
    if (!manifest.dependencies.empty()) {
        std::vector<std::uint8_t> value;
        put32(value, static_cast<std::uint32_t>(manifest.dependencies.size()));
        for (const Dependency& dependency : manifest.dependencies) {
            put16(value, static_cast<std::uint16_t>(dependency.id.size()));
            putText(value, dependency.id);
            put32(value, dependency.minVersion.major);
            put32(value, dependency.minVersion.minor);
            put32(value, dependency.minVersion.patch);
        }
        putRecord(out, kTagDependencies, value);
    }
    return out;
}

Status decodeManifest(
    std::span<const std::uint8_t> bytes,
    std::uint64_t baseOffset,
    const PackageLimits& limits,
    bool tolerateUnknownOptional,
    Manifest& out) {
    out = {};
    std::size_t pos = 0;
    int previousTag = -1;
    unsigned seen = 0;

    while (pos < bytes.size()) {
        const std::uint64_t at = baseOffset + pos;
        if (bytes.size() - pos < 6) {
            return fail(PackageError::ManifestMalformed, at, "truncated record header");
        }
        const std::uint16_t tag = get16(bytes.data() + pos);
        const std::uint32_t length = get32(bytes.data() + pos + 2);
        pos += 6;
        if (length > bytes.size() - pos) {
            return fail(PackageError::ManifestMalformed, at, "record length exceeds the manifest");
        }
        if (static_cast<int>(tag) <= previousTag) {
            return fail(
                PackageError::ManifestMalformed, at, static_cast<int>(tag) == previousTag ? "duplicate tag " + hexTag(tag) : "tags are not in ascending order");
        }
        previousTag = tag;
        const std::span<const std::uint8_t> value = bytes.subspan(pos, length);
        pos += length;

        Status status;
        switch (tag) {
        case kTagDisplayName:
            if (value.empty()) {
                status = fail(PackageError::ManifestInvalidValue, at, "empty display name must be omitted");
            }
            out.displayName = toString(value);
            break;
        case kTagOptionalCapabilities:
            status = decodeList(value, at, limits.maxCapabilities, out.optionalCapabilities);
            break;
        case kTagId:
            out.id = toString(value);
            break;
        case kTagVersion:
            if (value.size() != 12) {
                status = fail(PackageError::ManifestMalformed, at, "version record must be 12 bytes");
            } else {
                out.version = Version{get32(value.data()), get32(value.data() + 4), get32(value.data() + 8)};
            }
            break;
        case kTagEntryDocument:
            out.entryDocument = toString(value);
            break;
        case kTagMinApiVersion:
            if (value.size() != 4) {
                status = fail(PackageError::ManifestMalformed, at, "minimum API version record must be 4 bytes");
            } else {
                out.minApiVersion = get32(value.data());
            }
            break;
        case kTagRequiredCapabilities:
            status = decodeList(value, at, limits.maxCapabilities, out.requiredCapabilities);
            break;
        case kTagDependencies:
            status = decodeDependencies(value, at, limits.maxDependencies, out.dependencies);
            break;
        default:
            if ((tag & kCriticalTagBit) != 0) {
                status = fail(PackageError::UnknownManifestField, at, "unknown critical tag " + hexTag(tag));
            } else if (!tolerateUnknownOptional) {
                status = fail(PackageError::UnknownManifestField, at, "unknown optional tag " + hexTag(tag) + " in a package this reader fully supports");
            }
            break;
        }
        if (!status.ok()) {
            return status;
        }
        if (tag == kTagId || tag == kTagVersion || tag == kTagEntryDocument || tag == kTagMinApiVersion) {
            seen |= 1u << (tag & 0xFu);
        }
    }

    constexpr std::array<std::pair<std::uint16_t, const char*>, 4> required = {{
        {kTagId, "id"},
        {kTagVersion, "version"},
        {kTagEntryDocument, "entry document"},
        {kTagMinApiVersion, "minimum API version"},
    }};
    for (const auto& [tag, name] : required) {
        if ((seen & (1u << (tag & 0xFu))) == 0) {
            return fail(PackageError::ManifestMissingField, baseOffset, std::string("missing required field: ") + name);
        }
    }

    Status valid = validateManifest(out, limits);
    if (!valid.ok()) {
        valid.offset = baseOffset;
    }
    return valid;
}

}  // namespace detail

}  // namespace uirepo::pkg
