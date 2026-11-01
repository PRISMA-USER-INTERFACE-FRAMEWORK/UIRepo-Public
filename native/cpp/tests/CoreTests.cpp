#include "uirepo/Core.h"
#include "uirepo/Controls.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace uirepo;

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

class FixedTextMeasurer final : public TextMeasurer {
public:
    [[nodiscard]] Size measure(std::string_view text, float fontSize) const noexcept override {
        return {static_cast<float>(text.size()) * fontSize, fontSize * 2.0f};
    }
};

void testTreeAndIndex() {
    auto root = std::make_unique<PanelNode>("root");
    auto& button = root->emplaceChild<ButtonNode>("play", "Play");
    Document document(std::move(root));

    require(document.find("root") == &document.root(), "Root lookup failed");
    require(document.find("play") == &button, "Child lookup failed");

    bool duplicateRejected = false;
    try {
        document.root().emplaceChild<TextNode>("play", "duplicate");
        (void)document.find("play");
    } catch (const std::invalid_argument&) {
        duplicateRejected = true;
    }
    require(duplicateRejected, "Duplicate node ID was not rejected");
}

void testLayoutAndDisplayList() {
    auto root = std::make_unique<StackNode>("root", Axis::Column);
    root->style().padding = Thickness{10.0f};
    root->style().gap = 5.0f;

    auto& title = root->emplaceChild<TextNode>("title", "UIRepo");
    title.style().height = Length::Pixels(30.0f);

    auto& button = root->emplaceChild<ButtonNode>("button", "Continue");
    button.style().height = Length::Pixels(40.0f);
    button.style().flexGrow = 1.0f;

    Document document(std::move(root));
    document.layout({300.0f, 200.0f});

    require(near(document.root().bounds().width, 300.0f), "Root layout width is wrong");
    require(near(title.bounds().y, 10.0f), "Title layout position is wrong");
    require(button.bounds().y > title.bounds().y, "Stack order is wrong");

    const auto commands = document.buildDisplayList();
    require(!commands.empty(), "Display list is empty");

    bool sawButtonText = false;
    for (const DrawCommand& command : commands) {
        if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            if (text->nodeId == "button" && text->text == "Continue") {
                sawButtonText = true;
            }
        }
    }
    require(sawButtonText, "Button text command missing");
}

void testTextMeasurerInjection() {
    FixedTextMeasurer measurer;

    auto root = std::make_unique<StackNode>("root", Axis::Column);
    root->style().alignItems = Align::Start;
    auto& text = root->emplaceChild<TextNode>("text", "abcd");
    text.setFontSize(10.0f);

    Document document(std::move(root), &measurer);
    document.layout({500.0f, 500.0f});
    require(near(text.bounds().width, 40.0f), "Injected text width was ignored");
    require(near(text.bounds().height, 20.0f), "Injected text height was ignored");

    document.setTextMeasurer(nullptr);
    document.layout({500.0f, 500.0f});
    require(text.bounds().width < 40.0f, "Fallback text measurer was not restored");
}

