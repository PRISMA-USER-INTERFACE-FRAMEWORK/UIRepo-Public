#include "DocumentRegistry.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace uirepo::fo4 {

DocumentRegistry::SurfaceHandle DocumentRegistry::nextSurfaceHandle() noexcept {
    if (nextSurface_ == 0) {
        return 0;
    }
    return nextSurface_++;
}

DocumentRegistry::DocumentHealth DocumentRegistry::effectiveHealth(
    const DocumentEntry& entry) const noexcept {
    switch (entry.health) {
    case DocumentHealth::Faulted:
    case DocumentHealth::Destroying:
    case DocumentHealth::Destroyed:
    case DocumentHealth::Creating:
        return entry.health;
    case DocumentHealth::Ready:
        return entry.visible ? DocumentHealth::Live : DocumentHealth::Ready;
    case DocumentHealth::Live:
    case DocumentHealth::Hidden:
    default:
        return entry.visible ? DocumentHealth::Live : DocumentHealth::Hidden;
    }
}

DocumentRegistry::DocumentHandle DocumentRegistry::createOwnedDocument(
    std::string rootId,
    OwnerHandle owner,
    DocumentRole role) {
    const DocumentHandle handle = createDocument(std::move(rootId));
    if (handle == 0) {
        return 0;
    }

    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(handle);
    if (!entry) {
        return 0;
    }
    entry->owner = owner;
    entry->dispatchOwner = handle;
    entry->role = role;
    entry->health = DocumentHealth::Live;
    entry->generation = 1;
    return handle;
}

DocumentRegistry::DocumentHandle DocumentRegistry::adoptOwnedDocument(
    std::unique_ptr<Document> document,
    OwnerHandle owner,
    DocumentRole role,
    bool visible) {
    if (!document) {
        return 0;
    }

    std::scoped_lock lock(mutex_);
    DocumentEntry entry{};
    entry.document = std::move(document);
    entry.document->setTextMeasurer(textMeasurer_);
    entry.sequence = nextSequence_++;

    const Node* const root = &entry.document->root();
    std::vector<Node*> pending{&entry.document->root()};
    while (!pending.empty()) {
        Node* node = pending.back();
        pending.pop_back();
        const NodeHandle handle = registerNode(entry, *node);
        if (handle == 0) {
            return 0;
        }
        if (node == root) {
            entry.rootHandle = handle;
        }
        const auto& children = node->children();
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            pending.push_back(child->get());
        }
    }

    const DocumentHandle handle = nextDocumentHandle();
    if (handle == 0) {
        return 0;
    }
    entry.owner = owner;
    entry.dispatchOwner = handle;
    entry.role = role;
    entry.health = DocumentHealth::Live;
    entry.visible = visible;
    documents_.emplace(handle, std::move(entry));
    return handle;
}

DocumentRegistry::NodeHandle DocumentRegistry::findNodeById(
    DocumentHandle document,
    std::string_view id) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    if (!entry || !entry->document) {
        return 0;
    }
    try {
        return findNodeHandle(*entry, entry->document->find(id));
    } catch (...) {
        return 0;
    }
}

DocumentRegistry::OwnerHandle DocumentRegistry::dispatchOwner(DocumentHandle document) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    if (!entry) {
        return 0;
    }
    return entry->dispatchOwner != 0 ? entry->dispatchOwner : document;
}

void DocumentRegistry::setOwnerCancelHook(OwnerCancelHook hook, void* userData) noexcept {
    std::scoped_lock lock(mutex_);
    ownerCancelHook_ = hook;
    ownerCancelUserData_ = userData;
}

void DocumentRegistry::setSurfaceReleaseHook(SurfaceReleaseHook hook, void* userData) noexcept {
    std::scoped_lock lock(mutex_);
    surfaceReleaseHook_ = hook;
    surfaceReleaseUserData_ = userData;
}

bool DocumentRegistry::setDocumentRole(DocumentHandle document, DocumentRole role) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || entry->health == DocumentHealth::Destroying) {
        return false;
    }
    entry->role = role;
    return true;
}

bool DocumentRegistry::faultDocument(DocumentHandle document) noexcept {
    CallbackSnapshot lost{};
    std::vector<PointerCallbackSnapshot> pointerCallbacks;
    {
        std::unique_lock lock(mutex_);
        auto* entry = findDocument(document);
        if (!entry || entry->health == DocumentHealth::Destroying) {
            return false;
        }
        if (entry->health == DocumentHealth::Faulted) {
            return true;
        }

        entry->health = DocumentHealth::Faulted;
        entry->visible = false;
        entry->inputEnabled = false;
        ++entry->generation;
        try {
            if (entry->document) {
                entry->document->root().setEnabled(false);
            }
        } catch (...) {
        }

        if (focusedDocument_ == document) {
            lost = clearFocusLocked(document);
        }
        auto hover = clearHoverLocked(document);
        auto gesture = clearGestureLocked(document);
        pointerCallbacks.insert(pointerCallbacks.end(), hover.begin(), hover.end());
        pointerCallbacks.insert(pointerCallbacks.end(), gesture.begin(), gesture.end());
    }

    invoke(lost);
    invoke(std::move(pointerCallbacks));
    return true;
}

bool DocumentRegistry::recoverDocument(DocumentHandle document) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || entry->health != DocumentHealth::Faulted) {
        return false;
    }
    try {
        if (entry->document) {
            entry->document->root().setEnabled(true);
        }
    } catch (...) {
        return false;
    }
    entry->health = DocumentHealth::Ready;
    entry->visible = false;
    entry->inputEnabled = false;
    ++entry->generation;
    return true;
}

