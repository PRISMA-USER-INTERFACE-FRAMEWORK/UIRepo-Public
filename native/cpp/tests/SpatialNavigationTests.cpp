#include "uirepo/InputRouter.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <variant>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void sizeButton(uirepo::ButtonNode& button) {
    button.style().width = uirepo::Length::Pixels(80.0f);
    button.style().height = uirepo::Length::Pixels(40.0f);
}

void testScoreOrdering() {
    using namespace uirepo;
    using namespace uirepo::input;

    constexpr Rect current{100.0f, 100.0f, 100.0f, 40.0f};

    SpatialScore nearDiagonal{};
    SpatialScore farAligned{};
    require(
        spatialScore(current, Rect{175.0f, 180.0f, 100.0f, 40.0f}, NavigationCommand::Right, 2, nearDiagonal),
        "near diagonal candidate should be eligible");
    require(
        spatialScore(current, Rect{300.0f, 100.0f, 100.0f, 40.0f}, NavigationCommand::Right, 1, farAligned),
        "far aligned candidate should be eligible");
    require(
        betterSpatialScore(farAligned, nearDiagonal),
        "same-lane candidate must beat an out-of-lane candidate");

    SpatialScore nearDiagonalWide{};
    SpatialScore farDiagonalTight{};
    require(
        spatialScore(current, Rect{175.0f, 190.0f, 100.0f, 40.0f}, NavigationCommand::Right, 3, nearDiagonalWide),
        "near diagonal score setup failed");
    require(
        spatialScore(current, Rect{260.0f, 155.0f, 100.0f, 40.0f}, NavigationCommand::Right, 1, farDiagonalTight),
        "far diagonal score setup failed");
    require(nearDiagonalWide.outsideLane && farDiagonalTight.outsideLane, "diagonal candidates must be outside the beam");
    require(
        betterSpatialScore(nearDiagonalWide, farDiagonalTight),
        "among diagonal candidates, primary-axis distance must be scored before cross-axis gap");

    SpatialScore aligned{};
    SpatialScore lessAligned{};
    require(
        spatialScore(current, Rect{220.0f, 105.0f, 100.0f, 40.0f}, NavigationCommand::Right, 3, aligned),
        "aligned score setup failed");
    require(
        spatialScore(current, Rect{220.0f, 125.0f, 100.0f, 40.0f}, NavigationCommand::Right, 0, lessAligned),
        "less-aligned score setup failed");
    require(!aligned.outsideLane && !lessAligned.outsideLane, "alignment comparison must keep both candidates in the beam");
    require(
        betterSpatialScore(aligned, lessAligned),
        "equal primary distance must prefer the closer cross-axis center");

    SpatialScore tiedFirst{};
    SpatialScore tiedSecond{};
    require(
        spatialScore(current, Rect{220.0f, 100.0f, 100.0f, 40.0f}, NavigationCommand::Right, 1, tiedFirst),
        "first tie score setup failed");
    require(
        spatialScore(current, Rect{220.0f, 100.0f, 100.0f, 40.0f}, NavigationCommand::Right, 2, tiedSecond),
        "second tie score setup failed");
    require(
        betterSpatialScore(tiedFirst, tiedSecond),
        "exact spatial ties must use traversal order deterministically");
}

void testGridDirectionsAndTraversal() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<StackNode>("grid", Axis::Column);
    root->style().gap = 30.0f;

    auto& topRow = root->emplaceChild<StackNode>("top-row", Axis::Row);
    topRow.style().height = Length::Pixels(40.0f);
    topRow.style().gap = 60.0f;
    auto& topLeft = topRow.emplaceChild<ButtonNode>("top-left", "TL");
    auto& topRight = topRow.emplaceChild<ButtonNode>("top-right", "TR");

    auto& bottomRow = root->emplaceChild<StackNode>("bottom-row", Axis::Row);
    bottomRow.style().height = Length::Pixels(40.0f);
    bottomRow.style().gap = 60.0f;
    auto& bottomLeft = bottomRow.emplaceChild<ButtonNode>("bottom-left", "BL");
    auto& bottomRight = bottomRow.emplaceChild<ButtonNode>("bottom-right", "BR");

    for (ButtonNode* button : std::array{&topLeft, &topRight, &bottomLeft, &bottomRight}) {
        sizeButton(*button);
    }
    bottomRight.style().margin.left = 20.0f;

    Document document(std::move(root));
    document.layout({400.0f, 200.0f});

    require(document.focus(topLeft), "could not focus top-left");
    require(navigationTarget(document, NavigationCommand::Right) == &topRight, "Right should select the button physically to the right");
    require(navigationTarget(document, NavigationCommand::Down) == &bottomLeft, "Down should select the vertically aligned candidate");
    require(navigationTarget(document, NavigationCommand::Next) == &topRight, "Tab traversal must remain tree-ordered");
    require(navigationTarget(document, NavigationCommand::Previous) == &bottomRight, "Shift+Tab traversal must remain tree-ordered and wrap");

    require(document.focus(bottomRight), "could not focus bottom-right");
    require(navigationTarget(document, NavigationCommand::Left) == &bottomLeft, "Left should prefer the same-row candidate over a nearer diagonal one");
    require(navigationTarget(document, NavigationCommand::Up) == &topRight, "Up should select the aligned button above");
    require(navigationTarget(document, NavigationCommand::Right) == nullptr, "directional navigation must not wrap at the right edge");
    require(navigationTarget(document, NavigationCommand::Down) == nullptr, "directional navigation must fall through at the bottom edge");
}

