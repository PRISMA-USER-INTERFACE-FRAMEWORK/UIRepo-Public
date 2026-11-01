#include "DeclarativeJson.h"

#include "DeclarativeNames.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace uirepo::declarative::json {
namespace {

[[nodiscard]] bool isContinuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

[[nodiscard]] int hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void appendUtf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

class Reader {
public:
    Reader(std::string_view text, const Limits& limits)
        : text_(text), maxBytes_(limits.maxBytes), maxDepth_(std::min(limits.maxDepth, kHardMaxDepth)) {}

    [[nodiscard]] ReadResult read() {
        ReadResult result;
        if (text_.size() > maxBytes_) {
            fail(ErrorCode::LimitSize, {}, "document exceeds the byte limit");
        } else if (text_.starts_with("\xEF\xBB\xBF")) {
            fail(ErrorCode::SyntaxError, {}, "a byte order mark is not allowed");
        } else {
            skipWhitespace();
            if (atEnd()) {
                fail(ErrorCode::SyntaxError, cur_, "empty document");
            } else if (value(result.root, 0)) {
                skipWhitespace();
                if (!atEnd()) {
                    fail(ErrorCode::SyntaxError, cur_, "unexpected content after the document");
                }
            }
        }
        if (failed_) {
            std::string path;
            for (auto it = path_.rbegin(); it != path_.rend(); ++it) {
                path += '/';
                path += *it;
            }
            error_.path = std::move(path);
            result.error = std::move(error_);
        }
        return result;
    }

private:
    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return text_[pos_]; }

    void bump() noexcept {
        const char c = text_[pos_++];
        constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
        if (c == '\n') {
            cur_.line += cur_.line != kMax ? 1 : 0;
            cur_.column = 1;
        } else if (!isContinuation(c)) {
            cur_.column += cur_.column != kMax ? 1 : 0;
        }
    }

