#include "uirepo/Animation.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

using namespace uirepo;
using namespace uirepo::anim;

namespace uirepo {
class RetainedAnimationTestAccess {
public:
    [[nodiscard]] static std::size_t activeTrackCount(const Document& document) noexcept {
        return document.animations_.size();
    }

    [[nodiscard]] static std::size_t trackCapacity(const Document& document) noexcept {
        return document.animations_.capacity();
    }
};
}

namespace {
constexpr int kStressCycles = 10001;

void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool approx(float left, float right, float epsilon = 1e-4f) {
    return std::fabs(left - right) <= epsilon;
}

bool sameColor(Color left, Color right) noexcept {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

class CountingTextMeasurer final : public TextMeasurer {
public:
    [[nodiscard]] Size measure(std::string_view, float) const noexcept override {
        ++calls;
        return {24.0f, 16.0f};
    }

    mutable int calls{};
};

FillRectCommand firstFill(std::span<const DrawCommand> commands) {
    for (const DrawCommand& command : commands) {
        if (const auto* fill = std::get_if<FillRectCommand>(&command)) {
            return *fill;
        }
    }
    require(false, "display list did not contain a fill command");
    return {};
}

void testDeterministicRenderOnlyPlayback() {
    auto root = std::make_unique<PanelNode>("root");
    root->setBackground({0, 0, 0, 255});
    root->emplaceChild<TextNode>("label", "animated");
    auto& rootRef = *root;
    CountingTextMeasurer measurer;
    Document document(std::move(root), &measurer);

    document.layout({400.0f, 200.0f});
    const int layoutCalls = measurer.calls;
    const AnimationId opacity = document.animateOpacity(rootRef, 0.0f, Playback{1.0f});
    const AnimationId background = document.animateBackground(
        rootRef, {100, 200, 50, 255}, Playback{1.0f});
    const AnimationId corner = document.animateCornerRadius(rootRef, 10.0f, Playback{1.0f});

    document.advanceAnimations(0.5f);
    require(approx(rootRef.opacity(), 0.5f), "opacity animation midpoint was not deterministic");
    require(sameColor(rootRef.background(), {50, 100, 25, 255}),
        "background animation midpoint was not deterministic");
    require(approx(rootRef.cornerRadius(), 5.0f),
        "corner-radius animation midpoint was not deterministic");

    const FillRectCommand midpoint = firstFill(document.buildDisplayList());
    require(approx(midpoint.opacity, 0.5f), "display list missed animated opacity");
    require(sameColor(midpoint.color, {50, 100, 25, 255}),
        "display list missed animated background");
    require(approx(midpoint.cornerRadius, 5.0f), "display list missed animated corner radius");

    document.layout({400.0f, 200.0f});
    require(measurer.calls == layoutCalls, "render-only animation triggered layout work");

    document.advanceAnimations(0.5f);
    require(approx(rootRef.opacity(), 0.0f), "opacity animation did not reach its target");
    require(sameColor(rootRef.background(), {100, 200, 50, 255}),
        "background animation did not reach its target");
    require(approx(rootRef.cornerRadius(), 10.0f),
        "corner-radius animation did not reach its target");
    require(!document.cancelAnimation(opacity), "completed opacity animation remained active");
    require(!document.cancelAnimation(background), "completed background animation remained active");
    require(!document.cancelAnimation(corner), "completed corner animation remained active");
}

void testReplacementCancellationAndAuthoredMutation() {
    auto root = std::make_unique<PanelNode>("root");
    auto& rootRef = *root;
    Document document(std::move(root));

    const AnimationId first = document.animateOpacity(rootRef, 0.0f, Playback{1.0f});
    document.advanceAnimations(0.25f);
    require(approx(rootRef.opacity(), 0.75f), "initial opacity animation did not advance");

    const AnimationId replacement = document.animateOpacity(rootRef, 1.0f, Playback{1.0f});
    require(!document.cancelAnimation(first), "replacement did not cancel the previous animation");
    document.advanceAnimations(0.5f);
    require(approx(rootRef.opacity(), 0.875f), "replacement did not start from current state");
    require(document.cancelAnimation(replacement), "active replacement could not be cancelled");
    document.advanceAnimations(0.5f);
    require(approx(rootRef.opacity(), 0.875f), "cancelled animation changed node state");

    const AnimationId authored = document.animateOpacity(rootRef, 0.0f, Playback{1.0f});
    rootRef.setOpacity(0.25f);
    require(!document.cancelAnimation(authored), "authored opacity mutation did not cancel animation");
    document.advanceAnimations(1.0f);
    require(approx(rootRef.opacity(), 0.25f), "authored opacity mutation was overwritten");

    const AnimationId generic = document.animateCornerRadius(rootRef, 12.0f, Playback{1.0f});
    rootRef.style().padding = Thickness{2.0f};
    require(!document.cancelAnimation(generic), "generic style mutation did not cancel animation");
    document.advanceAnimations(1.0f);
    require(approx(rootRef.cornerRadius(), 0.0f), "generic style mutation left animation active");
}

void testLifecycleAndInvalidDelta() {
    auto root = std::make_unique<PanelNode>("root");
    auto child = std::make_unique<PanelNode>("child");
    auto& childRef = *child;
    root->addChild(std::move(child));
    Document document(std::move(root));

    const AnimationId invalidDelta = document.animateOpacity(childRef, 0.0f, Playback{1.0f});
    document.advanceAnimations(std::numeric_limits<float>::quiet_NaN());
    require(approx(childRef.opacity(), 1.0f), "invalid delta changed animation state");
    require(document.cancelAnimation(invalidDelta), "invalid-delta animation could not be cancelled");

    const AnimationId hidden = document.animateOpacity(childRef, 0.0f, Playback{1.0f});
    childRef.setVisible(false);
    document.advanceAnimations(1.0f);
    require(approx(childRef.opacity(), 1.0f), "hidden node received an animation write");
    require(!document.cancelAnimation(hidden), "hiding a node did not cancel its animation");
    childRef.setVisible(true);

    childRef.setOpacity(0.4f);
    const AnimationId detached = document.animateOpacity(childRef, 0.0f, Playback{1.0f});
    auto removed = document.root().removeChild(childRef);
    require(removed != nullptr, "could not detach animated node");
    document.advanceAnimations(1.0f);
    require(approx(removed->opacity(), 0.4f), "detached node received a stale animation write");
    require(!document.cancelAnimation(detached), "detaching a node left its animation active");

    bool rejected = false;
    try {
        document.animateOpacity(*removed, 0.0f, Playback{1.0f});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "animation accepted a node from another document");
}

void testAllocationBoundedness() {
    auto root = std::make_unique<PanelNode>("root");
    auto& rootRef = *root;
    auto& first = rootRef.emplaceChild<PanelNode>("first");
    auto& second = rootRef.emplaceChild<PanelNode>("second");
    auto& third = rootRef.emplaceChild<PanelNode>("third");
    const std::array<Node*, 3> nodes{&first, &second, &third};
    CountingTextMeasurer measurer;
    Document document(std::move(root), &measurer);

    document.layout({400.0f, 200.0f});

    auto startAllProperties = [&](int cycle) {
        Node& opacityNode = *nodes[cycle % nodes.size()];
        Node& backgroundNode = *nodes[(cycle + 1) % nodes.size()];
        Node& cornerNode = *nodes[(cycle + 2) % nodes.size()];
        return std::array<AnimationId, 3>{
            document.animateOpacity(opacityNode, cycle % 2 == 0 ? 0.0f : 1.0f, Playback{1.0f}),
            document.animateBackground(
                backgroundNode, cycle % 2 == 0 ? Color{20, 40, 60, 255} : Color{200, 180, 160, 255},
                Playback{1.0f}),
            document.animateCornerRadius(cornerNode, cycle % 2 == 0 ? 0.0f : 12.0f, Playback{1.0f}),
        };
    };

    auto startNodeProperties = [&](Node& node, int cycle) {
        return std::array<AnimationId, 3>{
            document.animateOpacity(node, cycle % 2 == 0 ? 0.0f : 1.0f, Playback{1.0f}),
            document.animateBackground(
                node, cycle % 2 == 0 ? Color{20, 40, 60, 255} : Color{200, 180, 160, 255},
                Playback{1.0f}),
            document.animateCornerRadius(node, cycle % 2 == 0 ? 0.0f : 12.0f, Playback{1.0f}),
        };
    };

    auto runReplacementStress = [&] {
        for (int cycle = 0; cycle < kStressCycles; ++cycle) {
            Node& node = *nodes[cycle % nodes.size()];
            const AnimationId replaced = document.animateOpacity(
                node, cycle % 2 == 0 ? 0.0f : 1.0f, Playback{1.0f});
            const AnimationId active = document.animateOpacity(
                node, cycle % 2 == 0 ? 1.0f : 0.0f, Playback{1.0f});
            require(!document.cancelAnimation(replaced), "replaced animation ID remained active");
            require(document.cancelAnimation(active), "active replacement could not be cancelled");
            require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
                "replacement stress retained an active track");
        }
    };

    auto runCancelStress = [&] {
        for (int cycle = 0; cycle < kStressCycles; ++cycle) {
            const auto ids = startAllProperties(cycle);
            for (const AnimationId id : ids) {
                require(document.cancelAnimation(id), "started animation could not be cancelled");
                require(!document.cancelAnimation(id), "cancelled animation ID remained active");
            }
            require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
                "cancel stress retained an active track");
        }
    };