void testHiddenDisabledAndDiagonalFallback() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<StackNode>("grid", Axis::Column);
    root->style().gap = 30.0f;

    auto& topRow = root->emplaceChild<StackNode>("top-row", Axis::Row);
    topRow.style().height = Length::Pixels(40.0f);
    topRow.style().gap = 60.0f;
    auto& topLeft = topRow.emplaceChild<ButtonNode>("top-left", "TL");
    auto& topRight = topRow.emplaceChild<ButtonNode>("top-right", "TR");

    auto& bottomRow = root->emplaceChild<StackNode>("bottom-row", Axis::Row);
    bottomRow.style().height = Length::Pixels(40.0f);
    bottomRow.style().gap = 60.0f;
    auto& bottomLeft = bottomRow.emplaceChild<ButtonNode>("bottom-left", "BL");
    auto& bottomRight = bottomRow.emplaceChild<ButtonNode>("bottom-right", "BR");

    for (ButtonNode* button : std::array{&topLeft, &topRight, &bottomLeft, &bottomRight}) {
        sizeButton(*button);
    }

    Document document(std::move(root));
    document.layout({400.0f, 200.0f});
    require(document.focus(topLeft), "could not focus top-left");

    bottomLeft.setEnabled(false);
    document.layout({400.0f, 200.0f});
    require(navigationTarget(document, NavigationCommand::Down) == &bottomRight, "disabled candidate should be skipped");

    bottomLeft.setEnabled(true);
    bottomLeft.setVisible(false);
    topRight.setVisible(false);
    document.layout({400.0f, 200.0f});
    require(document.focus(topLeft), "could not restore top-left focus");
    require(navigationTarget(document, NavigationCommand::Right) == &bottomRight, "a diagonal candidate should be used when no same-lane target exists");
}

void testDiagonalPrimaryDistanceScoring() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<PanelNode>("overlay");
    auto& current = root->emplaceChild<ButtonNode>("current", "Current");
    auto& nearDiagonal = root->emplaceChild<ButtonNode>("near-diagonal", "Near");
    auto& fartherDiagonal = root->emplaceChild<ButtonNode>("farther-diagonal", "Far");

    for (ButtonNode* button : std::array{&current, &nearDiagonal, &fartherDiagonal}) {
        button->style().width = Length::Pixels(100.0f);
        button->style().height = Length::Pixels(40.0f);
    }
    current.style().margin = Thickness{80.0f, 60.0f, 0.0f, 0.0f};
    nearDiagonal.style().margin = Thickness{150.0f, 145.0f, 0.0f, 0.0f};
    fartherDiagonal.style().margin = Thickness{250.0f, 120.0f, 0.0f, 0.0f};

    Document document(std::move(root));
    document.layout({520.0f, 260.0f});
    require(document.focus(current), "could not focus diagonal source");
    require(
        navigationTarget(document, NavigationCommand::Right) == &nearDiagonal,
        "among diagonal candidates, the nearer primary-axis target should win before cross-axis gap");
}

void testOverlapAndDeterministicTies() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<PanelNode>("overlay");
    auto& current = root->emplaceChild<ButtonNode>("current", "Current");
    auto& overlapping = root->emplaceChild<ButtonNode>("overlap", "Overlap");
    auto& tiedFirst = root->emplaceChild<ButtonNode>("tie-first", "First");
    auto& tiedSecond = root->emplaceChild<ButtonNode>("tie-second", "Second");

    for (ButtonNode* button : std::array{&current, &overlapping, &tiedFirst, &tiedSecond}) {
        button->style().width = Length::Pixels(100.0f);
        button->style().height = Length::Pixels(40.0f);
        button->style().margin.top = 80.0f;
    }
    current.style().margin.left = 80.0f;
    overlapping.style().margin.left = 140.0f;
    tiedFirst.style().margin.left = 260.0f;
    tiedSecond.style().margin.left = 260.0f;

    Document document(std::move(root));
    document.layout({500.0f, 220.0f});
    require(document.focus(current), "could not focus current button");
    require(
        current.bounds().x + current.bounds().width > overlapping.bounds().x,
        "partial-overlap test requires intersecting primary-axis bounds");
    require(
        navigationTarget(document, NavigationCommand::Right) == &overlapping,
        "partially overlapping control with a center to the right should remain eligible");

    overlapping.setVisible(false);
    document.layout({500.0f, 220.0f});
    require(document.focus(current), "could not restore current focus");
    require(
        navigationTarget(document, NavigationCommand::Right) == &tiedFirst,
        "equal spatial scores must use traversal order as the deterministic tie-breaker");
}

