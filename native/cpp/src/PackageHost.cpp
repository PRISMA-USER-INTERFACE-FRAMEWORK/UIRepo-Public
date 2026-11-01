#include "uirepo/PackageHost.h"

#include "PackageInternal.h"

#include <algorithm>
#include <new>
#include <utility>

namespace uirepo::pkghost {
namespace {

using pkg::detail::quoted;

[[nodiscard]] HostStatus failure(HostError code, std::string detail) {
    HostStatus status;
    status.code = code;
    status.detail = std::move(detail);
    return status;
}

[[nodiscard]] std::string diagnosticText(const std::vector<declarative::Diagnostic>& diagnostics) {
    const declarative::Diagnostic& first = diagnostics.front();
    std::string text = std::string(declarative::errorCodeName(first.code)) + " at " + quoted(first.path) + " " +
                       std::to_string(first.line) + ":" + std::to_string(first.column) + ": " + quoted(first.message);
    if (diagnostics.size() > 1) {
        text += " (and " + std::to_string(diagnostics.size() - 1) + " more)";
    }
    return text;
}

[[nodiscard]] bool manifestRequires(const pkg::Manifest& manifest, std::string_view capability) noexcept {
    return std::find(manifest.requiredCapabilities.begin(), manifest.requiredCapabilities.end(), capability) !=
           manifest.requiredCapabilities.end();
}

class Loader final {
public:
    Loader(const HostOptions& options, const pkg::Package& package, LoadedPackage& out)
        : options_(options), package_(package), out_(out) {}

    [[nodiscard]] HostStatus run() {
        std::size_t documentEntries = 0;
        for (const pkg::Entry& entry : package_.entries) {
            documentEntries += entry.kind == pkg::EntryKind::Document ? 1u : 0u;
        }
        if (documentEntries > options_.limits.maxDocuments) {
            return failure(
                HostError::LimitExceeded,
                "package has " + std::to_string(documentEntries) + " documents, the limit is " +
                    std::to_string(options_.limits.maxDocuments));
        }

        for (const pkg::Entry& entry : package_.entries) {
            switch (entry.kind) {
            case pkg::EntryKind::Document:
                if (HostStatus status = addDocument(entry); !status.ok()) {
                    return status;
                }
                break;
            case pkg::EntryKind::Style:
                out_.styles.push_back({entry.path, entry.data});
                break;
            case pkg::EntryKind::Binding:
                out_.bindings.push_back({entry.path, entry.data});
                break;
            case pkg::EntryKind::Asset:
                out_.assets.push_back({entry.path, entry.data});
                break;
            }
        }

        const auto entryDocument = std::find_if(out_.documents.begin(), out_.documents.end(), [this](const HostedDocument& document) {
            return document.path == package_.manifest.entryDocument;
        });
        if (entryDocument == out_.documents.end()) {
            return failure(HostError::Internal, "entry document " + quoted(package_.manifest.entryDocument) + " was not loaded");
        }
        out_.entryIndex = static_cast<std::size_t>(entryDocument - out_.documents.begin());
        return {};
    }

private:
    [[nodiscard]] HostStatus addDocument(const pkg::Entry& entry) {
        const std::string_view text(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
        declarative::ParseResult parsed = declarative::parseDocument(text, options_.limits.document);
        if (!parsed.ok()) {
            HostStatus status = failure(
                HostError::DocumentRefused,
                "document " + quoted(entry.path) + ": " +
                    (parsed.diagnostics.empty() ? std::string("no document and no diagnostic") : diagnosticText(parsed.diagnostics)));
            if (!parsed.diagnostics.empty()) {
                status.documentError = parsed.diagnostics.front().code;
            }
            return status;
        }

        HostedDocument hosted;
        hosted.path = entry.path;
        hosted.id = parsed.document->root().id();
        hosted.document = std::move(parsed.document);
        if (HostStatus status = walk(hosted); !status.ok()) {
            return status;
        }
        out_.documents.push_back(std::move(hosted));
        return {};
    }

    [[nodiscard]] HostStatus walk(HostedDocument& hosted) {
        std::vector<const Node*> stack{&hosted.document->root()};
        while (!stack.empty()) {
            const Node* node = stack.back();
            stack.pop_back();

            if (++nodes_ > options_.limits.maxTotalNodes) {
                return failure(
                    HostError::LimitExceeded,
                    "package documents exceed " + std::to_string(options_.limits.maxTotalNodes) + " nodes in total");
            }

            const std::string* source = nullptr;
            bool video = false;
            if (const auto* image = dynamic_cast<const ImageNode*>(node)) {
                source = &image->source();
            } else if (const auto* clip = dynamic_cast<const VideoNode*>(node)) {
                source = &clip->source();
                video = true;
            }
            if (source != nullptr) {
                if (++references_ > options_.limits.maxAssetReferences) {
                    return failure(
                        HostError::LimitExceeded,
                        "package documents exceed " + std::to_string(options_.limits.maxAssetReferences) +
                            " asset references in total");
                }
                if (HostStatus status = resolveAsset(hosted, *node, *source, video); !status.ok()) {
                    return status;
                }
            }

            const auto& children = node->children();
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                stack.push_back(child->get());
            }
        }
        return {};
    }

