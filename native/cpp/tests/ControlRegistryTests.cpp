#include "DocumentRegistry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using uirepo::Document;
using uirepo::Length;
using uirepo::PanelNode;
using uirepo::Rect;
using uirepo::Size;
using uirepo::Style;
using uirepo::TextAlignment;
using uirepo::TextInputNode;
using uirepo::TextLayoutOptions;
using uirepo::TextMeasurer;
using uirepo::TextOverflow;
using uirepo::TextWrapping;
using uirepo::Thickness;
using uirepo::fo4::DocumentRegistry;
using uirepo::input::NavigationCommand;
using uirepo::input::TextEvent;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] bool near(float left, float right) noexcept {
    return std::fabs(left - right) < 0.001f;
}

class FixedTextMeasurer final : public TextMeasurer {
public:
    [[nodiscard]] Size measure(std::string_view text, float fontSize) const noexcept override {
        ++calls;
        return {static_cast<float>(text.size()) * 10.0f, fontSize};
    }

    mutable int calls{};
};

struct EventLog {
    int valueChanged{};
};

void eventCallback(
    UIRepoDocumentHandle,
    UIRepoNodeHandle,
    std::uint32_t eventType,
    void* userData) {
    auto& log = *static_cast<EventLog*>(userData);
    if (eventType == UIREPO_EVENT_VALUE_CHANGED) {
        ++log.valueChanged;
    }
}

void testToggleRegistryValueContract() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("toggle-registry-root");
    require(document != 0, "could not create toggle registry document");
    const auto root = registry.rootNode(document);
    const auto toggle = registry.createToggle(document, root, "toggle", false);
    require(toggle != 0, "could not create native toggle through registry");
    bool value = true;
    require(registry.getToggleValue(document, toggle, value), "could not read toggle value");
    require(!value, "toggle initial value mismatch");
    require(registry.setToggleValue(document, toggle, true), "could not change toggle value");
    require(registry.getToggleValue(document, toggle, value) && value, "toggle value did not persist");
    require(registry.destroyNode(document, toggle), "could not destroy toggle");
    require(!registry.getToggleValue(document, toggle, value), "stale toggle handle remained readable");
    require(!registry.setToggleValue(document, toggle, false), "stale toggle handle remained writable");
}

void testSliderRegistryValueContract() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("slider-registry-root");
    require(document != 0, "could not create slider registry document");
    const auto root = registry.rootNode(document);
    const auto slider = registry.createSlider(document, root, "slider", 5.0f, 0.0f, 10.0f, 0.5f);
    require(slider != 0, "could not create native slider through registry");
    float value = -1.0f;
    require(registry.getSliderValue(document, slider, value), "could not read slider value");
    require(near(value, 5.0f), "slider initial value mismatch");
    require(registry.setSliderValue(document, slider, 12.0f), "could not change slider value");
    require(registry.getSliderValue(document, slider, value), "could not reread slider value");
    require(near(value, 10.0f), "slider registry did not preserve clamping");
    require(registry.destroyDocument(document), "could not destroy slider document");
    require(!registry.getSliderValue(document, slider, value), "destroyed document left slider handle readable");
    require(!registry.setSliderValue(document, slider, 2.0f), "destroyed document left slider handle writable");
}

void testStepperRegistryValueContract() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("stepper-registry-root");
    require(document != 0, "could not create stepper registry document");
    const auto root = registry.rootNode(document);
    const auto stepper = registry.createStepper(
        document, root, "stepper", std::vector<std::string>{"Low", "Medium", "High"}, 1);
    require(stepper != 0, "could not create native stepper through registry");
    std::size_t index = 0;
    require(registry.getStepperIndex(document, stepper, index), "could not read stepper index");
    require(index == 1, "stepper initial index mismatch");
    require(registry.setStepperIndex(document, stepper, 99), "could not normalize stepper index");
    require(registry.getStepperIndex(document, stepper, index) && index == 2, "stepper index did not clamp to last option");
    require(registry.destroyNode(document, stepper), "could not destroy stepper");
    require(!registry.getStepperIndex(document, stepper, index), "stale stepper handle remained readable");
    require(!registry.setStepperIndex(document, stepper, 0), "stale stepper handle remained writable");
}

