#include "DocumentRegistry.h"

#ifdef _WIN32
#include <spdlog/spdlog.h>
#endif

#include <exception>

namespace uirepo::fo4 {

bool DocumentRegistry::setDocumentKeyEventHandler(
    DocumentHandle document,
    UIRepoDocumentKeyEventCallback callback,
    void* userData) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || entry->health == DocumentHealth::Faulted ||
        entry->health == DocumentHealth::Destroying) {
        return false;
    }

    entry->keyHandler = KeyHandler{callback, callback != nullptr ? userData : nullptr};
    return true;
}

bool DocumentRegistry::hasDocumentKeyEventHandler(DocumentHandle document) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    return entry != nullptr && entry->health != DocumentHealth::Faulted &&
           entry->health != DocumentHealth::Destroying && entry->keyHandler.callback != nullptr;
}

DocumentRegistry::KeyCallbackSnapshot DocumentRegistry::snapshotKeyHandler(
    DocumentHandle document,
    const DocumentEntry& entry,
    const input::KeyEvent& source) const noexcept {
    if (entry.keyHandler.callback == nullptr) {
        return {};
    }

    return {
        .callback = entry.keyHandler.callback,
        .userData = entry.keyHandler.userData,
        .document = document,
        .event = UIRepoKeyEventV1{
            .size = sizeof(UIRepoKeyEventV1),
            .type = static_cast<std::uint32_t>(
                source.type == input::KeyEventType::Down ? UIREPO_KEY_DOWN : UIREPO_KEY_UP),
            .keyCode = source.keyCode,
            .modifiers = source.modifiers,
            .repeat = static_cast<std::uint8_t>(source.repeat ? 1u : 0u),
            .reserved = {},
        },
    };
}

bool DocumentRegistry::invoke(KeyCallbackSnapshot snapshot) noexcept {
    if (!snapshot) {
        return false;
    }

    try {
        return snapshot.callback(snapshot.document, &snapshot.event, snapshot.userData) != 0;
#ifdef _WIN32
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 SDK: document key callback threw: {}", exception.what());
#else
    } catch (const std::exception&) {
#endif
    } catch (...) {
#ifdef _WIN32
        spdlog::error("UIRepo_F4 SDK: document key callback threw an unknown exception");
#endif
    }
    return false;
}

bool DocumentRegistry::routeDocumentKeyEvent(const input::KeyEvent& event) noexcept {
    KeyCallbackSnapshot callback{};
    {
        std::scoped_lock lock(mutex_);
        const DocumentHandle document = focusedDocument_;
        const auto* entry = findDocument(document);
        if (document == 0 || entry == nullptr || !inputEligible(*entry) || focusedNode_ == 0) {
            return false;
        }
        callback = snapshotKeyHandler(document, *entry, event);
    }

    return invoke(callback);
}

} // namespace uirepo::fo4
