#include "DeclarativeFields.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace uirepo::declarative {

std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::SyntaxError: return "syntax_error";
    case ErrorCode::InvalidUtf8: return "invalid_utf8";
    case ErrorCode::InvalidString: return "invalid_string";
    case ErrorCode::DuplicateKey: return "duplicate_key";
    case ErrorCode::NumberRange: return "number_range";
    case ErrorCode::LimitSize: return "limit_size";
    case ErrorCode::LimitDepth: return "limit_depth";
    case ErrorCode::LimitNodes: return "limit_nodes";
    case ErrorCode::TypeMismatch: return "type_mismatch";
    case ErrorCode::MissingField: return "missing_field";
    case ErrorCode::UnknownField: return "unknown_field";
    case ErrorCode::UnsupportedFormat: return "unsupported_format";
    case ErrorCode::UnsupportedVersion: return "unsupported_version";
    case ErrorCode::UnsupportedType: return "unsupported_type";
    case ErrorCode::InvalidId: return "invalid_id";
    case ErrorCode::DuplicateId: return "duplicate_id";
    case ErrorCode::InvalidEnum: return "invalid_enum";
    case ErrorCode::InvalidColor: return "invalid_color";
    case ErrorCode::InvalidValue: return "invalid_value";
    case ErrorCode::InvalidRange: return "invalid_range";
    case ErrorCode::InvalidStep: return "invalid_step";
    case ErrorCode::ValueOutOfRange: return "value_out_of_range";
    case ErrorCode::ChildrenNotAllowed: return "children_not_allowed";
    }
    return "unknown";
}

} // namespace uirepo::declarative

