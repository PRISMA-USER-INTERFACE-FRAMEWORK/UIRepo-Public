#include "PackageTestSupport.h"

#include <array>
#include <cstdio>
#include <numeric>

namespace {

using namespace pkgtest;
using uirepo::pkg::Dependency;
using uirepo::pkg::EntryKind;
using uirepo::pkg::Manifest;
using uirepo::pkg::PackageBuilder;
using uirepo::pkg::PackageLimits;
using uirepo::pkg::ReadOptions;
using uirepo::pkg::Version;

std::string toHex(const Bytes& bytes) {
    std::string text;
    char pair[3];
    for (const std::uint8_t byte : bytes) {
        std::snprintf(pair, sizeof(pair), "%02x", static_cast<unsigned>(byte));
        text += pair;
    }
    return text;
}

Bytes fromHex(std::string_view text) {
    Bytes bytes;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    }
    return bytes;
}

std::string digestHex(const uirepo::pkg::Digest& digest) {
    return toHex(Bytes(digest.begin(), digest.end()));
}

Manifest fullManifest() {
    Manifest m;
    m.id = "ui.example";
    m.version = Version{2, 1, 0};
    m.displayName = "Example UI";
    m.entryDocument = "documents/main";
    m.minApiVersion = 1;
    m.requiredCapabilities = {"text-layout", "retained-documents"};
    m.optionalCapabilities = {"offscreen-surfaces"};
    m.dependencies = {Dependency{"zeta.lib", Version{1, 0, 0}}, Dependency{"alpha.lib", Version{2, 3, 4}}};
    return m;
}

struct Item {
    EntryKind kind;
    std::string path;
    Bytes data;
};

std::vector<Item> sampleItems() {
    Bytes icon(1000);
    std::uint32_t state = 12345;
    for (std::uint8_t& byte : icon) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    return {
        {EntryKind::Document, "documents/main", bytesOf("<main>")},
        {EntryKind::Document, "documents/sub/panel", bytesOf("<panel>")},
        {EntryKind::Style, "styles/theme", bytesOf("theme")},
        {EntryKind::Binding, "bindings/actions", bytesOf("actions")},
        {EntryKind::Asset, "assets/img/icon.png", icon},
        {EntryKind::Asset, "assets/empty", {}},
        {EntryKind::Asset, "assets/odd", Bytes{9, 8, 7}},
    };
}

Bytes buildFrom(const Manifest& manifest, const std::vector<Item>& items, const std::vector<std::size_t>& order) {
    PackageBuilder builder(manifest);
    for (const std::size_t index : order) {
        const Item& item = items[index];
        const auto status = builder.add(item.kind, item.path, item.data);
        require(status.ok(), "add failed for " + item.path + ": " + status.detail);
    }
    Bytes out;
    const auto status = builder.build(out);
    require(status.ok(), "build failed: " + status.detail);
    return out;
}