void testTextLayoutPolicyAffectsRetainedLayout() {
    auto root = std::make_unique<StackNode>("root", Axis::Column);
    root->style().alignItems = Align::Start;

    auto& wrapped = root->emplaceChild<TextNode>("wrapped", "abcdefghij");
    wrapped.setFontSize(10.0f);
    wrapped.style().width = Length::Pixels(30.0f);
    wrapped.setTextLayout(TextLayoutOptions{
        .wrapping = TextWrapping::Wrap,
        .alignment = TextAlignment::Center,
        .overflow = TextOverflow::Ellipsis,
    });

    auto& cappedWrapped = root->emplaceChild<TextNode>("capped-wrapped", "abcdefghij");
    cappedWrapped.setFontSize(10.0f);
    cappedWrapped.style().maxWidth = 30.0f;
    cappedWrapped.setTextLayout(TextLayoutOptions{
        .wrapping = TextWrapping::Wrap,
        .alignment = TextAlignment::Start,
        .overflow = TextOverflow::Clip,
    });

    auto& singleLine = root->emplaceChild<TextNode>("single", "abcdefghij");
    singleLine.setFontSize(10.0f);
    singleLine.setTextLayout(TextLayoutOptions{
        .wrapping = TextWrapping::NoWrap,
        .alignment = TextAlignment::End,
        .overflow = TextOverflow::Clip,
    });

    auto& explicitLines = root->emplaceChild<TextNode>("lines", "aa\nbb");
    explicitLines.setFontSize(10.0f);

    Document document(std::move(root));
    document.layout({200.0f, 200.0f});

    require(near(wrapped.bounds().width, 30.0f), "wrapped text did not keep its explicit width");
    require(wrapped.bounds().height > 12.5f, "wrapped text did not increase retained height");
    require(
        cappedWrapped.bounds().width > 0.0f && cappedWrapped.bounds().width <= 30.0f,
        "maxWidth did not cap auto wrapped text width");
    require(cappedWrapped.bounds().height > 12.5f, "maxWidth was applied after text measurement instead of before wrapping");
    require(singleLine.bounds().height < wrapped.bounds().height, "NoWrap text unexpectedly gained wrapped height");
    require(explicitLines.bounds().height > singleLine.bounds().height, "explicit newline did not produce multiple retained lines");

    bool sawWrappedPolicy = false;
    bool sawSinglePolicy = false;
    for (const auto& command : document.buildDisplayList()) {
        const auto* text = std::get_if<DrawTextCommand>(&command);
        if (text == nullptr) {
            continue;
        }
        if (text->nodeId == "wrapped") {
            sawWrappedPolicy =
                text->layout.wrapping == TextWrapping::Wrap &&
                text->layout.alignment == TextAlignment::Center &&
                text->layout.overflow == TextOverflow::Ellipsis;
        }
        if (text->nodeId == "single") {
            sawSinglePolicy =
                text->layout.wrapping == TextWrapping::NoWrap &&
                text->layout.alignment == TextAlignment::End &&
                text->layout.overflow == TextOverflow::Clip;
        }
    }
    require(sawWrappedPolicy, "wrapped text layout policy was not propagated to renderer command");
    require(sawSinglePolicy, "single-line text layout policy was not propagated to renderer command");
}

void testFocusInvalidation() {
    auto root = std::make_unique<PanelNode>("root");
    auto panel = std::make_unique<PanelNode>("panel");
    auto& button = panel->emplaceChild<ButtonNode>("button", "Select");
    Node& panelRef = root->addChild(std::move(panel));

    Document document(std::move(root));
    document.layout({1920.0f, 1080.0f});

    require(document.focus(button), "Focus request failed");
    require(document.focused() == &button, "Focused node mismatch");

    panelRef.setVisible(false);
    require(document.focused() == nullptr, "Focus was not invalidated");

    panelRef.setVisible(true);
    require(document.focus(button), "Focus request failed");

    auto detached = document.root().removeChild(panelRef);
    require(detached != nullptr, "Subtree detach failed");
    require(document.focused() == nullptr, "Focus was not invalidated");
}

void testHitTestingUsesReverseChildOrder() {
    auto root = std::make_unique<PanelNode>("root");
    auto& lower = root->emplaceChild<ButtonNode>("lower", "Lower");
    auto& upper = root->emplaceChild<ButtonNode>("upper", "Upper");
    lower.style().width = Length::Pixels(100.0f);
    lower.style().height = Length::Pixels(100.0f);
    upper.style().width = Length::Pixels(100.0f);
    upper.style().height = Length::Pixels(100.0f);

    Document document(std::move(root));
    document.layout({100.0f, 100.0f});

    require(document.hitTest({50.0f, 50.0f}) == &upper, "Hit testing did not prefer topmost child");
}