bool DocumentRegistry::documentState(DocumentHandle document, DocumentState& state) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    if (!entry) {
        return false;
    }

    state = DocumentState{
        .handle = document,
        .owner = entry->owner,
        .role = entry->role,
        .health = effectiveHealth(*entry),
        .visible = entry->visible,
        .inputEnabled = entry->inputEnabled,
        .focused = focusedDocument_ == document,
        .order = entry->order,
        .generation = entry->generation,
        .surfaceCount = static_cast<std::uint32_t>(
            std::min<std::size_t>(entry->surfaces.size(), UINT32_MAX)),
    };
    return true;
}

std::vector<DocumentRegistry::DocumentState> DocumentRegistry::enumerateDocuments() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::pair<std::uint64_t, DocumentState>> ordered;
    ordered.reserve(documents_.size());

    for (const auto& [handle, entry] : documents_) {
        ordered.emplace_back(
            entry.sequence,
            DocumentState{
                .handle = handle,
                .owner = entry.owner,
                .role = entry.role,
                .health = effectiveHealth(entry),
                .visible = entry.visible,
                .inputEnabled = entry.inputEnabled,
                .focused = focusedDocument_ == handle,
                .order = entry.order,
                .generation = entry.generation,
                .surfaceCount = static_cast<std::uint32_t>(
                    std::min<std::size_t>(entry.surfaces.size(), UINT32_MAX)),
            });
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    std::vector<DocumentState> states;
    states.reserve(ordered.size());
    for (auto& [sequence, state] : ordered) {
        (void)sequence;
        states.push_back(state);
    }
    return states;
}

std::uint64_t DocumentRegistry::documentGeneration(DocumentHandle document) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    return entry ? entry->generation : 0;
}

bool DocumentRegistry::acceptsGeneration(
    DocumentHandle document,
    std::uint64_t generation) const noexcept {
    if (generation == 0) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    return entry != nullptr &&
           entry->health != DocumentHealth::Faulted &&
           entry->health != DocumentHealth::Destroying &&
           entry->generation == generation;
}

DocumentRegistry::SurfaceHandle DocumentRegistry::attachSurface(DocumentHandle document) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || entry->health == DocumentHealth::Faulted ||
        entry->health == DocumentHealth::Destroying) {
        return 0;
    }

    const SurfaceHandle surface = nextSurfaceHandle();
    if (surface == 0) {
        return 0;
    }

    try {
        surfaces_.emplace(surface, document);
        entry->surfaces.insert(surface);
        return surface;
    } catch (...) {
        surfaces_.erase(surface);
        entry->surfaces.erase(surface);
        return 0;
    }
}

bool DocumentRegistry::detachSurface(SurfaceHandle surface) noexcept {
    if (surface == 0) {
        return false;
    }

    SurfaceReleaseHook releaseHook{};
    void* releaseUserData{};
    {
        std::scoped_lock lock(mutex_);
        const auto it = surfaces_.find(surface);
        if (it == surfaces_.end()) {
            return destroyedSurfaces_.contains(surface);
        }

        const DocumentHandle document = it->second;
        if (auto* entry = findDocument(document)) {
            entry->surfaces.erase(surface);
        }
        surfaces_.erase(it);
        destroyedSurfaces_.insert(surface);
        releaseHook = surfaceReleaseHook_;
        releaseUserData = surfaceReleaseUserData_;
    }

    if (releaseHook != nullptr) {
        releaseHook(surface, releaseUserData);
    }
    return true;
}

DocumentRegistry::DocumentHandle DocumentRegistry::surfaceDocument(SurfaceHandle surface) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto it = surfaces_.find(surface);
    return it == surfaces_.end() ? 0 : it->second;
}

bool DocumentRegistry::destroyDocumentManaged(DocumentHandle document) noexcept {
    OwnerHandle callbackOwner{};
    OwnerCancelHook cancelHook{};
    void* cancelUserData{};
    std::vector<SurfaceHandle> attached;

    {
        std::scoped_lock lock(mutex_);
        if (destroyedDocuments_.contains(document)) {
            return true;
        }

        auto* entry = findDocument(document);
        if (!entry) {
            return false;
        }
        if (entry->health == DocumentHealth::Destroying) {
            return true;
        }

        entry->health = DocumentHealth::Destroying;
        entry->visible = false;
        entry->inputEnabled = false;
        ++entry->generation;
        destroyedDocuments_.insert(document);

        callbackOwner = entry->dispatchOwner != 0 ? entry->dispatchOwner : document;
        cancelHook = ownerCancelHook_;
        cancelUserData = ownerCancelUserData_;
        attached.assign(entry->surfaces.begin(), entry->surfaces.end());
    }

    if (cancelHook != nullptr && callbackOwner != 0) {
        cancelHook(callbackOwner, cancelUserData);
    }

    for (SurfaceHandle surface : attached) {
        (void)detachSurface(surface);
    }

    if (destroyDocument(document)) {
        return true;
    }

    std::scoped_lock lock(mutex_);
    if (findDocument(document) != nullptr) {
        destroyedDocuments_.erase(document);
    }
    return false;
}

std::size_t DocumentRegistry::destroyOwnerDocuments(OwnerHandle owner) noexcept {
    if (owner == 0) {
        return 0;
    }

    std::vector<DocumentHandle> handles;
    {
        std::scoped_lock lock(mutex_);
        for (const auto& [handle, entry] : documents_) {
            if (entry.owner == owner && entry.health != DocumentHealth::Destroying) {
                handles.push_back(handle);
            }
        }
    }

    std::size_t destroyed = 0;
    for (DocumentHandle handle : handles) {
        if (destroyDocumentManaged(handle)) {
            ++destroyed;
        }
    }
    return destroyed;
}

} // namespace uirepo::fo4
