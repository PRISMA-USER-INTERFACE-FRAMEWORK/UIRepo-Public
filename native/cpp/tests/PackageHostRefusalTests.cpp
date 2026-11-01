#include "PackageHostTestSupport.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace pkghosttest;
namespace host = uirepo::pkghost;
namespace pkg = uirepo::pkg;
namespace declarative = uirepo::declarative;

namespace {

constexpr std::array<std::string_view, 3> kHostCapabilitiesWithFormats = {"retained-documents", "styles", "bindings"};

host::LoadResult load(const Bytes& bytes, const host::HostOptions& options = hostOptions()) {
    return host::loadPackage(std::span<const std::uint8_t>(bytes), options);
}

Part mainDocument(const std::string& children = "") {
    return {EntryKind::Document, "documents/main", documentJson("main", children)};
}

bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

std::string imageNode(const std::string& id, const std::string& source) {
    return R"({"type":"image","id":")" + id + R"(","source":")" + source + R"(","naturalSize":[8,8]})";
}

void testCodesAreStable() {
    static_assert(static_cast<int>(HostError::Ok) == 0 && static_cast<int>(HostError::InvalidOptions) == 1 &&
                  static_cast<int>(HostError::Container) == 2 && static_cast<int>(HostError::RequiredFormatUnsupported) == 3 &&
                  static_cast<int>(HostError::DocumentRefused) == 4 && static_cast<int>(HostError::LimitExceeded) == 5 &&
                  static_cast<int>(HostError::BadAssetReference) == 6 && static_cast<int>(HostError::MissingAsset) == 7 &&
                  static_cast<int>(HostError::NotAnAsset) == 8 && static_cast<int>(HostError::OutOfMemory) == 9 &&
                  static_cast<int>(HostError::Internal) == 10);
    for (int code = 0; code <= 10; ++code) {
        const char* text = host::describe(static_cast<HostError>(code));
        require(text != nullptr && text[0] != '\0', "every host code has a description");
    }
    const char* unknown = host::describe(static_cast<HostError>(999));
    require(unknown != nullptr && unknown[0] != '\0', "an unknown code still has a description");
}

void testBadDeclarativeDocument() {
    const Bytes unknownField = buildPackage(
        manifestFor("ui.bad"),
        {mainDocument(),
         {EntryKind::Document, "documents/broken", R"({"format":"uirepo.ui","version":1,"id":"b","bogus":1})"}});
    host::LoadResult result = load(unknownField);
    requireRefused(result, HostError::DocumentRefused, "unknown field in a second document");
    require(result.status.documentError == declarative::ErrorCode::UnknownField, "declarative code is carried");
    require(contains(result.status.detail, "documents/broken") && contains(result.status.detail, "unknown_field") &&
                contains(result.status.detail, "/bogus"),
            "detail names the document, the code and the path: " + result.status.detail);

    struct Case {
        const char* what;
        std::string text;
        declarative::ErrorCode code;
    };
    const std::vector<Case> cases = {
        {"empty document", "", declarative::ErrorCode::SyntaxError},
        {"not json", "<html></html>", declarative::ErrorCode::SyntaxError},
        {"truncated json", R"({"format":"uirepo.ui","version":1,"id":"r")", declarative::ErrorCode::SyntaxError},
        {"wrong format", R"({"format":"other","version":1,"id":"r"})", declarative::ErrorCode::UnsupportedFormat},
        {"future version", R"({"format":"uirepo.ui","version":2,"id":"r"})", declarative::ErrorCode::UnsupportedVersion},
        {"invalid utf-8", std::string(R"({"format":"uirepo.ui","version":1,"id":"r","x-a":")") + "\xC3\x28" + R"("})",
         declarative::ErrorCode::InvalidUtf8},
        {"duplicate id", documentJson("r", R"({"type":"toggle","id":"t"},{"type":"toggle","id":"t"})"),
         declarative::ErrorCode::DuplicateId},
    };
    for (const Case& test : cases) {
        const Bytes bytes = buildPackage(manifestFor("ui.bad"), {{EntryKind::Document, "documents/main", test.text}});
        host::LoadResult refused = load(bytes);
        requireRefused(refused, HostError::DocumentRefused, test.what);
        require(refused.status.documentError == test.code, std::string(test.what) + ": wrong declarative code: " + refused.status.detail);
        require(contains(refused.status.detail, "documents/main"), std::string(test.what) + ": detail names the document");
    }

    std::string deep;
    for (int level = 0; level < 200; ++level) {
        deep += R"({"type":"panel","id":"p)" + std::to_string(level) + R"(","children":[)";
    }
    for (int level = 0; level < 200; ++level) {
        deep += "]}";
    }
    requireRefused(
        load(buildPackage(manifestFor("ui.bad"), {mainDocument(deep)})),
        HostError::DocumentRefused,
        "nesting beyond the declarative depth limit");
}

void testAssetReferenceRefusals() {
    const std::vector<Part> assets = {
        {EntryKind::Asset, "assets/logo.png", "png"},
        {EntryKind::Style, "styles/theme", "s"},
        {EntryKind::Document, "documents/other", documentJson("other")},
    };
    const auto build = [&assets](const std::string& source) {
        std::vector<Part> parts = assets;
        parts.push_back(mainDocument(imageNode("pic", source)));
        return buildPackage(manifestFor("ui.refs"), parts);
    };

    require(load(build("/assets/logo.png")).ok(), "control: the reference resolves");

    host::LoadResult missing = load(build("/assets/missing.png"));
    requireRefused(missing, HostError::MissingAsset, "missing asset");
    require(contains(missing.status.detail, "documents/main") && contains(missing.status.detail, "'pic'") &&
                contains(missing.status.detail, "/assets/missing.png"),
            "missing asset detail names document, node and reference: " + missing.status.detail);

    requireRefused(load(build("../../../x.png")), HostError::BadAssetReference, "reference above the package root");
    requireRefused(load(build("/assets/Logo.PNG")), HostError::BadAssetReference, "upper case reference");
    requireRefused(load(build("assets\\\\logo.png")), HostError::BadAssetReference, "backslash reference");
    requireRefused(load(build("/assets//logo.png")), HostError::BadAssetReference, "empty segment");
    requireRefused(load(build("http://host/a.png")), HostError::BadAssetReference, "scheme reference");
    requireRefused(load(build("/assets/.hidden")), HostError::BadAssetReference, "hidden file reference");
    requireRefused(load(build("/documents/other")), HostError::NotAnAsset, "reference to a document");
    requireRefused(load(build("/styles/theme")), HostError::NotAnAsset, "reference to a style");
    requireRefused(load(build("/assets")), HostError::MissingAsset, "reference to a directory name");

    host::LoadResult video = load(buildPackage(
        manifestFor("ui.refs"),
        {mainDocument(R"({"type":"video","id":"clip","source":"/assets/none.bk2","naturalSize":[4,4]})")}));
    requireRefused(video, HostError::MissingAsset, "missing video asset");

    const std::string longReference = "/assets/" + std::string(400, 'a');
    host::LoadResult longResult = load(build(longReference));
    requireRefused(longResult, HostError::BadAssetReference, "over-long segment");
    require(longResult.status.detail.size() < 400 && contains(longResult.status.detail, "..."),
            "a long reference is truncated in the detail");

    host::LoadResult nonAscii = load(build("/assets/\\u00e9\\u00e9.png"));
    requireRefused(nonAscii, HostError::BadAssetReference, "non-ascii reference");
    bool raw = false;
    for (const char c : nonAscii.status.detail) {
        raw = raw || static_cast<unsigned char>(c) >= 0x80 || static_cast<unsigned char>(c) < 0x20;
    }
    require(!raw && contains(nonAscii.status.detail, "\\xc3\\xa9"), "reference bytes are escaped in the detail: " + nonAscii.status.detail);
}

void testRequiredFormatsAreRefused() {
    for (const std::string_view format : {host::kCapabilityStyles, host::kCapabilityBindings}) {
        pkg::Manifest manifest = manifestFor("ui.required");
        manifest.requiredCapabilities = {std::string(format), "retained-documents"};
        const Bytes bytes = buildPackage(manifest, {mainDocument(), {EntryKind::Style, "styles/theme", "s"}});

        host::HostOptions permissive = hostOptions();
        permissive.capabilities = kHostCapabilitiesWithFormats;
        host::LoadResult result = load(bytes, permissive);
        requireRefused(result, HostError::RequiredFormatUnsupported, "required " + std::string(format));
        require(contains(result.status.detail, std::string(format)), "detail names the format");

        host::LoadResult strict = load(bytes);
        requireRefused(strict, HostError::Container, "required " + std::string(format) + " against the built-in capabilities");
        require(strict.status.container == pkg::PackageError::UnsupportedCapability, "the container refuses the capability first");
    }

    pkg::Manifest optional = manifestFor("ui.optional");
    optional.optionalCapabilities = {"bindings", "styles"};
    host::LoadResult accepted = load(buildPackage(optional, {mainDocument(), {EntryKind::Style, "styles/theme", "s"}}));
    require(accepted.ok() && accepted.package.styles.size() == 1, "optional style and binding capabilities are accepted");
}

void testCapabilityAndApiVersionRefusals() {
    pkg::Manifest capability = manifestFor("ui.caps");
    capability.requiredCapabilities = {"offscreen-surfaces", "retained-documents"};
    const Bytes needsSurfaces = buildPackage(capability, {mainDocument()});
    require(load(needsSurfaces).ok(), "control: the built-in set provides offscreen surfaces");

    const std::array<std::string_view, 1> onlyDocuments = {"retained-documents"};
    host::HostOptions narrow = hostOptions();
    narrow.capabilities = onlyDocuments;
    host::LoadResult refused = load(needsSurfaces, narrow);
    requireRefused(refused, HostError::Container, "missing capability");
    require(refused.status.container == pkg::PackageError::UnsupportedCapability, "capability refusal keeps the container code");
    require(contains(refused.status.detail, "offscreen-surfaces"), "detail names the capability: " + refused.status.detail);

    pkg::Manifest api = manifestFor("ui.api");
    api.minApiVersion = 9;
    const Bytes needsNewer = buildPackage(api, {mainDocument()});
    host::LoadResult tooNew = load(needsNewer);
    requireRefused(tooNew, HostError::Container, "package needs a newer API");
    require(tooNew.status.container == pkg::PackageError::IncompatibleHost, "API refusal keeps the container code");
    host::HostOptions future = hostOptions();
    future.hostApiVersion = 9;
    require(load(needsNewer, future).ok(), "the same package loads once the host advertises the API");

    host::HostOptions unset;
    requireRefused(load(needsNewer, unset), HostError::InvalidOptions, "unset host API version");
}

void testTruncatedAndCorruptedBytes() {
    const Bytes bytes = buildPackage(
        manifestFor("ui.corrupt"),
        {mainDocument(imageNode("pic", "/assets/a.png")), {EntryKind::Asset, "assets/a.png", "payload"}});
    require(load(bytes).ok(), "control: the untouched package loads");

    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const host::LoadResult result =
            host::loadPackage(std::span<const std::uint8_t>(bytes.data(), length), hostOptions());
        require(result.status.code == HostError::Container && result.package.documents.empty(),
                "truncation to " + std::to_string(length) + " bytes is a container refusal");
    }

    Bytes extended = bytes;
    extended.push_back(0);
    requireRefused(load(extended), HostError::Container, "trailing byte");

    for (std::size_t at = 0; at < bytes.size(); ++at) {
        for (const std::uint8_t mask : {std::uint8_t{0x01}, std::uint8_t{0x80}, std::uint8_t{0xFF}}) {
            Bytes mutated = bytes;
            mutated[at] = static_cast<std::uint8_t>(mutated[at] ^ mask);
            requireRefused(load(mutated), HostError::Container, "byte " + std::to_string(at) + " xor " + std::to_string(mask));
        }
    }

    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int round = 0; round < 4000; ++round) {
        Bytes mutated = bytes;
        const std::size_t edits = 1 + static_cast<std::size_t>(next() % 6);
        for (std::size_t edit = 0; edit < edits; ++edit) {
            mutated[static_cast<std::size_t>(next() % mutated.size())] ^= static_cast<std::uint8_t>(1 + next() % 255);
        }
        if (next() % 4 == 0) {
            mutated.resize(static_cast<std::size_t>(next() % (mutated.size() + 1)));
        }
        if (mutated == bytes) {
            continue;
        }
        const host::LoadResult result = load(mutated);
        require(!result.ok() && result.package.documents.empty(), "a corrupted package was accepted in round " + std::to_string(round));
    }
}

