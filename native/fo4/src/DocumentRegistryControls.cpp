#include "DocumentRegistry.h"

namespace uirepo::fo4 {

DocumentRegistry::NodeHandle DocumentRegistry::createToggle(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    bool value) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }

    auto& node = parentNode->emplaceChild<ToggleNode>(std::move(id), value);
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createSlider(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    float value,
    float minimum,
    float maximum,
    float step) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }

    auto& node = parentNode->emplaceChild<SliderNode>(
        std::move(id), value, minimum, maximum, step);
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createStepper(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    std::vector<std::string> options,
    std::size_t index) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id)) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }

    auto& node = parentNode->emplaceChild<StepperNode>(
        std::move(id), std::move(options), index);
    return registerNode(*entry, node);
}

DocumentRegistry::NodeHandle DocumentRegistry::createTextInput(
    DocumentHandle document,
    NodeHandle parent,
    std::string id,
    std::string value,
    std::size_t maxBytes) {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    if (!entry || !idAvailable(*entry, id) || maxBytes == 0) {
        return 0;
    }
    auto* parentNode = findNode(*entry, parent);
    if (!parentNode) {
        return 0;
    }

    auto& node = parentNode->emplaceChild<TextInputNode>(
        std::move(id), std::move(value), maxBytes);
    return registerNode(*entry, node);
}

bool DocumentRegistry::setToggleValue(
    DocumentHandle document,
    NodeHandle node,
    bool value) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    auto* toggle = target ? dynamic_cast<ToggleNode*>(target) : nullptr;
    if (!toggle) {
        return false;
    }
    (void)toggle->setValue(value);
    return true;
}

bool DocumentRegistry::getToggleValue(
    DocumentHandle document,
    NodeHandle node,
    bool& value) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    const auto* target = entry ? findNode(*entry, node) : nullptr;
    const auto* toggle = target ? dynamic_cast<const ToggleNode*>(target) : nullptr;
    if (!toggle) {
        return false;
    }
    value = toggle->value();
    return true;
}

bool DocumentRegistry::setSliderValue(
    DocumentHandle document,
    NodeHandle node,
    float value) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    auto* slider = target ? dynamic_cast<SliderNode*>(target) : nullptr;
    if (!slider || !std::isfinite(value)) {
        return false;
    }
    (void)slider->setValue(value);
    return true;
}

bool DocumentRegistry::getSliderValue(
    DocumentHandle document,
    NodeHandle node,
    float& value) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    const auto* target = entry ? findNode(*entry, node) : nullptr;
    const auto* slider = target ? dynamic_cast<const SliderNode*>(target) : nullptr;
    if (!slider) {
        return false;
    }
    value = slider->value();
    return true;
}

bool DocumentRegistry::setStepperIndex(
    DocumentHandle document,
    NodeHandle node,
    std::size_t index) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    auto* stepper = target ? dynamic_cast<StepperNode*>(target) : nullptr;
    if (!stepper) {
        return false;
    }
    return stepper->setIndex(index);
}

bool DocumentRegistry::getStepperIndex(
    DocumentHandle document,
    NodeHandle node,
    std::size_t& index) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    const auto* target = entry ? findNode(*entry, node) : nullptr;
    const auto* stepper = target ? dynamic_cast<const StepperNode*>(target) : nullptr;
    if (!stepper) {
        return false;
    }
    index = stepper->index();
    return true;
}

bool DocumentRegistry::setTextInputValue(
    DocumentHandle document,
    NodeHandle node,
    std::string value) noexcept {
    std::scoped_lock lock(mutex_);
    auto* entry = findDocument(document);
    auto* target = entry ? findNode(*entry, node) : nullptr;
    auto* input = target ? dynamic_cast<TextInputNode*>(target) : nullptr;
    if (!input || value.size() > input->maxBytes()) {
        return false;
    }
    if (value == input->value()) {
        return true;
    }
    return input->setValue(std::move(value));
}

bool DocumentRegistry::getTextInputValue(
    DocumentHandle document,
    NodeHandle node,
    std::string& value) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto* entry = findDocument(document);
    const auto* target = entry ? findNode(*entry, node) : nullptr;
    const auto* input = target ? dynamic_cast<const TextInputNode*>(target) : nullptr;
    if (!input) {
        return false;
    }
    value = input->value();
    return true;
}

bool DocumentRegistry::focusedTextInputCaret(Rect& caret) const noexcept {
    std::scoped_lock lock(mutex_);
    if (focusedDocument_ == 0 || focusedNode_ == 0) {
        return false;
    }

    const auto* entry = findDocument(focusedDocument_);
    if (!entry || !entry->document || !inputEligible(*entry)) {
        return false;
    }
    switch (entry->health) {
    case DocumentHealth::Creating:
    case DocumentHealth::Faulted:
    case DocumentHealth::Destroying:
    case DocumentHealth::Destroyed:
        return false;
    case DocumentHealth::Ready:
    case DocumentHealth::Live:
    case DocumentHealth::Hidden:
    default:
        break;
    }

    const auto* target = findNode(*entry, focusedNode_);
    const auto* input = target ? dynamic_cast<const TextInputNode*>(target) : nullptr;
    if (!input || !nodeInteractive(input) || entry->document->focused() != input) {
        return false;
    }

    const Rect next = input->endCaretRect(entry->document->textMeasurer());
    if (next.empty() || !std::isfinite(next.x) || !std::isfinite(next.y) ||
        !std::isfinite(next.width) || !std::isfinite(next.height)) {
        return false;
    }

    caret = next;
    return true;
}

} // namespace uirepo::fo4
