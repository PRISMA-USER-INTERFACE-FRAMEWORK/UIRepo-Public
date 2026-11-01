#include "DocumentRegistry.h"

namespace uirepo::fo4 {

bool DocumentRegistry::routeHorizontalWheelEvent(Point point, float wheelDelta) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        auto* entry = topmostHitDocument(point);
        if (!entry || !entry->document) {
            return false;
        }
        return input::routeHorizontalWheel(*entry->document, point, wheelDelta);
    } catch (...) {
        return false;
    }
}

} // namespace uirepo::fo4