void testLimits() {
    const std::string threeNodes = imageNode("a", "/assets/a.png") + "," + imageNode("b", "/assets/a.png");
    const std::vector<Part> parts = {
        mainDocument(threeNodes),
        {EntryKind::Document, "documents/second", documentJson("second")},
        {EntryKind::Asset, "assets/a.png", "payload"},
    };
    const Bytes bytes = buildPackage(manifestFor("ui.limits"), parts);
    require(load(bytes).ok(), "control: default limits accept the package");

    host::HostOptions options = hostOptions();
    options.limits.maxDocuments = 1;
    requireRefused(load(bytes, options), HostError::LimitExceeded, "document count limit");

    options = hostOptions();
    options.limits.maxTotalNodes = 3;
    requireRefused(load(bytes, options), HostError::LimitExceeded, "total node limit");
    options.limits.maxTotalNodes = 4;
    require(load(bytes, options).ok(), "the node limit is inclusive");

    options = hostOptions();
    options.limits.maxAssetReferences = 1;
    requireRefused(load(bytes, options), HostError::LimitExceeded, "asset reference limit");
    options.limits.maxAssetReferences = 2;
    require(load(bytes, options).ok(), "the asset reference limit is inclusive");

    options = hostOptions();
    options.limits.document.maxNodes = 2;
    host::LoadResult nodes = load(bytes, options);
    requireRefused(nodes, HostError::DocumentRefused, "declarative node limit");
    require(nodes.status.documentError == declarative::ErrorCode::LimitNodes, "declarative node limit code");

    options = hostOptions();
    options.limits.document.maxBytes = 32;
    host::LoadResult size = load(bytes, options);
    requireRefused(size, HostError::DocumentRefused, "declarative size limit");
    require(size.status.documentError == declarative::ErrorCode::LimitSize, "declarative size limit code");

    options = hostOptions();
    options.limits.package.maxEntries = 2;
    host::LoadResult entries = load(bytes, options);
    requireRefused(entries, HostError::Container, "container entry limit");
    require(entries.status.container == pkg::PackageError::LimitExceeded, "container limit code");

    options = hostOptions();
    options.limits.package.maxTotalSize = 64;
    requireRefused(load(bytes, options), HostError::Container, "container size limit");
}

} // namespace

namespace uirepo::pkghost::testing {

void runRefusalTests() {
    testCodesAreStable();
    testBadDeclarativeDocument();
    testAssetReferenceRefusals();
    testRequiredFormatsAreRefused();
    testCapabilityAndApiVersionRefusals();
    testTruncatedAndCorruptedBytes();
    testLimits();
}

} // namespace uirepo::pkghost::testing
