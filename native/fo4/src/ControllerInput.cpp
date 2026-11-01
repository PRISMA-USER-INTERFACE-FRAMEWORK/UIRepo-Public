#include "ControllerInput.h"

#include "ActiveDeviceState.h"
#include "DocumentRegistry.h"

#include "uirepo/InputPolicy.h"

#include <RE/Fallout.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

namespace uirepo::fo4::ControllerInput {
namespace {

class ControllerHandler final : public RE::PlayerInputHandler {
public:
    explicit ControllerHandler(RE::PlayerControlsData& data) noexcept :
        RE::PlayerInputHandler(data) {}

    bool ShouldHandleEvent(const RE::InputEvent* event) override {
        if (event == nullptr || event->device.get() != RE::INPUT_DEVICE::kGamepad ||
            event->handled.get() == RE::InputEvent::HANDLED_RESULT::kStop) {
            return false;
        }
        return event->As<RE::ButtonEvent>() != nullptr;
    }

    void OnButtonEvent(const RE::ButtonEvent* event) override {
        if (event == nullptr || event->device.get() != RE::INPUT_DEVICE::kGamepad) {
            return;
        }

        if (event->QJustPressed()) {
            ActiveDeviceState::noteControllerButton(true);
        }

        const auto code = static_cast<std::uint32_t>(event->GetBSButtonCode());
        const auto command = input::navigationForButton(code);
        if (command == input::NavigationCommand::None) {
            return;
        }

        if (event->QJustPressed()) {
            ownedButtons_.erase(code);
            if (DocumentRegistry::instance().routeNavigation(command)) {
                ownedButtons_.insert(code);
                stopEvent(event);
            }
            return;
        }

        const auto owned = ownedButtons_.find(code);
        if (owned == ownedButtons_.end()) {
            return;
        }

        stopEvent(event);
        if (!event->QPressed()) {
            ownedButtons_.erase(owned);
        }
    }

private:
    static void stopEvent(const RE::ButtonEvent* event) noexcept {
        auto* mutableEvent = const_cast<RE::ButtonEvent*>(event);
        mutableEvent->handled = RE::InputEvent::HANDLED_RESULT::kStop;
    }

    std::unordered_set<std::uint32_t> ownedButtons_;
};

std::unique_ptr<ControllerHandler> g_handler;

} // namespace

bool install() noexcept {
    if (g_handler) {
        return true;
    }

    auto* controls = RE::PlayerControls::GetSingleton();
    if (controls == nullptr) {
        spdlog::warn("UIRepo_F4 input: PlayerControls singleton unavailable");
        return false;
    }

    try {
        auto handler = std::make_unique<ControllerHandler>(controls->data);
        controls->RegisterHandler(handler.get());
        g_handler = std::move(handler);
        spdlog::info("UIRepo_F4 input: registered controller navigation handler");
        return true;
    } catch (...) {
        spdlog::error("UIRepo_F4 input: failed to register controller navigation handler");
        return false;
    }
}

} // namespace uirepo::fo4::ControllerInput
