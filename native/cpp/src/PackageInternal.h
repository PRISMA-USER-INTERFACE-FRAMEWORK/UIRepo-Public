#pragma once

#include "uirepo/Package.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace uirepo::pkg::detail {

inline constexpr std::uint16_t kTagDisplayName = 0x0001;
inline constexpr std::uint16_t kTagOptionalCapabilities = 0x0002;
inline constexpr std::uint16_t kTagId = 0x8001;
inline constexpr std::uint16_t kTagVersion = 0x8002;
inline constexpr std::uint16_t kTagEntryDocument = 0x8003;
inline constexpr std::uint16_t kTagMinApiVersion = 0x8004;
inline constexpr std::uint16_t kTagRequiredCapabilities = 0x8005;
inline constexpr std::uint16_t kTagDependencies = 0x8006;
inline constexpr std::uint16_t kCriticalTagBit = 0x8000;

[[nodiscard]] constexpr std::uint64_t align8(std::uint64_t value) noexcept {
    return (value + 7u) & ~std::uint64_t{7};
}

[[nodiscard]] inline Status fail(PackageError code, std::uint64_t offset, std::string detail) {
    return Status{code, offset, std::move(detail)};
}

inline void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

inline void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

inline void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] inline std::uint16_t get16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

[[nodiscard]] inline std::uint32_t get32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] inline std::uint64_t get64(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(get32(p)) | (static_cast<std::uint64_t>(get32(p + 4)) << 32);
}

[[nodiscard]] bool isIdentifier(std::string_view text) noexcept;
[[nodiscard]] std::string quoted(std::string_view text);
[[nodiscard]] std::string_view findAncestorEntry(std::string_view path, const std::function<bool(std::string_view)>& exists);
[[nodiscard]] Status validateManifest(const Manifest& manifest, const PackageLimits& limits);
[[nodiscard]] std::vector<std::uint8_t> encodeManifest(const Manifest& manifest);
[[nodiscard]] Status decodeManifest(
    std::span<const std::uint8_t> bytes,
    std::uint64_t baseOffset,
    const PackageLimits& limits,
    bool tolerateUnknownOptional,
    Manifest& out);

}  // namespace uirepo::pkg::detail
