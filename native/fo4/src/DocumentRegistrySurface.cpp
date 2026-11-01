#include "DocumentRegistry.h"

#include <cmath>

namespace uirepo::fo4 {

bool DocumentRegistry::snapshotDisplayList(
    DocumentHandle document,
    Size viewport,
    std::vector<DrawCommand>& commands) noexcept {
    commands.clear();
    if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
        viewport.width <= 0.0f || viewport.height <= 0.0f) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !entry->document ||
        entry->visible || entry->inputEnabled ||
        entry->health == DocumentHealth::Faulted ||
        entry->health == DocumentHealth::Destroying ||
        entry->health == DocumentHealth::Destroyed) {
        return false;
    }

    try {
        entry->document->layout(viewport);
        const auto display = entry->document->buildDisplayList();
        commands.assign(display.begin(), display.end());
        return true;
    } catch (...) {
        commands.clear();
        return false;
    }
}

} // namespace uirepo::fo4
