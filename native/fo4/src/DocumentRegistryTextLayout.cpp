#include "DocumentRegistry.h"

namespace uirepo::fo4 {

bool DocumentRegistry::setTextLayout(
    DocumentHandle document,
    NodeHandle node,
    TextLayoutOptions layout) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    if (!entry || !target || entry->health == DocumentHealth::Faulted ||
        entry->health == DocumentHealth::Destroying) {
        return false;
    }

    try {
        if (auto* text = dynamic_cast<TextNode*>(target)) {
            text->setTextLayout(layout);
            return true;
        }
        if (auto* button = dynamic_cast<ButtonNode*>(target)) {
            button->setTextLayout(layout);
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

} // namespace uirepo::fo4
