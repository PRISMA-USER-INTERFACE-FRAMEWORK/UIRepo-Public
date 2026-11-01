#include "uirepo/DeclarativeUi.h"

#include "DeclarativeFields.h"

#include <array>
#include <limits>

namespace uirepo::declarative {
namespace {

using detail::Context;
using detail::Fields;
using detail::Rule;
using names::Kind;

constexpr std::array<std::string_view, 4> kCommonKeys{"style", "visible", "enabled", "children"};
constexpr std::array<std::string_view, 3> kRootKeys{"format", "version", "id"};
constexpr std::array<std::string_view, 2> kHeaderKeys{"type", "id"};
constexpr std::array<std::string_view, 0> kPanelKeys{};
constexpr std::array<std::string_view, 2> kStackKeys{"axis", "scrollable"};
constexpr std::array<std::string_view, 4> kTextKeys{"text", "fontSize", "color", "textLayout"};
constexpr std::array<std::string_view, 5> kButtonKeys{"text", "fontSize", "textColor", "focusedBackground", "textLayout"};
constexpr std::array<std::string_view, 2> kImageKeys{"source", "naturalSize"};
constexpr std::array<std::string_view, 5> kVideoKeys{"source", "naturalSize", "playback", "loop", "volume"};
constexpr std::array<std::string_view, 1> kToggleKeys{"value"};
constexpr std::array<std::string_view, 4> kSliderKeys{"min", "max", "step", "value"};
constexpr std::array<std::string_view, 2> kStepperKeys{"options", "index"};
constexpr std::array<std::string_view, 2> kTextInputKeys{"value", "maxBytes"};

[[nodiscard]] std::span<const std::string_view> kindKeys(Kind kind) noexcept {
    switch (kind) {
    case Kind::Panel: return kPanelKeys;
    case Kind::Stack: return kStackKeys;
    case Kind::Text: return kTextKeys;
    case Kind::Button: return kButtonKeys;
    case Kind::Image: return kImageKeys;
    case Kind::Video: return kVideoKeys;
    case Kind::Toggle: return kToggleKeys;
    case Kind::Slider: return kSliderKeys;
    case Kind::Stepper: return kStepperKeys;
    case Kind::TextInput: return kTextInputKeys;
    }
    return {};
}

[[nodiscard]] std::optional<float> requiredNumber(Fields& fields, std::string_view key, Rule rule) {
    auto value = fields.number(key, rule);
    if (!value && fields.find(key) == nullptr) {
        fields.missing(key);
    }
    return value;
}

[[nodiscard]] std::optional<std::string> readId(Fields& fields) {
    auto id = fields.string("id", true);
    if (!id) {
        return std::nullopt;
    }
    const json::Value& at = *fields.find("id");
    if (!names::validId(*id)) {
        fields.error(ErrorCode::InvalidId, "id", at, "id must match [A-Za-z_][A-Za-z0-9_.-]{0,127}");
        return std::nullopt;
    }
    if (!fields.context().ids.insert(*id).second) {
        fields.error(ErrorCode::DuplicateId, "id", at, "duplicate id \"" + *id + "\"");
        return std::nullopt;
    }
    return id;
}

[[nodiscard]] std::unique_ptr<Node> makeStack(Fields& f, std::string id) {
    const auto axis = f.choice("axis", names::kAxes, true);
    const auto scrollable = f.boolean("scrollable");
    if (f.failed()) {
        return nullptr;
    }
    auto node = std::make_unique<StackNode>(std::move(id), *axis);
    if (scrollable) {
        node->setScrollable(*scrollable);
    }
    return node;
}

[[nodiscard]] std::unique_ptr<Node> makeText(Fields& f, std::string id) {
    const auto text = f.string("text", false);
    const auto fontSize = f.number("fontSize", Rule::Positive);
    const auto color = detail::readColor(f, "color");
    const auto layout = detail::readTextLayout(f);
    if (f.failed()) {
        return nullptr;
    }
    auto node = std::make_unique<TextNode>(std::move(id), text.value_or(std::string{}));
    if (fontSize) {
        node->setFontSize(*fontSize);
    }
    if (color) {
        node->setColor(*color);
    }
    if (layout) {
        node->setTextLayout(*layout);
    }
    return node;
}

[[nodiscard]] std::unique_ptr<Node> makeButton(Fields& f, std::string id) {
    const auto text = f.string("text", false);
    const auto fontSize = f.number("fontSize", Rule::Positive);
    const auto textColor = detail::readColor(f, "textColor");
    const auto focused = detail::readColor(f, "focusedBackground");
    const auto layout = detail::readTextLayout(f);
    if (f.failed()) {
        return nullptr;
    }
    auto node = std::make_unique<ButtonNode>(std::move(id), text.value_or(std::string{}));
    if (fontSize) {
        node->setFontSize(*fontSize);
    }
    if (textColor) {
        node->setTextColor(*textColor);
    }
    if (focused) {
        node->setFocusedBackground(*focused);
    }
    if (layout) {
        node->setTextLayout(*layout);
    }
    return node;
}

[[nodiscard]] std::unique_ptr<Node> makeImage(Fields& f, std::string id) {
    auto source = f.asset("source");
    const auto size = detail::readNaturalSize(f);
    if (f.failed()) {
        return nullptr;
    }
    return std::make_unique<ImageNode>(std::move(id), std::move(*source), size.value_or(Size{}));
}

[[nodiscard]] std::unique_ptr<Node> makeVideo(Fields& f, std::string id) {
    auto source = f.asset("source");
    const auto size = detail::readNaturalSize(f);
    const auto playback = f.choice("playback", names::kPlayback, false);
    const auto loop = f.boolean("loop");
    const auto volume = f.number("volume", Rule::Unit);
    if (f.failed()) {
        return nullptr;
    }
    auto node = std::make_unique<VideoNode>(std::move(id), std::move(*source), size.value_or(Size{}));
    if (playback) {
        node->setPlaybackState(*playback);
    }
    if (loop) {
        node->setLoop(*loop);
    }
    if (volume) {
        node->setVolume(*volume);
    }
    return node;
}

[[nodiscard]] std::unique_ptr<Node> makeToggle(Fields& f, std::string id) {
    const auto value = f.boolean("value");
    if (f.failed()) {
        return nullptr;
    }
    return std::make_unique<ToggleNode>(std::move(id), value.value_or(false));
}

[[nodiscard]] std::unique_ptr<Node> makeSlider(Fields& f, std::string id) {
    const auto minimum = requiredNumber(f, "min", Rule::Finite);
    const auto maximum = requiredNumber(f, "max", Rule::Finite);
    const auto step = f.number("step", Rule::Finite);
    const auto value = f.number("value", Rule::Finite);
    if (minimum && maximum && *minimum >= *maximum) {
        f.error(ErrorCode::InvalidRange, "max", *f.find("max"), "max must be greater than min");
    }
    if (step && *step <= 0.0f) {
        f.error(ErrorCode::InvalidStep, "step", *f.find("step"), "step must be greater than zero");
    }
    if (f.failed()) {
        return nullptr;
    }
    if (!detail::sliderRangeFits(*minimum, *maximum)) {
        f.error(ErrorCode::InvalidRange, "max", *f.find("max"), "the range between min and max is too large");
        return nullptr;
    }
    const float stepValue = step.value_or(names::kDefaultSliderStep);
    const bool explicitStep = step.has_value();
    const char* const stepKey = explicitStep ? "step" : "max";
    if (static_cast<double>(stepValue) > static_cast<double>(*maximum) - static_cast<double>(*minimum)) {
        f.error(
            ErrorCode::InvalidStep,
            stepKey,
            *f.find(stepKey),
            explicitStep ? "step is larger than the range" : "the default step of 1 is larger than the range");
        return nullptr;
    }
    const float initial = value.value_or(*minimum);
    if (initial < *minimum || initial > *maximum) {
        f.error(ErrorCode::ValueOutOfRange, "value", *f.find("value"), "value is outside [min, max]");
        return nullptr;
    }
    auto node = std::make_unique<SliderNode>(std::move(id), initial, *minimum, *maximum, stepValue);
    if (!detail::sliderSettles(node->value(), *minimum, *maximum, stepValue)) {
        f.error(ErrorCode::InvalidStep, stepKey, *f.find(stepKey), "step is too fine for the precision of the range");
        return nullptr;
    }
    return node;
}

[[nodiscard]] std::unique_ptr<Node> makeStepper(Fields& f, std::string id) {
    std::vector<std::string> options;
    if (const json::Value* list = f.container("options", json::Kind::Array)) {
        const std::string base = detail::childPath(f.path(), "options");
        if (list->items.empty()) {
            f.error(ErrorCode::InvalidValue, "options", *list, "options must not be empty");
        }
        for (std::size_t index = 0; index < list->items.size(); ++index) {
            const json::Value& item = list->items[index];
            if (item.kind != json::Kind::String) {
                f.context().report(ErrorCode::TypeMismatch, detail::indexPath(base, index), item.pos, "expected a string");
                f.fail();
            } else {
                options.push_back(item.text);
            }
        }
    } else if (f.find("options") == nullptr) {
        f.missing("options");
    }
    const auto index = f.integer("index", 0, std::numeric_limits<std::int32_t>::max());
    if (f.failed()) {
        return nullptr;
    }
    if (static_cast<std::size_t>(index.value_or(0)) >= options.size()) {
        f.error(ErrorCode::ValueOutOfRange, "index", *f.find("index"), "index is not a valid option position");
        return nullptr;
    }
    return std::make_unique<StepperNode>(std::move(id), std::move(options), static_cast<std::size_t>(index.value_or(0)));
}

[[nodiscard]] std::unique_ptr<Node> makeTextInput(Fields& f, std::string id) {
    auto value = f.string("value", false);
    const auto maxBytes = f.integer("maxBytes", 1, static_cast<std::int64_t>(kMaxTextInputBytes));
    if (f.failed()) {
        return nullptr;
    }
    const auto budget = static_cast<std::size_t>(maxBytes.value_or(static_cast<std::int64_t>(names::kDefaultTextInputBytes)));
    if (value && value->size() > budget) {
        f.error(ErrorCode::ValueOutOfRange, "value", *f.find("value"), "value is longer than maxBytes");
        return nullptr;
    }
    return std::make_unique<TextInputNode>(std::move(id), value.value_or(std::string{}), budget);
}

[[nodiscard]] std::unique_ptr<Node> makeKind(Kind kind, Fields& f, std::string id) {
    switch (kind) {
    case Kind::Panel: return f.failed() ? nullptr : std::make_unique<PanelNode>(std::move(id));
    case Kind::Stack: return makeStack(f, std::move(id));
    case Kind::Text: return makeText(f, std::move(id));
    case Kind::Button: return makeButton(f, std::move(id));
    case Kind::Image: return makeImage(f, std::move(id));
    case Kind::Video: return makeVideo(f, std::move(id));
    case Kind::Toggle: return makeToggle(f, std::move(id));
    case Kind::Slider: return makeSlider(f, std::move(id));
    case Kind::Stepper: return makeStepper(f, std::move(id));
    case Kind::TextInput: return makeTextInput(f, std::move(id));
    }
    return nullptr;
}

[[nodiscard]] std::unique_ptr<Node> buildNode(Context& ctx, const json::Value& value, const std::string& path);

[[nodiscard]] std::unique_ptr<Node> buildFrom(
    Context& ctx, Fields& f, Kind kind, std::span<const std::string_view> headerKeys) {
    if (++ctx.nodeCount > ctx.limits.maxNodes) {
        f.errorHere(ErrorCode::LimitNodes, "node count exceeds the limit");
        ctx.stop = true;
        return nullptr;
    }
    f.rejectUnknown({headerKeys, kCommonKeys, kindKeys(kind)});
    const auto id = readId(f);
    auto node = makeKind(kind, f, id.value_or(std::string{}));
    const auto style = detail::readStyle(f);
    const auto visible = f.boolean("visible");
    const auto enabled = f.boolean("enabled");

    std::vector<std::unique_ptr<Node>> children;
    if (const json::Value* list = f.find("children")) {
        if (kind != Kind::Panel && kind != Kind::Stack) {
            f.error(ErrorCode::ChildrenNotAllowed, "children", *list, "this node type cannot have children");
        } else if (f.container("children", json::Kind::Array) != nullptr) {
            const std::string base = detail::childPath(f.path(), "children");
            for (std::size_t index = 0; index < list->items.size() && !ctx.stop; ++index) {
                if (auto child = buildNode(ctx, list->items[index], detail::indexPath(base, index))) {
                    children.push_back(std::move(child));
                } else {
                    f.fail();
                }
            }
        }
    }
    if (f.failed() || !node) {
        return nullptr;
    }

    if (style) {
        detail::applyStyle(*style, node->style());
    }
    if (visible) {
        node->setVisible(*visible);
    }
    if (enabled) {
        node->setEnabled(*enabled);
    }
    for (auto& child : children) {
        node->addChild(std::move(child));
    }
    return node;
}

[[nodiscard]] std::unique_ptr<Node> buildNode(Context& ctx, const json::Value& value, const std::string& path) {
    Fields f(ctx, value, path);
    if (f.failed()) {
        return nullptr;
    }
    const auto typeName = f.string("type", true);
    if (!typeName) {
        return nullptr;
    }
    const auto kind = names::fromName(names::kKinds, *typeName);
    if (!kind) {
        f.error(ErrorCode::UnsupportedType, "type", *f.find("type"), "unsupported node type \"" + *typeName + "\"");
        return nullptr;
    }
    return buildFrom(ctx, f, *kind, kHeaderKeys);
}

} // namespace

ParseResult parseDocument(std::string_view text, const Limits& limits, const TextMeasurer* textMeasurer) {
    ParseResult result;
    auto read = json::readDocument(text, limits);
    if (read.error) {
        result.diagnostics.push_back(std::move(*read.error));
        return result;
    }

    Context ctx(limits, result.diagnostics);
    Fields top(ctx, read.root, {});
    if (top.failed()) {
        return result;
    }

    const auto format = top.string("format", true);
    if (!format) {
        return result;
    }
    if (*format != kFormatId) {
        top.error(ErrorCode::UnsupportedFormat, "format", *top.find("format"), "unsupported format identifier");
        return result;
    }
    const json::Value* versionValue = top.find("version");
    if (versionValue == nullptr) {
        top.missing("version");
        return result;
    }
    const auto version = detail::readInteger(
        ctx, *versionValue, "/version", std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());
    if (!version) {
        return result;
    }
    if (*version < 1) {
        top.error(ErrorCode::InvalidValue, "version", *versionValue, "version must be 1 or greater");
        return result;
    }
    if (*version > kFormatVersion) {
        top.error(ErrorCode::UnsupportedVersion, "version", *versionValue, "document version is newer than this reader");
        return result;
    }

    auto root = buildFrom(ctx, top, Kind::Panel, kRootKeys);
    if (result.diagnostics.empty() && root) {
        result.document = std::make_unique<Document>(std::move(root), textMeasurer);
    }
    return result;
}

} // namespace uirepo::declarative
