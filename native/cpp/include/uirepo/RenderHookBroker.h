#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uirepo::render {

enum class RenderPhase {
    UIBeforeComposite,
    UIComposite,
    UIAfterComposite,
    ResizeBegin,
    ResizeComplete,
};

enum class TargetClaimStatus {
    Ok,
    RejectedFrozen,
    RejectedDuplicateOwner,
};

enum class PhaseRegisterStatus {
    Ok,
    RejectedFrozen,
    RejectedDuplicate,
};

enum class WrapOrderStatus {
    Ok,
    UnknownTarget,
    CountMismatch,
    Inverted,
    Mismatch,
};

using HookTarget = std::uint64_t;

class RenderHookBroker final {
public:
    struct TargetClaim {
        HookTarget target{};
        std::string owner;
        int wrapPriority{};
        std::uint64_t sequence{};
    };

    struct PhaseRegistration {
        RenderPhase phase{};
        std::string owner;
        int priority{};
        HookTarget target{};
        std::uint64_t sequence{};
    };

    // Physical hook-site ownership / wrap chain. Allocates, so not noexcept: a resource failure
    // throws and is handled at the eventual (not-yet-built) adapter boundary, never silently mapped
    // to a "frozen" status.
    TargetClaimStatus claimTarget(HookTarget target, std::string owner, int wrapPriority);

    // Render-phase participation, independent of physical wrap ordering.
    PhaseRegisterStatus registerPhase(
        RenderPhase phase,
        std::string owner,
        int priority,
        HookTarget target = 0);

    void freeze() noexcept;
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }

    [[nodiscard]] std::vector<std::string> orderedOwnersForTarget(HookTarget target) const;
    [[nodiscard]] std::optional<std::string> primaryOwner(HookTarget target) const;
    [[nodiscard]] std::vector<std::string> orderedOwners(RenderPhase phase) const;

    [[nodiscard]] WrapOrderStatus verifyWrapOrder(
        HookTarget target,
        const std::vector<std::string>& appliedOutermostFirst) const;

    [[nodiscard]] std::size_t targetClaimCount() const noexcept { return targetClaims_.size(); }
    [[nodiscard]] std::size_t phaseRegistrationCount() const noexcept { return phaseRegistrations_.size(); }

private:
    std::vector<TargetClaim> targetClaims_;
    std::vector<PhaseRegistration> phaseRegistrations_;
    bool frozen_{};
    std::uint64_t nextSequence_{};
};

} // namespace uirepo::render
