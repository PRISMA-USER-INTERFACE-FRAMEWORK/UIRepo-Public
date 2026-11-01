#include "uirepo/Core.h"
#include "uirepo/Controls.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <variant>

using namespace uirepo;

namespace {
void require(bool value, const char* message)
{
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class CountingTextMeasurer final : public TextMeasurer {
public:
    [[nodiscard]] Size measure(std::string_view, float) const noexcept override {
        ++calls;
        return {24.0f, 16.0f};
    }

    mutable int calls{};
};

bool sameColor(Color left, Color right) noexcept {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

FillRectCommand firstFill(std::span<const DrawCommand> commands) {
    for (const DrawCommand& command : commands) {
        if (const auto* fill = std::get_if<FillRectCommand>(&command)) {
            return *fill;
        }
    }
    require(false, "display list did not contain a fill command");
    return {};
}

void testRenderOnlyMutationPath() {
    auto root = std::make_unique<PanelNode>("root");
    root->setBackground({10, 20, 30, 255});
    root->emplaceChild<TextNode>("label", "render-only");
    auto& rootRef = *root;
    CountingTextMeasurer measurer;
    Document document(std::move(root), &measurer);

    document.layout({800.0f, 600.0f});
    const int layoutCalls = measurer.calls;
    const FillRectCommand initial = firstFill(document.buildDisplayList());

    rootRef.setOpacity(0.5f);
    rootRef.setBackground({40, 50, 60, 255});
    rootRef.setCornerRadius(6.0f);
    const FillRectCommand changed = firstFill(document.buildDisplayList());

    require(changed.opacity == 0.5f, "render-only opacity mutation was not drawn");
    require(sameColor(changed.color, {40, 50, 60, 255}),
        "render-only background mutation was not drawn");
    require(changed.cornerRadius == 6.0f, "render-only corner mutation was not drawn");
    require(initial.opacity != changed.opacity, "opacity display command did not change");
    require(measurer.calls == layoutCalls, "render-only mutation unexpectedly measured text");

    document.layout({800.0f, 600.0f});
    require(measurer.calls == layoutCalls, "render-only mutation unexpectedly reran layout");

    document.root().style().padding = Thickness{1.0f};
    document.layout({800.0f, 600.0f});
    require(measurer.calls > layoutCalls, "generic style mutation did not invalidate layout");
}

void testRenderOnlyValidation() {
    auto root = std::make_unique<PanelNode>("root");
    auto& rootRef = *root;
    rootRef.setOpacity(0.25f);
    rootRef.setCornerRadius(2.0f);
    Document document(std::move(root));

    const auto expectRejected = [](auto mutate, const char* message) {
        bool rejected = false;
        try {
            mutate();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);
    };

    expectRejected([&] { rootRef.setOpacity(-1.0f); }, "negative opacity was accepted");
    require(rootRef.opacity() == 0.25f, "invalid opacity changed retained state");
    expectRejected([&] { rootRef.setOpacity(std::numeric_limits<float>::quiet_NaN()); },
        "NaN opacity was accepted");
    require(rootRef.opacity() == 0.25f, "invalid NaN opacity changed retained state");
    expectRejected([&] { rootRef.setCornerRadius(-1.0f); },
        "negative corner radius was accepted");
    require(rootRef.cornerRadius() == 2.0f, "invalid corner radius changed retained state");
    expectRejected([&] { rootRef.setCornerRadius(std::numeric_limits<float>::quiet_NaN()); },
        "NaN corner radius was accepted");
    require(rootRef.cornerRadius() == 2.0f, "invalid NaN corner radius changed retained state");
    require(document.root().opacity() == 0.25f, "node was not retained by document");
}
}

int main()
{
    auto root = std::make_unique<PanelNode>("root");
    auto child = std::make_unique<PanelNode>("child");
    auto& childRef = *child;

    auto& nested = childRef.emplaceChild<ButtonNode>("button", "OK");
    root->addChild(std::move(child));

    Document document(std::move(root));
    document.layout({800.0f, 600.0f});

    require(document.find("child") == &childRef, "Child lookup failed after attach");
    require(document.find("button") == &nested, "Nested lookup failed after attach");
    require(nested.parent() == &childRef, "Parent link was not preserved");

    auto detached = document.root().removeChild(childRef);
    require(detached != nullptr, "Detach returned null subtree");
    require(document.find("child") == nullptr, "Detached node remained indexed");
    require(detached->parent() == nullptr, "Detached node retained parent");

    document.root().addChild(std::move(detached));
    require(document.find("child") == &childRef, "Reattached node was not indexed");
    require(document.find("button") == &nested, "Reattached child was not indexed");

    testRenderOnlyMutationPath();
    testRenderOnlyValidation();

    return 0;
}
