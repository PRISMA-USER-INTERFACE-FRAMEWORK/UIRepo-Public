#include "uirepo/RenderHookBroker.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace uirepo::render;

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testPhaseParticipationDoesNotCollideWithTargetClaim() {
    RenderHookBroker broker;
    require(
        broker.claimTarget(0x1000u, "core", 0) == TargetClaimStatus::Ok,
        "physical target claim must succeed");
    require(
        broker.registerPhase(RenderPhase::UIBeforeComposite, "core", 0, 0x1000u) ==
            PhaseRegisterStatus::Ok,
        "the target owner may register a before-composite phase");
    require(
        broker.registerPhase(RenderPhase::UIAfterComposite, "core", 0, 0x1000u) ==
            PhaseRegisterStatus::Ok,
        "the same owner may also register an after-composite phase on the same target");
    require(broker.phaseRegistrationCount() == 2, "both phase registrations recorded");
    require(broker.targetClaimCount() == 1, "phase registration must not create a target claim");
}

void testDuplicatePhysicalOwnershipRejected() {
    RenderHookBroker broker;
    require(broker.claimTarget(0x1000u, "core", 0) == TargetClaimStatus::Ok, "first claim ok");
    require(
        broker.claimTarget(0x1000u, "core", 9) == TargetClaimStatus::RejectedDuplicateOwner,
        "same owner claiming the same physical target twice must be rejected");
    require(
        broker.claimTarget(0x1000u, "overlay", 9) == TargetClaimStatus::Ok,
        "a distinct owner may chain on the same physical target");
    require(broker.targetClaimCount() == 2, "only the duplicate physical claim is rejected");
}

void testDuplicatePhaseRegistrationRejectedOnlyForSameTriple() {
    RenderHookBroker broker;
    require(
        broker.registerPhase(RenderPhase::UIComposite, "core", 0, 0x50u) == PhaseRegisterStatus::Ok,
        "first phase registration ok");
    require(
        broker.registerPhase(RenderPhase::UIComposite, "core", 3, 0x50u) ==
            PhaseRegisterStatus::RejectedDuplicate,
        "same (phase,target,owner) twice is a duplicate");
    require(
        broker.registerPhase(RenderPhase::UIComposite, "core", 3, 0x51u) == PhaseRegisterStatus::Ok,
        "same phase/owner on a different target is allowed");
}

void testPhaseIsolationAndStablePriority() {
    RenderHookBroker broker;
    (void)broker.registerPhase(RenderPhase::UIComposite, "b", 10);
    (void)broker.registerPhase(RenderPhase::UIComposite, "a", 0);
    (void)broker.registerPhase(RenderPhase::UIComposite, "c", 10);
    (void)broker.registerPhase(RenderPhase::UIAfterComposite, "after", 0);
    require(
        broker.orderedOwners(RenderPhase::UIComposite) == std::vector<std::string>{"a", "b", "c"},
        "phase owners sort by priority then registration order");
    require(
        broker.orderedOwners(RenderPhase::UIAfterComposite) == std::vector<std::string>{"after"},
        "phase ordering must not leak across phases");
    require(
        broker.orderedOwners(RenderPhase::ResizeBegin).empty(),
        "a phase with no registrations is empty");
}

void testPhysicalWrapPriorityIsStable() {
    RenderHookBroker broker;
    (void)broker.claimTarget(0x900u, "b", 10);
    (void)broker.claimTarget(0x900u, "a", 0);
    (void)broker.claimTarget(0x900u, "c", 10);
    require(
        broker.orderedOwnersForTarget(0x900u) == std::vector<std::string>{"a", "b", "c"},
        "physical wrap order sorts by wrap priority then claim order");
    require(broker.primaryOwner(0x900u) == std::optional<std::string>{"a"}, "primary owner is outermost");
    require(!broker.primaryOwner(0xDEADu).has_value(), "unclaimed target has no owner");
}

void testWrapOrderStatusDistinctions() {
    RenderHookBroker broker;
    (void)broker.claimTarget(0x77u, "outer", 0);
    (void)broker.claimTarget(0x77u, "middle", 5);
    (void)broker.claimTarget(0x77u, "inner", 10);

    require(
        broker.verifyWrapOrder(0x77u, {"outer", "middle", "inner"}) == WrapOrderStatus::Ok,
        "exact match is Ok");
    require(
        broker.verifyWrapOrder(0x77u, {"inner", "middle", "outer"}) == WrapOrderStatus::Inverted,
        "exact reverse is Inverted");
    require(
        broker.verifyWrapOrder(0x77u, {"outer", "inner", "middle"}) == WrapOrderStatus::Mismatch,
        "a same-size non-reverse permutation is Mismatch, not Inverted");
    require(
        broker.verifyWrapOrder(0x77u, {"outer", "middle", "stranger"}) == WrapOrderStatus::Mismatch,
        "an unexpected owner is Mismatch, not Inverted");
    require(
        broker.verifyWrapOrder(0x77u, {"outer", "middle"}) == WrapOrderStatus::CountMismatch,
        "a partial chain is CountMismatch");
    require(
        broker.verifyWrapOrder(0xBEEFu, {}) == WrapOrderStatus::UnknownTarget,
        "an unregistered target is UnknownTarget");
}

void testTwoElementReversalIsInverted() {
    RenderHookBroker broker;
    (void)broker.claimTarget(0x5u, "outer", 0);
    (void)broker.claimTarget(0x5u, "inner", 1);
    require(
        broker.verifyWrapOrder(0x5u, {"inner", "outer"}) == WrapOrderStatus::Inverted,
        "two-element exact reverse is Inverted");
}

void testFreezeRejectsWithoutMutation() {
    RenderHookBroker broker;
    (void)broker.claimTarget(0x1u, "core", 0);
    (void)broker.registerPhase(RenderPhase::UIComposite, "core", 0);
    broker.freeze();
    require(broker.frozen(), "freeze sets frozen");
    require(
        broker.claimTarget(0x2u, "late", 0) == TargetClaimStatus::RejectedFrozen,
        "claim after freeze rejected");
    require(
        broker.registerPhase(RenderPhase::UIComposite, "late", 0) == PhaseRegisterStatus::RejectedFrozen,
        "phase registration after freeze rejected");
    require(broker.targetClaimCount() == 1, "rejected claim must not mutate state");
    require(broker.phaseRegistrationCount() == 1, "rejected phase registration must not mutate state");
}

} // namespace

int main() {
    testPhaseParticipationDoesNotCollideWithTargetClaim();
    testDuplicatePhysicalOwnershipRejected();
    testDuplicatePhaseRegistrationRejectedOnlyForSameTriple();
    testPhaseIsolationAndStablePriority();
    testPhysicalWrapPriorityIsStable();
    testWrapOrderStatusDistinctions();
    testTwoElementReversalIsInverted();
    testFreezeRejectsWithoutMutation();
    std::cout << "UIRepo RenderHookBroker regressions passed\n";
    return 0;
}
