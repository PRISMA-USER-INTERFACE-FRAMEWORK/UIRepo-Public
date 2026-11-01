#pragma once

#include "uirepo/DispatchQueue.h"

#include <REX/W32/BASE.h>

#include <cstdint>
#include <functional>

namespace uirepo::fo4::GameThreadDispatcher {

using OwnerHandle = dispatch::OwnerHandle;

void captureCurrentThread() noexcept;
[[nodiscard]] bool attachWindow(REX::W32::HWND hwnd) noexcept;
void detachWindow(REX::W32::HWND hwnd) noexcept;

// Window subclassing (SetWindowSubclass) is only valid from the thread that owns the window, which
// is the Fallout render thread -- not necessarily the thread F4SE dispatches kGameDataReady on. Any
// thread may queue the one-shot window bring-up here; runGameThreadBootstrap() executes it on the
// first render-hook frame, which is guaranteed to run on the render/window-owner thread.
void queueGameThreadBootstrap(std::function<void()> task) noexcept;
void runGameThreadBootstrap() noexcept;

// Always deferred through the verified Fallout HWND, even when called by that thread. This lets
// input/menu/detour stacks unwind before consumer code mutates engine state.
[[nodiscard]] bool dispatch(std::function<void()> task, OwnerHandle owner = 0);

// May consume the queue's reserved safety slot so focus/input cleanup can still be scheduled when
// ordinary consumer work has saturated the queue.
[[nodiscard]] bool dispatchSafety(std::function<void()> task);

// Applies client-cursor ownership without touching Win32's ShowCursor visibility counter.
// Acquisition clears stale Fallout clipping once; release hands WM_SETCURSOR back to Fallout.
void applyCursorLease(bool owned) noexcept;

// Document/surface destruction must call this before freeing callback userdata. Pending work is
// discarded and this waits for an already-running callback owned by the same handle to finish.
void cancelOwner(OwnerHandle owner) noexcept;

[[nodiscard]] bool isGameThread() noexcept;
[[nodiscard]] bool isReady() noexcept;
[[nodiscard]] std::uint32_t messageId() noexcept;

} // namespace uirepo::fo4::GameThreadDispatcher
