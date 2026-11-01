#include "DeclarativeFields.h"

#include <array>

namespace uirepo::declarative::detail {
namespace {

constexpr std::array<std::string_view, 16> kStyleKeys{
    "width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight", "padding", "margin",
    "gap", "flexGrow", "alignItems", "background", "opacity", "zIndex", "cornerRadius", "clipToBounds",
};

constexpr std::array<std::string_view, 3> kTextLayoutKeys{"wrapping", "alignment", "overflow"};
constexpr std::array<std::string_view, 1> kPercentKeys{"percent"};

std::optional<Length> readLength(Fields& style, std::string_view key) {
    const json::Value* value = style.find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    switch (value->kind) {
    case json::Kind::Number: {
        const auto pixels = style.number(key, Rule::NonNegative);
        return pixels ? std::optional<Length>(Length::Pixels(*pixels)) : std::nullopt;
    }
    case json::Kind::String:
        if (value->text == "auto") {
            return Length::Auto();
        }
        style.error(ErrorCode::InvalidEnum, key, *value, "expected \"auto\", a number, or {\"percent\": number}");
        return std::nullopt;
    case json::Kind::Object: {
        Fields percent(style.context(), *value, childPath(style.path(), key));
        percent.rejectUnknown({kPercentKeys});
        const auto amount = percent.percentFraction("percent");
        if (!amount && !percent.failed()) {
            percent.missing("percent");
        }
        if (percent.failed()) {
            style.fail();
            return std::nullopt;
        }
        return Length::Percent(*amount);
    }
    default:
        style.error(ErrorCode::TypeMismatch, key, *value, "expected \"auto\", a number, or {\"percent\": number}");
        return std::nullopt;
    }
}

std::optional<Thickness> readThickness(Fields& style, std::string_view key) {
    const json::Value* value = style.find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->kind == json::Kind::Number) {
        const auto uniform = style.number(key, Rule::NonNegative);
        return uniform ? std::optional<Thickness>(Thickness(*uniform)) : std::nullopt;
    }
    if (value->kind != json::Kind::Array) {
        style.error(ErrorCode::TypeMismatch, key, *value, "expected a number or [left, top, right, bottom]");
        return std::nullopt;
    }
    if (value->items.size() != 4) {
        style.error(ErrorCode::InvalidValue, key, *value, "expected exactly four numbers: [left, top, right, bottom]");
        return std::nullopt;
    }
    std::array<float, 4> sides{};
    bool ok = true;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto side = readFloat(
            style.context(), value->items[index], indexPath(childPath(style.path(), key), index), Rule::NonNegative);
        ok = ok && side.has_value();
        sides[index] = side.value_or(0.0f);
    }
    if (!ok) {
        style.fail();
        return std::nullopt;
    }
    return Thickness{sides[0], sides[1], sides[2], sides[3]};
}

void checkBounds(Fields& style, std::string_view minKey, std::string_view maxKey, std::optional<float> minimum, std::optional<float> maximum) {
    if (maximum && *maximum < minimum.value_or(0.0f)) {
        style.error(ErrorCode::InvalidRange, maxKey, *style.find(maxKey), "maximum is smaller than \"" + std::string(minKey) + "\"");
    }
}

} // namespace

std::optional<Color> readColor(Fields& fields, std::string_view key) {
    const json::Value* value = fields.find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->kind != json::Kind::String) {
        fields.error(ErrorCode::TypeMismatch, key, *value, "expected a color string");
        return std::nullopt;
    }
    const std::string& text = value->text;
    std::array<std::uint8_t, 4> channels{0, 0, 0, 255};
    bool valid = (text.size() == 7 || text.size() == 9) && text[0] == '#';
    for (std::size_t index = 1; valid && index < text.size(); ++index) {
        const char c = text[index];
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }
        valid = digit >= 0;
        auto& channel = channels[(index - 1) / 2];
        channel = static_cast<std::uint8_t>(index % 2 == 1 ? digit * 16 : channel + digit);
    }
    if (!valid) {
        fields.error(ErrorCode::InvalidColor, key, *value, "expected \"#rrggbb\" or \"#rrggbbaa\"");
        return std::nullopt;
    }
    return Color{channels[0], channels[1], channels[2], channels[3]};
}