void testToggleNodeValueAndRendering() {
    auto root = std::make_unique<PanelNode>("root");
    auto& toggle = root->emplaceChild<ToggleNode>("toggle", false);
    Document document(std::move(root));
    document.layout({100.0f, 100.0f});

    require(toggle.focusable(), "Toggle must participate in native focus/navigation");
    require(!toggle.value(), "Toggle initial value is wrong");
    require(toggle.setValue(true), "Toggle value change was not reported");
    require(toggle.value(), "Toggle value did not update");
    require(!toggle.setValue(true), "No-op toggle update must not report a change");
    require(toggle.toggle(), "Toggle action did not report a change");
    require(!toggle.value(), "Toggle action did not invert the value");

    const auto commands = document.buildDisplayList();
    std::size_t fills = 0;
    for (const auto& command : commands) {
        if (std::holds_alternative<FillRectCommand>(command)) {
            ++fills;
        }
    }
    require(fills >= 2, "Toggle must render a track and thumb using native draw commands");
}

void testSliderNodeRangeStepAndRendering() {
    auto root = std::make_unique<PanelNode>("root");
    auto& slider = root->emplaceChild<SliderNode>("slider", 7.0f, 0.0f, 10.0f, 2.0f);
    Document document(std::move(root));
    document.layout({300.0f, 100.0f});

    require(slider.focusable(), "Slider must participate in native focus/navigation");
    require(near(slider.value(), 8.0f), "Slider initial value must snap to its configured step");
    require(slider.setValue(20.0f), "Slider clamp to maximum was not reported");
    require(near(slider.value(), 10.0f), "Slider did not clamp to maximum");
    require(slider.adjustSteps(-1), "Slider step adjustment was not reported");
    require(near(slider.value(), 8.0f), "Slider step adjustment produced the wrong value");
    require(slider.setValue(-5.0f), "Slider clamp to minimum was not reported");
    require(near(slider.value(), 0.0f), "Slider did not clamp to minimum");
    require(!slider.adjustSteps(-1), "Slider must not move below its minimum");
    require(slider.setValue(4.0f), "Slider could not move to a renderable progress value");

    const auto commands = document.buildDisplayList();
    std::size_t fills = 0;
    for (const auto& command : commands) {
        if (std::holds_alternative<FillRectCommand>(command)) {
            ++fills;
        }
    }
    require(fills >= 3, "Slider must render track, progress, and thumb using native draw commands");

    bool invalidRangeRejected = false;
    try {
        SliderNode invalid("invalid-slider", 0.0f, 1.0f, 1.0f, 0.1f);
    } catch (const std::invalid_argument&) {
        invalidRangeRejected = true;
    }
    require(invalidRangeRejected, "Slider must reject an empty range");
}

void testStepperNodeIndexWrapAndRendering() {
    auto root = std::make_unique<PanelNode>("root");
    auto& stepper = root->emplaceChild<StepperNode>(
        "stepper",
        std::vector<std::string>{"Low", "Medium", "High"},
        1);
    Document document(std::move(root));
    document.layout({400.0f, 100.0f});

    require(stepper.focusable(), "Stepper must participate in native focus/navigation");
    require(stepper.index() == 1, "Stepper initial index mismatch");
    require(stepper.value() == "Medium", "Stepper initial label mismatch");
    require(stepper.stepBy(1), "Stepper forward step was not reported");
    require(stepper.index() == 2 && stepper.value() == "High", "Stepper forward step selected wrong option");
    require(stepper.stepBy(1), "Stepper forward wrap was not reported");
    require(stepper.index() == 0 && stepper.value() == "Low", "Stepper did not wrap forward");
    require(stepper.stepBy(-1), "Stepper backward wrap was not reported");
    require(stepper.index() == 2 && stepper.value() == "High", "Stepper did not wrap backward");
    require(stepper.setIndex(99), "Stepper out-of-range index was not normalized");
    require(stepper.index() == 2, "Stepper out-of-range index must clamp to the last option");

    const auto commands = document.buildDisplayList();
    std::size_t fills = 0;
    std::size_t texts = 0;
    bool sawValue = false;
    for (const auto& command : commands) {
        if (std::holds_alternative<FillRectCommand>(command)) {
            ++fills;
        }
        if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            ++texts;
            if (text->text == "High") {
                sawValue = true;
            }
        }
    }
    require(fills >= 3, "Stepper must render left, value, and right native surfaces");
    require(texts >= 3 && sawValue, "Stepper must render arrows and selected option text");

    StepperNode empty("empty-stepper", {}, 0);
    require(empty.value().empty(), "Empty stepper must expose an empty value");
    require(!empty.stepBy(1), "Empty stepper must not report a selection change");
}

