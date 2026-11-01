#pragma once

#include <cstdint>

namespace uirepo::registry {

using DocumentHandle = std::uint64_t;
using SurfaceHandle = std::uint64_t;
using OwnerHandle = std::uint64_t;

enum class DocumentRole : std::uint8_t {
    HUD,
    Panel,
    Overlay,
    WorldSurface,
    Diagnostic,
};

enum class DocumentHealth : std::uint8_t {
    Creating,
    Ready,
    Live,
    Hidden,
    Faulted,
    Destroying,
    Destroyed,
};

struct DocumentState {
    DocumentHandle handle{};
    OwnerHandle owner{};
    DocumentRole role{DocumentRole::Panel};
    DocumentHealth health{DocumentHealth::Creating};
    bool visible{};
    bool inputEnabled{};
    bool focused{};
    std::int32_t order{};
    std::uint64_t generation{};
    std::uint32_t surfaceCount{};
};

} // namespace uirepo::registry