void testHashVectors() {
    using namespace uirepo::pkg;
    require(crc32(bytesOf("123456789")) == 0xCBF43926u, "crc32 check value is wrong");
    require(crc32({}) == 0, "crc32 of empty input must be zero");
    const Bytes whole = bytesOf("hello, uipkg");
    const std::uint32_t split = crc32(Bytes(whole.begin() + 5, whole.end()), crc32(Bytes(whole.begin(), whole.begin() + 5)));
    require(split == crc32(whole), "incremental crc32 must equal one-shot crc32");

    require(
        digestHex(sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 of empty input is wrong");
    require(
        digestHex(sha256(bytesOf("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 of abc is wrong");
    require(
        digestHex(sha256(bytesOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "sha256 of the two-block message is wrong");

    const Bytes million(1000000, 'a');
    require(
        digestHex(sha256(million)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "sha256 of one million 'a' is wrong");

    Sha256 chunked;
    for (std::size_t at = 0, step = 1; at < million.size(); at += step, step = step % 97 + 1) {
        chunked.update(std::span<const std::uint8_t>(million.data() + at, std::min(step, million.size() - at)));
    }
    require(chunked.finish() == sha256(million), "chunked sha256 must equal one-shot sha256");
}

void testRoundTrip() {
    const std::vector<Item> items = sampleItems();
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), 0);
    const Bytes bytes = buildFrom(fullManifest(), items, order);

    const auto result = read(bytes);
    require(result.status.ok(), "round trip read failed: " + result.status.detail);
    const auto& pkg = result.package;
    require(pkg.formatMajor == uirepo::pkg::kFormatMajor && pkg.formatMinor == uirepo::pkg::kFormatMinor, "format version lost");
    require(!pkg.newerMinor, "current minor must not be flagged newer");

    Manifest expected = fullManifest();
    expected.requiredCapabilities = {"retained-documents", "text-layout"};
    expected.dependencies = {Dependency{"alpha.lib", Version{2, 3, 4}}, Dependency{"zeta.lib", Version{1, 0, 0}}};
    require(pkg.manifest == expected, "manifest did not round trip in canonical form");

    require(pkg.entries.size() == items.size(), "entry count changed");
    for (const Item& item : items) {
        const auto* entry = pkg.find(item.path);
        require(entry != nullptr, "missing entry " + item.path);
        require(entry->kind == item.kind, "kind changed for " + item.path);
        require(Bytes(entry->data.begin(), entry->data.end()) == item.data, "data changed for " + item.path);
        require(entry->crc32 == uirepo::pkg::crc32(item.data), "crc recorded wrongly for " + item.path);
    }
    for (std::size_t i = 1; i < pkg.entries.size(); ++i) {
        require(pkg.entries[i - 1].path < pkg.entries[i].path, "entries are not path sorted");
    }
    require(pkg.find("documents/missing") == nullptr, "find returned a phantom entry");

    const auto digest = uirepo::pkg::sha256(std::span<const std::uint8_t>(bytes.data(), bytes.size() - uirepo::pkg::kFooterSize));
    require(pkg.digest == digest, "package digest is not the sha256 of the preceding bytes");

    PackageBuilder again(pkg.manifest);
    for (const auto& entry : pkg.entries) {
        require(again.add(entry.kind, entry.path, entry.data).ok(), "re-add failed");
    }
    Bytes repacked;
    require(again.build(repacked).ok() && repacked == bytes, "unpack then pack must reproduce the same bytes");
    require(uirepo::pkg::validatePackage(bytes).ok(), "validatePackage rejected a valid package");
}

void testDeterminism() {
    const std::vector<Item> items = sampleItems();
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), 0);
    const Bytes reference = buildFrom(fullManifest(), items, order);

    std::size_t permutations = 0;
    do {
        require(buildFrom(fullManifest(), items, order) == reference, "insertion order changed the output bytes");
        ++permutations;
    } while (std::next_permutation(order.begin(), order.end()));
    require(permutations == 5040, "did not visit every permutation");

    Manifest shuffled = fullManifest();
    std::reverse(shuffled.requiredCapabilities.begin(), shuffled.requiredCapabilities.end());
    std::reverse(shuffled.dependencies.begin(), shuffled.dependencies.end());
    std::iota(order.begin(), order.end(), 0);
    require(buildFrom(shuffled, items, order) == reference, "capability or dependency order changed the output bytes");
}

constexpr std::string_view kGoldenHex =
    "895549504b470d0a010000000000000068010000000000006a00000003000000"
    "29000000906e45c0000000008d686099010006000000476f6c64656e01800a00"
    "0000676f6c64656e2e706b6702800c0000000100000002000000030000000380"
    "0e000000646f63756d656e74732f6d61696e0480040000000100000005801800"
    "000001000000120072657461696e65642d646f63756d656e7473000000000000"
    "000000000f00000004000000ccd35a5130010000000000000500000000000000"
    "0f0000000e000000010000005bf9f41638010000000000000400000000000000"
    "1d0000000c000000020000000bcf0e1b40010000000000000100000000000000"
    "6173736574732f626c6f622e62696e646f63756d656e74732f6d61696e737479"
    "6c65732f7468656d65000000000000000001020304000000726f6f7400000000"
    "7300000000000000ebca38a3580bb80b44c4ac658ae61c3e77261367a35de6b6"
    "4a4927a6398b3806";

Bytes goldenPackage() {
    Manifest m;
    m.id = "golden.pkg";
    m.version = Version{1, 2, 3};
    m.displayName = "Golden";
    m.entryDocument = "documents/main";
    m.requiredCapabilities = {"retained-documents"};
    PackageBuilder builder(m);
    require(builder.add(EntryKind::Document, "documents/main", bytesOf("root")).ok(), "golden add document");
    require(builder.add(EntryKind::Style, "styles/theme", bytesOf("s")).ok(), "golden add style");
    require(builder.add(EntryKind::Asset, "assets/blob.bin", Bytes{0, 1, 2, 3, 4}).ok(), "golden add asset");
    Bytes out;
    require(builder.build(out).ok(), "golden build");
    return out;
}

void testGolden() {
    const Bytes built = goldenPackage();
    if (toHex(built) != kGoldenHex) {
        std::cerr << "golden bytes differ; current output:\n" << toHex(built) << '\n';
        fail("golden byte vector mismatch (a format change must bump the version and the vector)");
    }
    const auto result = read(fromHex(kGoldenHex));
    require(result.status.ok(), "golden vector no longer reads: " + result.status.detail);
    require(result.package.entries.size() == 3 && result.package.manifest.id == "golden.pkg", "golden vector contents changed");
}

void testBuilderRejections() {
    struct Case {
        EntryKind kind;
        const char* path;
        PackageError expected;
    };
    const std::array<Case, 20> cases = {{
        {EntryKind::Document, "", PackageError::BadPath},
        {EntryKind::Document, "/documents/x", PackageError::BadPath},
        {EntryKind::Document, "documents/../x", PackageError::BadPath},
        {EntryKind::Document, "documents/./x", PackageError::BadPath},
        {EntryKind::Document, "documents//x", PackageError::BadPath},
        {EntryKind::Document, "documents/x/", PackageError::BadPath},
        {EntryKind::Document, "documents\\x", PackageError::BadPath},
        {EntryKind::Document, "Documents/x", PackageError::BadPath},
        {EntryKind::Document, "documents/X", PackageError::BadPath},
        {EntryKind::Document, "documents/a b", PackageError::BadPath},
        {EntryKind::Document, "documents/.hidden", PackageError::BadPath},
        {EntryKind::Document, "documents/trailing.", PackageError::BadPath},
        {EntryKind::Document, "documents/con", PackageError::BadPath},
        {EntryKind::Document, "documents/nul.txt", PackageError::BadPath},
        {EntryKind::Document, "documents/com1", PackageError::BadPath},
        {EntryKind::Document, "documents/c:x", PackageError::BadPath},
        {EntryKind::Document, "styles/x", PackageError::KindPathMismatch},
        {EntryKind::Asset, "documents/x", PackageError::KindPathMismatch},
        {EntryKind::Asset, "assets", PackageError::KindPathMismatch},
        {static_cast<EntryKind>(9), "assets/x", PackageError::UnknownEntryKind},
    }};
    for (const Case& c : cases) {
        PackageBuilder builder(fullManifest());
        const auto status = builder.add(c.kind, c.path, {});
        requireError(status, c.expected, std::string("builder path '") + c.path + "'");
    }
    {
        PackageBuilder builder(fullManifest());
        require(builder.add(EntryKind::Document, "documents/main", {}).ok(), "first add");
        requireError(builder.add(EntryKind::Document, "documents/main", {}), PackageError::DuplicatePath, "duplicate add");
    }
    {
        PackageBuilder builder(fullManifest());
        require(builder.add(EntryKind::Style, "styles/main", {}).ok(), "add style");
        Bytes out;
        requireError(builder.build(out), PackageError::EntryDocumentMissing, "no entry document");
        require(out.empty(), "failed build must leave the output empty");
    }

    const auto buildWith = [](const Manifest& manifest) {
        PackageBuilder builder(manifest);
        require(builder.add(EntryKind::Document, "documents/main", {}).ok(), "add document");
        Bytes out;
        return builder.build(out);
    };
    Manifest bad = fullManifest();
    bad.id = "Bad Id";
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "uppercase id");
    bad = fullManifest();
    bad.entryDocument = "styles/main";
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "entry document outside documents/");
    bad = fullManifest();
    bad.minApiVersion = 0;
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "zero api version");
    bad = fullManifest();
    bad.requiredCapabilities = {"text-layout", "text-layout"};
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "duplicate capability");
    bad = fullManifest();
    bad.optionalCapabilities = {"text-layout"};
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "capability both required and optional");
    bad = fullManifest();
    bad.dependencies.push_back(Dependency{"ui.example", Version{}});
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "self dependency");
    bad = fullManifest();
    bad.dependencies.push_back(Dependency{"alpha.lib", Version{}});
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "duplicate dependency");
    bad = fullManifest();
    bad.displayName = "tab\there";
    requireError(buildWith(bad), PackageError::ManifestInvalidValue, "control character in display name");
}