void testTextInputUtf8EditingAndBounds() {
    auto root = std::make_unique<PanelNode>("root");
    auto& input = root->emplaceChild<TextInputNode>("input", "abc", 8);
    Document document(std::move(root));
    document.layout({400.0f, 100.0f});

    require(input.focusable(), "Text input must participate in native focus/navigation");
    require(input.value() == "abc", "Text input initial value mismatch");
    require(input.appendCodepoint(U'!'), "Text input rejected ASCII text");
    require(input.value() == "abc!", "Text input ASCII append mismatch");
    require(input.appendCodepoint(U'\u00E9'), "Text input rejected a valid non-ASCII scalar");
    require(input.value() == "abc!\xC3\xA9", "Text input UTF-8 encoding mismatch");
    require(input.backspace(), "Text input backspace failed");
    require(input.value() == "abc!", "Text input backspace did not remove one UTF-8 codepoint");
    require(!input.appendCodepoint(static_cast<char32_t>(0xD800)), "Text input accepted a UTF-16 surrogate scalar");
    require(input.setValue("12345678"), "Text input could not fill its configured byte budget");
    require(!input.appendCodepoint(U'x'), "Text input exceeded its configured byte budget");
    require(!input.setValue("123456789"), "Text input accepted an over-budget replacement value");
    require(input.value() == "12345678", "Rejected text replacement mutated the stored value");

    const auto commands = document.buildDisplayList();
    bool sawBackground = false;
    bool sawText = false;
    for (const auto& command : commands) {
        if (const auto* fill = std::get_if<FillRectCommand>(&command); fill && fill->nodeId == "input") {
            sawBackground = true;
        }
        if (const auto* text = std::get_if<DrawTextCommand>(&command); text && text->nodeId == "input" && text->text == "12345678") {
            sawText = true;
        }
    }
    require(sawBackground && sawText, "Text input must render native background and text commands");

    bool invalidBudgetRejected = false;
    try {
        TextInputNode invalid("invalid-input", "", 0);
    } catch (const std::invalid_argument&) {
        invalidBudgetRejected = true;
    }
    require(invalidBudgetRejected, "Text input must reject a zero byte budget");
}

void testFlexGrowAndStretchRespectMaxConstraints() {
    auto column = std::make_unique<StackNode>("column", Axis::Column);
    column->style().alignItems = Align::Stretch;

    auto& grow = column->emplaceChild<PanelNode>("grow");
    grow.style().height = Length::Pixels(50.0f);
    grow.style().maxHeight = 100.0f;
    grow.style().maxWidth = 120.0f;
    grow.style().flexGrow = 1.0f;

    auto& fixed = column->emplaceChild<PanelNode>("fixed");
    fixed.style().height = Length::Pixels(50.0f);

    Document columnDocument(std::move(column));
    columnDocument.layout({400.0f, 300.0f});
    require(near(grow.bounds().height, 100.0f), "flex-grow exceeded maxHeight during arrange");
    require(near(grow.bounds().width, 120.0f), "stack stretch exceeded maxWidth during arrange");

    auto overlay = std::make_unique<PanelNode>("overlay");
    overlay->style().alignItems = Align::Stretch;
    auto& stretched = overlay->emplaceChild<PanelNode>("stretched");
    stretched.style().maxWidth = 140.0f;
    stretched.style().maxHeight = 90.0f;

    Document overlayDocument(std::move(overlay));
    overlayDocument.layout({500.0f, 400.0f});
    require(near(stretched.bounds().width, 140.0f), "overlay stretch exceeded maxWidth");
    require(near(stretched.bounds().height, 90.0f), "overlay stretch exceeded maxHeight");

    auto row = std::make_unique<StackNode>("row", Axis::Row);
    auto& rowGrow = row->emplaceChild<PanelNode>("row-grow");
    rowGrow.style().width = Length::Pixels(40.0f);
    rowGrow.style().maxWidth = 90.0f;
    rowGrow.style().flexGrow = 1.0f;
    auto& rowFixed = row->emplaceChild<PanelNode>("row-fixed");
    rowFixed.style().width = Length::Pixels(40.0f);

    Document rowDocument(std::move(row));
    rowDocument.layout({300.0f, 100.0f});
    require(near(rowGrow.bounds().width, 90.0f), "row flex-grow exceeded maxWidth during arrange");
}

