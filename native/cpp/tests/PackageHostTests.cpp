#include "DeclarativeTestSupport.h"
#include "PackageHostTestSupport.h"

#include "uirepo/NativeApi.h"

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace uirepo::pkghost::testing {
void runRefusalTests();
}

using namespace pkghosttest;
using namespace uirepo;
namespace dt = uirepo::declarative::testing;
namespace host = uirepo::pkghost;

namespace {

constexpr char kFullDocument[] = R"JSON({
  "format": "uirepo.ui",
  "version": 1,
  "id": "main",
  "style": {"width": {"percent": 100}, "height": 600, "padding": 8, "gap": 6,
            "alignItems": "center", "background": "#102030", "opacity": 0.5},
  "children": [
    {"type": "stack", "id": "column", "axis": "column", "scrollable": true, "style": {"gap": 4},
     "children": [
       {"type": "text", "id": "title", "text": "Settings", "fontSize": 24.5, "color": "#FFCC00",
        "textLayout": {"wrapping": "wrap", "alignment": "center", "overflow": "ellipsis"}},
       {"type": "button", "id": "ok", "text": "OK", "style": {"padding": [4, 2, 4, 2]}},
       {"type": "image", "id": "logo", "source": "/assets/img/logo.png", "naturalSize": [64, 32],
        "style": {"width": {"percent": 50}}},
       {"type": "video", "id": "intro", "source": "../assets/video/intro.bk2", "naturalSize": [1280, 720],
        "playback": "playing", "loop": true, "volume": 0.25},
       {"type": "toggle", "id": "mute", "value": true},
       {"type": "slider", "id": "volume", "min": 0, "max": 100, "step": 5, "value": 80},
       {"type": "stepper", "id": "quality", "options": ["Low", "Medium", "High"], "index": 1},
       {"type": "textInput", "id": "profile", "value": "Default", "maxBytes": 32}
     ]}
  ]
})JSON";

constexpr char kSecondDocument[] = R"JSON({
  "format": "uirepo.ui", "version": 1, "id": "hud",
  "style": {"width": 300, "height": 120, "background": "#000000cc"},
  "children": [
    {"type": "text", "id": "label", "text": "Ammo", "fontSize": 18},
    {"type": "image", "id": "icon", "source": "../../assets/img/logo.png", "naturalSize": [16, 16]}
  ]
})JSON";

const std::string kLogoBytes = std::string("\x89PNG\r\n\x1a\n", 8) + "logo-bytes";
const std::string kVideoBytes = "intro-bytes";

std::vector<Part> fullParts() {
    return {
        {EntryKind::Document, "documents/main", kFullDocument},
        {EntryKind::Document, "documents/hud/overlay", kSecondDocument},
        {EntryKind::Asset, "assets/img/logo.png", kLogoBytes},
        {EntryKind::Asset, "assets/video/intro.bk2", kVideoBytes},
        {EntryKind::Style, "styles/theme", "style-bytes-not-defined-yet"},
        {EntryKind::Binding, "bindings/model", std::string("binding\0bytes", 13)},
    };
}

host::LoadResult loadOk(const Bytes& bytes, const std::string& what) {
    host::LoadResult result = host::loadPackage(std::span<const std::uint8_t>(bytes), hostOptions());
    require(result.ok(), what + ": " + result.status.detail);
    return result;
}

void testMinimalRoundTrip() {
    const Bytes bytes = buildPackage(
        manifestFor("ui.minimal"), {{EntryKind::Document, "documents/main", documentJson("root")}});
    host::LoadResult result = loadOk(bytes, "minimal");

    require(result.status.code == HostError::Ok && result.status.detail.empty(), "ok status carries no detail");
    require(result.package.manifest.id == "ui.minimal", "manifest id is exposed");
    require(result.package.manifest.entryDocument == "documents/main", "manifest entry document is exposed");
    require(result.package.documents.size() == 1, "one document");
    require(result.package.entry().path == "documents/main" && result.package.entry().id == "root", "entry document id");
    require(result.package.entry().document != nullptr, "entry document is live");
    require(result.package.assets.empty() && result.package.styles.empty() && result.package.bindings.empty(), "no raw entries");
    require(result.package.storage == bytes, "the package owns a copy of its bytes");
    require(result.package.digest == pkg::readPackage(bytes).package.digest, "digest matches the container digest");
    require(!result.package.newerMinor, "format minor is current");
}

