#pragma once

#include "uirepo/NativeApi.h"

namespace uirepo::fo4 {

[[nodiscard]] const UIRepoNativeApiV1& nativeApiV1() noexcept;
[[nodiscard]] const UIRepoNativeApiV2& nativeApiV2() noexcept;

} // namespace uirepo::fo4