void testArrangeRespectsMinConstraintsWhenSpaceIsSmaller() {
    auto column = std::make_unique<StackNode>("column-min", Axis::Column);
    column->style().alignItems = Align::Stretch;
    auto& child = column->emplaceChild<PanelNode>("column-min-child");
    child.style().minWidth = 180.0f;
    child.style().minHeight = 120.0f;

    Document columnDocument(std::move(column));
    columnDocument.layout({100.0f, 80.0f});
    require(near(child.bounds().width, 180.0f), "stack cross-axis arrangement violated minWidth");
    require(near(child.bounds().height, 120.0f), "stack main-axis arrangement violated minHeight");

    auto overlay = std::make_unique<PanelNode>("overlay-min");
    overlay->style().alignItems = Align::Stretch;
    auto& overlayChild = overlay->emplaceChild<PanelNode>("overlay-min-child");
    overlayChild.style().minWidth = 160.0f;
    overlayChild.style().minHeight = 110.0f;

    Document overlayDocument(std::move(overlay));
    overlayDocument.layout({90.0f, 70.0f});
    require(near(overlayChild.bounds().width, 160.0f), "overlay arrangement violated minWidth");
    require(near(overlayChild.bounds().height, 110.0f), "overlay arrangement violated minHeight");
}

void testNestedPercentUsesResolvedParentContent() {
    auto root = std::make_unique<PanelNode>("root");
    auto& parent = root->emplaceChild<PanelNode>("parent");
    parent.style().width = Length::Percent(0.5f);
    parent.style().height = Length::Percent(0.5f);
    parent.style().padding = Thickness{10.0f};

    auto& child = parent.emplaceChild<PanelNode>("child");
    child.style().width = Length::Percent(0.5f);
    child.style().height = Length::Percent(0.5f);

    Document document(std::move(root));
    document.layout({400.0f, 200.0f});

    require(near(parent.bounds().width, 200.0f), "parent percent width is wrong");
    require(near(parent.bounds().height, 100.0f), "parent percent height is wrong");
    require(near(child.bounds().width, 90.0f), "nested percent width ignored resolved parent content");
    require(near(child.bounds().height, 40.0f), "nested percent height ignored resolved parent content");
}

void testViewportRelayoutRefreshesPercentGeometry() {
    auto root = std::make_unique<PanelNode>("root");
    auto& parent = root->emplaceChild<PanelNode>("parent");
    parent.style().width = Length::Percent(0.5f);
    parent.style().height = Length::Percent(0.5f);
    auto& child = parent.emplaceChild<PanelNode>("child");
    child.style().width = Length::Percent(0.5f);
    child.style().height = Length::Percent(0.5f);

    Document document(std::move(root));
    document.layout({400.0f, 200.0f});
    require(near(parent.bounds().width, 200.0f), "initial parent percent width is wrong");
    require(near(child.bounds().width, 100.0f), "initial child percent width is wrong");

    document.layout({800.0f, 400.0f});
    require(near(parent.bounds().width, 400.0f), "viewport resize did not refresh parent percent width");
    require(near(parent.bounds().height, 200.0f), "viewport resize did not refresh parent percent height");
    require(near(child.bounds().width, 200.0f), "viewport resize did not refresh nested percent width");
    require(near(child.bounds().height, 100.0f), "viewport resize did not refresh nested percent height");
}