void testMultiDocument() {
    const Bytes bytes = buildPackage(manifestFor("ui.multi"), fullParts());
    host::LoadResult result = loadOk(bytes, "multi");
    host::LoadedPackage& loaded = result.package;

    require(loaded.documents.size() == 2, "two documents");
    require(loaded.documents[0].path == "documents/hud/overlay" && loaded.documents[1].path == "documents/main",
            "documents are in path order");
    require(loaded.entry().path == "documents/main" && loaded.entry().id == "main", "entry document found by manifest path");
    const host::HostedDocument* other = loaded.findDocument("documents/hud/overlay");
    require(other != nullptr && other->id == "hud" && other->document != nullptr, "the other document is exposed");
    require(loaded.findDocument("documents/missing") == nullptr, "unknown document path");
    require(loaded.findDocument("documents/main") == &loaded.entry(), "entry is one of the documents");
}

void testAssetsAndRawEntries() {
    const Bytes bytes = buildPackage(manifestFor("ui.assets"), fullParts());
    host::LoadResult result = loadOk(bytes, "assets");
    host::LoadedPackage& loaded = result.package;

    require(loaded.assets.size() == 2, "two assets");
    const host::EntryView* logo = loaded.findAsset("assets/img/logo.png");
    require(logo != nullptr && std::string(logo->data.begin(), logo->data.end()) == kLogoBytes, "asset bytes by package path");
    const host::EntryView* video = loaded.findAsset("assets/video/intro.bk2");
    require(video != nullptr && std::string(video->data.begin(), video->data.end()) == kVideoBytes, "second asset bytes");
    require(loaded.findAsset("assets/nope.png") == nullptr, "unknown asset path");
    require(loaded.findAsset("documents/main") == nullptr, "a document is not an asset");

    require(loaded.styles.size() == 1 && loaded.styles[0].path == "styles/theme", "style entry is surfaced");
    require(std::string(loaded.styles[0].data.begin(), loaded.styles[0].data.end()) == "style-bytes-not-defined-yet",
            "style entry is carried verbatim");
    require(loaded.bindings.size() == 1 && loaded.bindings[0].path == "bindings/model", "binding entry is surfaced");
    require(std::string(loaded.bindings[0].data.begin(), loaded.bindings[0].data.end()) == std::string("binding\0bytes", 13),
            "binding entry is carried verbatim, including NUL");

    const host::HostedDocument& entry = loaded.entry();
    require(entry.assets.size() == 2, "entry document has two asset references");
    require(entry.assets[0].nodeId == "logo" && entry.assets[0].reference == "/assets/img/logo.png" &&
                entry.assets[0].assetPath == "assets/img/logo.png" && !entry.assets[0].video,
            "absolute image reference resolved");
    require(entry.assets[1].nodeId == "intro" && entry.assets[1].reference == "../assets/video/intro.bk2" &&
                entry.assets[1].assetPath == "assets/video/intro.bk2" && entry.assets[1].video,
            "relative video reference resolved");
    const host::HostedDocument* hud = loaded.findDocument("documents/hud/overlay");
    require(hud->assets.size() == 1 && hud->assets[0].assetPath == "assets/img/logo.png",
            "a reference resolves against the document directory");
}