void testPaths() {
    using uirepo::pkg::isCanonicalPath;
    for (const char* ok : {"documents/main", "a", "a/b/c", "assets/img/icon_2-x.v1.png", "x.y"}) {
        require(isCanonicalPath(ok), std::string("rejected canonical path ") + ok);
    }
    const PackageLimits tight{.maxPathLength = 8, .maxSegmentLength = 4};
    require(!isCanonicalPath("abcde", tight), "segment length limit not enforced");
    require(isCanonicalPath("ab/cd/ef", tight), "path at the length limit must pass");
    require(!isCanonicalPath("ab/cd/efg", tight), "path length limit not enforced");
    require(std::string(uirepo::pkg::pathViolation("documents/..")) == "dot segment", "violation reason is not reported");
    require(uirepo::pkg::pathViolation("documents/ok") == nullptr, "pathViolation must be null for a canonical path");
    require(
        std::string(uirepo::pkg::pathViolation("/documents/a")).starts_with("empty segment"), "leading slash reason is not reported");
}

void testFileDirectoryConflicts() {
    const auto addAll = [](const std::vector<std::string>& paths) {
        PackageBuilder builder(fullManifest());
        uirepo::pkg::Status last;
        for (const std::string& path : paths) {
            last = builder.add(EntryKind::Asset, path, {});
            if (!last.ok()) {
                break;
            }
        }
        return last;
    };
    const std::vector<std::vector<std::string>> conflicts = {
        {"assets/a", "assets/a/b"},
        {"assets/a/b", "assets/a"},
        {"assets/a/b", "assets/a/b/c/d"},
        {"assets/a/b/c/d", "assets/a/b"},
        {"assets/a", "assets/a-x", "assets/a.x", "assets/a/b"},
        {"assets/a/b", "assets/a.x", "assets/a-x", "assets/a"},
    };
    for (const std::vector<std::string>& paths : conflicts) {
        requireError(addAll(paths), PackageError::FileDirectoryConflict, "builder conflict starting with " + paths.front());
    }
    require(addAll({"assets/a", "assets/a-x", "assets/a.x", "assets/ab", "assets/b/a"}).ok(), "siblings sharing a name prefix must be accepted");
    require(std::string(describe(PackageError::FileDirectoryConflict)) != "unknown package error", "conflict code has no description");
}

