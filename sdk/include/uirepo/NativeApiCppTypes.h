#pragma once

/* Handle kinds, Result<T> and struct factories for NativeApiCpp.h; pure C ABI, no Windows headers. */

#include "uirepo/NativeApi.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace uirepo::sdk::native {

struct DocumentHandle final {
    UIRepoDocumentHandle value{};
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
};

struct NodeHandle final {
    UIRepoNodeHandle value{};
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
};

struct SurfaceHandle final {
    UIRepoSurfaceHandle value{};
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
};

static_assert(!std::is_convertible_v<DocumentHandle, NodeHandle>);
static_assert(!std::is_convertible_v<DocumentHandle, SurfaceHandle>);
static_assert(!std::is_convertible_v<NodeHandle, SurfaceHandle>);

template <class T>
class Result final {
public:
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(std::is_nothrow_move_assignable_v<T>);

    Result() noexcept = default;

    [[nodiscard]] static Result success(T value) noexcept {
        Result result;
        result.value_ = std::move(value);
        result.code_ = UIREPO_RESULT_OK;
        result.ok_ = true;
        return result;
    }

    [[nodiscard]] static Result failure(std::uint32_t code) noexcept {
        Result result;
        result.code_ = code;
        return result;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return ok_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::uint32_t code() const noexcept { return code_; }
    [[nodiscard]] T& value() & noexcept { return value_; }
    [[nodiscard]] const T& value() const& noexcept { return value_; }
    [[nodiscard]] T&& value() && noexcept { return std::move(value_); }

private:
    T value_{};
    std::uint32_t code_{UIREPO_RESULT_UNKNOWN};
    bool ok_{};
};

template <>
class Result<void> final {
public:
    [[nodiscard]] static Result success() noexcept {
        return Result(true, UIREPO_RESULT_OK);
    }

    [[nodiscard]] static Result failure(std::uint32_t code) noexcept {
        return Result(false, code);
    }

    [[nodiscard]] explicit operator bool() const noexcept { return ok_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::uint32_t code() const noexcept { return code_; }

private:
    Result(bool ok, std::uint32_t code) noexcept : ok_(ok), code_(code) {}

    bool ok_{};
    std::uint32_t code_{UIREPO_RESULT_UNKNOWN};
};

[[nodiscard]] constexpr UIRepoStyleV1 makeStyle() noexcept {
    UIRepoStyleV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoTextLayoutV1 makeTextLayout() noexcept {
    UIRepoTextLayoutV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoPointerEventV1 makePointerEvent() noexcept {
    UIRepoPointerEventV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoKeyEventV1 makeKeyEvent() noexcept {
    UIRepoKeyEventV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoFocusRequirementsV1 makeFocusRequirements() noexcept {
    UIRepoFocusRequirementsV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoSurfaceDescV1 makeSurfaceDesc() noexcept {
    UIRepoSurfaceDescV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoSurfaceInfoV1 makeSurfaceInfo() noexcept {
    UIRepoSurfaceInfoV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoSurfaceStatsV1 makeSurfaceStats() noexcept {
    UIRepoSurfaceStatsV1 value{};
    value.size = sizeof(value);
    return value;
}

[[nodiscard]] constexpr UIRepoDocumentStateV1 makeDocumentState() noexcept {
    UIRepoDocumentStateV1 value{};
    value.size = sizeof(value);
    return value;
}

} // namespace uirepo::sdk::native
