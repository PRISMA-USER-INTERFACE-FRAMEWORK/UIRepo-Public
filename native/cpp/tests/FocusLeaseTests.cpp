#include "uirepo/FocusLease.h"

#include <cstdlib>
#include <iostream>

namespace {

using uirepo::input::FocusLeaseAction;
using uirepo::input::FocusLeaseState;
using uirepo::input::FocusRecoveryDecision;
using uirepo::input::FocusRequirements;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testFirstFocusAcquiresSessionResourcesOnce() {
    FocusLeaseState state;
    const auto first = state.focus(10, FocusRequirements{
        .inputMask = true,
        .cursor = true,
        .escape = false,
        .pause = false,
    });

    require(first.sessionStarted && !first.sessionEnded, "first focus did not start one focus session");
    require(first.previous == 0 && first.current == 10, "first focus transition handles are wrong");
    require(first.has(FocusLeaseAction::AcquireInputMask), "first focus did not request the input mask");
    require(first.has(FocusLeaseAction::AcquireCursor), "first focus did not request cursor ownership");
    require(!first.has(FocusLeaseAction::AcquireEscape), "first focus acquired Escape without policy");
    require(!first.has(FocusLeaseAction::RequestPause), "first focus requested pause without policy");

    const auto duplicate = state.focus(10, state.requirements());
    require(!duplicate.changed(), "same-document focus reacquired existing session resources");
    require(state.episodeGeneration() == first.episodeGeneration, "same-document focus started a new recovery episode");
}

void testPolicyUpdateDoesNotRestartSession() {
    FocusLeaseState state;
    const auto initial = state.focus(10);
    const auto generation = initial.episodeGeneration;

    const auto updated = state.focus(10, FocusRequirements{
        .inputMask = true,
        .cursor = true,
        .escape = true,
        .pause = true,
    });

    require(!updated.sessionStarted && !updated.sessionEnded, "policy update restarted the focus session");
    require(updated.episodeGeneration == generation, "same-owner policy update reset recovery generation");
    require(updated.actionCount == 2, "policy update changed unrelated resources");
    require(updated.has(FocusLeaseAction::AcquireEscape), "policy update did not acquire Escape");
    require(updated.has(FocusLeaseAction::RequestPause), "policy update did not request pause");
    require(!updated.has(FocusLeaseAction::AcquireInputMask), "policy update double-acquired input mask");
    require(!updated.has(FocusLeaseAction::AcquireCursor), "policy update double-acquired cursor");
}

void testHandoffDiffsResourcesWithoutSessionTearDown() {
    FocusLeaseState state;
    const auto first = state.focus(10, FocusRequirements{
        .inputMask = true,
        .cursor = true,
        .escape = false,
        .pause = false,
    });

    (void)state.advancePresentationFrame(true, false, false, 5);
    require(state.stalePresentedFrames() == 1, "stale recovery clock did not advance for first focus owner");

    const auto handoff = state.focus(20, FocusRequirements{
        .inputMask = true,
        .cursor = true,
        .escape = true,
        .pause = true,
    });

    require(!handoff.sessionStarted && !handoff.sessionEnded, "A to B handoff tore down the whole session");
    require(handoff.previous == 10 && handoff.current == 20, "handoff owners are wrong");
    require(handoff.episodeGeneration > first.episodeGeneration, "handoff did not start a fresh recovery episode");
    require(state.stalePresentedFrames() == 0, "handoff inherited stale presentation time from prior owner");
    require(!handoff.has(FocusLeaseAction::AcquireInputMask), "handoff double-acquired input mask");
    require(!handoff.has(FocusLeaseAction::AcquireCursor), "handoff double-acquired cursor");
    require(handoff.has(FocusLeaseAction::AcquireEscape), "handoff did not apply B Escape policy");
    require(handoff.has(FocusLeaseAction::RequestPause), "handoff did not apply B pause policy");
}

void testFinalReleaseReleasesEntireLeaseOnce() {
    FocusLeaseState state;
    (void)state.focus(30, FocusRequirements{
        .inputMask = true,
        .cursor = true,
        .escape = true,
        .pause = true,
    });

    const auto unrelated = state.release(99);
    require(!unrelated.changed(), "unrelated document released the active lease");
    require(state.focusedDocument() == 30, "unrelated release changed focus owner");

    const auto released = state.release(30);
    require(released.sessionEnded && released.current == 0, "final release did not end the focus session");
    require(released.has(FocusLeaseAction::ReleasePause), "final release left pause ownership active");
    require(released.has(FocusLeaseAction::ReleaseEscape), "final release left Escape ownership active");
    require(released.has(FocusLeaseAction::ReleaseCursor), "final release left cursor ownership active");
    require(released.has(FocusLeaseAction::ReleaseInputMask), "final release left input mask active");
    require(!state.active(), "final release left focus lease active");

    const auto duplicate = state.release(30);
    require(!duplicate.changed(), "duplicate final release repeated resource teardown");
}

void testRecoveryUsesPresentedFramesAndSuspension() {
    FocusLeaseState state;
    const auto first = state.focus(40);

    require(
        state.advancePresentationFrame(true, false, true, 2) == FocusRecoveryDecision::None,
        "suspended presentation triggered recovery");
    require(state.stalePresentedFrames() == 0, "suspended presentation advanced stale clock");

    require(
        state.advancePresentationFrame(false, false, false, 2) == FocusRecoveryDecision::None,
        "non-presenting document triggered recovery");
    require(state.stalePresentedFrames() == 0, "non-presenting document advanced stale clock");

    require(
        state.advancePresentationFrame(true, false, false, 2) == FocusRecoveryDecision::None,
        "recovery fired before grace window");
    require(
        state.advancePresentationFrame(true, false, false, 2) == FocusRecoveryDecision::Release,
        "recovery did not fire at bounded grace window");

    state.markPresented();
    require(state.stalePresentedFrames() == 0, "presented frame did not reset stale clock");

    const auto handoff = state.focus(41);
    require(handoff.episodeGeneration > first.episodeGeneration, "handoff did not refresh recovery generation");
    require(state.stalePresentedFrames() == 0, "new focus episode inherited stale clock");
}

void testUnavailableDocumentReleasesOnlyItsOwnLease() {
    FocusLeaseState state;
    (void)state.focus(50);

    const auto unrelated = state.onDocumentUnavailable(51);
    require(!unrelated.changed(), "unrelated document fault released active focus owner");

    const auto active = state.onDocumentUnavailable(50);
    require(active.sessionEnded, "active document fault did not release lease");
    require(!state.active(), "active document fault left focus lease active");
}

} // namespace

int main() {
    testFirstFocusAcquiresSessionResourcesOnce();
    testPolicyUpdateDoesNotRestartSession();
    testHandoffDiffsResourcesWithoutSessionTearDown();
    testFinalReleaseReleasesEntireLeaseOnce();
    testRecoveryUsesPresentedFramesAndSuspension();
    testUnavailableDocumentReleasesOnlyItsOwnLease();
    std::cout << "UIRepo focus lease regressions passed\n";
    return 0;
}