void testDetailIsPrintable() {
    const auto printable = [](const std::string& text) {
        return std::all_of(text.begin(), text.end(), [](char c) { return c >= 0x20 && c < 0x7F; });
    };
    const std::string hostile = std::string("documents/a\n\x01\xFF") + std::string(300, 'z');
    PackageBuilder builder(fullManifest());
    const auto status = builder.add(EntryKind::Document, hostile, {});
    requireError(status, PackageError::BadPath, "builder hostile path");
    require(printable(status.detail), "builder detail carries raw control bytes");
    require(status.detail.size() < 200, "builder detail is not truncated");
    require(status.detail.find("\\x0a") != std::string::npos, "control byte is not escaped");
}

void testResolveUsesReadLimits() {
    std::string relative;
    for (int i = 0; i < 5; ++i) {
        relative += (i == 0 ? "" : "/") + std::string(60, 'a');
    }
    PackageLimits wide;
    wide.maxPathLength = 400;
    PackageBuilder builder(fullManifest(), wide);
    require(builder.add(EntryKind::Document, "documents/main", {}).ok(), "add main");
    require(builder.add(EntryKind::Asset, "assets/" + relative, {}).ok(), "add long entry");
    Bytes bytes;
    require(builder.build(bytes).ok(), "build long package");
    requireError(read(bytes).status, PackageError::BadPath, "default limits refuse the long path");

    ReadOptions options;
    options.limits = wide;
    const auto result = read(bytes, options);
    require(result.status.ok(), "wide read: " + result.status.detail);
    const uirepo::pkg::Entry* entry = nullptr;
    const auto status = result.package.resolve("documents/main", "/assets/" + relative, entry);
    require(status.ok() && entry != nullptr, "resolve must use the limits the package was read with: " + status.detail);
}

