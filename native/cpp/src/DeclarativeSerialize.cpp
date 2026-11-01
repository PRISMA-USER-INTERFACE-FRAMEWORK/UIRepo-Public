#include "uirepo/DeclarativeUi.h"

#include "DeclarativeFields.h"
#include "StyleValidation.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace uirepo::declarative {
namespace {

using detail::childPath;
using detail::indexPath;
using detail::sliderRangeFits;
using detail::sliderSettles;
using names::Kind;

class Writer {
public:
    [[nodiscard]] const std::string& text() const noexcept { return out_; }
    [[nodiscard]] std::size_t maxDepth() const noexcept { return maxDepth_; }

    void beginObject() { open('{'); }
    void endObject() { close('}'); }
    void beginArray() { open('['); }
    void endArray() { close(']'); }

    void key(std::string_view name) {
        separate();
        quote(name);
        out_ += ':';
        afterKey_ = true;
    }

    void string(std::string_view value) {
        separate();
        quote(value);
    }

    void boolean(bool value) {
        separate();
        out_ += value ? "true" : "false";
    }

    void integer(std::int64_t value) {
        separate();
        append(value);
    }

    void number(float value) {
        separate();
        append(value == 0.0f ? 0.0f : value);
    }

    void percent(float fraction) {
        separate();
        out_ += detail::percentToken(fraction);
    }

    void color(Color value) {
        static constexpr char kDigits[] = "0123456789abcdef";
        separate();
        out_ += "\"#";
        const std::uint8_t channels[4] = {value.r, value.g, value.b, value.a};
        for (std::size_t index = 0; index < (value.a == 255 ? 3u : 4u); ++index) {
            out_ += kDigits[channels[index] >> 4];
            out_ += kDigits[channels[index] & 0x0F];
        }
        out_ += '"';
    }

private:
    void open(char bracket) {
        separate();
        out_ += bracket;
        needComma_.push_back(false);
        maxDepth_ = std::max(maxDepth_, needComma_.size());
    }

    void close(char bracket) {
        needComma_.pop_back();
        out_ += bracket;
    }

    void separate() {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!needComma_.empty()) {
            if (needComma_.back()) {
                out_ += ',';
            }
            needComma_.back() = true;
        }
    }

