#include "PackageInternal.h"

#include <algorithm>
#include <array>

namespace uirepo::pkg {

namespace {

constexpr std::array<std::string_view, 4> kReservedDeviceNames = {"con", "prn", "aux", "nul"};

bool isReservedDeviceName(std::string_view segment) noexcept {
    const std::string_view stem = segment.substr(0, segment.find('.'));
    if (std::find(kReservedDeviceNames.begin(), kReservedDeviceNames.end(), stem) != kReservedDeviceNames.end()) {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

bool isPathChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

const char* segmentViolation(std::string_view segment, const PackageLimits& limits) noexcept {
    if (segment.empty()) {
        return "empty segment (leading, doubled or trailing slash)";
    }
    if (segment == "." || segment == "..") {
        return "dot segment";
    }
    if (segment.size() > limits.maxSegmentLength) {
        return "segment too long";
    }
    for (const char c : segment) {
        if (c == '\\') {
            return "backslash separator";
        }
        if (c >= 'A' && c <= 'Z') {
            return "uppercase letter";
        }
        if (!isPathChar(c)) {
            return "character outside [a-z0-9._-]";
        }
    }
    if (segment.front() == '.') {
        return "segment starts with a dot";
    }
    if (segment.back() == '.') {
        return "segment ends with a dot";
    }
    if (isReservedDeviceName(segment)) {
        return "reserved device name";
    }
    return nullptr;
}

}  // namespace

namespace detail {

std::string quoted(std::string_view text) {
    constexpr std::size_t kMaxShown = 64;
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string out = "'";
    for (const char c : text.substr(0, kMaxShown)) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte < 0x7F) {
            out.push_back(c);
        } else {
            out += {'\\', 'x', kHex[byte >> 4], kHex[byte & 15]};
        }
    }
    if (text.size() > kMaxShown) {
        out += "...";
    }
    out.push_back('\'');
    return out;
}

std::string_view findAncestorEntry(std::string_view path, const std::function<bool(std::string_view)>& exists) {
    for (std::size_t slash = path.find('/'); slash != std::string_view::npos; slash = path.find('/', slash + 1)) {
        if (exists(path.substr(0, slash))) {
            return path.substr(0, slash);
        }
    }
    return {};
}

}  // namespace detail

std::string_view kindDirectory(EntryKind kind) noexcept {
    switch (kind) {
    case EntryKind::Document:
        return "documents/";
    case EntryKind::Style:
        return "styles/";
    case EntryKind::Binding:
        return "bindings/";
    case EntryKind::Asset:
        return "assets/";
    }
    return {};
}

const char* pathViolation(std::string_view path, const PackageLimits& limits) noexcept {
    if (path.empty()) {
        return "empty path";
    }
    if (path.size() > limits.maxPathLength) {
        return "path too long";
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        const std::string_view segment = path.substr(start, slash == std::string_view::npos ? slash : slash - start);
        if (const char* reason = segmentViolation(segment, limits)) {
            return reason;
        }
        if (slash == std::string_view::npos) {
            return nullptr;
        }
        start = slash + 1;
    }
}

bool isCanonicalPath(std::string_view path, const PackageLimits& limits) noexcept {
    return pathViolation(path, limits) == nullptr;
}

Status resolveReference(
    std::string_view basePath,
    std::string_view reference,
    std::string& out,
    const PackageLimits& limits) {
    out.clear();
    if (reference.empty()) {
        return detail::fail(PackageError::BadReference, 0, "empty reference");
    }
    if (!isCanonicalPath(basePath, limits)) {
        return detail::fail(PackageError::BadReference, 0, "base path is not canonical");
    }

    std::vector<std::string_view> segments;
    if (reference.front() != '/') {
        const std::size_t lastSlash = basePath.rfind('/');
        std::string_view directory = lastSlash == std::string_view::npos ? std::string_view{} : basePath.substr(0, lastSlash);
        while (!directory.empty()) {
            const std::size_t slash = directory.find('/');
            segments.push_back(directory.substr(0, slash));
            if (slash == std::string_view::npos) {
                break;
            }
            directory.remove_prefix(slash + 1);
        }
    } else {
        reference.remove_prefix(1);
    }

    std::size_t start = 0;
    while (true) {
        const std::size_t slash = reference.find('/', start);
        const std::string_view segment = reference.substr(start, slash == std::string_view::npos ? slash : slash - start);
        if (segment.empty()) {
            return detail::fail(PackageError::BadReference, 0, "empty segment in reference");
        }
        if (segment == "..") {
            if (segments.empty()) {
                return detail::fail(PackageError::BadReference, 0, "reference escapes the package root");
            }
            segments.pop_back();
        } else if (segment != ".") {
            segments.push_back(segment);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }

    for (const std::string_view segment : segments) {
        if (!out.empty()) {
            out.push_back('/');
        }
        out.append(segment);
    }
    if (const char* reason = pathViolation(out, limits)) {
        const std::string detailText = std::string("resolved path is not canonical: ") + reason;
        out.clear();
        return detail::fail(PackageError::BadReference, 0, detailText);
    }
    return {};
}

const Entry* Package::find(std::string_view path) const noexcept {
    const auto it = std::lower_bound(
        entries.begin(), entries.end(), path, [](const Entry& entry, std::string_view key) { return entry.path < key; });
    return it != entries.end() && it->path == path ? &*it : nullptr;
}

Status Package::resolve(std::string_view basePath, std::string_view reference, const Entry*& out) const {
    out = nullptr;
    std::string resolved;
    Status status = resolveReference(basePath, reference, resolved, limits);
    if (!status.ok()) {
        return status;
    }
    out = find(resolved);
    if (out == nullptr) {
        return detail::fail(PackageError::BadReference, 0, "no entry at " + resolved);
    }
    return {};
}

}  // namespace uirepo::pkg
