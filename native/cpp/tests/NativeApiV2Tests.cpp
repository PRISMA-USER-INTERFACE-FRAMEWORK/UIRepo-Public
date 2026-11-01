#include "DocumentRegistry.h"

#include "uirepo/DispatchQueue.h"
#include "uirepo/NativeApi.h"
#include "uirepo/NativeApiBridge.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using uirepo::fo4::DocumentRegistry;
namespace bridge = uirepo::native_bridge;
namespace registry = uirepo::registry;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testStateMappingCoversEveryField() {
    registry::DocumentState state;
    state.handle = 41;
    state.owner = 0xABCDEF0123456789ull;
    state.role = registry::DocumentRole::Overlay;
    state.health = registry::DocumentHealth::Hidden;
    state.visible = true;
    state.inputEnabled = true;
    state.focused = true;
    state.order = -7;
    state.generation = 0x1122334455667788ull;

    const UIRepoDocumentStateV1 abi = bridge::makeDocumentState(state);
    require(abi.size == sizeof(UIRepoDocumentStateV1), "mapped state must carry the exact published size");
    require(abi.visible == 1 && abi.inputEnabled == 1 && abi.focused == 1, "boolean fields were not mapped");
    require(abi.health == UIREPO_DOCUMENT_HEALTH_HIDDEN, "health was not mapped");
    require(abi.role == UIREPO_DOCUMENT_ROLE_OVERLAY, "role was not mapped");
    require(abi.order == -7, "order was not mapped");
    require(abi.ownerId == 0xABCDEF0123456789ull, "owner was not mapped");
    require(abi.generation == 0x1122334455667788ull, "generation was not mapped");
    require(abi.reserved0 == 0 && abi.reserved1 == 0, "reserved fields must be zero");

    state.visible = false;
    state.inputEnabled = false;
    state.focused = false;
    const UIRepoDocumentStateV1 cleared = bridge::makeDocumentState(state);
    require(cleared.visible == 0 && cleared.inputEnabled == 0 && cleared.focused == 0, "false booleans were not mapped");
}

void testEveryHealthAndRoleSurvivesMapping() {
    const registry::DocumentHealth healths[] = {
        registry::DocumentHealth::Creating, registry::DocumentHealth::Ready,
        registry::DocumentHealth::Live,     registry::DocumentHealth::Hidden,
        registry::DocumentHealth::Faulted,  registry::DocumentHealth::Destroying,
        registry::DocumentHealth::Destroyed,
    };
    const std::uint32_t expectedHealth[] = {
        UIREPO_DOCUMENT_HEALTH_CREATING, UIREPO_DOCUMENT_HEALTH_READY,
        UIREPO_DOCUMENT_HEALTH_LIVE,     UIREPO_DOCUMENT_HEALTH_HIDDEN,
        UIREPO_DOCUMENT_HEALTH_FAULTED,  UIREPO_DOCUMENT_HEALTH_DESTROYING,
        UIREPO_DOCUMENT_HEALTH_DESTROYED,
    };
    for (std::size_t i = 0; i < std::size(healths); ++i) {
        registry::DocumentState state;
        state.health = healths[i];
        require(bridge::makeDocumentState(state).health == expectedHealth[i], "health value drifted");
    }

    const registry::DocumentRole roles[] = {
        registry::DocumentRole::HUD,          registry::DocumentRole::Panel,
        registry::DocumentRole::Overlay,      registry::DocumentRole::WorldSurface,
        registry::DocumentRole::Diagnostic,
    };
    const std::uint32_t expectedRole[] = {
        UIREPO_DOCUMENT_ROLE_HUD,           UIREPO_DOCUMENT_ROLE_PANEL,
        UIREPO_DOCUMENT_ROLE_OVERLAY,       UIREPO_DOCUMENT_ROLE_WORLD_SURFACE,
        UIREPO_DOCUMENT_ROLE_DIAGNOSTIC,
    };
    for (std::size_t i = 0; i < std::size(roles); ++i) {
        registry::DocumentState state;
        state.role = roles[i];
        require(bridge::makeDocumentState(state).role == expectedRole[i], "role value drifted");
    }
}

