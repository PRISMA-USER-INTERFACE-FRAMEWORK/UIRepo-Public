#include "PackageHostTestSupport.h"

#include "PackageHostRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

using namespace pkghosttest;
using uirepo::fo4::DocumentRegistry;
using uirepo::fo4::PackageHostRuntime;
using FileFailure = PackageHostRuntime::FileFailure;
namespace fs = std::filesystem;
namespace host = uirepo::pkghost;

namespace {

struct TempDir {
    fs::path path;

    TempDir() {
        static unsigned counter = 0;
        path = fs::temp_directory_path() /
               ("uirepo-pkghost-" + std::to_string(++counter) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void writeFile(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(out), "cannot create " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(out), "cannot write " + path.string());
}

PackageHostRuntime::Config configFor(const fs::path& directory) {
    PackageHostRuntime::Config config;
    config.packagesDirectory = directory;
    config.host = hostOptions();
    return config;
}

Bytes packageWithImage(const std::string& id) {
    return buildPackage(
        manifestFor(id),
        {{EntryKind::Document, "documents/main",
          documentJson("main", R"({"type":"text","id":"label","text":"hi"},{"type":"image","id":"logo","source":"/assets/logo.png","naturalSize":[8,8]})")},
         {EntryKind::Asset, "assets/logo.png", "png"}});
}

Bytes packageWithTwoDocuments(const std::string& id) {
    return buildPackage(
        manifestFor(id),
        {{EntryKind::Document, "documents/main", documentJson("main", R"({"type":"text","id":"label","text":"hi"})")},
         {EntryKind::Document, "documents/side", documentJson("side", R"({"type":"toggle","id":"flag"})")},
         {EntryKind::Style, "styles/theme", "s"}});
}

const PackageHostRuntime::FileResult& fileNamed(const PackageHostRuntime::ScanReport& report, const std::string& name) {
    const auto found = std::find_if(report.files.begin(), report.files.end(), [&name](const auto& file) { return file.file == name; });
    require(found != report.files.end(), "no scan result for " + name);
    return *found;
}

std::string describe(const PackageHostRuntime::ScanReport& report) {
    std::string text = "directoryFound=" + std::to_string(report.directoryFound) + " problem=[" + report.directoryProblem + "]";
    for (const auto& file : report.files) {
        text += " | " + file.file + " failure=" + std::to_string(static_cast<int>(file.failure)) + " [" + file.detail + "]";
    }
    return text;
}

bool hasImageSource(DocumentRegistry& registry, DocumentRegistry::DocumentHandle handle, const std::string& source) {
    std::vector<uirepo::DrawCommand> commands;
    require(registry.snapshotDisplayList(handle, uirepo::Size{800.0f, 600.0f}, commands), "hosted document cannot be rendered offscreen");
    return std::any_of(commands.begin(), commands.end(), [&source](const uirepo::DrawCommand& command) {
        const auto* image = std::get_if<uirepo::DrawImageCommand>(&command);
        return image != nullptr && image->source == source;
    });
}


void testMissingAndUnusableDirectories() {
    TempDir temp;
    DocumentRegistry registry;

    PackageHostRuntime missing(registry, configFor(temp.path / "absent"));
    auto report = missing.scan();
    require(!report.directoryFound && report.directoryProblem.empty() && report.files.empty(), "a missing folder is normal and silent");

    writeFile(temp.path / "plain-file", bytesOf("x"));
    PackageHostRuntime notDirectory(registry, configFor(temp.path / "plain-file"));
    report = notDirectory.scan();
    require(!report.directoryFound && !report.directoryProblem.empty() && report.files.empty(), "a file where the folder should be is reported");

    fs::create_directories(temp.path / "real");
    writeFile(temp.path / "real" / "alpha.uipkg", packageWithImage("ui.alpha"));
    std::error_code error;
    fs::create_directory_symlink(temp.path / "real", temp.path / "linked", error);
    if (!error) {
        PackageHostRuntime linked(registry, configFor(temp.path / "linked"));
        report = linked.scan();
        require(!report.directoryFound && !report.directoryProblem.empty() && report.files.empty() && registry.enumerateDocuments().empty(),
                "a linked packages folder must not be followed");
    }
    require(registry.enumerateDocuments().empty(), "no document may appear from a refused folder");
}

void testLoadsPackagesHiddenAndOwned() {
    TempDir temp;
    writeFile(temp.path / "alpha.uipkg", packageWithImage("ui.alpha"));
    writeFile(temp.path / "Beta.UIPKG", packageWithTwoDocuments("ui.beta"));
    writeFile(temp.path / "notes.txt", bytesOf("not a package"));
    fs::create_directories(temp.path / "sub");
    writeFile(temp.path / "sub" / "nested.uipkg", packageWithImage("ui.nested"));

    DocumentRegistry registry;
    PackageHostRuntime runtime(registry, configFor(temp.path));
    const auto report = runtime.scan();
    require(report.directoryFound && report.directoryProblem.empty(), "packages folder found");
    require(report.files.size() == 2 && report.loadedCount() == 2, "only top-level .uipkg files are considered");
    require(report.files[0].file == "Beta.UIPKG" && report.files[1].file == "alpha.uipkg", "files load in bytewise name order");
    require(report.files[0].packageId == "ui.beta" && report.files[1].packageId == "ui.alpha", "package ids are reported");

    const auto packages = runtime.packages();
    require(packages.size() == 2, "two packages hosted");
    const auto& beta = packages[0];
    const auto& alpha = packages[1];
    require(beta.id == "ui.beta" && alpha.id == "ui.alpha" && beta.file == "Beta.UIPKG", "package records in load order");
    require(beta.owner == PackageHostRuntime::kOwnerBase + 1 && alpha.owner == PackageHostRuntime::kOwnerBase + 2,
            "owner ids come from the reserved range, one per package");
    require(beta.documents.size() == 2 && alpha.documents.size() == 1, "document records");
    require(beta.documents[beta.entryIndex].path == "documents/main" && beta.documents[beta.entryIndex].id == "main", "entry document record");
    require(beta.styleEntries == 1 && beta.bindingEntries == 0 && alpha.assetReferences == 1, "raw entry and asset reference counts");
    require(beta.version == uirepo::pkg::Version{1, 0, 0}, "version is recorded");

    require(registry.enumerateDocuments().size() == 3, "one registry document per package document");
    for (const auto& package : packages) {
        for (const auto& record : package.documents) {
            DocumentRegistry::DocumentState state{};
            require(registry.documentState(record.handle, state), "hosted document is registered");
            require(state.owner == package.owner && state.role == DocumentRegistry::DocumentRole::Panel, "hosted document owner and role");
            require(!state.visible && !state.inputEnabled && !state.focused, "hosted documents start hidden with input disabled");
            require(state.health == DocumentRegistry::DocumentHealth::Hidden, "hosted documents report Hidden");
            require(registry.findNodeById(record.handle, record.id) == registry.rootNode(record.handle), "root node id matches the record");
        }
    }
    require(registry.visibleCount() == 0, "nothing is drawn until a consumer shows a document");

    require(hasImageSource(registry, alpha.documents[0].handle, "uipkg:ui.alpha/assets/logo.png"),
            "image sources carry the package identity, not the authored reference");
    require(!hasImageSource(registry, alpha.documents[0].handle, "/assets/logo.png"), "the authored reference does not reach the renderer");
}

void testUnloadDestroysExactlyOwnDocuments() {
    TempDir temp;
    writeFile(temp.path / "alpha.uipkg", packageWithImage("ui.alpha"));
    writeFile(temp.path / "beta.uipkg", packageWithTwoDocuments("ui.beta"));

    DocumentRegistry registry;
    const auto foreign = registry.createOwnedDocument("foreign", 77);
    const auto anonymous = registry.createDocument("anonymous");
    PackageHostRuntime runtime(registry, configFor(temp.path));
    require(runtime.scan().loadedCount() == 2, "both packages load");
    const auto before = runtime.packages();
    const auto alphaHandle = before[0].documents[0].handle;
    const auto betaHandles = before[1].documents;

    require(!runtime.unload("ui.missing"), "unknown package id");
    require(runtime.unload("ui.alpha"), "unload by id");
    require(!runtime.unload("ui.alpha"), "unloading twice is refused");
    DocumentRegistry::DocumentState state{};
    require(!registry.documentState(alphaHandle, state), "the unloaded package's document is destroyed");
    for (const auto& record : betaHandles) {
        require(registry.documentState(record.handle, state), "another package's documents survive");
    }
    require(registry.documentState(foreign, state) && registry.documentState(anonymous, state), "documents of other owners survive");
    require(runtime.packages().size() == 1, "package record removed");

    require(runtime.scan().files.size() == 2, "a rescan sees both files again");
    const auto reloaded = runtime.packages();
    require(reloaded.size() == 2 && reloaded[1].id == "ui.alpha", "an unloaded package can be loaded again");
    require(reloaded[1].owner > before[1].owner && reloaded[1].documents[0].handle > betaHandles.back().handle,
            "owner ids and document handles are not reused");

    require(runtime.unloadAll() == 3, "unloadAll reports the documents it destroyed");
    require(runtime.packages().empty() && runtime.unloadAll() == 0, "nothing left to unload");
    require(registry.documentState(foreign, state) && registry.documentState(anonymous, state), "unloadAll leaves other owners alone");
    require(registry.enumerateDocuments().size() == 2, "only the foreign documents remain");
}

std::vector<DocumentRegistry::DocumentHandle> g_released;

void recordRelease(DocumentRegistry::DocumentHandle handle) noexcept {
    g_released.push_back(handle);
}

void testUnloadReleasesEveryDocumentThroughTheHook() {
    TempDir temp;
    writeFile(temp.path / "alpha.uipkg", packageWithImage("ui.alpha"));
    writeFile(temp.path / "beta.uipkg", packageWithTwoDocuments("ui.beta"));

    DocumentRegistry registry;
    PackageHostRuntime::Config config = configFor(temp.path);
    config.releaseDocument = &recordRelease;
    PackageHostRuntime runtime(registry, std::move(config));
    require(runtime.scan().loadedCount() == 2, "both packages load");
    const auto packages = runtime.packages();
    require(g_released.empty(), "loading releases nothing");

    require(runtime.unload("ui.alpha"), "unload by id");
    require(g_released == std::vector{packages[0].documents[0].handle}, "unload releases the package's document");

    g_released.clear();
    require(runtime.unloadAll() == 2, "unloadAll destroys the remaining documents");
    require(g_released.size() == 2 && g_released[0] == packages[1].documents[0].handle &&
                g_released[1] == packages[1].documents[1].handle,
            "unloadAll releases every remaining document");
    g_released.clear();
    require(!runtime.unload("ui.beta") && g_released.empty(), "an unknown package releases nothing");
}

void testRefusalsAreIsolatedAndLeaveNothingBehind() {
    TempDir temp;
    writeFile(temp.path / "good.uipkg", packageWithImage("ui.good"));
    writeFile(temp.path / "garbage.uipkg", bytesOf("this is not a package at all"));
    writeFile(temp.path / "empty.uipkg", {});

    Bytes corrupt = packageWithImage("ui.corrupt");
    corrupt[corrupt.size() / 2] ^= 0x55;
    writeFile(temp.path / "corrupt.uipkg", corrupt);

    writeFile(temp.path / "baddoc.uipkg",
              buildPackage(manifestFor("ui.baddoc"), {{EntryKind::Document, "documents/main", R"({"format":"uirepo.ui","version":1,"id":"r","bogus":1})"}}));
    writeFile(temp.path / "missing-asset.uipkg",
              buildPackage(manifestFor("ui.noasset"),
                           {{EntryKind::Document, "documents/main",
                             documentJson("main", R"({"type":"image","id":"logo","source":"/assets/none.png","naturalSize":[8,8]})")}}));
    uirepo::pkg::Manifest needsStyles = manifestFor("ui.styles");
    needsStyles.requiredCapabilities = {"retained-documents", "styles"};
    writeFile(temp.path / "needs-styles.uipkg", buildPackage(needsStyles, {{EntryKind::Document, "documents/main", documentJson("main")}}));

    DocumentRegistry registry;
    PackageHostRuntime runtime(registry, configFor(temp.path));
    const auto report = runtime.scan();
    require(report.files.size() == 7 && report.loadedCount() == 1, "exactly the good package loads: " + describe(report));
    require(fileNamed(report, "good.uipkg").loaded(), "the good package loads despite its refused neighbours");

    const std::vector<std::pair<std::string, host::HostError>> expected = {
        {"garbage.uipkg", host::HostError::Container},
        {"empty.uipkg", host::HostError::Container},
        {"corrupt.uipkg", host::HostError::Container},
        {"baddoc.uipkg", host::HostError::DocumentRefused},
        {"missing-asset.uipkg", host::HostError::MissingAsset},
        {"needs-styles.uipkg", host::HostError::Container},
    };
    for (const auto& [name, code] : expected) {
        const auto& result = fileNamed(report, name);
        require(result.failure == FileFailure::Host && result.host.code == code && !result.detail.empty() && result.packageId.empty(),
                name + ": expected a host refusal, got " + result.detail);
    }
    require(registry.enumerateDocuments().size() == 1, "refused packages left no document in the registry");
    require(runtime.packages().size() == 1 && runtime.packages()[0].id == "ui.good", "only the good package is recorded");
}

void testFileSafety() {
    TempDir temp;
    const Bytes good = packageWithImage("ui.safe");
    fs::create_directories(temp.path / "dir.uipkg");
    writeFile(temp.path / ".hidden.uipkg", good);
    writeFile(temp.path / "bad$name.uipkg", good);
    writeFile(temp.path / (std::string(129, 'a') + ".uipkg"), good);
    writeFile(temp.path / "my pack.uipkg", good);

    std::error_code error;
    writeFile(temp.path / (std::string("caf") + "\xC3\xA9" + ".uipkg"), good);

    const TempDir outside;
    writeFile(outside.path / "elsewhere.uipkg", packageWithImage("ui.elsewhere"));
    fs::create_symlink(outside.path / "elsewhere.uipkg", temp.path / "link.uipkg", error);
    const bool linked = !error;

    DocumentRegistry registry;
    PackageHostRuntime runtime(registry, configFor(temp.path));
    const auto report = runtime.scan();

    require(fileNamed(report, "dir.uipkg").failure == FileFailure::NotRegularFile, "a directory named like a package is refused");
    if (linked) {
        require(fileNamed(report, "link.uipkg").failure == FileFailure::NotRegularFile, "a link out of the folder is refused");
    }
    require(fileNamed(report, ".hidden.uipkg").failure == FileFailure::BadName, "a leading dot is refused");
    require(fileNamed(report, "bad$name.uipkg").failure == FileFailure::BadName, "a shell character is refused");
    require(fileNamed(report, std::string(129, 'a') + ".uipkg").failure == FileFailure::BadName, "an over-long name is refused");
    const auto nonAscii = std::find_if(report.files.begin(), report.files.end(), [](const auto& file) {
        return file.file.find('?') != std::string::npos;
    });
    require(nonAscii != report.files.end() && nonAscii->failure == FileFailure::BadName, "a non-ASCII name is refused and reported with escapes");
    require(fileNamed(report, "my pack.uipkg").loaded(), "a name with a space is accepted");

    const auto packages = runtime.packages();
    require(packages.size() == 1 && packages[0].id == "ui.safe", "only the safe name loaded, never the linked package");
    require(registry.enumerateDocuments().size() == 1, "refused names created no document");
}

void testLimitsAndDuplicates() {
    TempDir temp;
    writeFile(temp.path / "a.uipkg", packageWithImage("ui.same"));
    writeFile(temp.path / "b.uipkg", packageWithImage("ui.same"));
    writeFile(temp.path / "c.uipkg", packageWithImage("ui.other"));

    DocumentRegistry registry;
    PackageHostRuntime runtime(registry, configFor(temp.path));
    auto report = runtime.scan();
    require(fileNamed(report, "a.uipkg").loaded() && fileNamed(report, "c.uipkg").loaded(), "first copy and other package load");
    const auto& duplicate = fileNamed(report, "b.uipkg");
    require(duplicate.failure == FileFailure::DuplicatePackage && duplicate.packageId == "ui.same", "a second package with the same id is refused");
    require(registry.enumerateDocuments().size() == 2, "the duplicate created no document");

    report = runtime.scan();
    require(report.loadedCount() == 0 && fileNamed(report, "a.uipkg").failure == FileFailure::DuplicatePackage,
            "a rescan does not load a hosted package twice");
    require(registry.enumerateDocuments().size() == 2, "a rescan added no document");

    DocumentRegistry capped;
    auto config = configFor(temp.path);
    config.maxPackages = 1;
    PackageHostRuntime limited(capped, config);
    report = limited.scan();
    require(fileNamed(report, "a.uipkg").loaded() && fileNamed(report, "b.uipkg").failure == FileFailure::TooManyPackages &&
                fileNamed(report, "c.uipkg").failure == FileFailure::TooManyPackages,
            "the package count limit stops further loads");

    DocumentRegistry small;
    config = configFor(temp.path);
    config.maxFileBytes = 16;
    PackageHostRuntime tiny(small, config);
    report = tiny.scan();
    require(report.loadedCount() == 0 && fileNamed(report, "a.uipkg").failure == FileFailure::TooLarge && small.enumerateDocuments().empty(),
            "the file size limit is enforced before the file is read");
}

} // namespace

int main() {
    testMissingAndUnusableDirectories();
    testLoadsPackagesHiddenAndOwned();
    testUnloadDestroysExactlyOwnDocuments();
    testUnloadReleasesEveryDocumentThroughTheHook();
    testRefusalsAreIsolatedAndLeaveNothingBehind();
    testFileSafety();
    testLimitsAndDuplicates();
    std::puts("package host runtime tests passed");
    return 0;
}