std::optional<TextLayoutOptions> readTextLayout(Fields& fields) {
    const json::Value* value = fields.container("textLayout", json::Kind::Object);
    if (value == nullptr) {
        return std::nullopt;
    }
    Fields layout(fields.context(), *value, childPath(fields.path(), "textLayout"));
    layout.rejectUnknown({kTextLayoutKeys});
    TextLayoutOptions options{};
    if (const auto wrapping = layout.choice("wrapping", names::kWrapping, false)) {
        options.wrapping = *wrapping;
    }
    if (const auto alignment = layout.choice("alignment", names::kTextAlignments, false)) {
        options.alignment = *alignment;
    }
    if (const auto overflow = layout.choice("overflow", names::kOverflow, false)) {
        options.overflow = *overflow;
    }
    if (layout.failed()) {
        fields.fail();
        return std::nullopt;
    }
    return options;
}

std::optional<Size> readNaturalSize(Fields& fields) {
    const json::Value* value = fields.container("naturalSize", json::Kind::Array);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->items.size() != 2) {
        fields.error(ErrorCode::InvalidValue, "naturalSize", *value, "expected exactly two numbers: [width, height]");
        return std::nullopt;
    }
    const std::string base = childPath(fields.path(), "naturalSize");
    const auto width = readFloat(fields.context(), value->items[0], indexPath(base, 0), Rule::NonNegative);
    const auto height = readFloat(fields.context(), value->items[1], indexPath(base, 1), Rule::NonNegative);
    if (!width || !height) {
        fields.fail();
        return std::nullopt;
    }
    return Size{*width, *height};
}

std::optional<StyleOverrides> readStyle(Fields& parent) {
    const json::Value* value = parent.container("style", json::Kind::Object);
    if (value == nullptr) {
        return std::nullopt;
    }
    Fields style(parent.context(), *value, childPath(parent.path(), "style"));
    style.rejectUnknown({kStyleKeys});

    StyleOverrides out;
    out.width = readLength(style, "width");
    out.height = readLength(style, "height");
    out.minWidth = style.number("minWidth", Rule::NonNegative);
    out.minHeight = style.number("minHeight", Rule::NonNegative);
    out.maxWidth = style.number("maxWidth", Rule::Positive);
    out.maxHeight = style.number("maxHeight", Rule::Positive);
    out.padding = readThickness(style, "padding");
    out.margin = readThickness(style, "margin");
    out.gap = style.number("gap", Rule::NonNegative);
    out.flexGrow = style.number("flexGrow", Rule::NonNegative);
    out.alignItems = style.choice("alignItems", names::kAligns, false);
    out.background = readColor(style, "background");
    out.opacity = style.number("opacity", Rule::Unit);
    if (const json::Value* z = style.find("zIndex")) {
        if (const auto index = readInteger(style.context(), *z, childPath(style.path(), "zIndex"), INT32_MIN, INT32_MAX)) {
            out.zIndex = static_cast<std::int32_t>(*index);
        } else {
            style.fail();
        }
    }
    out.cornerRadius = style.number("cornerRadius", Rule::NonNegative);
    out.clipToBounds = style.boolean("clipToBounds");

    checkBounds(style, "minWidth", "maxWidth", out.minWidth, out.maxWidth);
    checkBounds(style, "minHeight", "maxHeight", out.minHeight, out.maxHeight);
    if (style.failed()) {
        parent.fail();
        return std::nullopt;
    }
    return out;
}

void applyStyle(const StyleOverrides& overrides, Style& style) noexcept {
    const auto assign = [](auto& target, const auto& source) {
        if (source) {
            target = *source;
        }
    };
    assign(style.width, overrides.width);
    assign(style.height, overrides.height);
    assign(style.minWidth, overrides.minWidth);
    assign(style.minHeight, overrides.minHeight);
    assign(style.maxWidth, overrides.maxWidth);
    assign(style.maxHeight, overrides.maxHeight);
    assign(style.padding, overrides.padding);
    assign(style.margin, overrides.margin);
    assign(style.gap, overrides.gap);
    assign(style.flexGrow, overrides.flexGrow);
    assign(style.alignItems, overrides.alignItems);
    assign(style.background, overrides.background);
    assign(style.opacity, overrides.opacity);
    assign(style.zIndex, overrides.zIndex);
    assign(style.cornerRadius, overrides.cornerRadius);
    assign(style.clipToBounds, overrides.clipToBounds);
}

} // namespace uirepo::declarative::detail
