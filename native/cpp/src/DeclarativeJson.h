#pragma once

#include "uirepo/DeclarativeUi.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo::declarative::json {

struct Position {
    std::uint32_t line{1};
    std::uint32_t column{1};
};

enum class Kind : std::uint8_t {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

struct Member;

struct Value {
    Kind kind{Kind::Null};
    Position pos{};
    bool boolean{};
    bool integerToken{};
    std::string_view number;
    std::string text;
    std::vector<Value> items;
    std::vector<Member> members;
};

struct Member {
    std::string key;
    Position keyPos{};
    Value value;
};

struct ReadResult {
    Value root;
    std::optional<Diagnostic> error;
};

// Number slices point into the input text, which must outlive the result.
[[nodiscard]] ReadResult readDocument(std::string_view text, const Limits& limits);

// Length of the well-formed UTF-8 sequence starting at text[at], or 0 when it is malformed.
[[nodiscard]] std::size_t utf8Sequence(std::string_view text, std::size_t at) noexcept;

} // namespace uirepo::declarative::json
