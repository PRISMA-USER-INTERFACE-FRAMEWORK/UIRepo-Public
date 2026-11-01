#pragma once

#include "uirepo/PackageHost.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace pkghosttest {

using Bytes = std::vector<std::uint8_t>;
using uirepo::pkg::EntryKind;
using uirepo::pkghost::HostError;

inline constexpr std::uint32_t kHostApi = 2;

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

inline Bytes bytesOf(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

struct Part {
    EntryKind kind{};
    std::string path;
    std::string text;
};

inline uirepo::pkg::Manifest manifestFor(const std::string& id, const std::string& entry = "documents/main") {
    uirepo::pkg::Manifest manifest;
    manifest.id = id;
    manifest.version = {1, 0, 0};
    manifest.entryDocument = entry;
    manifest.requiredCapabilities = {"retained-documents"};
    return manifest;
}

inline Bytes buildPackage(
    const uirepo::pkg::Manifest& manifest,
    const std::vector<Part>& parts,
    const uirepo::pkg::PackageLimits& limits = {}) {
    uirepo::pkg::PackageBuilder builder(manifest, limits);
    for (const Part& part : parts) {
        const Bytes data = bytesOf(part.text);
        const uirepo::pkg::Status added = builder.add(part.kind, part.path, data);
        require(added.ok(), "builder refused " + part.path + ": " + added.detail);
    }
    Bytes out;
    const uirepo::pkg::Status built = builder.build(out);
    require(built.ok(), "builder refused the package: " + built.detail);
    return out;
}

inline uirepo::pkghost::HostOptions hostOptions() {
    uirepo::pkghost::HostOptions options;
    options.hostApiVersion = kHostApi;
    return options;
}

inline std::string documentJson(const std::string& id, const std::string& children = "") {
    return R"({"format":"uirepo.ui","version":1,"id":")" + id + R"(","children":[)" + children + "]}";
}

inline void requireRefused(
    const uirepo::pkghost::LoadResult& result,
    HostError expected,
    const std::string& what) {
    if (result.status.code != expected) {
        require(
            false,
            what + ": expected '" + uirepo::pkghost::describe(expected) + "' but got '" +
                uirepo::pkghost::describe(result.status.code) + "' (" + result.status.detail + ")");
    }
    require(result.package.documents.empty(), what + ": a refused load exposed documents");
    require(result.package.assets.empty(), what + ": a refused load exposed assets");
    require(result.package.styles.empty() && result.package.bindings.empty(), what + ": a refused load exposed raw entries");
    require(result.package.storage.empty(), what + ": a refused load kept the package bytes");
    require(!result.status.detail.empty() || expected == HostError::Internal, what + ": refusal has no detail");
}

} // namespace pkghosttest
