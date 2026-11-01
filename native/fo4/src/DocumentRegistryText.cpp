#include "DocumentRegistry.h"

namespace uirepo::fo4 {

bool DocumentRegistry::routeTextEvent(const input::TextEvent& event) noexcept {
    CallbackSnapshot valueChanged{};
    bool handled = false;
    {
        std::scoped_lock lock(mutex_);
        auto* entry = findDocument(focusedDocument_);
        Node* focused = entry ? findNode(*entry, focusedNode_) : nullptr;
        auto* input = focused ? dynamic_cast<TextInputNode*>(focused) : nullptr;
        if (!entry || !inputEligible(*entry) || !input || !nodeInteractive(input)) {
            return false;
        }

        handled = true;
        bool changed = false;
        if (event.codepoint == U'\b') {
            changed = input->backspace();
        } else {
            changed = input->appendCodepoint(event.codepoint);
        }
        if (changed) {
            valueChanged = snapshotHandler(
                focusedDocument_, *entry, focusedNode_, UIREPO_EVENT_VALUE_CHANGED);
        }
    }

    invoke(valueChanged);
    return handled;
}

} // namespace uirepo::fo4