void testTextInputRegistryAndTyping() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("text-input-root");
    require(document != 0, "could not create text input document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable text input document");

    const auto root = registry.rootNode(document);
    const auto input = registry.createTextInput(document, root, "text-input", "ab", 16);
    require(input != 0, "could not create native text input through registry");

    std::string value;
    require(registry.getTextInputValue(document, input, value) && value == "ab", "text input initial value mismatch");
    require(registry.setTextInputValue(document, input, "seed"), "could not set text input value");
    require(registry.getTextInputValue(document, input, value) && value == "seed", "text input set value did not persist");

    EventLog log{};
    require(registry.setNodeEventHandler(document, input, &eventCallback, &log), "could not register text value callback");
    require(registry.focusNode(document, input), "could not focus text input");
    require(registry.routeTextEvent(TextEvent{U'!'}), "focused text input did not consume text event");
    require(registry.routeTextEvent(TextEvent{U'\u00E9'}), "focused text input did not consume Unicode text event");
    require(registry.getTextInputValue(document, input, value) && value == "seed!\xC3\xA9", "text events did not update UTF-8 value");

    require(registry.routeTextEvent(TextEvent{U'\b'}), "focused text input did not consume Backspace text event");
    require(registry.getTextInputValue(document, input, value) && value == "seed!", "Backspace did not remove one UTF-8 codepoint");
    require(log.valueChanged == 3, "text edits must emit one value-changed event per mutation");
}

void testTextInputCaretGeometry() {
    FixedTextMeasurer measurer;
    auto root = std::make_unique<PanelNode>("caret-root");
    auto& input = root->emplaceChild<TextInputNode>("caret-input", "", 256);
    input.style().width = Length::Pixels(40.0f);
    input.style().height = Length::Pixels(20.0f);
    input.style().padding = Thickness{4.0f};

    Document document(std::move(root), &measurer);
    document.layout(Size{100.0f, 50.0f});

    const Rect content = input.bounds().deflate(input.style().padding);
    const Rect empty = input.endCaretRect(measurer);
    require(near(empty.x, content.x), "empty text caret must start at the input content origin");
    require(near(empty.y, content.y), "empty text caret must use the input content top");
    require(empty.width > 0.0f && empty.right() <= content.right(), "empty text caret must remain inside content bounds");

    require(input.setValue("abc"), "could not update direct text input for caret test");
    const Rect filled = input.endCaretRect(measurer);
    require(near(filled.x - empty.x, 30.0f), "non-empty caret did not use active text measurement");
    require(filled.x > empty.x, "changing text must move the end caret");

    require(input.setValue(std::string(64, 'x')), "could not set overflowing text for caret test");
    const Rect overflow = input.endCaretRect(measurer);
    require(overflow.x >= content.x, "overflow caret moved before the content start");
    require(overflow.right() <= content.right(), "overflow caret rectangle escaped the visible content bounds");
    require(overflow.bottom() <= content.bottom(), "caret height escaped the visible content bounds");
}

void testFocusedTextInputCaretRegistryQuery() {
    FixedTextMeasurer measurer;
    DocumentRegistry registry;
    registry.setTextMeasurer(&measurer);

    const auto document = registry.createDocument("ime-caret-root");
    require(document != 0, "could not create IME caret registry document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable IME caret registry document");

    const auto root = registry.rootNode(document);
    const auto input = registry.createTextInput(document, root, "ime-caret-input", "abc", 256);
    const auto button = registry.createButton(document, root, "ime-caret-button", "Button");
    require(input != 0 && button != 0, "could not create IME caret registry controls");

    Style inputStyle{};
    inputStyle.width = Length::Pixels(80.0f);
    inputStyle.height = Length::Pixels(24.0f);
    inputStyle.padding = Thickness{5.0f};
    require(registry.setNodeStyle(document, input, inputStyle), "could not style IME caret input");
    registry.layoutVisible(Size{200.0f, 80.0f});
    require(registry.focusNode(document, input), "could not focus IME caret input");

    Rect caret{};
    const int callsBefore = measurer.calls;
    require(registry.focusedTextInputCaret(caret), "focused text input did not report an IME caret");
    require(measurer.calls == callsBefore + 1, "IME caret query did not use the document's active text measurer");
    require(caret.width > 0.0f && caret.height > 0.0f, "IME caret query returned empty geometry");

    require(registry.setTextInputValue(document, input, "abcdef"), "could not update focused IME caret input");
    Rect changed{};
    require(registry.focusedTextInputCaret(changed), "updated focused text input lost its IME caret");
    require(changed.x > caret.x, "changing focused text did not move the IME caret");

    require(registry.focusNode(document, button), "could not focus non-text control for IME caret rejection");
    require(!registry.focusedTextInputCaret(caret), "non-text-input focus incorrectly reported an IME caret");
}

void testTextLayoutRegistryContract() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("text-layout-root");
    require(document != 0, "could not create text-layout document");
    const auto root = registry.rootNode(document);
    const auto text = registry.createText(document, root, "text-layout", "Wrapped text");
    const auto button = registry.createButton(document, root, "button-layout", "Button");
    const auto panel = registry.createPanel(document, root, "panel-layout");
    require(text != 0 && button != 0 && panel != 0, "could not create text-layout nodes");

    const TextLayoutOptions layout{
        .wrapping = TextWrapping::Wrap,
        .alignment = TextAlignment::Center,
        .overflow = TextOverflow::Ellipsis,
    };
    require(registry.setTextLayout(document, text, layout), "text node rejected text-layout policy");
    require(registry.setTextLayout(document, button, layout), "button rejected text-layout policy");
    require(!registry.setTextLayout(document, panel, layout), "panel incorrectly accepted text-layout policy");

    require(registry.destroyNode(document, text), "could not destroy text-layout node");
    require(!registry.setTextLayout(document, text, layout), "stale text handle remained writable");
}