void testRootConstraintsRemainHardDuringViewportLayout() {
    auto root = std::make_unique<PanelNode>("root");
    root->style().maxWidth = 320.0f;
    root->style().maxHeight = 180.0f;
    auto& child = root->emplaceChild<PanelNode>("root-percent-child");
    child.style().width = Length::Percent(0.5f);
    child.style().height = Length::Percent(0.5f);

    Document document(std::move(root));
    document.layout({1920.0f, 1080.0f});
    require(near(document.root().bounds().width, 320.0f), "root maxWidth was ignored during arrange");
    require(near(document.root().bounds().height, 180.0f), "root maxHeight was ignored during arrange");
    require(near(child.bounds().width, 160.0f), "percent child ignored capped auto-root width");
    require(near(child.bounds().height, 90.0f), "percent child ignored capped auto-root height");
}

void testStyleThicknessValidation() {
    auto root = std::make_unique<PanelNode>("style-validation-root");
    auto& child = root->emplaceChild<PanelNode>("style-validation-child");
    child.style().width = Length::Pixels(40.0f);
    child.style().height = Length::Pixels(20.0f);
    child.style().padding = Thickness{2.0f};
    child.style().margin = Thickness{3.0f};

    Document document(std::move(root));
    document.layout({200.0f, 100.0f});

    const Rect rootBefore = document.root().bounds();
    const Rect childBefore = child.bounds();
    require(std::isfinite(childBefore.x) && std::isfinite(childBefore.y) &&
            std::isfinite(childBefore.width) && std::isfinite(childBefore.height),
        "valid thickness produced non-finite geometry");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const auto expectRejected = [&](Thickness padding, Thickness margin, const char* message) {
        child.style().padding = padding;
        child.style().margin = margin;

        bool rejected = false;
        try {
            document.layout({200.0f, 100.0f});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);

        const Rect rootAfter = document.root().bounds();
        const Rect childAfter = child.bounds();
        require(
            near(rootAfter.x, rootBefore.x) && near(rootAfter.y, rootBefore.y) &&
            near(rootAfter.width, rootBefore.width) && near(rootAfter.height, rootBefore.height),
            "failed thickness validation changed root arranged bounds");
        require(
            near(childAfter.x, childBefore.x) && near(childAfter.y, childBefore.y) &&
            near(childAfter.width, childBefore.width) && near(childAfter.height, childBefore.height),
            "failed thickness validation changed child arranged bounds");
    };

    expectRejected(Thickness{-1.0f, 0.0f, 0.0f, 0.0f}, Thickness{}, "negative padding was accepted");
    expectRejected(Thickness{nan, 0.0f, 0.0f, 0.0f}, Thickness{}, "NaN padding was accepted");
    expectRejected(Thickness{infinity, 0.0f, 0.0f, 0.0f}, Thickness{}, "infinite padding was accepted");
    expectRejected(Thickness{}, Thickness{-1.0f, 0.0f, 0.0f, 0.0f}, "negative margin was accepted");
    expectRejected(Thickness{}, Thickness{nan, 0.0f, 0.0f, 0.0f}, "NaN margin was accepted");
    expectRejected(Thickness{}, Thickness{infinity, 0.0f, 0.0f, 0.0f}, "infinite margin was accepted");

    child.style().padding = Thickness{1.0f, 2.0f, 3.0f, 4.0f};
    child.style().margin = Thickness{4.0f, 3.0f, 2.0f, 1.0f};
    document.layout({200.0f, 100.0f});
    const Rect validAfter = child.bounds();
    require(std::isfinite(validAfter.x) && std::isfinite(validAfter.y) &&
            std::isfinite(validAfter.width) && std::isfinite(validAfter.height),
        "valid positive thickness failed after rejected values");
}