    [[nodiscard]] HostStatus resolveAsset(HostedDocument& hosted, const Node& node, const std::string& source, bool video) {
        const std::string where =
            "document " + quoted(hosted.path) + " node " + quoted(node.id()) + " reference " + quoted(source);

        std::string assetPath;
        const pkg::Status resolved = pkg::resolveReference(hosted.path, source, assetPath, options_.limits.package);
        if (!resolved.ok()) {
            return failure(HostError::BadAssetReference, where + ": " + resolved.detail);
        }
        const pkg::Entry* target = package_.find(assetPath);
        if (target == nullptr) {
            return failure(HostError::MissingAsset, where + " names no entry (" + quoted(assetPath) + ")");
        }
        if (target->kind != pkg::EntryKind::Asset) {
            return failure(HostError::NotAnAsset, where + " names a " + std::string(pkg::kindDirectory(target->kind)) + " entry, not an asset");
        }
        hosted.assets.push_back({node.id(), source, std::move(assetPath), video});
        return {};
    }

    const HostOptions& options_;
    const pkg::Package& package_;
    LoadedPackage& out_;
    std::size_t nodes_{};
    std::size_t references_{};
};

[[nodiscard]] LoadResult loadOwned(std::vector<std::uint8_t>&& bytes, const HostOptions& options) {
    LoadResult result;
    if (options.hostApiVersion == 0) {
        result.status = failure(HostError::InvalidOptions, "hostApiVersion must be set");
        return result;
    }

    pkg::ReadOptions read;
    read.limits = options.limits.package;
    read.hostApiVersion = options.hostApiVersion;
    read.hostCapabilities = options.capabilities;
    pkg::ReadResult parsed = pkg::readPackage(bytes, read);
    if (!parsed.status.ok()) {
        result.status = failure(HostError::Container, parsed.status.detail);
        result.status.container = parsed.status.code;
        result.status.offset = parsed.status.offset;
        return result;
    }

    for (const std::string_view format : {kCapabilityStyles, kCapabilityBindings}) {
        if (manifestRequires(parsed.package.manifest, format)) {
            result.status = failure(
                HostError::RequiredFormatUnsupported,
                "package requires the " + quoted(format) + " format, which this host does not implement");
            return result;
        }
    }

    LoadedPackage loaded;
    Loader loader(options, parsed.package, loaded);
    result.status = loader.run();
    if (!result.status.ok()) {
        return result;
    }

    loaded.manifest = std::move(parsed.package.manifest);
    loaded.digest = parsed.package.digest;
    loaded.newerMinor = parsed.package.newerMinor;
    loaded.storage = std::move(bytes);
    result.package = std::move(loaded);
    return result;
}

} // namespace

const char* describe(HostError error) noexcept {
    switch (error) {
    case HostError::Ok:
        return "ok";
    case HostError::InvalidOptions:
        return "invalid host options";
    case HostError::Container:
        return "package container refused";
    case HostError::RequiredFormatUnsupported:
        return "package requires a format this host does not implement";
    case HostError::DocumentRefused:
        return "declarative document refused";
    case HostError::LimitExceeded:
        return "package exceeds a host limit";
    case HostError::BadAssetReference:
        return "asset reference does not resolve inside the package";
    case HostError::MissingAsset:
        return "asset reference names no package entry";
    case HostError::NotAnAsset:
        return "asset reference names an entry that is not an asset";
    case HostError::OutOfMemory:
        return "out of memory while loading the package";
    case HostError::Internal:
        return "internal error while loading the package";
    }
    return "unknown host error";
}

const HostedDocument* LoadedPackage::findDocument(std::string_view path) const noexcept {
    const auto it = std::lower_bound(
        documents.begin(), documents.end(), path, [](const HostedDocument& document, std::string_view key) {
            return document.path < key;
        });
    return it != documents.end() && it->path == path ? &*it : nullptr;
}

const EntryView* LoadedPackage::findAsset(std::string_view path) const noexcept {
    const auto it = std::lower_bound(
        assets.begin(), assets.end(), path, [](const EntryView& entry, std::string_view key) { return entry.path < key; });
    return it != assets.end() && it->path == path ? &*it : nullptr;
}

LoadResult loadPackage(std::vector<std::uint8_t>&& bytes, const HostOptions& options) noexcept {
    try {
        return loadOwned(std::move(bytes), options);
    } catch (const std::bad_alloc&) {
        LoadResult result;
        result.status = failure(HostError::OutOfMemory, "no memory");
        return result;
    } catch (...) {
        LoadResult result;
        result.status = failure(HostError::Internal, "unexpected");
        return result;
    }
}

LoadResult loadPackage(std::span<const std::uint8_t> bytes, const HostOptions& options) noexcept {
    try {
        return loadPackage(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), options);
    } catch (...) {
        LoadResult result;
        result.status.code = HostError::OutOfMemory;
        result.status.detail = "no memory";
        return result;
    }
}

std::string assetUrl(std::string_view packageId, std::string_view assetPath) {
    std::string url = "uipkg:";
    url.append(packageId);
    url.push_back('/');
    url.append(assetPath);
    return url;
}

std::size_t bindAssetUrls(std::string_view packageId, HostedDocument& document) {
    std::size_t rewritten = 0;
    for (const AssetReference& asset : document.assets) {
        Node* node = document.document->find(asset.nodeId);
        if (auto* image = dynamic_cast<ImageNode*>(node)) {
            image->setSource(assetUrl(packageId, asset.assetPath));
            ++rewritten;
        } else if (auto* clip = dynamic_cast<VideoNode*>(node)) {
            clip->setSource(assetUrl(packageId, asset.assetPath));
            ++rewritten;
        }
    }
    return rewritten;
}

std::vector<std::string_view> capabilityNamesFromAbiMask(std::uint64_t mask) {
    const std::span<const std::string_view> names = pkg::builtinCapabilities();
    std::vector<std::string_view> result;
    for (std::size_t bit = 0; bit < names.size() && bit < 64; ++bit) {
        if (((mask >> bit) & 1u) != 0) {
            result.push_back(names[bit]);
        }
    }
    return result;
}

} // namespace uirepo::pkghost