    template <typename T>
    void append(T value) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        out_.append(buffer, result.ptr);
    }

    void quote(std::string_view value) {
        static constexpr char kDigits[] = "0123456789abcdef";
        out_ += '"';
        for (const char c : value) {
            const auto byte = static_cast<unsigned char>(c);
            switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\b': out_ += "\\b"; break;
            case '\f': out_ += "\\f"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (byte < 0x20) {
                    out_ += "\\u00";
                    out_ += kDigits[byte >> 4];
                    out_ += kDigits[byte & 0x0F];
                } else {
                    out_ += c;
                }
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<bool> needComma_;
    std::size_t maxDepth_{};
    bool afterKey_{};
};

[[nodiscard]] bool sameColor(Color a, Color b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

[[nodiscard]] bool sameLength(Length a, Length b) noexcept {
    return a.kind == b.kind && (a.kind == LengthKind::Auto || a.value == b.value);
}

[[nodiscard]] bool sameThickness(Thickness a, Thickness b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

[[nodiscard]] std::optional<Kind> kindOf(const Node& node) noexcept {
    if (dynamic_cast<const PanelNode*>(&node)) return Kind::Panel;
    if (dynamic_cast<const StackNode*>(&node)) return Kind::Stack;
    if (dynamic_cast<const TextNode*>(&node)) return Kind::Text;
    if (dynamic_cast<const ButtonNode*>(&node)) return Kind::Button;
    if (dynamic_cast<const ImageNode*>(&node)) return Kind::Image;
    if (dynamic_cast<const VideoNode*>(&node)) return Kind::Video;
    if (dynamic_cast<const ToggleNode*>(&node)) return Kind::Toggle;
    if (dynamic_cast<const SliderNode*>(&node)) return Kind::Slider;
    if (dynamic_cast<const StepperNode*>(&node)) return Kind::Stepper;
    if (dynamic_cast<const TextInputNode*>(&node)) return Kind::TextInput;
    return std::nullopt;
}

[[nodiscard]] std::unique_ptr<Node> makeDefaultNode(Kind kind) {
    switch (kind) {
    case Kind::Panel: return std::make_unique<PanelNode>("d");
    case Kind::Stack: return std::make_unique<StackNode>("d", Axis::Row);
    case Kind::Text: return std::make_unique<TextNode>("d", "");
    case Kind::Button: return std::make_unique<ButtonNode>("d", "");
    case Kind::Image: return std::make_unique<ImageNode>("d", "d", Size{});
    case Kind::Video: return std::make_unique<VideoNode>("d", "d", Size{});
    case Kind::Toggle: return std::make_unique<ToggleNode>("d");
    case Kind::Slider: return std::make_unique<SliderNode>("d", 0.0f, 0.0f, 1.0f);
    case Kind::Stepper: return std::make_unique<StepperNode>("d", std::vector<std::string>{"d"});
    case Kind::TextInput: return std::make_unique<TextInputNode>("d");
    }
    return nullptr;
}

class Walk {
public:
    Walk(const Limits& limits, std::vector<Diagnostic>& sink)
        : limits_(limits), sink_(sink) {}

    void run(const Document& document) {
        writer_.beginObject();
        writer_.key("format");
        writer_.string(kFormatId);
        writer_.key("version");
        writer_.integer(kFormatVersion);
        node(document.root(), {}, 0, true);
        writer_.endObject();
        if (writer_.maxDepth() > std::min(limits_.maxDepth, kHardMaxDepth)) {
            report(ErrorCode::LimitDepth, {}, "output nesting exceeds the depth limit");
        }
        if (writer_.text().size() > limits_.maxBytes) {
            report(ErrorCode::LimitSize, {}, "output exceeds the byte limit");
        }
    }

    [[nodiscard]] std::string take() { return std::string(writer_.text()); }

private:
    void report(ErrorCode code, const std::string& path, std::string message) {
        if (sink_.size() < std::max<std::size_t>(1, limits_.maxDiagnostics)) {
            sink_.push_back(Diagnostic{code, path, 0, 0, std::move(message)});
        }
        stop_ = stop_ || sink_.size() >= std::max<std::size_t>(1, limits_.maxDiagnostics);
    }

    [[nodiscard]] std::string_view checked(std::string_view value, const std::string& path) {
        for (std::size_t at = 0; at < value.size();) {
            const std::size_t length = json::utf8Sequence(value, at);
            if (length == 0) {
                report(ErrorCode::InvalidUtf8, path, "string is not valid UTF-8");
                return {};
            }
            if (value[at] == '\0') {
                report(ErrorCode::InvalidString, path, "NUL is not allowed in strings");
                return {};
            }
            at += length;
        }
        return value;
    }

    void writeColor(std::string_view key, Color value, Color defaultValue) {
        if (!sameColor(value, defaultValue)) {
            writer_.key(key);
            writer_.color(value);
        }
    }

    void writeFloat(std::string_view key, float value, float defaultValue) {
        if (value != defaultValue) {
            writer_.key(key);
            writer_.number(value);
        }
    }

    void writeThickness(Thickness value) {
        if (value.left == value.top && value.top == value.right && value.right == value.bottom) {
            writer_.number(value.left);
            return;
        }
        writer_.beginArray();
        writer_.number(value.left);
        writer_.number(value.top);
        writer_.number(value.right);
        writer_.number(value.bottom);
        writer_.endArray();
    }

    void writeLength(Length value) {
        if (value.kind == LengthKind::Auto) {
            writer_.string("auto");
        } else if (value.kind == LengthKind::Pixels) {
            writer_.number(value.value);
        } else {
            writer_.beginObject();
            writer_.key("percent");
            writer_.percent(value.value);
            writer_.endObject();
        }
    }

    void writeTextLayout(TextLayoutOptions value) {
        const TextLayoutOptions defaults{};
        if (value == defaults) {
            return;
        }
        writer_.key("textLayout");
        writer_.beginObject();
        if (value.wrapping != defaults.wrapping) {
            writer_.key("wrapping");
            writer_.string(names::toName(names::kWrapping, value.wrapping));
        }
        if (value.alignment != defaults.alignment) {
            writer_.key("alignment");
            writer_.string(names::toName(names::kTextAlignments, value.alignment));
        }
        if (value.overflow != defaults.overflow) {
            writer_.key("overflow");
            writer_.string(names::toName(names::kOverflow, value.overflow));
        }
        writer_.endObject();
    }

    void writeSize(std::string_view key, Size value, const std::string& path) {
        if (!std::isfinite(value.width) || !std::isfinite(value.height) || value.width < 0.0f || value.height < 0.0f) {
            report(ErrorCode::InvalidValue, childPath(path, key), "natural size must be finite and non-negative");
            return;
        }
        if (value.width == 0.0f && value.height == 0.0f) {
            return;
        }
        writer_.key(key);
        writer_.beginArray();
        writer_.number(value.width);
        writer_.number(value.height);
        writer_.endArray();
    }

    void writeStyle(const Node& node, const Style& defaults, const std::string& path) {
        const Style& style = node.style();
        try {
            style_validation::validate(style);
        } catch (const std::invalid_argument& error) {
            report(ErrorCode::InvalidValue, childPath(path, "style"), error.what());
            return;
        }

        bool open = false;
        const auto field = [&](std::string_view key) {
            if (!open) {
                writer_.key("style");
                writer_.beginObject();
                open = true;
            }
            writer_.key(key);
        };
        const auto lengthField = [&](std::string_view key, Length value, Length defaultValue) {
            if (!sameLength(value, defaultValue)) {
                field(key);
                writeLength(value);
            }
        };
        const auto floatField = [&](std::string_view key, float value, float defaultValue) {
            if (value != defaultValue) {
                field(key);
                writer_.number(value);
            }
        };
        const auto boundField = [&](std::string_view key, float value) {
            if (value > 0.0f && std::isfinite(value)) {
                field(key);
                writer_.number(value);
            }
        };
        const auto thicknessField = [&](std::string_view key, Thickness value, Thickness defaultValue) {
            if (!sameThickness(value, defaultValue)) {
                field(key);
                writeThickness(value);
            }
        };

        lengthField("width", style.width, defaults.width);
        lengthField("height", style.height, defaults.height);
        floatField("minWidth", style.minWidth, defaults.minWidth);
        floatField("minHeight", style.minHeight, defaults.minHeight);
        boundField("maxWidth", style.maxWidth);
        boundField("maxHeight", style.maxHeight);
        thicknessField("padding", style.padding, defaults.padding);
        thicknessField("margin", style.margin, defaults.margin);
        floatField("gap", style.gap, defaults.gap);
        floatField("flexGrow", style.flexGrow, defaults.flexGrow);
        if (style.alignItems != defaults.alignItems) {
            field("alignItems");
            writer_.string(names::toName(names::kAligns, style.alignItems));
        }
        if (!sameColor(style.background, defaults.background)) {
            field("background");
            writer_.color(style.background);
        }
        floatField("opacity", style.opacity, defaults.opacity);
        if (style.zIndex != defaults.zIndex) {
            field("zIndex");
            writer_.integer(style.zIndex);
        }
        floatField("cornerRadius", style.cornerRadius, defaults.cornerRadius);
        if (style.clipToBounds != defaults.clipToBounds) {
            field("clipToBounds");
            writer_.boolean(style.clipToBounds);
        }
        if (open) {
            writer_.endObject();
        }
    }

    void writeKindFields(Kind kind, const Node& node, const std::string& path) {
        switch (kind) {
        case Kind::Panel:
            break;
        case Kind::Stack: {
            const auto& stack = static_cast<const StackNode&>(node);
            writer_.key("axis");
            writer_.string(names::toName(names::kAxes, stack.axis()));
            if (stack.scrollable()) {
                writer_.key("scrollable");
                writer_.boolean(true);
            }
            break;
        }
        case Kind::Text: {
            const auto& text = static_cast<const TextNode&>(node);
            const TextNode defaults{"d", ""};
            if (!text.text().empty()) {
                writer_.key("text");
                writer_.string(checked(text.text(), childPath(path, "text")));
            }
            writeFloat("fontSize", text.fontSize(), defaults.fontSize());
            writeColor("color", text.color(), defaults.color());
            writeTextLayout(text.textLayout());
            break;
        }
        case Kind::Button: {
            const auto& button = static_cast<const ButtonNode&>(node);
            const ButtonNode defaults{"d", ""};
            if (!button.text().empty()) {
                writer_.key("text");
                writer_.string(checked(button.text(), childPath(path, "text")));
            }
            writeFloat("fontSize", button.fontSize(), defaults.fontSize());
            writeColor("textColor", button.textColor(), defaults.textColor());
            writeColor("focusedBackground", button.focusedBackground(), defaults.focusedBackground());
            writeTextLayout(button.textLayout());
            break;
        }
        case Kind::Image: {
            const auto& image = static_cast<const ImageNode&>(node);
            writeSource(image.source(), path);
            writeSize("naturalSize", image.naturalSize(), path);
            break;
        }
        case Kind::Video: {
            const auto& video = static_cast<const VideoNode&>(node);
            writeSource(video.source(), path);
            writeSize("naturalSize", video.naturalSize(), path);
            if (video.playbackState() != VideoPlaybackState::Stopped) {
                writer_.key("playback");
                writer_.string(names::toName(names::kPlayback, video.playbackState()));
            }
            if (video.loop()) {
                writer_.key("loop");
                writer_.boolean(true);
            }
            writeFloat("volume", video.volume(), 1.0f);
            break;
        }
        case Kind::Toggle:
            if (static_cast<const ToggleNode&>(node).value()) {
                writer_.key("value");
                writer_.boolean(true);
            }
            break;
        case Kind::Slider: {
            const auto& slider = static_cast<const SliderNode&>(node);
            writer_.key("min");
            writer_.number(slider.minimum());
            writer_.key("max");
            writer_.number(slider.maximum());
            if (!sliderRangeFits(slider.minimum(), slider.maximum())) {
                report(ErrorCode::InvalidRange, childPath(path, "max"), "the range between min and max is too large");
            } else if (static_cast<double>(slider.step()) > static_cast<double>(slider.maximum()) - static_cast<double>(slider.minimum())) {
                report(
                    ErrorCode::InvalidStep,
                    childPath(path, "step"),
                    slider.step() == names::kDefaultSliderStep ? "the default step of 1 is larger than the range"
                                                              : "step is larger than the range");
            } else if (!sliderSettles(slider.value(), slider.minimum(), slider.maximum(), slider.step())) {
                report(ErrorCode::InvalidStep, childPath(path, "step"), "step is too fine for the precision of the range");
            }
            writeFloat("step", slider.step(), names::kDefaultSliderStep);
            writeFloat("value", slider.value(), slider.minimum());
            break;
        }
        case Kind::Stepper: {
            const auto& stepper = static_cast<const StepperNode&>(node);
            if (stepper.options().empty()) {
                report(ErrorCode::InvalidValue, childPath(path, "options"), "stepper has no options");
                break;
            }
            writer_.key("options");
            writer_.beginArray();
            for (std::size_t index = 0; index < stepper.options().size(); ++index) {
                writer_.string(checked(stepper.options()[index], indexPath(childPath(path, "options"), index)));
            }
            writer_.endArray();
            if (stepper.index() != 0) {
                writer_.key("index");
                writer_.integer(static_cast<std::int64_t>(stepper.index()));
            }
            break;
        }
        case Kind::TextInput: {
            const auto& input = static_cast<const TextInputNode&>(node);
            if (!input.value().empty()) {
                writer_.key("value");
                writer_.string(checked(input.value(), childPath(path, "value")));
            }
            if (input.maxBytes() > kMaxTextInputBytes) {
                report(ErrorCode::InvalidValue, childPath(path, "maxBytes"), "maxBytes exceeds the format limit");
            } else if (input.maxBytes() != names::kDefaultTextInputBytes) {
                writer_.key("maxBytes");
                writer_.integer(static_cast<std::int64_t>(input.maxBytes()));
            }
            break;
        }
        }
    }

    void writeSource(const std::string& source, const std::string& path) {
        writer_.key("source");
        if (!names::validAsset(source)) {
            report(ErrorCode::InvalidValue, childPath(path, "source"), "asset reference is not representable");
            writer_.string("");
            return;
        }
        writer_.string(checked(source, childPath(path, "source")));
    }

    void node(const Node& value, const std::string& path, std::size_t level, bool isRoot) {
        if (++nodes_ > limits_.maxNodes) {
            report(ErrorCode::LimitNodes, path, "node count exceeds the limit");
            stop_ = true;
            return;
        }
        if (2 * level + 1 > std::min(limits_.maxDepth, kHardMaxDepth)) {
            report(ErrorCode::LimitDepth, path, "tree nesting exceeds the depth limit");
            stop_ = true;
            return;
        }
        const auto kind = kindOf(value);
        if (!kind || (isRoot && *kind != Kind::Panel)) {
            report(ErrorCode::UnsupportedType, path, "node type has no declarative representation");
            return;
        }

        if (!isRoot) {
            writer_.beginObject();
            writer_.key("type");
            writer_.string(names::toName(names::kKinds, *kind));
        }
        writer_.key("id");
        if (!names::validId(value.id())) {
            report(ErrorCode::InvalidId, childPath(path, "id"), "id is not valid in the declarative format");
            writer_.string("");
        } else {
            writer_.string(value.id());
        }

        writeKindFields(*kind, value, path);
        writeStyle(value, makeDefaultNode(*kind)->style(), path);
        if (!value.visible()) {
            writer_.key("visible");
            writer_.boolean(false);
        }
        if (!value.enabled()) {
            writer_.key("enabled");
            writer_.boolean(false);
        }

        if (!value.children().empty()) {
            if (*kind != Kind::Panel && *kind != Kind::Stack) {
                report(ErrorCode::ChildrenNotAllowed, childPath(path, "children"), "this node type cannot have children");
            } else {
                const std::string base = childPath(path, "children");
                writer_.key("children");
                writer_.beginArray();
                for (std::size_t index = 0; index < value.children().size() && !stop_; ++index) {
                    node(*value.children()[index], indexPath(base, index), level + 1, false);
                }
                writer_.endArray();
            }
        }
        if (!isRoot) {
            writer_.endObject();
        }
    }

    const Limits& limits_;
    std::vector<Diagnostic>& sink_;
    Writer writer_;
    std::size_t nodes_{};
    bool stop_{};
};

} // namespace

SerializeResult serializeDocument(const Document& document, const Limits& limits) {
    SerializeResult result;
    Walk walk(limits, result.diagnostics);
    walk.run(document);
    if (result.diagnostics.empty()) {
        result.text = walk.take();
    }
    return result;
}

} // namespace uirepo::declarative
