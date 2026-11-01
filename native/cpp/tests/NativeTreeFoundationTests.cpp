#include "uirepo/Core.h"
#include "uirepo/Controls.h"

#include <cstdlib>
#include <iostream>
#include <memory>

using namespace uirepo;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void testRetainedTreeOwnership() {
    auto root = std::make_unique<PanelNode>("root");
    auto& child = root->emplaceChild<PanelNode>("child");
    auto& leaf = child.emplaceChild<TextNode>("label", "Native UI");

    Document document(std::move(root));

    require(document.find("root") == &document.root(), "root lookup failed");
    require(document.find("child") == &child, "child lookup failed");
    require(document.find("label") == &leaf, "leaf lookup failed");
    require(leaf.parent() == &child, "parent ownership was not preserved");
}

void testTreeMutationInvalidatesLayoutPath() {
    auto root = std::make_unique<PanelNode>("root");
    Document document(std::move(root));

    auto& child = document.root().emplaceChild<TextNode>("label", "updated");
    document.layout({800.0f, 600.0f});

    require(child.bounds().width >= 0.0f, "child did not receive layout state");
    require(document.find("label") == &child, "new child was not indexed");
}

bool sameRect(Rect left, Rect right) {
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

void testStyleScalarValidation() {
    const float infinity = INFINITY;
    const float nan = infinity - infinity;

    auto expectRejected = [](auto mutate, const char* message) {
        auto root = std::make_unique<PanelNode>("root");
        auto& child = root->emplaceChild<PanelNode>("child");
        child.style().width = Length::Pixels(40.0f);
        child.style().height = Length::Pixels(20.0f);
        child.style().minWidth = 10.0f;
        child.style().maxWidth = 100.0f;
        child.style().minHeight = 5.0f;
        child.style().maxHeight = 80.0f;

        Document document(std::move(root));
        document.layout({200.0f, 100.0f});
        const Rect rootBefore = document.root().bounds();
        const Rect childBefore = child.bounds();

        mutate(child.style());

        bool rejected = false;
        try {
            document.layout({200.0f, 100.0f});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);
        require(sameRect(document.root().bounds(), rootBefore), message);
        require(sameRect(child.bounds(), childBefore), message);
    };

    auto expectAccepted = [](auto mutate, const char* message) {
        auto root = std::make_unique<PanelNode>("root");
        auto& child = root->emplaceChild<PanelNode>("child");
        mutate(child.style());

        Document document(std::move(root));
        try {
            document.layout({200.0f, 100.0f});
        } catch (const std::invalid_argument&) {
            require(false, message);
        }
        require(std::isfinite(child.bounds().width), message);
        require(std::isfinite(child.bounds().height), message);
    };

    expectAccepted([&](Style& style) {
        style.width = {LengthKind::Auto, nan};
        style.height = {LengthKind::Auto, -infinity};
        style.minWidth = 10.0f;
        style.minHeight = 5.0f;
        style.maxWidth = 0.0f;
        style.maxHeight = infinity;
        style.cornerRadius = 4.0f;
    }, "valid scalar compatibility case was rejected");

    expectRejected([](Style& style) { style.width = Length::Pixels(-1.0f); },
        "negative pixel width was accepted");
    expectRejected([&](Style& style) { style.width = Length::Pixels(nan); },
        "NaN pixel width was accepted");
    expectRejected([&](Style& style) { style.width = Length::Pixels(infinity); },
        "infinite pixel width was accepted");
    expectRejected([](Style& style) { style.height = Length::Percent(-1.0f); },
        "negative percent height was accepted");
    expectRejected([&](Style& style) { style.height = Length::Percent(nan); },
        "NaN percent height was accepted");
    expectRejected([&](Style& style) { style.height = Length::Percent(infinity); },
        "infinite percent height was accepted");
    expectRejected([](Style& style) {
        style.width.kind = static_cast<LengthKind>(0xff);
    }, "unknown length kind was accepted");
    expectRejected([](Style& style) { style.minWidth = -1.0f; },
        "negative minimum width was accepted");
    expectRejected([&](Style& style) { style.minHeight = nan; },
        "NaN minimum height was accepted");
    expectRejected([](Style& style) { style.maxWidth = -1.0f; },
        "negative maximum width was accepted");
    expectRejected([&](Style& style) { style.maxHeight = nan; },
        "NaN maximum height was accepted");
    expectRejected([&](Style& style) { style.maxWidth = -infinity; },
        "negative infinite maximum width was accepted");
    expectRejected([](Style& style) { style.maxWidth = 5.0f; },
        "maximum width below minimum was accepted");
    expectRejected([](Style& style) { style.maxHeight = 4.0f; },
        "maximum height below minimum was accepted");
    expectRejected([](Style& style) { style.cornerRadius = -1.0f; },
        "negative corner radius was accepted");
    expectRejected([&](Style& style) { style.cornerRadius = nan; },
        "NaN corner radius was accepted");
    expectRejected([&](Style& style) { style.cornerRadius = infinity; },
        "infinite corner radius was accepted");

    {
        auto root = std::make_unique<PanelNode>("root");
        auto& child = root->emplaceChild<PanelNode>("child");
        Document document(std::move(root));
        document.layout({200.0f, 100.0f});
        const Rect rootBefore = document.root().bounds();
        const Rect childBefore = child.bounds();
        document.root().style().width = Length::Pixels(nan);

        bool rejected = false;
        try {
            document.layout({200.0f, 100.0f});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "invalid root length was accepted");
        require(sameRect(document.root().bounds(), rootBefore),
            "invalid root length changed root bounds");
        require(sameRect(child.bounds(), childBefore),
            "invalid root length changed child bounds");
    }

    {
        auto root = std::make_unique<PanelNode>("root");
        root->style().background = Color{10, 20, 30, 255};
        auto& child = root->emplaceChild<PanelNode>("child");
        Document document(std::move(root));
        document.layout({200.0f, 100.0f});
        const auto initial = document.buildDisplayList();
        require(!initial.empty(), "baseline display list was empty");
        const std::size_t initialSize = initial.size();
        child.style().cornerRadius = nan;

        bool rejected = false;
        try {
            (void)document.buildDisplayList();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "invalid display-list style was accepted");
        require(initial.size() == initialSize,
            "rejected display-list style cleared the prior display list");

        child.style().cornerRadius = 0.0f;
        require(document.buildDisplayList().size() == initialSize,
            "display list did not recover after restoring valid style");
    }
}

}

int main() {
    testRetainedTreeOwnership();
    testTreeMutationInvalidatesLayoutPath();
    testStyleScalarValidation();
    return 0;
}
