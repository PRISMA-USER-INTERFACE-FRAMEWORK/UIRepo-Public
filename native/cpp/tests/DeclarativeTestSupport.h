#pragma once

#include "uirepo/Controls.h"
#include "uirepo/Core.h"
#include "uirepo/DeclarativeUi.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <variant>

namespace uirepo::declarative::testing {

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

inline bool sameColor(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline bool sameLength(Length a, Length b) {
    return a.kind == b.kind && a.value == b.value;
}

inline bool sameThickness(Thickness a, Thickness b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

inline bool sameStyle(const Style& a, const Style& b) {
    return sameLength(a.width, b.width) && sameLength(a.height, b.height) &&
           a.minWidth == b.minWidth && a.minHeight == b.minHeight &&
           a.maxWidth == b.maxWidth && a.maxHeight == b.maxHeight &&
           sameThickness(a.padding, b.padding) && sameThickness(a.margin, b.margin) &&
           a.gap == b.gap && a.flexGrow == b.flexGrow && a.alignItems == b.alignItems &&
           sameColor(a.background, b.background) && a.opacity == b.opacity &&
           a.zIndex == b.zIndex && a.cornerRadius == b.cornerRadius && a.clipToBounds == b.clipToBounds;
}

template <typename T>
const T* as(const Node& node) {
    return dynamic_cast<const T*>(&node);
}

inline bool sameKindValues(const Node& a, const Node& b) {
    if (typeid(a) != typeid(b)) {
        return false;
    }
    if (const auto* x = as<StackNode>(a)) {
        const auto* y = as<StackNode>(b);
        return x->axis() == y->axis() && x->scrollable() == y->scrollable();
    }
    if (const auto* x = as<TextNode>(a)) {
        const auto* y = as<TextNode>(b);
        return x->text() == y->text() && x->fontSize() == y->fontSize() && sameColor(x->color(), y->color()) &&
               x->textLayout() == y->textLayout();
    }
    if (const auto* x = as<ButtonNode>(a)) {
        const auto* y = as<ButtonNode>(b);
        return x->text() == y->text() && x->fontSize() == y->fontSize() &&
               sameColor(x->textColor(), y->textColor()) &&
               sameColor(x->focusedBackground(), y->focusedBackground()) && x->textLayout() == y->textLayout();
    }
    if (const auto* x = as<ImageNode>(a)) {
        const auto* y = as<ImageNode>(b);
        return x->source() == y->source() && x->naturalSize().width == y->naturalSize().width &&
               x->naturalSize().height == y->naturalSize().height;
    }
    if (const auto* x = as<VideoNode>(a)) {
        const auto* y = as<VideoNode>(b);
        return x->source() == y->source() && x->naturalSize().width == y->naturalSize().width &&
               x->naturalSize().height == y->naturalSize().height && x->playbackState() == y->playbackState() &&
               x->loop() == y->loop() && x->volume() == y->volume();
    }
    if (const auto* x = as<ToggleNode>(a)) {
        return x->value() == as<ToggleNode>(b)->value();
    }
    if (const auto* x = as<SliderNode>(a)) {
        const auto* y = as<SliderNode>(b);
        return x->value() == y->value() && x->minimum() == y->minimum() && x->maximum() == y->maximum() &&
               x->step() == y->step();
    }
    if (const auto* x = as<StepperNode>(a)) {
        const auto* y = as<StepperNode>(b);
        return x->options() == y->options() && x->index() == y->index();
    }
    if (const auto* x = as<TextInputNode>(a)) {
        const auto* y = as<TextInputNode>(b);
        return x->value() == y->value() && x->maxBytes() == y->maxBytes();
    }
    return true;
}

inline bool sameTree(const Node& a, const Node& b) {
    if (a.id() != b.id() || a.visible() != b.visible() || a.enabled() != b.enabled() ||
        !sameStyle(a.style(), b.style()) || !sameKindValues(a, b) || a.children().size() != b.children().size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.children().size(); ++index) {
        if (!sameTree(*a.children()[index], *b.children()[index])) {
            return false;
        }
    }
    return true;
}

inline bool sameRect(Rect a, Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

inline bool sameCommand(const DrawCommand& a, const DrawCommand& b) {
    if (a.index() != b.index()) {
        return false;
    }
    return std::visit(
        [&b](const auto& left) {
            using T = std::decay_t<decltype(left)>;
            const auto& right = std::get<T>(b);
            bool same = left.nodeId == right.nodeId && sameRect(left.bounds, right.bounds) &&
                        left.opacity == right.opacity && left.zIndex == right.zIndex;
            if constexpr (std::is_same_v<T, FillRectCommand>) {
                same = same && sameColor(left.color, right.color) && left.cornerRadius == right.cornerRadius;
            } else if constexpr (std::is_same_v<T, DrawTextCommand>) {
                same = same && left.text == right.text && left.fontSize == right.fontSize &&
                       sameColor(left.color, right.color) && left.layout == right.layout;
            } else if constexpr (std::is_same_v<T, DrawImageCommand>) {
                same = same && left.source == right.source;
            } else if constexpr (std::is_same_v<T, DrawVideoCommand>) {
                same = same && left.source == right.source && left.playbackState == right.playbackState &&
                       left.loop == right.loop && left.volume == right.volume;
            }
            return same;
        },
        a);
}

inline ParseResult parseOk(std::string_view text, const char* label) {
    ParseResult result = parseDocument(text);
    if (!result.ok()) {
        std::string detail;
        for (const auto& d : result.diagnostics) {
            detail += "\n  " + std::string(errorCodeName(d.code)) + " " + d.path + " " + std::to_string(d.line) +
                      ":" + std::to_string(d.column) + " " + d.message;
        }
        require(false, std::string(label) + " failed to parse:" + detail);
    }
    return result;
}

inline std::string canonical(const Document& document) {
    const SerializeResult result = serializeDocument(document);
    require(result.ok(), "serialize failed");
    return result.text;
}

} // namespace uirepo::declarative::testing
