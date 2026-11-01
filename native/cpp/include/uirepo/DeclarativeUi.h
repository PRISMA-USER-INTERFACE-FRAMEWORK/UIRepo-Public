#pragma once

#include "uirepo/Controls.h"
#include "uirepo/Core.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace uirepo::declarative {

inline constexpr std::string_view kFormatId = "uirepo.ui";
inline constexpr std::int64_t kFormatVersion = 1;
inline constexpr std::size_t kHardMaxDepth = 512;
inline constexpr std::size_t kMaxIdBytes = 128;
inline constexpr std::size_t kMaxAssetBytes = 1024;
inline constexpr std::size_t kMaxTextInputBytes = 1u << 20;

// Codes are stable: never renumber, only append.
enum class ErrorCode : std::uint16_t {
    SyntaxError = 1,
    InvalidUtf8 = 2,
    InvalidString = 3,
    DuplicateKey = 4,
    NumberRange = 5,
    LimitSize = 6,
    LimitDepth = 7,
    LimitNodes = 8,
    TypeMismatch = 9,
    MissingField = 10,
    UnknownField = 11,
    UnsupportedFormat = 12,
    UnsupportedVersion = 13,
    UnsupportedType = 14,
    InvalidId = 15,
    DuplicateId = 16,
    InvalidEnum = 17,
    InvalidColor = 18,
    InvalidValue = 19,
    InvalidRange = 20,
    InvalidStep = 21,
    ValueOutOfRange = 22,
    ChildrenNotAllowed = 23,
};

[[nodiscard]] std::string_view errorCodeName(ErrorCode code) noexcept;

struct Diagnostic {
    ErrorCode code{ErrorCode::SyntaxError};
    std::string path;
    std::uint32_t line{};
    std::uint32_t column{};
    std::string message;
};

struct Limits {
    std::size_t maxBytes{1u << 20};
    std::size_t maxDepth{64};
    std::size_t maxNodes{4096};
    std::size_t maxDiagnostics{16};
};

struct ParseResult {
    std::unique_ptr<Document> document;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return document != nullptr; }
};

struct SerializeResult {
    std::string text;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

// Throws only std::bad_alloc. On any diagnostic the result has no document.
[[nodiscard]] ParseResult parseDocument(
    std::string_view text,
    const Limits& limits = {},
    const TextMeasurer* textMeasurer = nullptr);

[[nodiscard]] SerializeResult serializeDocument(const Document& document, const Limits& limits = {});

} // namespace uirepo::declarative
