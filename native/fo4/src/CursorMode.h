#pragma once

#include <RE/Fallout.h>

#include <string_view>

namespace uirepo::fo4 {

class CursorMode final : public RE::IMenu {
public:
    static constexpr std::string_view kMenuName = "UIRepo_CursorMode";

    static RE::IMenu* creator(const RE::UIMessage& message);
    static void setEnabled(bool enabled) noexcept;
    [[nodiscard]] static bool isOpen() noexcept;
    [[nodiscard]] static bool isRequested() noexcept;
    static void tick() noexcept;

    void AdvanceMovie(float interval, std::uint64_t currentTime) override;
    RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override;

private:
    CursorMode();
    [[nodiscard]] bool valid() const noexcept { return uiMovie != nullptr; }
};

} // namespace uirepo::fo4
