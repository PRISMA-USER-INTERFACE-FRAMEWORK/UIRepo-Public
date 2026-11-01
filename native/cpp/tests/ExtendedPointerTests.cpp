#include "uirepo/InputRouter.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

using namespace uirepo;
using namespace uirepo::input;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testExtendedPointerSequenceOwnership() {
    PointerSequenceState sequences;

    sequences.begin(PointerButton::X1, true);
    sequences.begin(PointerButton::X2, false);
    require(
        sequences.owner(PointerButton::X1) == SequenceOwner::UIRepo,
        "X1 handled press must be UIRepo-owned");
    require(
        sequences.owner(PointerButton::X2) == SequenceOwner::Fallout,
        "X2 unhandled press must remain Fallout-owned");
    require(sequences.anyOwned(), "X1 UIRepo ownership must participate in anyOwned");
    require(sequences.end(PointerButton::X1), "X1 UIRepo release must stay consumed");
    require(!sequences.end(PointerButton::X2), "X2 Fallout release must fall through");

    sequences.begin(PointerButton::X2, true);
    sequences.clear();
    require(
        sequences.owner(PointerButton::X2) == SequenceOwner::None,
        "clear must reset X2 ownership");
}

void testHorizontalWheelScrollsRowOnly() {
    auto row = std::make_unique<StackNode>("row", Axis::Row);
    row->style().width = Length::Pixels(100.0f);
    row->style().height = Length::Pixels(40.0f);
    row->setScrollable(true);

    for (int index = 0; index < 3; ++index) {
        auto& child = row->emplaceChild<PanelNode>("row-child-" + std::to_string(index));
        child.style().width = Length::Pixels(80.0f);
        child.style().height = Length::Pixels(40.0f);
    }

    StackNode* rowPointer = row.get();
    Document rowDocument(std::move(row));
    rowDocument.layout({100.0f, 40.0f});
    require(rowPointer->scrollRange() > 0.0f, "row must have horizontal overflow");
    require(rowPointer->scrollOffset() == 0.0f, "row must start at zero scroll offset");
    require(
        routeHorizontalWheel(rowDocument, Point{10.0f, 10.0f}, WheelDeltaPerNotch),
        "positive horizontal wheel must scroll overflowing row");
    require(rowPointer->scrollOffset() > 0.0f, "horizontal wheel must advance row scroll offset");

    auto column = std::make_unique<StackNode>("column", Axis::Column);
    column->style().width = Length::Pixels(40.0f);
    column->style().height = Length::Pixels(100.0f);
    column->setScrollable(true);
    for (int index = 0; index < 3; ++index) {
        auto& child = column->emplaceChild<PanelNode>("column-child-" + std::to_string(index));
        child.style().width = Length::Pixels(40.0f);
        child.style().height = Length::Pixels(80.0f);
    }

    StackNode* columnPointer = column.get();
    Document columnDocument(std::move(column));
    columnDocument.layout({40.0f, 100.0f});
    require(columnPointer->scrollRange() > 0.0f, "column must have vertical overflow");
    require(
        !routeHorizontalWheel(columnDocument, Point{10.0f, 10.0f}, WheelDeltaPerNotch),
        "horizontal wheel must not move a column scroll container");
    require(columnPointer->scrollOffset() == 0.0f, "horizontal wheel changed column scroll offset");
    require(
        routeWheel(columnDocument, Point{10.0f, 10.0f}, -WheelDeltaPerNotch),
        "existing vertical wheel routing must remain functional");
    require(columnPointer->scrollOffset() > 0.0f, "vertical wheel did not advance column scroll offset");
}

} // namespace

int main() {
    testExtendedPointerSequenceOwnership();
    testHorizontalWheelScrollsRowOnly();
    std::cout << "extended pointer tests passed\n";
    return 0;
}