void testToggleActivationChangesValue() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("toggle-input-root");
    require(document != 0, "could not create toggle input document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable toggle input");
    const auto root = registry.rootNode(document);
    const auto toggle = registry.createToggle(document, root, "toggle-input", false);
    require(toggle != 0, "could not create toggle input control");
    EventLog log{};
    require(registry.setNodeEventHandler(document, toggle, &eventCallback, &log), "could not register toggle value callback");
    require(registry.focusNode(document, toggle), "could not focus toggle");
    require(registry.routeNavigation(NavigationCommand::Accept), "Accept should be consumed by focused toggle");
    bool value = false;
    require(registry.getToggleValue(document, toggle, value) && value, "Accept did not toggle the focused control");
    require(log.valueChanged == 1, "toggle activation must emit one value-changed event");
}

void testSliderDirectionalInputChangesValue() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("slider-input-root");
    require(document != 0, "could not create slider input document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable slider input");
    const auto root = registry.rootNode(document);
    const auto slider = registry.createSlider(document, root, "slider-input", 5.0f, 0.0f, 10.0f, 0.5f);
    require(slider != 0, "could not create slider input control");
    EventLog log{};
    require(registry.setNodeEventHandler(document, slider, &eventCallback, &log), "could not register slider value callback");
    require(registry.focusNode(document, slider), "could not focus slider");
    require(registry.routeNavigation(NavigationCommand::Right), "Right should be consumed by focused slider");
    float value = 0.0f;
    require(registry.getSliderValue(document, slider, value) && near(value, 5.5f), "Right did not advance slider by one step");
    require(registry.routeNavigation(NavigationCommand::Left), "Left should be consumed by focused slider");
    require(registry.getSliderValue(document, slider, value) && near(value, 5.0f), "Left did not reduce slider by one step");
    require(log.valueChanged == 2, "slider directional changes must emit one value-changed event each");
}

void testStepperDirectionalInputChangesValue() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("stepper-input-root");
    require(document != 0, "could not create stepper input document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable stepper input");
    const auto root = registry.rootNode(document);
    const auto stepper = registry.createStepper(document, root, "stepper-input", std::vector<std::string>{"A", "B", "C"}, 0);
    require(stepper != 0, "could not create stepper input control");
    EventLog log{};
    require(registry.setNodeEventHandler(document, stepper, &eventCallback, &log), "could not register stepper value callback");
    require(registry.focusNode(document, stepper), "could not focus stepper");
    require(registry.routeNavigation(NavigationCommand::Left), "Left should be consumed by focused stepper");
    std::size_t index = 0;
    require(registry.getStepperIndex(document, stepper, index) && index == 2, "Left did not wrap stepper backward");
    require(registry.routeNavigation(NavigationCommand::Right), "Right should be consumed by focused stepper");
    require(registry.getStepperIndex(document, stepper, index) && index == 0, "Right did not wrap stepper forward");
    require(log.valueChanged == 2, "stepper directional changes must emit one value-changed event each");
}

void testWrongControlTypeRejected() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("wrong-type-root");
    require(document != 0, "could not create wrong-type document");
    const auto root = registry.rootNode(document);
    const auto button = registry.createButton(document, root, "button", "Button");
    require(button != 0, "could not create wrong-type button");
    bool toggleValue = false;
    float sliderValue = 0.0f;
    std::size_t stepperIndex = 0;
    std::string textValue;
    require(!registry.getToggleValue(document, button, toggleValue), "button was accepted as toggle");
    require(!registry.setToggleValue(document, button, true), "button was writable as toggle");
    require(!registry.getSliderValue(document, button, sliderValue), "button was accepted as slider");
    require(!registry.setSliderValue(document, button, 1.0f), "button was writable as slider");
    require(!registry.getStepperIndex(document, button, stepperIndex), "button was accepted as stepper");
    require(!registry.setStepperIndex(document, button, 1), "button was writable as stepper");
    require(!registry.getTextInputValue(document, button, textValue), "button was accepted as text input");
    require(!registry.setTextInputValue(document, button, "x"), "button was writable as text input");
}

} // namespace

int main() {
    testToggleRegistryValueContract();
    testSliderRegistryValueContract();
    testStepperRegistryValueContract();
    testTextInputRegistryAndTyping();
    testTextInputCaretGeometry();
    testFocusedTextInputCaretRegistryQuery();
    testTextLayoutRegistryContract();
    testToggleActivationChangesValue();
    testSliderDirectionalInputChangesValue();
    testStepperDirectionalInputChangesValue();
    testWrongControlTypeRejected();
    std::cout << "UIRepo control registry regressions passed\n";
    return 0;
}