void testScrollableStackWheelClippingAndFocusReveal() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<StackNode>("scroll", Axis::Column);
    root->setScrollable(true);
    root->style().gap = 8.0f;

    auto& first = root->emplaceChild<ButtonNode>("first", "First");
    auto& second = root->emplaceChild<ButtonNode>("second", "Second");
    auto& third = root->emplaceChild<ButtonNode>("third", "Third");
    auto& fourth = root->emplaceChild<ButtonNode>("fourth", "Fourth");
    auto& fifth = root->emplaceChild<ButtonNode>("fifth", "Fifth");
    for (ButtonNode* button : std::array{&first, &second, &third, &fourth, &fifth}) {
        button->style().height = Length::Pixels(40.0f);
        button->style().width = Length::Pixels(120.0f);
    }

    StackNode* scroll = root.get();
    Document document(std::move(root));
    document.layout({160.0f, 120.0f});

    require(scroll->scrollRange() > 0.0f, "scrollable stack should expose a positive scroll range");
    require(visibleForNavigation(first), "first item should begin visible");
    require(visibleForNavigation(second), "second item should begin visible");
    require(!visibleForNavigation(fourth), "fully clipped fourth item must not be treated as visible");
    require(!visibleForNavigation(fifth), "fully clipped fifth item must not be treated as visible");

    require(document.focus(first), "could not focus first scroll item");
    require(
        navigationTarget(document, NavigationCommand::Down) == &second,
        "visible spatial candidate must beat a clipped candidate farther down the scroll container");

    require(routeWheel(document, {20.0f, 20.0f}, -WheelDeltaPerNotch), "wheel-down should scroll the container");
    require(scroll->scrollOffset() > 0.0f, "wheel-down did not advance the scroll offset");
    require(first.bounds().y < 0.0f, "wheel scrolling did not move retained child bounds");
    require(routeWheel(document, {20.0f, 20.0f}, WheelDeltaPerNotch), "wheel-up should scroll back toward the start");
    require(scroll->scrollOffset() == 0.0f, "wheel-up should return to the start in one equal notch");
    require(!routeWheel(document, {20.0f, 20.0f}, WheelDeltaPerNotch), "wheel-up at the top must fall through");

    require(document.focus(fourth), "direct focus of an off-screen item should succeed");
    require(scroll->scrollOffset() > 0.0f, "focusing an off-screen item must reveal it by scrolling");
    require(visibleForNavigation(fourth), "focused item must be visible after focus-driven reveal");
    require(fourth.bounds().bottom() <= scroll->scrollViewport().bottom() + 0.001f, "focus reveal must fit the item inside the viewport");

    const auto display = document.buildDisplayList();
    require(!display.empty() && std::holds_alternative<PushClipCommand>(display.front()), "scrollable stack must push a clip command");
}

void testDirectionalMovementAcrossScrollBoundary() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<StackNode>("scroll", Axis::Column);
    root->setScrollable(true);
    root->style().gap = 8.0f;

    auto& first = root->emplaceChild<ButtonNode>("first", "First");
    auto& second = root->emplaceChild<ButtonNode>("second", "Second");
    auto& third = root->emplaceChild<ButtonNode>("third", "Third");
    auto& fourth = root->emplaceChild<ButtonNode>("fourth", "Fourth");
    for (ButtonNode* button : std::array{&first, &second, &third, &fourth}) {
        button->style().height = Length::Pixels(40.0f);
        button->style().width = Length::Pixels(120.0f);
    }

    StackNode* scroll = root.get();
    Document document(std::move(root));
    document.layout({160.0f, 120.0f});

    require(document.focus(third), "could not focus partially visible boundary item");
    const float before = scroll->scrollOffset();
    Node* next = navigationTarget(document, NavigationCommand::Down);
    require(next == &fourth, "Down at the visible boundary must select the next clipped item in the scroll container");
    require(!visibleForNavigation(fourth), "boundary target should still be clipped before focus moves");
    require(document.focus(*next), "could not move focus across scroll boundary");
    require(scroll->scrollOffset() > before, "moving focus across the boundary must advance scrolling");
    require(visibleForNavigation(fourth), "directional target must be visible after focus moves");

    require(document.focus(first), "could not focus first item after scrolling back");
    require(scroll->scrollOffset() == 0.0f, "focusing the first item must reveal the top of the scroll container");
    require(navigationTarget(document, NavigationCommand::Up) == nullptr, "Up at the first item must still fall through");
}

} // namespace

int main() {
    testScoreOrdering();
    testGridDirectionsAndTraversal();
    testHiddenDisabledAndDiagonalFallback();
    testDiagonalPrimaryDistanceScoring();
    testOverlapAndDeterministicTies();
    testScrollableStackWheelClippingAndFocusReveal();
    testDirectionalMovementAcrossScrollBoundary();
    std::cout << "UIRepo spatial navigation regressions passed\n";
    return 0;
}