void testOverflowHitTestingMatchesClipping() {
    auto root = std::make_unique<PanelNode>("overflow-root");
    root->style().alignItems = Align::Start;

    auto& parent = root->emplaceChild<PanelNode>("overflow-parent");
    parent.style().width = Length::Pixels(50.0f);
    parent.style().height = Length::Pixels(50.0f);
    parent.style().alignItems = Align::Start;

    auto& child = parent.emplaceChild<ButtonNode>("overflow-child", "Overflow");
    child.style().width = Length::Pixels(100.0f);
    child.style().height = Length::Pixels(40.0f);

    Document document(std::move(root));
    document.layout({200.0f, 100.0f});

    const Point overflowPoint{75.0f, 20.0f};
    require(!parent.bounds().contains(overflowPoint), "overflow test point unexpectedly lies inside parent");
    require(child.bounds().contains(overflowPoint), "overflow test point does not lie inside child");
    require(document.hitTest(overflowPoint) == &child, "non-clipped overflowing child was not hittable");

    parent.style().clipToBounds = true;
    document.layout({200.0f, 100.0f});
    require(document.hitTest(overflowPoint) != &child, "clipped overflowing child remained hittable");
    require(document.hitTest(overflowPoint) != &parent, "parent was hittable outside its own bounds");

    parent.style().clipToBounds = false;
    document.layout({200.0f, 100.0f});
    parent.setVisible(false);
    require(document.hitTest(overflowPoint) != &child, "hidden ancestor did not block overflowing child hit");
    parent.setVisible(true);
    document.layout({200.0f, 100.0f});
    parent.setEnabled(false);
    require(document.hitTest(overflowPoint) != &child, "disabled ancestor did not block overflowing child hit");
    parent.setEnabled(true);

    require(document.hitTest({25.0f, 20.0f}) == &child, "inside-parent child hit behavior regressed");
}

void testScrollableHitTestingTracksOffsetAndResize() {
    auto root = std::make_unique<PanelNode>("scroll-hit-root");
    root->style().alignItems = Align::Start;

    auto& stack = root->emplaceChild<StackNode>("scroll-hit-stack", Axis::Column);
    stack.style().width = Length::Pixels(100.0f);
    stack.style().height = Length::Percent(0.5f);
    stack.setScrollable(true);

    auto& first = stack.emplaceChild<ButtonNode>("scroll-hit-first", "First");
    first.style().height = Length::Pixels(50.0f);
    auto& second = stack.emplaceChild<ButtonNode>("scroll-hit-second", "Second");
    second.style().height = Length::Pixels(50.0f);

    Document document(std::move(root));
    document.layout({200.0f, 100.0f});

    require(near(stack.bounds().height, 50.0f), "initial scroll viewport height is wrong");
    require(near(stack.scrollRange(), 50.0f), "initial scroll range is wrong");
    require(document.hitTest({20.0f, 25.0f}) == &first, "initial visible scroll child was not hittable");
    require(document.hitTest({20.0f, 75.0f}) != &second, "child outside scroll viewport was hittable");

    require(stack.scrollBy(50.0f), "scroll offset did not change");
    document.layout({200.0f, 100.0f});
    require(near(stack.scrollOffset(), 50.0f), "scroll offset was not retained after layout");
    require(document.hitTest({20.0f, 25.0f}) == &second, "hit target did not move with scroll offset");

    document.layout({200.0f, 200.0f});
    require(near(stack.bounds().height, 100.0f), "resized scroll viewport height is wrong");
    require(near(stack.scrollRange(), 0.0f), "scroll range did not shrink after resize");
    require(near(stack.scrollOffset(), 0.0f), "scroll offset did not clamp after resize");
    require(document.hitTest({20.0f, 25.0f}) == &first, "first child hit did not recover after resize");
    require(document.hitTest({20.0f, 75.0f}) == &second, "second child hit did not recover after resize");
}

} // namespace