namespace uirepo::declarative::detail {

void Context::report(ErrorCode code, std::string path, json::Position at, std::string message) {
    const std::size_t cap = std::max<std::size_t>(1, limits.maxDiagnostics);
    if (diagnostics.size() < cap) {
        diagnostics.push_back(Diagnostic{code, std::move(path), at.line, at.column, std::move(message)});
    }
    if (diagnostics.size() >= cap) {
        stop = true;
    }
}

std::string childPath(const std::string& path, std::string_view key) {
    std::string out = path;
    out += '/';
    out += names::pointerEscape(key);
    return out;
}

std::string indexPath(const std::string& path, std::size_t index) {
    return path + '/' + std::to_string(index);
}

bool sliderRangeFits(float minimum, float maximum) noexcept {
    return static_cast<double>(maximum) - static_cast<double>(minimum) <= static_cast<double>(std::numeric_limits<float>::max());
}

bool sliderSettles(float value, float minimum, float maximum, float step) {
    return SliderNode("s", value, minimum, maximum, step).value() == value;
}

namespace {

// Constraint: token[marker] is the 'e' or 'E' of a validated JSON number; the exponent is clamped to 1e12.
long long exponentAfter(std::string_view token, std::size_t marker) noexcept {
    std::size_t at = marker + 1;
    const bool negative = at < token.size() && token[at] == '-';
    at += at < token.size() && (token[at] == '-' || token[at] == '+') ? 1 : 0;
    long long exponent = 0;
    for (; at < token.size(); ++at) {
        exponent = std::min(exponent * 10 + (token[at] - '0'), 1'000'000'000'000LL);
    }
    return negative ? -exponent : exponent;
}

// The token is a validated JSON number; true when its magnitude is below one.
bool numberUnderflows(std::string_view token) noexcept {
    std::size_t at = !token.empty() && token.front() == '-' ? 1 : 0;
    long long integerDigits = 0;
    long long firstSignificant = -1;
    long long digitIndex = 0;
    bool afterPoint = false;
    for (; at < token.size() && token[at] != 'e' && token[at] != 'E'; ++at) {
        if (token[at] == '.') {
            afterPoint = true;
            continue;
        }
        integerDigits += afterPoint ? 0 : 1;
        if (firstSignificant < 0 && token[at] != '0') {
            firstSignificant = digitIndex;
        }
        ++digitIndex;
    }
    const long long exponent = at < token.size() ? exponentAfter(token, at) : 0;
    return firstSignificant >= 0 && integerDigits - 1 - firstSignificant + exponent < 0;
}

std::optional<float> floatFromToken(std::string_view token) {
    float parsed{};
    const char* const first = token.data();
    const char* const last = first + token.size();
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec == std::errc::result_out_of_range && numberUnderflows(token)) {
        return 0.0f;
    }
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

// Constraint: token must be a validated JSON number.
std::string shiftedToken(std::string_view token, int shift) {
    const std::size_t split = token.find_first_of("eE");
    const long long exponent = split != std::string_view::npos ? exponentAfter(token, split) : 0;
    return std::string(token.substr(0, split)) + 'e' + std::to_string(exponent + shift);
}

std::optional<float> fractionFromPercentToken(std::string_view token) {
    return floatFromToken(shiftedToken(token, kPercentShift));
}

// Constraint: input is std::to_chars scientific output of a positive value.
std::string positionalText(std::string_view scientific) {
    const std::size_t marker = scientific.find('e');
    std::string digits;
    for (const char c : scientific.substr(0, marker)) {
        if (c != '.') {
            digits += c;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    const int exponent = std::stoi(std::string(scientific.substr(marker + 1)));
    const int length = static_cast<int>(digits.size());
    if (exponent < -5 || exponent > 20) {
        return digits.substr(0, 1) + (length > 1 ? "." + digits.substr(1) : "") + 'e' + std::to_string(exponent);
    }
    if (exponent < 0) {
        return "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
    }
    if (length <= exponent + 1) {
        return digits + std::string(static_cast<std::size_t>(exponent + 1 - length), '0');
    }
    return digits.substr(0, static_cast<std::size_t>(exponent) + 1) + '.' + digits.substr(static_cast<std::size_t>(exponent) + 1);
}

} // namespace

std::string percentToken(float fraction) {
    if (fraction == 0.0f) {
        return "0";
    }
    const double exact = static_cast<double>(fraction) * 100.0;
    std::string text;
    for (int digits = 1; digits <= 17; ++digits) {
        char buffer[48];
        const auto end = std::to_chars(buffer, buffer + sizeof buffer, exact, std::chars_format::scientific, digits - 1).ptr;
        text = positionalText(std::string_view(buffer, static_cast<std::size_t>(end - buffer)));
        if (fractionFromPercentToken(text) == fraction) {
            break;
        }
    }
    return text;
}

std::optional<float> readFloat(
    Context& ctx, const json::Value& value, const std::string& path, Rule rule, int decimalShift) {
    if (value.kind != json::Kind::Number) {
        ctx.report(ErrorCode::TypeMismatch, path, value.pos, "expected a number");
        return std::nullopt;
    }
    const auto decoded = decimalShift == 0 ? floatFromToken(value.number)
                                           : floatFromToken(shiftedToken(value.number, decimalShift));
    if (!decoded) {
        ctx.report(
            ErrorCode::NumberRange,
            path,
            value.pos,
            decimalShift == 0 ? "number is not representable as a finite 32-bit float"
                              : "percent is too large: its fraction is not representable as a finite 32-bit float");
        return std::nullopt;
    }
    float parsed = *decoded;
    if (parsed == 0.0f) {
        parsed = 0.0f;
    }
    const char* problem = nullptr;
    switch (rule) {
    case Rule::Finite:
        break;
    case Rule::NonNegative:
        problem = parsed < 0.0f ? "must be zero or greater" : nullptr;
        break;
    case Rule::Positive:
        problem = parsed <= 0.0f ? "must be greater than zero" : nullptr;
        break;
    case Rule::Unit:
        problem = parsed < 0.0f || parsed > 1.0f ? "must be between 0 and 1" : nullptr;
        break;
    }
    if (problem != nullptr) {
        ctx.report(ErrorCode::InvalidValue, path, value.pos, problem);
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::int64_t> readInteger(
    Context& ctx, const json::Value& value, const std::string& path, std::int64_t low, std::int64_t high) {
    if (value.kind != json::Kind::Number || !value.integerToken) {
        ctx.report(ErrorCode::TypeMismatch, path, value.pos, "expected an integer");
        return std::nullopt;
    }
    std::int64_t parsed{};
    const char* const first = value.number.data();
    const char* const last = first + value.number.size();
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last) {
        ctx.report(ErrorCode::NumberRange, path, value.pos, "integer does not fit in 64 bits");
        return std::nullopt;
    }
    if (parsed < low || parsed > high) {
        ctx.report(
            ErrorCode::InvalidValue,
            path,
            value.pos,
            "must be between " + std::to_string(low) + " and " + std::to_string(high));
        return std::nullopt;
    }
    return parsed;
}

Fields::Fields(Context& ctx, const json::Value& object, std::string path)
    : ctx_(ctx), object_(object), path_(std::move(path)) {
    if (object_.kind != json::Kind::Object) {
        ctx_.report(ErrorCode::TypeMismatch, path_, object_.pos, "expected an object");
        failed_ = true;
    }
}

const json::Value* Fields::find(std::string_view key) const noexcept {
    for (const auto& member : object_.members) {
        if (member.key == key) {
            return &member.value;
        }
    }
    return nullptr;
}

void Fields::rejectUnknown(std::initializer_list<std::span<const std::string_view>> groups) {
    const auto listed = [&groups](const std::string& key) {
        return std::any_of(groups.begin(), groups.end(), [&key](std::span<const std::string_view> keys) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        });
    };
    for (const auto& member : object_.members) {
        if (member.key.starts_with("x-") || listed(member.key)) {
            continue;
        }
        ctx_.report(ErrorCode::UnknownField, childPath(path_, member.key), member.keyPos, "unknown field");
        failed_ = true;
    }
}

void Fields::error(ErrorCode code, std::string_view key, const json::Value& at, std::string message) {
    ctx_.report(code, childPath(path_, key), at.pos, std::move(message));
    failed_ = true;
}

void Fields::errorHere(ErrorCode code, std::string message) {
    ctx_.report(code, path_, object_.pos, std::move(message));
    failed_ = true;
}

void Fields::missing(std::string_view key) {
    errorHere(ErrorCode::MissingField, "missing required field \"" + std::string(key) + "\"");
}

std::optional<bool> Fields::boolean(std::string_view key) {
    const json::Value* value = find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (value->kind != json::Kind::Bool) {
        error(ErrorCode::TypeMismatch, key, *value, "expected true or false");
        return std::nullopt;
    }
    return value->boolean;
}

std::optional<float> Fields::number(std::string_view key, Rule rule) {
    const json::Value* value = find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    auto parsed = readFloat(ctx_, *value, childPath(path_, key), rule);
    failed_ = failed_ || !parsed;
    return parsed;
}

std::optional<float> Fields::percentFraction(std::string_view key) {
    const json::Value* value = find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    auto parsed = readFloat(ctx_, *value, childPath(path_, key), Rule::NonNegative, kPercentShift);
    failed_ = failed_ || !parsed;
    return parsed;
}

std::optional<std::int64_t> Fields::integer(std::string_view key, std::int64_t low, std::int64_t high) {
    const json::Value* value = find(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    auto parsed = readInteger(ctx_, *value, childPath(path_, key), low, high);
    failed_ = failed_ || !parsed;
    return parsed;
}

std::optional<std::string> Fields::string(std::string_view key, bool required) {
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
    return value->text;
}

std::optional<std::string> Fields::asset(std::string_view key) {
    auto text = string(key, true);
    if (text && !names::validAsset(*text)) {
        error(
            ErrorCode::InvalidValue,
            key,
            *find(key),
            "asset reference must be 1 to 1024 bytes without control characters");
        return std::nullopt;
    }
    return text;
}

const json::Value* Fields::container(std::string_view key, json::Kind kind) {
    const json::Value* value = find(key);
    if (value != nullptr && value->kind != kind) {
        error(
            ErrorCode::TypeMismatch,
            key,
            *value,
            kind == json::Kind::Array ? "expected an array" : "expected an object");
        return nullptr;
    }
    return value;
}

} // namespace uirepo::declarative::detail