void testRealRegistryStateReportsEffectiveHealth() {
    DocumentRegistry registry;
    const auto handle = registry.createOwnedDocument("state-probe", 0, DocumentRegistry::DocumentRole::Panel);
    require(handle != 0, "could not create the state probe");

    DocumentRegistry::DocumentState state;
    require(registry.documentState(handle, state), "registry refused its own document");
    UIRepoDocumentStateV1 abi = bridge::makeDocumentState(state);
    require(abi.visible == 1 && abi.health == UIREPO_DOCUMENT_HEALTH_LIVE, "a fresh visible document must be Live");
    require(abi.role == UIREPO_DOCUMENT_ROLE_PANEL && abi.ownerId == 0, "ABI-created documents are owner 0 Panels");
    require(abi.generation == 1, "a fresh document starts at generation 1");

    require(registry.setDocumentOrder(handle, 7), "could not set order");
    require(registry.setDocumentVisible(handle, false), "could not hide");
    require(registry.documentState(handle, state), "registry lost the hidden document");
    abi = bridge::makeDocumentState(state);
    require(abi.visible == 0 && abi.health == UIREPO_DOCUMENT_HEALTH_HIDDEN, "a hidden document must report Hidden");
    require(abi.order == 7, "order must round-trip");

    require(registry.faultDocument(handle), "could not fault");
    require(registry.documentState(handle, state), "registry lost the faulted document");
    abi = bridge::makeDocumentState(state);
    require(abi.health == UIREPO_DOCUMENT_HEALTH_FAULTED && abi.visible == 0, "a faulted document must report Faulted and hidden");

    require(registry.destroyDocument(handle), "could not destroy");
    require(!registry.documentState(handle, state), "a destroyed handle must be unknown");
}

struct EnumerationProbe {
    DocumentRegistry* registry{};
    std::vector<UIRepoDocumentHandle> seen;
    std::vector<UIRepoDocumentStateV1> states;
    UIRepoDocumentHandle destroyOnFirst{};
    UIRepoDocumentHandle createdDuring{};
};

void recordDocument(UIRepoDocumentHandle document, const UIRepoDocumentStateV1* state, void* userData) {
    auto& probe = *static_cast<EnumerationProbe*>(userData);
    probe.seen.push_back(document);
    probe.states.push_back(*state);
    if (probe.seen.size() == 1) {
        // Reaching into the registry here would deadlock if the snapshot still held its lock.
        require(probe.registry->destroyDocument(probe.destroyOnFirst), "callback could not destroy a document");
        probe.createdDuring = probe.registry->createOwnedDocument("created-during-enumeration", 0);
    }
}

void testEnumerationSnapshotSurvivesCallbacksThatMutateTheRegistry() {
    DocumentRegistry registry;
    const auto first = registry.createOwnedDocument("first", 0);
    const auto second = registry.createOwnedDocument("second", 0);
    const auto third = registry.createOwnedDocument("third", 0);
    require(first != 0 && first < second && second < third, "handles must be monotonic");
    require(registry.setDocumentOrder(second, 5), "could not order second");
    require(registry.setDocumentVisible(third, false), "could not hide third");

    EnumerationProbe probe;
    probe.registry = &registry;
    probe.destroyOnFirst = second;
    const bool ok = bridge::enumerateSnapshot(
        [&] { return registry.enumerateDocuments(); }, &recordDocument, &probe);

    require(ok, "enumeration of a populated registry failed");
    require(probe.seen == std::vector<UIRepoDocumentHandle>({first, second, third}),
            "the snapshot must deliver every document in creation order, including ones destroyed mid-pass");
    require(probe.states[1].order == 5 && probe.states[1].visible == 1, "snapshot state of the destroyed document must be its state at snapshot time");
    require(probe.states[2].visible == 0 && probe.states[2].health == UIREPO_DOCUMENT_HEALTH_HIDDEN,
            "snapshot state must reflect hidden documents");
    require(probe.createdDuring != 0, "callback could not create a document");

    DocumentRegistry::DocumentState state;
    require(!registry.documentState(second, state), "the document destroyed by the callback must be gone");

    EnumerationProbe second_pass;
    second_pass.registry = &registry;
    second_pass.destroyOnFirst = 0;
    const bool again = bridge::enumerateSnapshot(
        [&] { return registry.enumerateDocuments(); },
        +[](UIRepoDocumentHandle document, const UIRepoDocumentStateV1*, void* userData) {
            static_cast<EnumerationProbe*>(userData)->seen.push_back(document);
        },
        &second_pass);
    require(again, "second enumeration failed");
    require(second_pass.seen == std::vector<UIRepoDocumentHandle>({first, third, probe.createdDuring}),
            "a later pass must see the post-callback registry");
}

void testEnumerationOfEmptyRegistrySucceedsAndNullCallbackFails() {
    DocumentRegistry registry;
    std::size_t calls = 0;
    const bool empty = bridge::enumerateSnapshot(
        [&] { return registry.enumerateDocuments(); },
        +[](UIRepoDocumentHandle, const UIRepoDocumentStateV1*, void* userData) {
            ++*static_cast<std::size_t*>(userData);
        },
        &calls);
    require(empty && calls == 0, "an empty registry is a successful empty enumeration");

    bool snapshotTaken = false;
    const bool refused = bridge::enumerateSnapshot(
        [&] {
            snapshotTaken = true;
            return registry.enumerateDocuments();
        },
        nullptr,
        nullptr);
    require(!refused && !snapshotTaken, "a null callback must be refused before any snapshot");
}

struct DispatchProbe {
    std::vector<int> order;
};

