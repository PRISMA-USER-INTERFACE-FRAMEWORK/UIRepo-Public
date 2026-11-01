#pragma once

#include "uirepo/FocusLease.h"
#include "uirepo/RegistryTypes.h"

#include <cstdint>

namespace uirepo::fo4::FocusLeaseManager {

using DocumentHandle = registry::DocumentHandle;

void setRequirements(DocumentHandle document, input::FocusRequirements requirements) noexcept;
void clearRequirements(DocumentHandle document) noexcept;
void sync(DocumentHandle document, input::FocusRequirements requirements) noexcept;
void syncFromRegistry() noexcept;
void release(DocumentHandle document = 0) noexcept;
void releaseAll() noexcept;

[[nodiscard]] DocumentHandle focusedDocument() noexcept;
[[nodiscard]] std::uint64_t episodeGeneration() noexcept;
[[nodiscard]] bool ownsEscape() noexcept;
[[nodiscard]] bool ownsCursor() noexcept;
[[nodiscard]] bool pauseRequested() noexcept;
[[nodiscard]] bool inputMaskRequested() noexcept;

void markPresented() noexcept;
void advancePresentationFrame(
    bool presentationExpected,
    bool framePresented,
    bool suspended = false) noexcept;

} // namespace uirepo::fo4::FocusLeaseManager
