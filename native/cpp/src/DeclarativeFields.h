#pragma once

#include "DeclarativeJson.h"
#include "DeclarativeNames.h"

#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace uirepo::declarative::detail {

enum class Rule : std::uint8_t {
    Finite,
    NonNegative,
    Positive,
    Unit,
};

struct Context {
    Limits limits;
    std::vector<Diagnostic>& diagnostics;
    std::unordered_set<std::string> ids;
    std::size_t nodeCount{};
    bool stop{};

    Context(const Limits& limitsValue, std::vector<Diagnostic>& sink)
        : limits(limitsValue), diagnostics(sink) {}

    void report(ErrorCode code, std::string path, json::Position at, std::string message);
};

[[nodiscard]] std::string childPath(const std::string& path, std::string_view key);
[[nodiscard]] std::string indexPath(const std::string& path, std::size_t index);

// Constraint: JSON percent n is stored as the core fraction n / 100.
inline constexpr int kPercentShift = -2;

[[nodiscard]] std::optional<float> readFloat(
    Context& ctx, const json::Value& value, const std::string& path, Rule rule, int decimalShift = 0);
// Constraint: fraction must be finite and non-negative.
[[nodiscard]] std::string percentToken(float fraction);
[[nodiscard]] std::optional<std::int64_t> readInteger(
    Context& ctx, const json::Value& value, const std::string& path, std::int64_t low, std::int64_t high);

// A slider span that overflows float cannot be normalised by SliderNode.
[[nodiscard]] bool sliderRangeFits(float minimum, float maximum) noexcept;
// True when constructing a SliderNode from an already normalised value leaves it unchanged.
[[nodiscard]] bool sliderSettles(float value, float minimum, float maximum, float step);

class Fields {
public:
    Fields(Context& ctx, const json::Value& object, std::string path);

    [[nodiscard]] Context& context() noexcept { return ctx_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const json::Value& object() const noexcept { return object_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    void fail() noexcept { failed_ = true; }

    [[nodiscard]] const json::Value* find(std::string_view key) const noexcept;
    void rejectUnknown(std::initializer_list<std::span<const std::string_view>> groups);

    void error(ErrorCode code, std::string_view key, const json::Value& at, std::string message);
    void errorHere(ErrorCode code, std::string message);
    void missing(std::string_view key);

    [[nodiscard]] std::optional<bool> boolean(std::string_view key);
    [[nodiscard]] std::optional<float> number(std::string_view key, Rule rule);
    [[nodiscard]] std::optional<float> percentFraction(std::string_view key);
    [[nodiscard]] std::optional<std::int64_t> integer(std::string_view key, std::int64_t low, std::int64_t high);
    [[nodiscard]] std::optional<std::string> string(std::string_view key, bool required);
    [[nodiscard]] std::optional<std::string> asset(std::string_view key);
    [[nodiscard]] const json::Value* container(std::string_view key, json::Kind kind);

    template <typename E, std::size_t N>
    [[nodiscard]] std::optional<E> choice(std::string_view key, const names::Table<E, N>& table, bool required) {
        const json::Value* value = find(key);
        if (value == nullptr) {
            if (required) {
                missing(key);
            }
            return std::nullopt;
        }
        if (value->kind != json::Kind::String) {
            error(ErrorCode::TypeMismatch, key, *value, "expected a string");
            return std::nullopt;
        }
        if (const auto found = names::fromName(table, value->text)) {
            return found;
        }
        error(ErrorCode::InvalidEnum, key, *value, names::expectedList(table));
        return std::nullopt;
    }

private:
    Context& ctx_;
    const json::Value& object_;
    std::string path_;
    bool failed_{};
};

struct StyleOverrides {
    std::optional<Length> width;
    std::optional<Length> height;
    std::optional<float> minWidth;
    std::optional<float> minHeight;
    std::optional<float> maxWidth;
    std::optional<float> maxHeight;
    std::optional<Thickness> padding;
    std::optional<Thickness> margin;
    std::optional<float> gap;
    std::optional<float> flexGrow;
    std::optional<Align> alignItems;
    std::optional<Color> background;
    std::optional<float> opacity;
    std::optional<std::int32_t> zIndex;
    std::optional<float> cornerRadius;
    std::optional<bool> clipToBounds;
};

[[nodiscard]] std::optional<StyleOverrides> readStyle(Fields& parent);
void applyStyle(const StyleOverrides& overrides, Style& style) noexcept;
[[nodiscard]] std::optional<Color> readColor(Fields& fields, std::string_view key);
[[nodiscard]] std::optional<TextLayoutOptions> readTextLayout(Fields& fields);
[[nodiscard]] std::optional<Size> readNaturalSize(Fields& fields);

} // namespace uirepo::declarative::detail