void testBytesOutliveTheInput() {
    Bytes bytes = buildPackage(manifestFor("ui.owned"), fullParts());
    const Bytes expected = bytes;

    Bytes copied = bytes;
    host::LoadResult fromSpan = host::loadPackage(std::span<const std::uint8_t>(copied), hostOptions());
    require(fromSpan.ok(), "span load");
    copied.assign(copied.size(), 0xAA);
    copied.clear();
    copied.shrink_to_fit();
    const host::EntryView* logo = fromSpan.package.findAsset("assets/img/logo.png");
    require(logo != nullptr && std::string(logo->data.begin(), logo->data.end()) == kLogoBytes,
            "asset bytes survive the caller's buffer");

    const std::uint8_t* original = bytes.data();
    host::LoadResult fromMove = host::loadPackage(std::move(bytes), hostOptions());
    require(fromMove.ok(), "move load");
    require(fromMove.package.storage.data() == original && fromMove.package.storage == expected,
            "an rvalue buffer is adopted without a copy");

    host::LoadedPackage moved = std::move(fromMove.package);
    const host::EntryView* movedLogo = moved.findAsset("assets/img/logo.png");
    require(movedLogo != nullptr && movedLogo->data.data() >= moved.storage.data() &&
                movedLogo->data.data() < moved.storage.data() + moved.storage.size(),
            "asset spans stay inside the owned storage after the package is moved");
    require(std::string(movedLogo->data.begin(), movedLogo->data.end()) == kLogoBytes, "moved asset bytes");
}

void requireSameDisplayList(Document& left, Document& right, Size viewport, const std::string& what) {
    left.layout(viewport);
    right.layout(viewport);
    const auto a = left.buildDisplayList();
    const auto b = right.buildDisplayList();
    require(!a.empty() && a.size() == b.size(), what + ": display list sizes differ");
    for (std::size_t index = 0; index < a.size(); ++index) {
        require(dt::sameCommand(a[index], b[index]), what + ": display list command " + std::to_string(index) + " differs");
    }
}

void testHostedDocumentsMatchDirectParse() {
    host::LoadResult result = loadOk(buildPackage(manifestFor("ui.equal"), fullParts()), "equivalence");
    const std::array<std::pair<const char*, const char*>, 2> sources{{
        {"documents/main", kFullDocument},
        {"documents/hud/overlay", kSecondDocument},
    }};
    for (const auto& [path, json] : sources) {
        host::HostedDocument* hosted = nullptr;
        for (host::HostedDocument& candidate : result.package.documents) {
            if (candidate.path == path) {
                hosted = &candidate;
            }
        }
        require(hosted != nullptr, std::string("hosted document ") + path);
        declarative::ParseResult direct = dt::parseOk(json, path);
        require(dt::sameTree(hosted->document->root(), direct.document->root()), std::string(path) + ": tree differs from direct parse");
        for (const Size viewport : {Size{1280.0f, 720.0f}, Size{320.0f, 200.0f}, Size{1920.0f, 1080.0f}}) {
            requireSameDisplayList(*hosted->document, *direct.document, viewport, path);
        }
        require(dt::canonical(*hosted->document) == dt::canonical(*direct.document), std::string(path) + ": canonical form differs");
    }
}

void testBindAssetUrls() {
    host::LoadResult result = loadOk(buildPackage(manifestFor("ui.bound"), fullParts()), "bind");
    host::HostedDocument& entry = result.package.entry();

    declarative::ParseResult direct = dt::parseOk(kFullDocument, "direct");
    require(host::bindAssetUrls("ui.bound", entry) == 2, "two sources rewritten");

    const auto* logo = dynamic_cast<const ImageNode*>(entry.document->find("logo"));
    const auto* intro = dynamic_cast<const VideoNode*>(entry.document->find("intro"));
    require(logo != nullptr && logo->source() == "uipkg:ui.bound/assets/img/logo.png", "image source is the package identity");
    require(intro != nullptr && intro->source() == "uipkg:ui.bound/assets/video/intro.bk2", "video source is the package identity");
    require(host::assetUrl("p.q", "assets/a.png") == "uipkg:p.q/assets/a.png", "assetUrl format");
    require(host::assetUrl("p.q", "assets/a.png").find('\\') == std::string::npos &&
                host::assetUrl("p.q", "assets/a.png").front() != '/',
            "an asset url is not an absolute or rooted filesystem path");

    entry.document->layout(Size{1280.0f, 720.0f});
    direct.document->layout(Size{1280.0f, 720.0f});
    const auto bound = entry.document->buildDisplayList();
    const auto authored = direct.document->buildDisplayList();
    require(bound.size() == authored.size(), "rewriting sources does not change the command count");
    for (std::size_t index = 0; index < bound.size(); ++index) {
        const auto* image = std::get_if<DrawImageCommand>(&bound[index]);
        if (image != nullptr) {
            require(image->source == "uipkg:ui.bound/assets/img/logo.png", "display list carries the package identity");
            const auto& original = std::get<DrawImageCommand>(authored[index]);
            require(image->bounds.x == original.bounds.x && image->bounds.width == original.bounds.width,
                    "rewriting a source does not move the node");
        }
    }
    require(host::bindAssetUrls("ui.bound", result.package.documents[0]) == 1, "second document has one reference");
}

