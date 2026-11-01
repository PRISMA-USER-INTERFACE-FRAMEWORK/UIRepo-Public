#include "uirepo/RenderHookBroker.h"

#include <algorithm>

namespace uirepo::render {
namespace {

template <class Registration>
std::vector<const Registration*> sortByPriority(
    const std::vector<Registration>& registrations,
    int Registration::* priorityMember,
    HookTarget target,
    bool filterByTarget) {
    std::vector<const Registration*> matches;
    for (const Registration& registration : registrations) {
        if (!filterByTarget || registration.target == target) {
            matches.push_back(&registration);
        }
    }
    std::sort(matches.begin(), matches.end(), [priorityMember](const Registration* left, const Registration* right) {
        if (left->*priorityMember != right->*priorityMember) {
            return left->*priorityMember < right->*priorityMember;
        }
        return left->sequence < right->sequence;
    });
    return matches;
}

} // namespace

TargetClaimStatus RenderHookBroker::claimTarget(HookTarget target, std::string owner, int wrapPriority) {
    if (frozen_) {
        return TargetClaimStatus::RejectedFrozen;
    }
    for (const TargetClaim& claim : targetClaims_) {
        if (claim.target == target && claim.owner == owner) {
            return TargetClaimStatus::RejectedDuplicateOwner;
        }
    }
    targetClaims_.push_back(TargetClaim{
        .target = target,
        .owner = std::move(owner),
        .wrapPriority = wrapPriority,
        .sequence = nextSequence_,
    });
    ++nextSequence_;
    return TargetClaimStatus::Ok;
}

PhaseRegisterStatus RenderHookBroker::registerPhase(
    RenderPhase phase,
    std::string owner,
    int priority,
    HookTarget target) {
    if (frozen_) {
        return PhaseRegisterStatus::RejectedFrozen;
    }
    for (const PhaseRegistration& registration : phaseRegistrations_) {
        if (registration.phase == phase && registration.target == target &&
            registration.owner == owner) {
            return PhaseRegisterStatus::RejectedDuplicate;
        }
    }
    phaseRegistrations_.push_back(PhaseRegistration{
        .phase = phase,
        .owner = std::move(owner),
        .priority = priority,
        .target = target,
        .sequence = nextSequence_,
    });
    ++nextSequence_;
    return PhaseRegisterStatus::Ok;
}

void RenderHookBroker::freeze() noexcept {
    frozen_ = true;
}

std::vector<std::string> RenderHookBroker::orderedOwnersForTarget(HookTarget target) const {
    const auto matches = sortByPriority(targetClaims_, &TargetClaim::wrapPriority, target, true);
    std::vector<std::string> owners;
    owners.reserve(matches.size());
    for (const TargetClaim* claim : matches) {
        owners.push_back(claim->owner);
    }
    return owners;
}

std::optional<std::string> RenderHookBroker::primaryOwner(HookTarget target) const {
    const auto matches = sortByPriority(targetClaims_, &TargetClaim::wrapPriority, target, true);
    if (matches.empty()) {
        return std::nullopt;
    }
    return matches.front()->owner;
}

std::vector<std::string> RenderHookBroker::orderedOwners(RenderPhase phase) const {
    std::vector<const PhaseRegistration*> matches;
    for (const PhaseRegistration& registration : phaseRegistrations_) {
        if (registration.phase == phase) {
            matches.push_back(&registration);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const PhaseRegistration* left, const PhaseRegistration* right) {
        if (left->priority != right->priority) {
            return left->priority < right->priority;
        }
        return left->sequence < right->sequence;
    });
    std::vector<std::string> owners;
    owners.reserve(matches.size());
    for (const PhaseRegistration* registration : matches) {
        owners.push_back(registration->owner);
    }
    return owners;
}

WrapOrderStatus RenderHookBroker::verifyWrapOrder(
    HookTarget target,
    const std::vector<std::string>& appliedOutermostFirst) const {
    const std::vector<std::string> expected = orderedOwnersForTarget(target);
    if (expected.empty()) {
        return WrapOrderStatus::UnknownTarget;
    }
    if (expected.size() != appliedOutermostFirst.size()) {
        return WrapOrderStatus::CountMismatch;
    }
    if (expected == appliedOutermostFirst) {
        return WrapOrderStatus::Ok;
    }
    if (std::equal(expected.rbegin(), expected.rend(), appliedOutermostFirst.begin())) {
        return WrapOrderStatus::Inverted;
    }
    return WrapOrderStatus::Mismatch;
}

} // namespace uirepo::render
