#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace uirepo::input {

using DocumentHandle = std::uint64_t;

enum class CaptureOwner : std::uint8_t {
    None,
    PassiveOverlay,
    FocusedDocument,
};

enum class CursorVisibilityAction : std::uint8_t {
    None,
    HideVanilla,
    RestoreVanilla,
};

[[nodiscard]] constexpr CaptureOwner afterFocusedDocumentRelease(bool passiveOverlayVisible) noexcept {
    return passiveOverlayVisible ? CaptureOwner::PassiveOverlay : CaptureOwner::None;
}

[[nodiscard]] constexpr bool shouldRoutePointer(
    CaptureOwner owner,
    bool focusedDocumentAcceptsPointer) noexcept {
    return owner == CaptureOwner::PassiveOverlay ||
           (owner == CaptureOwner::FocusedDocument && focusedDocumentAcceptsPointer);
}

[[nodiscard]] constexpr bool ownsCursor(
    CaptureOwner owner,
    bool passiveOverlayInteractive = false) noexcept {
    return owner == CaptureOwner::FocusedDocument ||
           (owner == CaptureOwner::PassiveOverlay && passiveOverlayInteractive);
}

[[nodiscard]] constexpr CursorVisibilityAction nextCursorVisibilityAction(
    CaptureOwner owner,
    bool vanillaCursorHiddenByUIRepo,
    bool passiveOverlayInteractive = false) noexcept {
    if (ownsCursor(owner, passiveOverlayInteractive)) {
        return vanillaCursorHiddenByUIRepo
            ? CursorVisibilityAction::None
            : CursorVisibilityAction::HideVanilla;
    }
    return vanillaCursorHiddenByUIRepo
        ? CursorVisibilityAction::RestoreVanilla
        : CursorVisibilityAction::None;
}

enum class CaptureMode : std::uint8_t {
    Full,
    Regions,
};

struct InputRegion {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] constexpr bool contains(std::int32_t px, std::int32_t py) const noexcept {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const std::int64_t right = static_cast<std::int64_t>(x) + width;
        const std::int64_t bottom = static_cast<std::int64_t>(y) + height;
        return px >= x && py >= y && px < right && py < bottom;
    }
};

inline constexpr std::size_t kMaxInputRegions = 256;

class RegionState final {
public:
    [[nodiscard]] DocumentHandle focusedDocument() const noexcept { return focusedDocument_; }
    [[nodiscard]] CaptureMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool cursorOwned() const noexcept { return cursorOwned_; }
    [[nodiscard]] std::span<const InputRegion> regions() const noexcept { return regions_; }

    void setFocusedDocument(DocumentHandle document) {
        if (focusedDocument_ != document) {
            regions_.clear();
            mode_ = CaptureMode::Full;
        }
        focusedDocument_ = document;
        cursorOwned_ = document != 0;
    }

    void clearFocusedDocument(DocumentHandle document = 0) {
        if (document != 0 && focusedDocument_ != document) {
            return;
        }
        reset();
    }

    void onDocumentDestroyed(DocumentHandle document) {
        if (document != 0 && focusedDocument_ == document) {
            reset();
        }
    }

    void setMode(CaptureMode mode) noexcept { mode_ = mode; }

    void setRegions(DocumentHandle document, std::span<const InputRegion> regions) {
        if (document == 0 || focusedDocument_ != document) {
            return;
        }
        const std::size_t count = std::min(regions.size(), kMaxInputRegions);
        regions_.assign(regions.begin(), regions.begin() + static_cast<std::ptrdiff_t>(count));
    }

    void clearRegions() noexcept { regions_.clear(); }

    [[nodiscard]] bool shouldRoute(std::int32_t x, std::int32_t y) const noexcept {
        if (focusedDocument_ == 0) {
            return false;
        }
        if (mode_ == CaptureMode::Full) {
            return true;
        }
        return std::ranges::any_of(regions_, [x, y](const InputRegion& region) {
            return region.contains(x, y);
        });
    }

private:
    void reset() noexcept {
        focusedDocument_ = 0;
        mode_ = CaptureMode::Full;
        cursorOwned_ = false;
        regions_.clear();
    }

    DocumentHandle focusedDocument_{};
    CaptureMode mode_{CaptureMode::Full};
    bool cursorOwned_{};
    std::vector<InputRegion> regions_;
};

enum class NavigationCommand : std::uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Next,
    Previous,
    Accept,
    Cancel,
};

struct ControllerRouteDecision {
    NavigationCommand command{NavigationCommand::None};
    bool routeToDocument{};
    bool consumeGameEvent{};
};

inline constexpr std::uint32_t kGamepadTag = 0x10000u;

[[nodiscard]] constexpr NavigationCommand navigationForButton(std::uint32_t bsButtonCode) noexcept {
    switch (bsButtonCode | kGamepadTag) {
    case 0x10001u:
        return NavigationCommand::Up;
    case 0x10002u:
        return NavigationCommand::Down;
    case 0x10004u:
        return NavigationCommand::Left;
    case 0x10008u:
        return NavigationCommand::Right;
    case 0x11000u:
        return NavigationCommand::Accept;
    case 0x12000u:
        return NavigationCommand::Cancel;
    default:
        return NavigationCommand::None;
    }
}

[[nodiscard]] constexpr ControllerRouteDecision routeController(
    bool isGamepad,
    bool documentFocused,
    std::uint32_t bsButtonCode) noexcept {
    if (!isGamepad || !documentFocused) {
        return {};
    }

    const NavigationCommand command = navigationForButton(bsButtonCode);
    if (command == NavigationCommand::None) {
        return {};
    }

    return {
        .command = command,
        .routeToDocument = true,
        .consumeGameEvent = false,
    };
}

} // namespace uirepo::input
