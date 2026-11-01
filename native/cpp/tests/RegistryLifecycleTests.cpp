#include "DocumentRegistry.h"

#include <cstdlib>
#include <limits>
#include <memory>
#include <iostream>
#include <string>
#include <vector>

namespace {

using uirepo::fo4::DocumentRegistry;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct CancelProbe {
    std::vector<DocumentRegistry::OwnerHandle> owners;
};

void cancelOwner(DocumentRegistry::OwnerHandle owner, void* userData) noexcept {
    static_cast<CancelProbe*>(userData)->owners.push_back(owner);
}

struct SurfaceReleaseProbe {
    std::vector<DocumentRegistry::SurfaceHandle> surfaces;
    DocumentRegistry* registry{};
    bool registryWasUnlocked{true};
};

void releaseSurface(DocumentRegistry::SurfaceHandle surface, void* userData) noexcept {
    auto& probe = *static_cast<SurfaceReleaseProbe*>(userData);
    probe.surfaces.push_back(surface);
    if (probe.registry != nullptr) {
        probe.registryWasUnlocked = probe.registryWasUnlocked &&
            probe.registry->surfaceDocument(surface) == 0;
    }
}

void testOwnedDocumentStateAndEnumeration() {
    DocumentRegistry registry;
    const auto hud = registry.createOwnedDocument(
        "registry-hud",
        101,
        DocumentRegistry::DocumentRole::HUD);
    const auto diagnostic = registry.createOwnedDocument(
        "registry-diagnostic",
        202,
        DocumentRegistry::DocumentRole::Diagnostic);

    require(hud != 0 && diagnostic != 0 && hud != diagnostic, "document handles must be unique");
    require(registry.dispatchOwner(hud) == hud, "each document must receive a unique dispatch owner");
    require(registry.dispatchOwner(diagnostic) == diagnostic, "dispatch owner must match the stable document lifetime");

    DocumentRegistry::DocumentState state{};
    require(registry.documentState(hud, state), "owned document state lookup failed");
    require(state.handle == hud, "state returned the wrong handle");
    require(state.owner == 101, "caller owner identity was not retained");
    require(state.role == DocumentRegistry::DocumentRole::HUD, "document role was not retained");
    require(state.health == DocumentRegistry::DocumentHealth::Live, "new visible document must be live");
    require(state.visible, "new document visibility mismatch");
    require(state.generation == 1, "new document generation must begin at one");

    require(registry.setDocumentVisible(hud, false), "could not hide owned document");
    require(registry.documentState(hud, state), "hidden document state lookup failed");
    require(state.health == DocumentRegistry::DocumentHealth::Hidden, "hidden document health mismatch");

    require(registry.setDocumentRole(hud, DocumentRegistry::DocumentRole::Overlay), "could not change document role");
    require(registry.documentState(hud, state), "role-updated document state lookup failed");
    require(state.role == DocumentRegistry::DocumentRole::Overlay, "updated document role did not persist");

    const auto states = registry.enumerateDocuments();
    require(states.size() == 2, "registry enumeration must return every live entry exactly once");
    require(states[0].handle == hud && states[1].handle == diagnostic, "registry enumeration must be deterministic by creation order");
}

void testFaultClearsFocusAndRejectsOldGeneration() {
    DocumentRegistry registry;
    const auto document = registry.createOwnedDocument(
        "fault-root",
        303,
        DocumentRegistry::DocumentRole::Panel);
    require(document != 0, "could not create fault test document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable document input");

    const auto root = registry.rootNode(document);
    const auto button = registry.createButton(document, root, "fault-button", "Button");
    require(button != 0, "could not create fault test button");
    require(registry.focusNode(document, button), "could not establish focus before fault");

    const auto generation = registry.documentGeneration(document);
    require(generation != 0 && registry.acceptsGeneration(document, generation), "current generation should be accepted before fault");
    require(registry.faultDocument(document), "fault transition failed");

    DocumentRegistry::DocumentState state{};
    require(registry.documentState(document, state), "faulted document state lookup failed");
    require(state.health == DocumentRegistry::DocumentHealth::Faulted, "fault health was not retained");
    require(!state.focused, "faulting a document must clear focus");
    require(!state.visible && !state.inputEnabled, "faulting a document must revoke visibility and input ownership");
    require(state.generation > generation, "fault must invalidate the document generation");
    require(!registry.acceptsGeneration(document, generation), "old worker generation remained valid after fault");

    require(registry.setDocumentVisible(document, true), "fault focus probe could not restore visibility flag");
    require(registry.setDocumentInputEnabled(document, true), "fault focus probe could not restore input flag");
    require(!registry.focusNode(document, button), "faulted document regained focus after flags were toggled");

    require(registry.recoverDocument(document), "fault recovery failed");
    require(registry.documentState(document, state), "recovered document state lookup failed");
    require(state.health == DocumentRegistry::DocumentHealth::Ready, "recovered document must return to Ready while hidden");
    require(state.generation > generation, "recovery must advance generation again");
}

void testSurfaceReleaseHookRunsOutsideRegistryLock() {
    DocumentRegistry registry;
    SurfaceReleaseProbe releaseProbe{.registry = &registry};
    registry.setSurfaceReleaseHook(&releaseSurface, &releaseProbe);

    const auto document = registry.createOwnedDocument(
        "surface-release-root",
        390,
        DocumentRegistry::DocumentRole::Panel);
    require(document != 0, "could not create surface release document");
    const auto surface = registry.attachSurface(document);
    require(surface != 0, "could not attach surface release probe");

    require(registry.detachSurface(surface), "explicit surface detach failed");
    require(releaseProbe.surfaces.size() == 1 && releaseProbe.surfaces.front() == surface,
        "explicit detach did not release the attached runtime surface exactly once");
    require(releaseProbe.registryWasUnlocked,
        "surface release hook could not reenter registry; callback ran while registry mutex was held");
    require(registry.detachSurface(surface), "duplicate surface detach must remain idempotent");
    require(releaseProbe.surfaces.size() == 1,
        "duplicate surface detach repeated the runtime release callback");
}

void testSurfaceAndManagedDestructionContract() {
    DocumentRegistry registry;
    CancelProbe probe{};
    SurfaceReleaseProbe releaseProbe{.registry = &registry};
    registry.setOwnerCancelHook(&cancelOwner, &probe);
    registry.setSurfaceReleaseHook(&releaseSurface, &releaseProbe);

    const auto document = registry.createOwnedDocument(
        "destroy-root",
        404,
        DocumentRegistry::DocumentRole::WorldSurface);
    require(document != 0, "could not create managed-destroy document");

    const auto firstSurface = registry.attachSurface(document);
    const auto secondSurface = registry.attachSurface(document);
    require(firstSurface != 0 && secondSurface != 0 && firstSurface != secondSurface, "surface handles must be unique");
    require(registry.surfaceDocument(firstSurface) == document, "surface did not retain document association");

    DocumentRegistry::DocumentState state{};
    require(registry.documentState(document, state) && state.surfaceCount == 2, "attached surface count mismatch");

    require(registry.destroyDocumentManaged(document), "managed destruction failed");
    require(probe.owners.size() == 1 && probe.owners.front() == document, "managed destruction did not cancel the document-specific dispatch owner");
    require(releaseProbe.surfaces.size() == 2, "managed destruction did not release every attached runtime surface");
    require(
        (releaseProbe.surfaces[0] == firstSurface || releaseProbe.surfaces[1] == firstSurface) &&
        (releaseProbe.surfaces[0] == secondSurface || releaseProbe.surfaces[1] == secondSurface),
        "managed destruction released the wrong surface handles");
    require(releaseProbe.registryWasUnlocked,
        "managed destruction invoked surface release while holding the registry mutex");
    require(!registry.documentState(document, state), "destroyed document remained addressable");
    require(registry.enumerateDocuments().empty(), "destroyed document remained in enumeration");
    require(registry.surfaceDocument(firstSurface) == 0 && registry.surfaceDocument(secondSurface) == 0, "destroy did not release attached surfaces");
    require(registry.detachSurface(firstSurface), "released surface destroy must be idempotent");
    require(releaseProbe.surfaces.size() == 2, "idempotent detach repeated a runtime surface release");
    require(registry.destroyDocumentManaged(document), "duplicate managed destroy must be harmless");
    require(probe.owners.size() == 1, "duplicate managed destroy cancelled the dispatch owner twice");
    require(!registry.destroyDocumentManaged(999999), "unknown document handle was accepted as a destroyed document");
}

void testCallerScopedDestructionDoesNotPoisonSiblingDispatchOwners() {
    DocumentRegistry registry;
    CancelProbe probe{};
    registry.setOwnerCancelHook(&cancelOwner, &probe);

    const auto first = registry.createOwnedDocument("owner-a-1", 501, DocumentRegistry::DocumentRole::Panel);
    const auto second = registry.createOwnedDocument("owner-a-2", 501, DocumentRegistry::DocumentRole::Overlay);
    const auto other = registry.createOwnedDocument("owner-b", 502, DocumentRegistry::DocumentRole::HUD);
    require(first && second && other, "could not create owner-scoped documents");
    require(registry.dispatchOwner(first) != registry.dispatchOwner(second), "sibling documents must not share a dispatch owner");

    require(registry.destroyOwnerDocuments(501) == 2, "caller-scoped destroy removed the wrong number of documents");
    require(probe.owners.size() == 2, "caller-scoped destroy did not cancel both document dispatch owners");
    require(
        (probe.owners[0] == first || probe.owners[1] == first) &&
        (probe.owners[0] == second || probe.owners[1] == second),
        "caller-scoped destroy cancelled the wrong dispatch owners");

    DocumentRegistry::DocumentState state{};
    require(!registry.documentState(first, state) && !registry.documentState(second, state), "caller-scoped documents remained live");
    require(registry.documentState(other, state), "caller-scoped destroy contaminated another caller");
    require(state.owner == 502, "surviving document caller identity changed unexpectedly");
    require(registry.dispatchOwner(other) == other, "surviving document dispatch owner was poisoned");

    const auto states = registry.enumerateDocuments();
    require(states.size() == 1 && states.front().handle == other, "enumeration returned destroyed caller entries");
}

void testDestroyedDocumentHandleNeverReused() {
    DocumentRegistry registry;
    const auto stale = registry.createOwnedDocument(
        "stale-document",
        601,
        DocumentRegistry::DocumentRole::Panel);
    require(stale != 0, "could not create stale document probe");
    require(registry.destroyDocumentManaged(stale), "could not destroy stale document probe");

    const auto replacement = registry.createOwnedDocument(
        "replacement-document",
        601,
        DocumentRegistry::DocumentRole::Panel);
    require(replacement != 0 && replacement != stale, "destroyed document handle was reused by a later allocation");

    DocumentRegistry::DocumentState state{};
    require(!registry.documentState(stale, state), "destroyed document handle became addressable after later allocation");
    require(!registry.setDocumentVisible(stale, true), "destroyed document handle accepted a later visibility mutation");
    require(registry.documentState(replacement, state), "replacement document was not addressable");
}

void testOffscreenDisplayListIsolation() {
    DocumentRegistry registry;
    const auto document = registry.createOwnedDocument(
        "offscreen-document",
        701,
        DocumentRegistry::DocumentRole::Panel);
    require(document != 0, "could not create offscreen document probe");
    require(
        registry.createText(document, registry.rootNode(document), "offscreen-label", "offscreen") != 0,
        "could not populate offscreen document probe");

    const uirepo::Size viewport{256.0f, 128.0f};
    std::vector<uirepo::DrawCommand> commands;

    // Documents are created visible, so offscreen use always starts with an explicit hide.
    require(!registry.snapshotDisplayList(document, viewport, commands), "newly created visible document was allowed offscreen rendering");
    require(registry.setDocumentVisible(document, false), "could not hide offscreen probe");

    // A hidden, input-disabled, healthy document is the only shape allowed offscreen.
    require(registry.snapshotDisplayList(document, viewport, commands), "isolated document was refused offscreen rendering");
    require(!commands.empty(), "offscreen snapshot produced no draw commands");

    // Every rejection must also clear the caller's buffer so stale geometry cannot be rendered.
    const auto rejects = [&](const char* message) {
        commands.assign(4, uirepo::DrawCommand{});
        const bool accepted = registry.snapshotDisplayList(document, viewport, commands);
        require(!accepted, message);
        require(commands.empty(), "rejected offscreen snapshot left stale draw commands in the caller buffer");
    };

    require(registry.setDocumentVisible(document, true), "could not make offscreen probe visible");
    rejects("visible document was allowed offscreen rendering");
    require(registry.setDocumentVisible(document, false), "could not hide offscreen probe");

    require(registry.setDocumentInputEnabled(document, true), "could not enable input on offscreen probe");
    rejects("input-active document was allowed offscreen rendering");
    require(registry.setDocumentInputEnabled(document, false), "could not disable input on offscreen probe");

    require(registry.faultDocument(document), "could not fault offscreen probe");
    rejects("faulted document was allowed offscreen rendering");
    require(registry.recoverDocument(document), "could not recover offscreen probe");

    require(registry.snapshotDisplayList(document, viewport, commands), "recovered document was refused offscreen rendering");
    require(!commands.empty(), "recovered offscreen snapshot produced no draw commands");

    // A degenerate surface size must never reach layout.
    for (const uirepo::Size bad : {
             uirepo::Size{0.0f, 128.0f},
             uirepo::Size{256.0f, 0.0f},
             uirepo::Size{-256.0f, 128.0f},
             uirepo::Size{256.0f, std::numeric_limits<float>::quiet_NaN()},
             uirepo::Size{std::numeric_limits<float>::infinity(), 128.0f},
         }) {
        commands.assign(4, uirepo::DrawCommand{});
        require(!registry.snapshotDisplayList(document, bad, commands), "degenerate surface size was accepted for offscreen rendering");
        require(commands.empty(), "degenerate offscreen snapshot left stale draw commands in the caller buffer");
    }

    // Unknown and destroyed documents are never renderable.
    commands.assign(4, uirepo::DrawCommand{});
    require(!registry.snapshotDisplayList(0, viewport, commands), "null document was allowed offscreen rendering");
    require(commands.empty(), "null-document snapshot left stale draw commands in the caller buffer");

    require(registry.destroyDocument(document), "could not destroy offscreen probe");
    commands.assign(4, uirepo::DrawCommand{});
    require(!registry.snapshotDisplayList(document, viewport, commands), "destroyed document was allowed offscreen rendering");
    require(commands.empty(), "destroyed-document snapshot left stale draw commands in the caller buffer");
}

std::unique_ptr<uirepo::Document> buildAdoptableTree() {
    auto root = std::make_unique<uirepo::PanelNode>("adopt-root");
    auto& column = root->emplaceChild<uirepo::StackNode>("adopt-column", uirepo::Axis::Column);
    column.emplaceChild<uirepo::TextNode>("adopt-label", "adopted");
    column.emplaceChild<uirepo::ToggleNode>("adopt-flag", false);
    return std::make_unique<uirepo::Document>(std::move(root));
}

void testAdoptedDocumentIsRegisteredLikeACreatedOne() {
    DocumentRegistry registry;
    const auto adopted = registry.adoptOwnedDocument(
        buildAdoptableTree(), 9001, DocumentRegistry::DocumentRole::Overlay, false);
    require(adopted != 0, "adoption of a prebuilt tree failed");

    DocumentRegistry::DocumentState state{};
    require(registry.documentState(adopted, state), "adopted document is not addressable");
    require(state.owner == 9001 && state.role == DocumentRegistry::DocumentRole::Overlay, "adopted owner or role is wrong");
    require(!state.visible && !state.inputEnabled && !state.focused, "adopted hidden document is visible or input-enabled");
    require(state.health == DocumentRegistry::DocumentHealth::Hidden && state.generation == 1, "adopted document health or generation is wrong");
    require(registry.dispatchOwner(adopted) == adopted, "adopted document must dispatch under its own handle");
    require(registry.visibleCount() == 0, "a hidden adopted document counted as visible");

    const auto root = registry.rootNode(adopted);
    const auto column = registry.findNodeById(adopted, "adopt-column");
    const auto label = registry.findNodeById(adopted, "adopt-label");
    const auto flag = registry.findNodeById(adopted, "adopt-flag");
    require(root != 0 && column != 0 && label != 0 && flag != 0, "adopted nodes were not all registered");
    require(root != column && column != label && label != flag, "adopted node handles are not distinct");
    require(registry.findNodeById(adopted, "adopt-root") == root, "root lookup by id disagrees with rootNode");
    require(registry.findNodeById(adopted, "missing") == 0 && registry.findNodeById(adopted + 1, "adopt-root") == 0,
            "unknown node or document lookup must return 0");

    bool value = true;
    require(registry.getToggleValue(adopted, flag, value) && !value, "adopted toggle did not read back");
    require(registry.setToggleValue(adopted, flag, true) && registry.getToggleValue(adopted, flag, value) && value,
            "registry mutation of an adopted node failed");
    require(registry.setText(adopted, label, "changed"), "registry text mutation of an adopted node failed");
    const auto child = registry.createText(adopted, column, "adopt-late", "late");
    require(child != 0 && registry.findNodeById(adopted, "adopt-late") == child, "node created after adoption is not addressable");
    require(registry.createText(adopted, column, "adopt-label", "duplicate") == 0, "duplicate id accepted in an adopted document");

    std::vector<uirepo::DrawCommand> commands;
    require(registry.snapshotDisplayList(adopted, uirepo::Size{320.0f, 200.0f}, commands) && !commands.empty(),
            "hidden adopted document could not be rendered offscreen");

    require(registry.setDocumentVisible(adopted, true) && registry.visibleCount() == 1, "adopted document could not be shown");
    const auto visibleByDefault = registry.adoptOwnedDocument(buildAdoptableTree(), 9001);
    require(visibleByDefault != 0 && visibleByDefault != adopted, "second adoption failed or reused a handle");
    require(registry.documentState(visibleByDefault, state) && state.visible && state.role == DocumentRegistry::DocumentRole::Panel,
            "default adoption must mirror createOwnedDocument");
}

void testAdoptionRefusalsAndOwnerTeardown() {
    DocumentRegistry registry;
    require(registry.adoptOwnedDocument(nullptr, 9001) == 0, "a null document was adopted");

    const auto first = registry.adoptOwnedDocument(buildAdoptableTree(), 9001, DocumentRegistry::DocumentRole::Panel, false);
    const auto second = registry.adoptOwnedDocument(buildAdoptableTree(), 9001, DocumentRegistry::DocumentRole::Panel, false);
    const auto sibling = registry.adoptOwnedDocument(buildAdoptableTree(), 9002, DocumentRegistry::DocumentRole::Panel, false);
    const auto created = registry.createOwnedDocument("created-sibling", 9001);
    const auto anonymous = registry.createDocument("anonymous-sibling");
    require(first != 0 && second != 0 && sibling != 0 && created != 0 && anonymous != 0, "setup documents");

    CancelProbe probe;
    registry.setOwnerCancelHook(&cancelOwner, &probe);
    require(registry.destroyOwnerDocuments(9001) == 3, "owner teardown must remove every document of the owner, adopted or created");
    DocumentRegistry::DocumentState state{};
    require(!registry.documentState(first, state) && !registry.documentState(second, state) && !registry.documentState(created, state),
            "owner teardown left a document of the owner behind");
    require(registry.documentState(sibling, state) && registry.documentState(anonymous, state),
            "owner teardown destroyed a document of another owner");
    require(registry.findNodeById(first, "adopt-root") == 0, "a destroyed adopted document still resolves nodes");
    require(probe.owners.size() == 3, "owner teardown must cancel dispatched work once per destroyed document");
    require(registry.adoptOwnedDocument(buildAdoptableTree(), 9001) > created, "handles must never be reused after teardown");
}

} // namespace

int main() {
    testOwnedDocumentStateAndEnumeration();
    testFaultClearsFocusAndRejectsOldGeneration();
    testSurfaceReleaseHookRunsOutsideRegistryLock();
    testSurfaceAndManagedDestructionContract();
    testCallerScopedDestructionDoesNotPoisonSiblingDispatchOwners();
    testDestroyedDocumentHandleNeverReused();
    testOffscreenDisplayListIsolation();
    testAdoptedDocumentIsRegisteredLikeACreatedOne();
    testAdoptionRefusalsAndOwnerTeardown();
    std::cout << "UIRepo registry lifecycle regressions passed\n";
    return 0;
}
