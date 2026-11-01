#pragma once

#include "uirepo/Package.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pkgtest {

using Bytes = std::vector<std::uint8_t>;
using uirepo::pkg::PackageError;

[[noreturn]] inline void fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

inline void requireError(const uirepo::pkg::Status& status, PackageError expected, const std::string& what) {
    if (status.code != expected) {
        fail(
            what + ": expected '" + uirepo::pkg::describe(expected) + "' but got '" + uirepo::pkg::describe(status.code) +
            "' (" + status.detail + ")");
    }
}

inline Bytes bytesOf(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

inline std::uint32_t load32(const Bytes& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

inline void store(Bytes& b, std::size_t at, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        b[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

inline void putLe(Bytes& out, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

inline void tlv(Bytes& out, std::uint16_t tag, const Bytes& value) {
    putLe(out, tag, 2);
    putLe(out, value.size(), 4);
    out.insert(out.end(), value.begin(), value.end());
}

inline Bytes versionValue(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
    Bytes value;
    putLe(value, major, 4);
    putLe(value, minor, 4);
    putLe(value, patch, 4);
    return value;
}

inline Bytes listValue(const std::vector<std::string>& items) {
    Bytes value;
    putLe(value, items.size(), 4);
    for (const std::string& item : items) {
        putLe(value, item.size(), 2);
        value.insert(value.end(), item.begin(), item.end());
    }
    return value;
}

inline Bytes manifestBytes(std::string_view id = "raw.test", std::string_view entryDocument = "documents/main") {
    Bytes out;
    tlv(out, 0x8001, bytesOf(id));
    tlv(out, 0x8002, versionValue(1, 2, 3));
    tlv(out, 0x8003, bytesOf(entryDocument));
    Bytes api;
    putLe(api, 1, 4);
    tlv(out, 0x8004, api);
    return out;
}

struct RawEntry {
    std::uint8_t kind = 1;
    std::string path;
    Bytes data;
    std::optional<std::uint32_t> pathOffset;
    std::optional<std::uint32_t> pathLength;
    std::optional<std::uint64_t> offset;
    std::optional<std::uint64_t> size;
    std::optional<std::uint32_t> crc;
    std::uint8_t recordPad = 0;
};

struct RawSpec {
    std::uint16_t major = 1;
    std::uint16_t minor = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
    Bytes manifest = manifestBytes();
    std::vector<RawEntry> entries;
    std::optional<std::uint64_t> total;
    std::optional<std::uint32_t> manifestSizeField;
    std::optional<std::uint32_t> entryCountField;
    std::optional<std::uint32_t> pathBlobField;
    std::optional<std::uint32_t> manifestCrc;
    std::optional<std::uint32_t> headerCrc;
    bool sealFooter = true;
    std::uint8_t paddingFill = 0;
};

inline std::uint64_t align8(std::uint64_t v) {
    return (v + 7) & ~std::uint64_t{7};
}

inline void padTo(Bytes& out, std::size_t size, std::uint8_t fill) {
    out.resize(size, fill);
}

inline void reseal(Bytes& b) {
    using namespace uirepo::pkg;
    if (b.size() < kHeaderSize + kFooterSize) {
        return;
    }
    const std::uint64_t manifestSize = load32(b, 24);
    if (kHeaderSize + manifestSize <= b.size() - kFooterSize) {
        store(b, 36, crc32(std::span<const std::uint8_t>(b.data() + kHeaderSize, manifestSize)), 4);
    }
    store(b, 44, crc32(std::span<const std::uint8_t>(b.data(), 44)), 4);
    const Digest digest = sha256(std::span<const std::uint8_t>(b.data(), b.size() - kFooterSize));
    std::copy(digest.begin(), digest.end(), b.end() - static_cast<std::ptrdiff_t>(kFooterSize));
}

inline Bytes buildRaw(const RawSpec& spec) {
    using namespace uirepo::pkg;
    Bytes pathBlob;
    for (const RawEntry& e : spec.entries) {
        pathBlob.insert(pathBlob.end(), e.path.begin(), e.path.end());
    }
    const std::uint64_t tableOffset = align8(kHeaderSize + spec.manifest.size());
    const std::uint64_t pathOffset = tableOffset + spec.entries.size() * kEntryRecordSize;
    const std::uint64_t dataOffset = align8(pathOffset + pathBlob.size());
    std::uint64_t cursor = dataOffset;
    std::vector<std::uint64_t> offsets;
    for (const RawEntry& e : spec.entries) {
        offsets.push_back(cursor);
        cursor = align8(cursor + e.data.size());
    }
    const std::uint64_t computedTotal = cursor + kFooterSize;

    Bytes out(kMagic.begin(), kMagic.end());
    putLe(out, spec.major, 2);
    putLe(out, spec.minor, 2);
    putLe(out, spec.flags, 4);
    putLe(out, spec.total.value_or(computedTotal), 8);
    putLe(out, spec.manifestSizeField.value_or(static_cast<std::uint32_t>(spec.manifest.size())), 4);
    putLe(out, spec.entryCountField.value_or(static_cast<std::uint32_t>(spec.entries.size())), 4);
    putLe(out, spec.pathBlobField.value_or(static_cast<std::uint32_t>(pathBlob.size())), 4);
    putLe(out, spec.manifestCrc.value_or(crc32(spec.manifest)), 4);
    putLe(out, spec.reserved, 4);
    putLe(out, spec.headerCrc.value_or(crc32(std::span<const std::uint8_t>(out.data(), out.size()))), 4);

    out.insert(out.end(), spec.manifest.begin(), spec.manifest.end());
    padTo(out, static_cast<std::size_t>(tableOffset), spec.paddingFill);

    std::uint32_t nextPath = 0;
    for (std::size_t i = 0; i < spec.entries.size(); ++i) {
        const RawEntry& e = spec.entries[i];
        putLe(out, e.pathOffset.value_or(nextPath), 4);
        putLe(out, e.pathLength.value_or(static_cast<std::uint32_t>(e.path.size())), 4);
        out.push_back(e.kind);
        out.insert(out.end(), 3, e.recordPad);
        putLe(out, e.crc.value_or(crc32(e.data)), 4);
        putLe(out, e.offset.value_or(offsets[i]), 8);
        putLe(out, e.size.value_or(e.data.size()), 8);
        nextPath += static_cast<std::uint32_t>(e.path.size());
    }
    out.insert(out.end(), pathBlob.begin(), pathBlob.end());
    padTo(out, static_cast<std::size_t>(dataOffset), spec.paddingFill);
    for (const RawEntry& e : spec.entries) {
        out.insert(out.end(), e.data.begin(), e.data.end());
        padTo(out, static_cast<std::size_t>(align8(out.size())), spec.paddingFill);
    }

    if (spec.sealFooter) {
        const Digest digest = sha256(out);
        out.insert(out.end(), digest.begin(), digest.end());
    } else {
        out.insert(out.end(), kFooterSize, 0xEE);
    }
    return out;
}

inline RawSpec basicSpec() {
    RawSpec spec;
    spec.entries.push_back(RawEntry{.kind = 1, .path = "documents/main", .data = bytesOf("root-document")});
    spec.entries.push_back(RawEntry{.kind = 2, .path = "styles/theme", .data = bytesOf("style")});
    spec.entries.push_back(RawEntry{.kind = 4, .path = "assets/blob.bin", .data = Bytes{0, 1, 2, 3, 4}});
    return spec;
}

inline uirepo::pkg::ReadResult read(const Bytes& bytes, const uirepo::pkg::ReadOptions& options = {}) {
    return uirepo::pkg::readPackage(bytes, options);
}

void runMalformedTests();

}  // namespace pkgtest