void testReferences() {
    using uirepo::pkg::resolveReference;
    std::string out;
    struct Case {
        const char* base;
        const char* reference;
        const char* expected;
    };
    for (const Case& c : std::array<Case, 8>{{
             {"documents/main", "../styles/theme", "styles/theme"},
             {"documents/main", "other", "documents/other"},
             {"documents/main", "./other", "documents/other"},
             {"documents/sub/panel", "../main", "documents/main"},
             {"documents/sub/panel", "/assets/img/a.png", "assets/img/a.png"},
             {"documents/main", "a/./b/../c", "documents/a/c"},
             {"main", "x", "x"},
             {"documents/main", "../assets/../styles/s", "styles/s"},
         }}) {
        const auto status = resolveReference(c.base, c.reference, out);
        require(status.ok() && out == c.expected, std::string("reference ") + c.reference + " from " + c.base + " resolved to '" + out + "'");
    }
    for (const char* reference : {"", "../../x", "../../../x", "/..", "a//b", "a/", "A", "a\\b", "http:x", "/", "..", "con"}) {
        const auto status = resolveReference("documents/main", reference, out);
        requireError(status, PackageError::BadReference, std::string("reference '") + reference + "'");
        require(out.empty(), "failed resolution must clear the output");
    }
    requireError(resolveReference("Documents/Main", "x", out), PackageError::BadReference, "non canonical base");

    const Bytes bytes = buildFrom(fullManifest(), sampleItems(), {0, 1, 2, 3, 4, 5, 6});
    const auto result = read(bytes);
    require(result.status.ok(), "read for resolve test");
    const uirepo::pkg::Entry* entry = nullptr;
    require(result.package.resolve("documents/main", "../styles/theme", entry).ok() && entry != nullptr && entry->path == "styles/theme", "package resolve failed");
    requireError(result.package.resolve("documents/main", "../styles/nothing", entry), PackageError::BadReference, "missing target");
    require(entry == nullptr, "failed resolve must null the entry");
}

void testLimits() {
    const std::vector<Item> items = sampleItems();
    {
        PackageLimits limits;
        limits.maxEntries = 2;
        PackageBuilder builder(fullManifest(), limits);
        require(builder.add(items[0].kind, items[0].path, items[0].data).ok(), "limit add 1");
        require(builder.add(items[2].kind, items[2].path, items[2].data).ok(), "limit add 2");
        requireError(builder.add(items[3].kind, items[3].path, items[3].data), PackageError::LimitExceeded, "entry count limit");
    }
    {
        PackageLimits limits;
        limits.maxEntrySize = 10;
        PackageBuilder builder(fullManifest(), limits);
        requireError(builder.add(items[4].kind, items[4].path, items[4].data), PackageError::LimitExceeded, "entry size limit");
    }
    {
        PackageLimits limits;
        limits.maxTotalSize = 128;
        PackageBuilder builder(fullManifest(), limits);
        require(builder.add(EntryKind::Document, "documents/main", bytesOf("x")).ok(), "total limit add");
        Bytes out;
        requireError(builder.build(out), PackageError::LimitExceeded, "total size limit");
    }
    {
        PackageLimits limits;
        limits.maxManifestSize = 16;
        PackageBuilder builder(fullManifest(), limits);
        require(builder.add(EntryKind::Document, "documents/main", bytesOf("x")).ok(), "manifest limit add");
        Bytes out;
        requireError(builder.build(out), PackageError::LimitExceeded, "manifest size limit");
    }

    const Bytes bytes = buildFrom(fullManifest(), items, {0, 1, 2, 3, 4, 5, 6});
    ReadOptions options;
    options.limits.maxEntries = 6;
    requireError(read(bytes, options).status, PackageError::LimitExceeded, "reader entry count limit");
    options = {};
    options.limits.maxEntrySize = 999;
    requireError(read(bytes, options).status, PackageError::LimitExceeded, "reader entry size limit");
    options = {};
    options.limits.maxTotalSize = bytes.size() - 1;
    requireError(read(bytes, options).status, PackageError::LimitExceeded, "reader total size limit");
    options = {};
    options.limits.maxManifestSize = 8;
    requireError(read(bytes, options).status, PackageError::LimitExceeded, "reader manifest size limit");
    options = {};
    options.limits.maxPathLength = 10;
    requireError(read(bytes, options).status, PackageError::LimitExceeded, "reader path table limit");
    options = {};
    options.limits.maxDependencies = 1;
    requireError(read(bytes, options).status, PackageError::LimitExceeded, "reader dependency limit");
}

}  // namespace

int main() {
    testHashVectors();
    testRoundTrip();
    testDeterminism();
    testGolden();
    testBuilderRejections();
    testPaths();
    testFileDirectoryConflicts();
    testDetailIsPrintable();
    testResolveUsesReadLimits();
    testReferences();
    testLimits();
    pkgtest::runMalformedTests();
    std::cout << "uirepo package tests passed\n";
    return 0;
}