    auto runCompletionStress = [&] {
        for (int cycle = 0; cycle < kStressCycles; ++cycle) {
            const auto ids = startAllProperties(cycle);
            document.advanceAnimations(1.0f);
            require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
                "completed animation stress retained an active track");
            for (const AnimationId id : ids) {
                require(!document.cancelAnimation(id), "completed animation ID remained active");
            }
        }
    };

    auto runVisibilityStress = [&] {
        for (int cycle = 0; cycle < kStressCycles; ++cycle) {
            Node& node = *nodes[cycle % nodes.size()];
            const auto ids = startNodeProperties(node, cycle);
            node.setVisible(false);
            require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
                "hiding a node retained an active track");
            for (const AnimationId id : ids) {
                require(!document.cancelAnimation(id), "hidden animation ID remained active");
            }
            node.setVisible(true);
        }
    };

    auto runDetachStress = [&] {
        for (int cycle = 0; cycle < kStressCycles; ++cycle) {
            Node& node = *nodes[cycle % nodes.size()];
            const auto ids = startNodeProperties(node, cycle);
            auto detached = rootRef.removeChild(node);
            require(detached != nullptr, "animated node could not be detached");
            require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
                "detaching a node retained an active track");
            for (const AnimationId id : ids) {
                require(!document.cancelAnimation(id), "detached animation ID remained active");
            }
            rootRef.addChild(std::move(detached));
        }
    };

    auto runFullStress = [&] {
        runReplacementStress();
        runCancelStress();
        runCompletionStress();
        runVisibilityStress();
        runDetachStress();
        require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
            "full animation stress retained an active track");
    };

    runFullStress();
    const std::size_t warmCapacity = RetainedAnimationTestAccess::trackCapacity(document);
    require(warmCapacity > 0, "animation stress never populated the track capacity");

    document.layout({400.0f, 200.0f});
    const int layoutCalls = measurer.calls;
    runReplacementStress();
    runCancelStress();
    runCompletionStress();
    document.layout({400.0f, 200.0f});
    require(measurer.calls == layoutCalls, "render-only animation stress triggered layout work");
    require(RetainedAnimationTestAccess::trackCapacity(document) == warmCapacity,
        "render-only animation stress increased track capacity after warm-up");

    runFullStress();
    require(RetainedAnimationTestAccess::trackCapacity(document) == warmCapacity,
        "equivalent animation stress increased track capacity after warm-up");

    const AnimationId fresh = document.animateOpacity(first, 0.0f, Playback{1.0f});
    document.advanceAnimations(0.5f);
    require(approx(first.opacity(), 0.5f), "fresh animation failed after stress cleanup");
    require(document.cancelAnimation(fresh), "fresh animation could not be cancelled after stress");
    require(RetainedAnimationTestAccess::activeTrackCount(document) == 0,
        "fresh animation cleanup retained an active track");
}
}

int main() {
    testDeterministicRenderOnlyPlayback();
    testReplacementCancellationAndAuthoredMutation();
    testLifecycleAndInvalidDelta();
    testAllocationBoundedness();
    return 0;
}