template <int Value>
void record(void* userData) {
    static_cast<DispatchProbe*>(userData)->order.push_back(Value);
}

[[nodiscard]] bool runOne(uirepo::dispatch::Queue& queue) {
    auto task = queue.takeNext();
    if (!task) {
        return false;
    }
    task->function();
    queue.finishExecuting();
    return true;
}

void testDispatchBridgeNeverRunsInlineAndKeepsOrder() {
    uirepo::dispatch::Queue queue;
    queue.attach();
    const auto enqueue = [&](std::function<void()> task) { return queue.tryEnqueue(std::move(task)).accepted; };

    DispatchProbe probe;
    require(bridge::bridgeDispatch(&record<1>, &probe, enqueue) == bridge::DispatchStatus::Queued, "first dispatch refused");
    require(bridge::bridgeDispatch(&record<2>, &probe, enqueue) == bridge::DispatchStatus::Queued, "second dispatch refused");
    require(bridge::bridgeDispatch(&record<3>, &probe, enqueue) == bridge::DispatchStatus::Queued, "third dispatch refused");
    require(probe.order.empty(), "dispatch ran a callback inline");

    while (runOne(queue)) {
    }
    require(probe.order == std::vector<int>({1, 2, 3}), "dispatch must be FIFO and pass userData through");
}

void testDispatchBridgeRefusalCases() {
    uirepo::dispatch::Queue queue;
    const auto enqueue = [&](std::function<void()> task) { return queue.tryEnqueue(std::move(task)).accepted; };
    DispatchProbe probe;

    require(bridge::bridgeDispatch(&record<1>, &probe, enqueue) == bridge::DispatchStatus::Refused,
            "a detached dispatcher must refuse work");

    queue.attach();
    bool dispatchTouched = false;
    const auto spy = [&](std::function<void()>) {
        dispatchTouched = true;
        return true;
    };
    require(bridge::bridgeDispatch(nullptr, &probe, spy) == bridge::DispatchStatus::NullCallback, "null callback was not classified");
    require(!dispatchTouched, "a null callback must never reach the dispatcher");

    for (std::size_t i = 0; i < uirepo::dispatch::kMaxQueue; ++i) {
        require(bridge::bridgeDispatch(&record<1>, &probe, enqueue) == bridge::DispatchStatus::Queued, "queue refused work below its bound");
    }
    require(bridge::bridgeDispatch(&record<2>, &probe, enqueue) == bridge::DispatchStatus::Refused, "the queue bound was not enforced");
    require(probe.order.empty(), "refusal paths ran a callback inline");

    queue.failClosed();
    require(bridge::bridgeDispatch(&record<3>, &probe, enqueue) == bridge::DispatchStatus::Refused, "a failed-closed dispatcher must refuse work");
    require(!runOne(queue), "failing closed must discard queued work");
    require(probe.order.empty(), "failing closed ran queued callbacks");
}

[[nodiscard]] UIRepoResultV1 resultOfThrowingEnumeration(void (*callback)(UIRepoDocumentHandle, const UIRepoDocumentStateV1*, void*)) {
    DocumentRegistry registry;
    (void)registry.createDocument("throw-probe");
    try {
        (void)bridge::enumerateSnapshot([&] { return registry.enumerateDocuments(); }, callback, nullptr);
    } catch (...) {
        return bridge::resultForActiveException();
    }
    return UIREPO_RESULT_OK;
}

void testThrowingCallbackMapsToItsOwnResult() {
    require(resultOfThrowingEnumeration([](UIRepoDocumentHandle, const UIRepoDocumentStateV1*, void*) { throw std::bad_alloc(); })
                == UIREPO_RESULT_OUT_OF_MEMORY,
            "bad_alloc must map to OUT_OF_MEMORY");
    require(resultOfThrowingEnumeration([](UIRepoDocumentHandle, const UIRepoDocumentStateV1*, void*) { throw std::runtime_error("consumer"); })
                == UIREPO_RESULT_UNKNOWN,
            "a consumer exception must not be reported as OUT_OF_MEMORY");
    require(resultOfThrowingEnumeration([](UIRepoDocumentHandle, const UIRepoDocumentStateV1*, void*) { throw 7; })
                == UIREPO_RESULT_UNKNOWN,
            "a non-std exception must map to UNKNOWN");
}

} // namespace

int main() {
    testStateMappingCoversEveryField();
    testEveryHealthAndRoleSurvivesMapping();
    testRealRegistryStateReportsEffectiveHealth();
    testEnumerationSnapshotSurvivesCallbacksThatMutateTheRegistry();
    testEnumerationOfEmptyRegistrySucceedsAndNullCallbackFails();
    testDispatchBridgeNeverRunsInlineAndKeepsOrder();
    testDispatchBridgeRefusalCases();
    testThrowingCallbackMapsToItsOwnResult();
    std::cout << "UIRepo native API v2 regressions passed\n";
    return 0;
}