void testAbiShapedControlColumnDoesNotCollapse() {
    auto root = std::make_unique<PanelNode>("root");

    auto& card = root->emplaceChild<StackNode>("harness-card", Axis::Column);
    card.style().width = Length::Pixels(420.0f);
    card.style().height = Length::Auto();
    card.style().maxWidth = 0.0f;
    card.style().maxHeight = 0.0f;
    card.style().padding = Thickness{20.0f, 18.0f, 20.0f, 18.0f};
    card.style().margin = Thickness{0.0f, 160.0f, 0.0f, 0.0f};
    card.style().gap = 10.0f;
    card.style().alignItems = Align::Start;
    card.style().background = Color{28, 18, 40, 232};
    card.style().cornerRadius = 6.0f;

    auto& title = card.emplaceChild<TextNode>("harness-title", "UIRepo input test");
    title.setFontSize(22.0f);
    auto& hint = card.emplaceChild<TextNode>("harness-hint", "Click the controls.");
    hint.setFontSize(14.0f);
    auto& counter = card.emplaceChild<TextNode>("harness-counter", "Clicks: 0");
    counter.setFontSize(17.0f);
    auto& clickButton = card.emplaceChild<ButtonNode>("harness-click", "Click me");
    auto& toggle = card.emplaceChild<ToggleNode>("harness-toggle", false);
    auto& slider = card.emplaceChild<SliderNode>("harness-slider", 0.5f, 0.0f, 1.0f, 0.1f);
    auto& status = card.emplaceChild<TextNode>("harness-status", "Toggle: OFF");
    status.setFontSize(15.0f);
    auto& closeButton = card.emplaceChild<ButtonNode>("harness-close", "Close");

    Document document(std::move(root));
    document.layout({2560.0f, 1440.0f});

    require(near(card.bounds().width, 420.0f), "Zero maxWidth collapsed the card width");
    require(card.bounds().height > 100.0f, "Auto-height control column collapsed");
    require(near(card.bounds().y, 160.0f), "Card top margin was not applied");
    require(card.bounds().x >= 0.0f && card.bounds().right() <= 2560.0f, "Card is off-screen horizontally");

    const Node* children[] = {
        &title, &hint, &counter, &clickButton, &toggle, &slider, &status, &closeButton};
    float previousBottom = -1.0f;
    for (const Node* child : children) {
        require(child->bounds().width > 0.0f, "A control collapsed to zero cross-axis width");
        require(child->bounds().height > 0.0f, "A control collapsed to zero main-axis height");
        require(child->bounds().x >= card.bounds().x, "A control escaped the card horizontally");
        require(child->bounds().y >= previousBottom, "Controls overlap instead of stacking");
        previousBottom = child->bounds().bottom();
    }

    require(near(toggle.bounds().width, 46.0f), "Toggle lost its intrinsic width in a column");
    require(near(slider.bounds().width, 140.0f), "Slider lost its intrinsic width in a column");

    const auto commands = document.buildDisplayList();
    require(!commands.empty(), "ABI-shaped control column produced an empty display list");

    bool sawCardFill = false;
    bool sawTitleText = false;
    for (const DrawCommand& command : commands) {
        if (const auto* fill = std::get_if<FillRectCommand>(&command)) {
            if (fill->nodeId == "harness-card") {
                sawCardFill = fill->bounds.width > 0.0f && fill->bounds.height > 0.0f;
            }
        } else if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            if (text->nodeId == "harness-title") {
                sawTitleText = true;
            }
        }
    }
    require(sawCardFill, "Card background rect missing or zero-sized");
    require(sawTitleText, "Title text command missing");
}

int main() {
    testTreeAndIndex();
    testLayoutAndDisplayList();
    testTextMeasurerInjection();
    testTextLayoutPolicyAffectsRetainedLayout();
    testFocusInvalidation();
    testHitTestingUsesReverseChildOrder();
    testToggleNodeValueAndRendering();
    testSliderNodeRangeStepAndRendering();
    testStepperNodeIndexWrapAndRendering();
    testTextInputUtf8EditingAndBounds();
    testFlexGrowAndStretchRespectMaxConstraints();
    testArrangeRespectsMinConstraintsWhenSpaceIsSmaller();
    testNestedPercentUsesResolvedParentContent();
    testViewportRelayoutRefreshesPercentGeometry();
    testRootConstraintsRemainHardDuringViewportLayout();
    testStyleThicknessValidation();
    testOverflowHitTestingMatchesClipping();
    testScrollableHitTestingTracksOffsetAndResize();
    testAbiShapedControlColumnDoesNotCollapse();
    return 0;
}