    void skipWhitespace() noexcept {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
            bump();
        }
    }

    bool fail(ErrorCode code, Position at, std::string_view message) {
        failed_ = true;
        error_.code = code;
        error_.line = at.line;
        error_.column = at.column;
        error_.message = std::string(message);
        return false;
    }

    bool value(Value& out, std::size_t depth) {
        out.pos = cur_;
        if (atEnd()) {
            return fail(ErrorCode::SyntaxError, cur_, "unexpected end of document");
        }
        const char c = peek();
        switch (c) {
        case '{':
            return object(out, depth);
        case '[':
            return array(out, depth);
        case '"':
            out.kind = Kind::String;
            return string(out.text);
        case 't':
            return literal("true", out, Kind::Bool, true);
        case 'f':
            return literal("false", out, Kind::Bool, false);
        case 'n':
            return literal("null", out, Kind::Null, false);
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return number(out);
            }
            return fail(ErrorCode::SyntaxError, cur_, "expected a value");
        }
    }

    bool literal(std::string_view word, Value& out, Kind kind, bool flag) {
        if (text_.compare(pos_, word.size(), word) != 0) {
            return fail(ErrorCode::SyntaxError, cur_, "expected a value");
        }
        for (std::size_t index = 0; index < word.size(); ++index) {
            bump();
        }
        out.kind = kind;
        out.boolean = flag;
        return true;
    }

    bool number(Value& out) {
        const Position start = cur_;
        const std::size_t first = pos_;
        const auto digits = [this]() {
            std::size_t count = 0;
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                bump();
                ++count;
            }
            return count;
        };

        bool integer = true;
        if (peek() == '-') {
            bump();
        }
        if (atEnd() || peek() < '0' || peek() > '9') {
            return fail(ErrorCode::SyntaxError, start, "invalid number");
        }
        if (peek() == '0') {
            bump();
            if (!atEnd() && peek() >= '0' && peek() <= '9') {
                return fail(ErrorCode::SyntaxError, start, "leading zeros are not allowed");
            }
        } else {
            digits();
        }
        if (!atEnd() && peek() == '.') {
            integer = false;
            bump();
            if (digits() == 0) {
                return fail(ErrorCode::SyntaxError, start, "invalid number");
            }
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            integer = false;
            bump();
            if (!atEnd() && (peek() == '+' || peek() == '-')) {
                bump();
            }
            if (digits() == 0) {
                return fail(ErrorCode::SyntaxError, start, "invalid number");
            }
        }
        out.kind = Kind::Number;
        out.integerToken = integer;
        out.number = text_.substr(first, pos_ - first);
        return true;
    }

    bool string(std::string& out) {
        const Position start = cur_;
        bump();
        for (;;) {
            if (atEnd()) {
                return fail(ErrorCode::SyntaxError, start, "unterminated string");
            }
            const char c = peek();
            const auto byte = static_cast<unsigned char>(c);
            if (c == '"') {
                bump();
                return true;
            }
            if (byte < 0x20) {
                return fail(ErrorCode::InvalidString, cur_, "control character in string");
            }
            if (c == '\\') {
                if (!escape(out)) {
                    return false;
                }
            } else if (byte < 0x80) {
                out += c;
                bump();
            } else {
                const std::size_t length = utf8Sequence(text_, pos_);
                if (length == 0) {
                    return fail(ErrorCode::InvalidUtf8, cur_, "malformed UTF-8");
                }
                out.append(text_, pos_, length);
                for (std::size_t index = 0; index < length; ++index) {
                    bump();
                }
            }
        }
    }

    [[nodiscard]] std::optional<std::uint32_t> hex4() {
        if (text_.size() - pos_ < 4) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const int digit = hexDigit(text_[pos_ + index]);
            if (digit < 0) {
                return std::nullopt;
            }
            value = value * 16 + static_cast<std::uint32_t>(digit);
        }
        for (std::size_t index = 0; index < 4; ++index) {
            bump();
        }
        return value;
    }

    bool escape(std::string& out) {
        const Position start = cur_;
        bump();
        if (atEnd()) {
            return fail(ErrorCode::SyntaxError, start, "unterminated string");
        }
        const char c = peek();
        bump();
        switch (c) {
        case '"': out += '"'; return true;
        case '\\': out += '\\'; return true;
        case '/': out += '/'; return true;
        case 'b': out += '\b'; return true;
        case 'f': out += '\f'; return true;
        case 'n': out += '\n'; return true;
        case 'r': out += '\r'; return true;
        case 't': out += '\t'; return true;
        case 'u': break;
        default:
            return fail(ErrorCode::InvalidString, start, "invalid escape sequence");
        }

        auto unit = hex4();
        if (!unit) {
            return fail(ErrorCode::InvalidString, start, "invalid \\u escape");
        }
        std::uint32_t codepoint = *unit;
        if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            return fail(ErrorCode::InvalidString, start, "unpaired surrogate escape");
        }
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (text_.compare(pos_, 2, "\\u") != 0) {
                return fail(ErrorCode::InvalidString, start, "unpaired surrogate escape");
            }
            bump();
            bump();
            const auto low = hex4();
            if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                return fail(ErrorCode::InvalidString, start, "unpaired surrogate escape");
            }
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*low - 0xDC00);
        }
        if (codepoint == 0) {
            return fail(ErrorCode::InvalidString, start, "NUL is not allowed in strings");
        }
        appendUtf8(out, codepoint);
        return true;
    }

    bool array(Value& out, std::size_t depth) {
        if (depth + 1 > maxDepth_) {
            return fail(ErrorCode::LimitDepth, cur_, "nesting exceeds the depth limit");
        }
        out.kind = Kind::Array;
        bump();
        skipWhitespace();
        if (!atEnd() && peek() == ']') {
            bump();
            return true;
        }
        for (;;) {
            out.items.emplace_back();
            if (!value(out.items.back(), depth + 1)) {
                path_.push_back(std::to_string(out.items.size() - 1));
                return false;
            }
            skipWhitespace();
            if (atEnd()) {
                return fail(ErrorCode::SyntaxError, cur_, "unexpected end of document");
            }
            if (peek() == ',') {
                bump();
                skipWhitespace();
                continue;
            }
            if (peek() == ']') {
                bump();
                return true;
            }
            return fail(ErrorCode::SyntaxError, cur_, "expected ',' or ']'");
        }
    }

    bool object(Value& out, std::size_t depth) {
        if (depth + 1 > maxDepth_) {
            return fail(ErrorCode::LimitDepth, cur_, "nesting exceeds the depth limit");
        }
        out.kind = Kind::Object;
        bump();
        skipWhitespace();
        if (!atEnd() && peek() == '}') {
            bump();
            return true;
        }
        std::unordered_set<std::string> seen;
        for (;;) {
            if (atEnd()) {
                return fail(ErrorCode::SyntaxError, cur_, "unexpected end of document");
            }
            if (peek() != '"') {
                return fail(ErrorCode::SyntaxError, cur_, "expected a string key");
            }
            Member& member = out.members.emplace_back();
            member.keyPos = cur_;
            if (!string(member.key)) {
                return false;
            }
            if (!seen.insert(member.key).second) {
                path_.push_back(names::pointerEscape(member.key));
                return fail(ErrorCode::DuplicateKey, member.keyPos, "duplicate object key");
            }
            skipWhitespace();
            if (atEnd() || peek() != ':') {
                return fail(ErrorCode::SyntaxError, cur_, "expected ':'");
            }
            bump();
            skipWhitespace();
            if (!value(member.value, depth + 1)) {
                path_.push_back(names::pointerEscape(member.key));
                return false;
            }
            skipWhitespace();
            if (atEnd()) {
                return fail(ErrorCode::SyntaxError, cur_, "unexpected end of document");
            }
            if (peek() == ',') {
                bump();
                skipWhitespace();
                continue;
            }
            if (peek() == '}') {
                bump();
                return true;
            }
            return fail(ErrorCode::SyntaxError, cur_, "expected ',' or '}'");
        }
    }

    std::string_view text_;
    std::size_t maxBytes_;
    std::size_t maxDepth_;
    std::size_t pos_{};
    Position cur_{};
    bool failed_{};
    Diagnostic error_{};
    std::vector<std::string> path_;
};

} // namespace

std::size_t utf8Sequence(std::string_view text, std::size_t at) noexcept {
    const auto byte = [&](std::size_t offset) noexcept {
        return at + offset < text.size() ? static_cast<unsigned char>(text[at + offset]) : 0u;
    };
    const unsigned lead = byte(0);
    const auto cont = [&](std::size_t offset, unsigned low = 0x80, unsigned high = 0xBF) noexcept {
        const unsigned value = byte(offset);
        return value >= low && value <= high;
    };
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return cont(1) ? 2 : 0;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        const unsigned low = lead == 0xE0 ? 0xA0 : 0x80;
        const unsigned high = lead == 0xED ? 0x9F : 0xBF;
        return cont(1, low, high) && cont(2) ? 3 : 0;
    }
    if (lead >= 0xF0 && lead <= 0xF4) {
        const unsigned low = lead == 0xF0 ? 0x90 : 0x80;
        const unsigned high = lead == 0xF4 ? 0x8F : 0xBF;
        return cont(1, low, high) && cont(2) && cont(3) ? 4 : 0;
    }
    return 0;
}

ReadResult readDocument(std::string_view text, const Limits& limits) {
    return Reader(text, limits).read();
}

} // namespace uirepo::declarative::json
