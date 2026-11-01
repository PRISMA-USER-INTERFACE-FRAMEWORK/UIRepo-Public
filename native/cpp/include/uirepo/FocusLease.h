#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uirepo::input {

using FocusDocumentHandle = std::uint64_t;

struct FocusRequirements {
    bool inputMask{true};
    bool cursor{true};
    bool escape{};
    bool pause{};

    friend constexpr bool operator==(const FocusRequirements&, const FocusRequirements&) noexcept = default;
};

enum class FocusLeaseAction : std::uint8_t {
    AcquireInputMask,
    ReleaseInputMask,
    AcquireCursor,
    ReleaseCursor,
    AcquireEscape,
    ReleaseEscape,
    RequestPause,
    ReleasePause,
};

struct FocusLeaseTransition {
    FocusDocumentHandle previous{};
    FocusDocumentHandle current{};
    std::uint64_t episodeGeneration{};
    bool sessionStarted{};
    bool sessionEnded{};
    std::array<FocusLeaseAction, 8> actions{};
    std::size_t actionCount{};

    [[nodiscard]] constexpr bool changed() const noexcept {
        return previous != current || actionCount != 0;
    }

    [[nodiscard]] constexpr bool has(FocusLeaseAction action) const noexcept {
        for (std::size_t index = 0; index < actionCount; ++index) {
            if (actions[index] == action) {
                return true;
            }
        }
        return false;
    }
};

enum class FocusRecoveryDecision : std::uint8_t {
    None,
    Release,
};

class FocusLeaseState final {
public:
    static constexpr std::uint32_t kDefaultRecoveryGraceFrames = 120;

    [[nodiscard]] FocusDocumentHandle focusedDocument() const noexcept { return focusedDocument_; }
    [[nodiscard]] FocusRequirements requirements() const noexcept { return requirements_; }
    [[nodiscard]] std::uint64_t episodeGeneration() const noexcept { return episodeGeneration_; }
    [[nodiscard]] std::uint32_t stalePresentedFrames() const noexcept { return stalePresentedFrames_; }
    [[nodiscard]] bool active() const noexcept { return focusedDocument_ != 0; }

    [[nodiscard]] FocusLeaseTransition focus(
        FocusDocumentHandle document,
        FocusRequirements requirements = {}) noexcept {
        if (document == 0) {
            return release();
        }

        FocusLeaseTransition transition{
            .previous = focusedDocument_,
            .current = document,
            .episodeGeneration = episodeGeneration_,
        };

        if (focusedDocument_ == document) {
            appendRequirementDiff(requirements_, requirements, transition);
            requirements_ = requirements;
            return transition;
        }

        transition.sessionStarted = focusedDocument_ == 0;
        appendRequirementDiff(requirementsForCurrent(), requirements, transition);
        focusedDocument_ = document;
        requirements_ = requirements;
        stalePresentedFrames_ = 0;
        ++episodeGeneration_;
        transition.episodeGeneration = episodeGeneration_;
        return transition;
    }

    [[nodiscard]] FocusLeaseTransition release(FocusDocumentHandle document = 0) noexcept {
        FocusLeaseTransition transition{
            .previous = focusedDocument_,
            .current = focusedDocument_,
            .episodeGeneration = episodeGeneration_,
        };

        if (focusedDocument_ == 0 || (document != 0 && document != focusedDocument_)) {
            return transition;
        }

        transition.current = 0;
        transition.sessionEnded = true;
        appendRequirementDiff(requirements_, FocusRequirements{false, false, false, false}, transition);
        focusedDocument_ = 0;
        requirements_ = {};
        stalePresentedFrames_ = 0;
        ++episodeGeneration_;
        transition.episodeGeneration = episodeGeneration_;
        return transition;
    }

    [[nodiscard]] FocusLeaseTransition onDocumentUnavailable(FocusDocumentHandle document) noexcept {
        return release(document);
    }

    void markPresented() noexcept {
        if (focusedDocument_ != 0) {
            stalePresentedFrames_ = 0;
        }
    }

    [[nodiscard]] FocusRecoveryDecision advancePresentationFrame(
        bool presentationExpected,
        bool framePresented,
        bool suspended = false,
        std::uint32_t graceFrames = kDefaultRecoveryGraceFrames) noexcept {
        if (focusedDocument_ == 0 || suspended || !presentationExpected) {
            return FocusRecoveryDecision::None;
        }
        if (framePresented) {
            stalePresentedFrames_ = 0;
            return FocusRecoveryDecision::None;
        }
        if (graceFrames == 0) {
            return FocusRecoveryDecision::Release;
        }
        if (stalePresentedFrames_ < UINT32_MAX) {
            ++stalePresentedFrames_;
        }
        return stalePresentedFrames_ >= graceFrames
            ? FocusRecoveryDecision::Release
            : FocusRecoveryDecision::None;
    }

private:
    [[nodiscard]] FocusRequirements requirementsForCurrent() const noexcept {
        return focusedDocument_ == 0
            ? FocusRequirements{false, false, false, false}
            : requirements_;
    }

    static void append(
        FocusLeaseTransition& transition,
        FocusLeaseAction action) noexcept {
        if (transition.actionCount < transition.actions.size()) {
            transition.actions[transition.actionCount++] = action;
        }
    }

    static void appendRequirementDiff(
        FocusRequirements before,
        FocusRequirements after,
        FocusLeaseTransition& transition) noexcept {
        if (before.pause && !after.pause) {
            append(transition, FocusLeaseAction::ReleasePause);
        }
        if (before.escape && !after.escape) {
            append(transition, FocusLeaseAction::ReleaseEscape);
        }
        if (before.cursor && !after.cursor) {
            append(transition, FocusLeaseAction::ReleaseCursor);
        }
        if (before.inputMask && !after.inputMask) {
            append(transition, FocusLeaseAction::ReleaseInputMask);
        }

        if (!before.inputMask && after.inputMask) {
            append(transition, FocusLeaseAction::AcquireInputMask);
        }
        if (!before.cursor && after.cursor) {
            append(transition, FocusLeaseAction::AcquireCursor);
        }
        if (!before.escape && after.escape) {
            append(transition, FocusLeaseAction::AcquireEscape);
        }
        if (!before.pause && after.pause) {
            append(transition, FocusLeaseAction::RequestPause);
        }
    }

    FocusDocumentHandle focusedDocument_{};
    FocusRequirements requirements_{};
    std::uint64_t episodeGeneration_{};
    std::uint32_t stalePresentedFrames_{};
};

} // namespace uirepo::input
