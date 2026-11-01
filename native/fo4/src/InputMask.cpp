#include "InputMask.h"

#include <RE/B/BSInputEnableLayer.h>
#include <RE/B/BSInputEnableManager.h>
#include <RE/O/OtherInputEvents.h>
#include <RE/U/UserEvents.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>

namespace uirepo::fo4::InputMask {
namespace {

using UserFlag = RE::UserEvents::USER_EVENT_FLAG;
using OtherFlag = RE::OtherInputEvents::OTHER_EVENT_FLAG;

RE::BSTSmartPointer<RE::BSInputEnableLayer> g_layer;
std::atomic<bool> g_applied{};

constexpr std::uint32_t kUserEventMask =
    static_cast<std::uint32_t>(UserFlag::kFighting) |
    static_cast<std::uint32_t>(UserFlag::kMainFour);

constexpr std::uint32_t kOtherEventMask =
    static_cast<std::uint32_t>(OtherFlag::kPOVChange) |
    static_cast<std::uint32_t>(OtherFlag::kActivation) |
    static_cast<std::uint32_t>(OtherFlag::kJournalTabs) |
    static_cast<std::uint32_t>(OtherFlag::kVATS) |
    static_cast<std::uint32_t>(OtherFlag::kFavorites);

void setMaskedFlags(bool enable) noexcept {
    auto* manager = RE::BSInputEnableManager::GetSingleton();
    if (manager == nullptr || !g_layer) {
        spdlog::warn(
            "UIRepo_F4 InputMask: SetMaskedFlags({}) unavailable (manager {}, layer {})",
            enable,
            manager != nullptr,
            g_layer != nullptr);
        return;
    }

    const std::uint32_t layerId = g_layer->layerID;
    const auto senderId = RE::UserEvents::SENDER_ID::kScript;
    const std::uint32_t userBefore = manager->cachedInputUserEventsFlags.underlying();
    const std::uint32_t otherBefore = manager->cachedOtherInputEventsFlags.underlying();

    const bool userOk = manager->EnableUserEvent(
        layerId,
        static_cast<UserFlag>(kUserEventMask),
        enable,
        senderId);
    const bool otherOk = manager->EnableOtherEvent(
        layerId,
        static_cast<OtherFlag>(kOtherEventMask),
        enable,
        senderId);

    const std::uint32_t userAfter = manager->cachedInputUserEventsFlags.underlying();
    const std::uint32_t otherAfter = manager->cachedOtherInputEventsFlags.underlying();
    const std::uint32_t userChanged = (userBefore ^ userAfter) & kUserEventMask;
    const std::uint32_t otherChanged = (otherBefore ^ otherAfter) & kOtherEventMask;

    spdlog::info(
        "UIRepo_F4 InputMask: enable={} layer={} user {:#x}->{:#x} ok={} other {:#x}->{:#x} ok={}",
        enable,
        layerId,
        userBefore,
        userAfter,
        userOk,
        otherBefore,
        otherAfter,
        otherOk);

    if (userChanged != kUserEventMask) {
        spdlog::warn(
            "UIRepo_F4 InputMask: requested user mask {:#x}, observed changed bits {:#x}",
            kUserEventMask,
            userChanged);
    }
    if (otherChanged != kOtherEventMask) {
        spdlog::warn(
            "UIRepo_F4 InputMask: requested other mask {:#x}, observed changed bits {:#x}",
            kOtherEventMask,
            otherChanged);
    }
}

} // namespace

void apply() noexcept {
    bool expected = false;
    if (!g_applied.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    auto* manager = RE::BSInputEnableManager::GetSingleton();
    if (manager == nullptr) {
        g_applied.store(false, std::memory_order_release);
        spdlog::warn("UIRepo_F4 InputMask: BSInputEnableManager unavailable");
        return;
    }

    if (!manager->AllocateNewLayer(g_layer, "UIRepo_F4") || !g_layer) {
        g_applied.store(false, std::memory_order_release);
        spdlog::warn("UIRepo_F4 InputMask: AllocateNewLayer failed");
        return;
    }

    setMaskedFlags(false);
    spdlog::info("UIRepo_F4 InputMask: applied layer {}", g_layer->layerID);
}

void release() noexcept {
    if (!g_applied.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    setMaskedFlags(true);
    const std::uint32_t layerId = g_layer ? g_layer->layerID : 0;
    g_layer.reset();
    spdlog::info("UIRepo_F4 InputMask: released layer {}", layerId);
}

bool isApplied() noexcept {
    return g_applied.load(std::memory_order_acquire);
}

} // namespace uirepo::fo4::InputMask