void testCapabilityNamesFromAbiMask() {
    struct Pair {
        std::uint64_t bit;
        const char* name;
    };
    const std::array<Pair, 16> pairs{{
        {UIREPO_CAP_RETAINED_DOCUMENTS, "retained-documents"},
        {UIREPO_CAP_NATIVE_D3D11, "native-d3d11"},
        {UIREPO_CAP_DIRECTWRITE_TEXT, "directwrite-text"},
        {UIREPO_CAP_ENGINE_COMPOSITION, "engine-composition"},
        {UIREPO_CAP_RENDERER_RECOVERY, "renderer-recovery"},
        {UIREPO_CAP_DXGI_FALLBACK, "dxgi-fallback"},
        {UIREPO_CAP_POINTER_INPUT, "pointer-input"},
        {UIREPO_CAP_KEYBOARD_INPUT, "keyboard-input"},
        {UIREPO_CAP_SCROLL_CONTAINERS, "scroll-containers"},
        {UIREPO_CAP_POINTER_EVENTS, "pointer-events"},
        {UIREPO_CAP_NATIVE_SETTINGS_CONTROLS, "native-settings-controls"},
        {UIREPO_CAP_FOCUS_LEASE, "focus-lease"},
        {UIREPO_CAP_DOCUMENT_KEY_EVENTS, "document-key-events"},
        {UIREPO_CAP_TEXT_LAYOUT, "text-layout"},
        {UIREPO_CAP_OFFSCREEN_SURFACES, "offscreen-surfaces"},
        {UIREPO_CAP_RESULT_CODES, "result-codes"},
    }};
    std::uint64_t all = 0;
    for (const Pair& pair : pairs) {
        const auto names = host::capabilityNamesFromAbiMask(pair.bit);
        require(names.size() == 1 && names[0] == pair.name, std::string("capability bit maps to ") + pair.name);
        all |= pair.bit;
    }
    require(host::capabilityNamesFromAbiMask(all).size() == 16, "all sixteen package capabilities");
    require(host::capabilityNamesFromAbiMask(0).empty(), "empty mask");
    require(host::capabilityNamesFromAbiMask(UIREPO_CAP_DOCUMENT_STATE | UIREPO_CAP_GAME_THREAD_DISPATCH).empty(),
            "ABI v2 bits have no package capability name");
    const auto some = host::capabilityNamesFromAbiMask(UIREPO_CAP_RETAINED_DOCUMENTS | UIREPO_CAP_TEXT_LAYOUT);
    require(some.size() == 2 && some[0] == "retained-documents" && some[1] == "text-layout", "mask order follows bit order");
}

void testLoadNeverThrows() {
    static_assert(noexcept(host::loadPackage(std::span<const std::uint8_t>{}, host::HostOptions{})));
    static_assert(noexcept(host::loadPackage(std::vector<std::uint8_t>{}, host::HostOptions{})));
    host::LoadResult empty = host::loadPackage(std::span<const std::uint8_t>{}, hostOptions());
    require(empty.status.code == HostError::Container && empty.status.container == pkg::PackageError::Truncated,
            "an empty buffer is a container refusal");
}

} // namespace

int main() {
    testMinimalRoundTrip();
    testMultiDocument();
    testAssetsAndRawEntries();
    testBytesOutliveTheInput();
    testHostedDocumentsMatchDirectParse();
    testBindAssetUrls();
    testCapabilityNamesFromAbiMask();
    testLoadNeverThrows();
    uirepo::pkghost::testing::runRefusalTests();
    std::puts("package host tests passed");
    return 0;
}
