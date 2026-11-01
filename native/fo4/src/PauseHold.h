#pragma once

#include <RE/Fallout.h>

#include <string_view>

namespace uirepo::fo4 {

// Invisible Fallout menu whose only job is to own kPausesGame. It deliberately does not own cursor
// or modal input; those belong to the future UIRepo focus lease/input layer.
class PauseHold final : public RE::IMenu {
public:
    static constexpr std::string_view kMenuName = "UIRepo_PauseHold";

    static RE::IMenu* creator(const RE::UIMessage& message);
    static void setPaused(bool paused) noexcept;
    [[nodiscard]] static bool isOpen() noexcept;
    [[nodiscard]] static bool isRequested() noexcept;
    static void tick() noexcept;

    void AdvanceMovie(float interval, std::uint64_t currentTime) override;
    RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override;

private:
    PauseHold();
    [[nodiscard]] bool valid() const noexcept { return uiMovie != nullptr; }
};

} // namespace uirepo::fo4
