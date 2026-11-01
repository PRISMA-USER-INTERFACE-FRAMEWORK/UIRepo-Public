#include "GameThreadDispatcher.h"

#include "ActiveDeviceState.h"
#include "DocumentRegistry.h"
#include "FocusLeaseManager.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <imm.h>
#include <windowsx.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <utility>

namespace uirepo::fo4::GameThreadDispatcher {
namespace {

constexpr auto kWakeBudget = std::chrono::milliseconds(2);
constexpr UINT_PTR kSubclassId = 0x55495245504F4744ull;
constexpr float kSyntheticReleaseCoordinate = -1.0f;

std::atomic<DWORD> g_gameThreadId{};
std::atomic<DWORD> g_drainThreadId{};
std::atomic<HWND> g_window{};
std::atomic<bool> g_ready{};
std::atomic<bool> g_failed{};
std::atomic<bool> g_pointerCapturedByUIRepo{};
input::PointerSequenceState g_pointerSequences;
input::KeySequenceState g_keySequences;
input::Utf16TextDecoder g_textDecoder;
dispatch::Queue g_tasks;
std::function<void()> g_bootstrapTask;
std::atomic<bool> g_bootstrapPending{};
std::atomic<bool> g_bootstrapDone{};
bool g_rawEscapeOwned{};
bool g_suppressLegacyEscape{};
bool g_cursorLeaseApplied{};
bool g_mouseLeaveArmed{};
DocumentRegistry::DocumentHandle g_rawEscapeDocument{};

[[nodiscard]] HWND nativeWindow(REX::W32::HWND hwnd) noexcept {
    return reinterpret_cast<HWND>(hwnd);
}

[[nodiscard]] REX::W32::HWND rexWindow(HWND hwnd) noexcept {
    return reinterpret_cast<REX::W32::HWND>(hwnd);
}

[[nodiscard]] std::uint32_t inputModifiers(LPARAM keyLParam = 0) noexcept {
    std::uint32_t modifiers = input::ModifierNone;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= input::ModifierShift;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= input::ModifierControl;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0 || (keyLParam & (LPARAM{1} << 29)) != 0) {
        modifiers |= input::ModifierAlt;
    }
    return modifiers;
}

[[nodiscard]] bool flushOwnedPointerSequences() noexcept {
    auto& registry = DocumentRegistry::instance();
    bool flushed = false;
    constexpr input::PointerButton buttons[] = {
        input::PointerButton::Left,
        input::PointerButton::Right,
        input::PointerButton::Middle,
        input::PointerButton::X1,
        input::PointerButton::X2,
    };

    for (const auto button : buttons) {
        if (g_pointerSequences.owner(button) != input::SequenceOwner::UIRepo) {
            continue;
        }
        flushed = true;
        ActiveDeviceState::notePointerButton(false);
        // Focus/capture loss is cancellation, not a click. Route the paired ButtonUp
        // outside the viewport so retained handlers unwind without activating a button.
        (void)registry.routePointerEvent(input::PointerEvent{
            .type = input::PointerEventType::ButtonUp,
            .button = button,
            .x = kSyntheticReleaseCoordinate,
            .y = kSyntheticReleaseCoordinate,
            .modifiers = input::ModifierNone,
        });
    }

    g_pointerSequences.clear();
    if (flushed) {
        (void)registry.handlePointerLeave();
    }
    return flushed;
}

void flushOwnedKeySequences() noexcept {
    auto& registry = DocumentRegistry::instance();
    for (std::uint32_t keyCode = 0; keyCode < input::KeySequenceState::kVirtualKeyCount; ++keyCode) {
        if (g_keySequences.owner(keyCode) != input::SequenceOwner::UIRepo) {
            continue;
        }
        ActiveDeviceState::noteKeyboardKey(false);
        const input::KeyEvent release{
            .type = input::KeyEventType::Up,
            .keyCode = keyCode,
            .modifiers = input::ModifierNone,
            .repeat = false,
        };
        (void)registry.routeDocumentKeyEvent(release);
        (void)registry.routeKeyEvent(release);
    }

    if (g_rawEscapeOwned) {
        ActiveDeviceState::noteKeyboardKey(false);
        if (FocusLeaseManager::focusedDocument() == g_rawEscapeDocument) {
            (void)registry.routeDocumentKeyEvent(input::KeyEvent{
                .type = input::KeyEventType::Up,
                .keyCode = input::KeyEscape,
                .modifiers = input::ModifierNone,
                .repeat = false,
            });
        }
    }

    g_keySequences.clear();
    g_rawEscapeOwned = false;
    g_suppressLegacyEscape = false;
    g_rawEscapeDocument = 0;
}

void resetInputOwnership(HWND hwnd) noexcept {
    (void)flushOwnedPointerSequences();
    flushOwnedKeySequences();
    g_textDecoder.clear();
    g_mouseLeaveArmed = false;
    ActiveDeviceState::resetPointerBaseline();

    const bool ownedCapture = g_pointerCapturedByUIRepo.exchange(false, std::memory_order_acq_rel);
    DocumentRegistry::instance().cancelPointerCapture();
    if (ownedCapture && GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

// WM_MOUSEMOVE stops firing once the cursor leaves the client area, so a retained hover would stay
// latched on whatever node was last under the cursor. TrackMouseEvent(TME_LEAVE) asks Win32 for a
// one-shot WM_MOUSELEAVE when that happens; re-arm on each move since it disarms after firing.
void armMouseLeaveTracking(HWND hwnd) noexcept {
    if (g_mouseLeaveArmed) {
        return;
    }
    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd;
    if (TrackMouseEvent(&track)) {
        g_mouseLeaveArmed = true;
    }
}

void routePointerLeave() noexcept {
    ActiveDeviceState::notePointerLeft();
    (void)DocumentRegistry::instance().handlePointerLeave();
}

void syncPointerCapture(HWND hwnd) noexcept {
    if (!g_pointerCapturedByUIRepo.load(std::memory_order_acquire) ||
        DocumentRegistry::instance().hasPointerCapture()) {
        return;
    }

    g_pointerCapturedByUIRepo.store(false, std::memory_order_release);
    if (GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

[[nodiscard]] input::PointerButton xButtonFromWParam(WPARAM wParam) noexcept {
    switch (GET_XBUTTON_WPARAM(wParam)) {
    case XBUTTON1:
        return input::PointerButton::X1;
    case XBUTTON2:
        return input::PointerButton::X2;
    default:
        return input::PointerButton::None;
    }
}

[[nodiscard]] bool routePointer(
    HWND hwnd,
    input::PointerEventType type,
    input::PointerButton button,
    LPARAM lParam,
    float wheelDelta = 0.0f) noexcept {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    const bool wheelEvent =
        type == input::PointerEventType::Wheel ||
        type == input::PointerEventType::HorizontalWheel;
    if (wheelEvent && !ScreenToClient(hwnd, &point)) {
        return false;
    }

    if (type == input::PointerEventType::Move) {
        ActiveDeviceState::notePointerPosition(
            static_cast<float>(point.x),
            static_cast<float>(point.y));
    } else if (wheelEvent) {
        ActiveDeviceState::noteWheel(wheelDelta);
    }

    auto& registry = DocumentRegistry::instance();
    const Point rawPoint{static_cast<float>(point.x), static_cast<float>(point.y)};
    Point routedPoint{rawPoint};
    bool insideClient = true;
    RECT client{};
    if (GetClientRect(hwnd, &client)) {
        const Size clientSize{
            static_cast<float>(client.right - client.left),
            static_cast<float>(client.bottom - client.top),
        };
        insideClient = input::pointInsideClient(rawPoint, clientSize);
        routedPoint = input::mapClientToViewport(
            routedPoint,
            clientSize,
            registry.inputViewportSize());
    }

    if (insideClient) {
        ActiveDeviceState::noteRoutedPoint(routedPoint.x, routedPoint.y);
    } else {
        ActiveDeviceState::notePointerLeft();
    }

    if (type == input::PointerEventType::HorizontalWheel) {
        return registry.routeHorizontalWheelEvent(routedPoint, wheelDelta);
    }

    return registry.routePointerEvent(input::PointerEvent{
        .type = type,
        .button = button,
        .x = routedPoint.x,
        .y = routedPoint.y,
        .wheelDelta = wheelDelta,
        .modifiers = inputModifiers(),
    });
}

[[nodiscard]] bool routePointerButtonDown(
    HWND hwnd,
    input::PointerButton button,
    LPARAM lParam) noexcept {
    ActiveDeviceState::notePointerButton(true);
    const bool handled = routePointer(hwnd, input::PointerEventType::ButtonDown, button, lParam);
    g_pointerSequences.begin(button, handled);
    if (!handled || !DocumentRegistry::instance().hasPointerCapture()) {
        return handled;
    }

    SetCapture(hwnd);
    const bool captured = GetCapture() == hwnd;
    g_pointerCapturedByUIRepo.store(captured, std::memory_order_release);
    if (!captured) {
        DocumentRegistry::instance().cancelPointerCapture();
    }
    return handled;
}

[[nodiscard]] bool routePointerButtonUp(
    HWND hwnd,
    input::PointerButton button,
    LPARAM lParam) noexcept {
    const bool ownedPress = g_pointerSequences.end(button);
    const bool routed = routePointer(hwnd, input::PointerEventType::ButtonUp, button, lParam);
    const bool handled = input::consumePointerRelease(ownedPress, routed);

    if (!g_pointerSequences.anyOwned()) {
        const bool ownedCapture = g_pointerCapturedByUIRepo.exchange(false, std::memory_order_acq_rel);
        if (ownedCapture && GetCapture() == hwnd) {
            ReleaseCapture();
        }
    }
    return handled;
}

[[nodiscard]] bool routeKey(UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const auto keyCode = static_cast<std::uint32_t>(wParam);
    const input::KeyEvent event{
        .type = down ? input::KeyEventType::Down : input::KeyEventType::Up,
        .keyCode = keyCode,
        .modifiers = inputModifiers(lParam),
        .repeat = down && (lParam & (LPARAM{1} << 30)) != 0,
    };

    ActiveDeviceState::noteKeyboardKey(down);

    if (event.keyCode == input::KeyF4 && (event.modifiers & input::ModifierAlt) != 0) {
        return false;
    }

    auto& registry = DocumentRegistry::instance();
    if (event.keyCode == input::KeyEscape) {
        if (g_suppressLegacyEscape) {
            if (!down) {
                g_suppressLegacyEscape = false;
            }
            return true;
        }
        if (!FocusLeaseManager::ownsEscape()) {
            return false;
        }
        (void)registry.routeDocumentKeyEvent(event);
        return true;
    }

    if (down) {
        if (event.repeat) {
            const auto owner = g_keySequences.owner(keyCode);
            if (owner != input::SequenceOwner::UIRepo) {
                return false;
            }
            const bool callbackHandled = registry.routeDocumentKeyEvent(event);
            if (!callbackHandled && !input::isActivationKey(keyCode)) {
                (void)registry.routeKeyEvent(event);
            }
            return true;
        }

        const bool handled =
            registry.routeDocumentKeyEvent(event) || registry.routeKeyEvent(event);
        g_keySequences.begin(keyCode, handled);
        return handled;
    }

    const bool ownedByUIRepo = g_keySequences.end(keyCode);
    if (!ownedByUIRepo) {
        return false;
    }
    (void)registry.routeDocumentKeyEvent(event);
    (void)registry.routeKeyEvent(event);
    return true;
}

[[nodiscard]] bool routeOwnedRawEscape(
    HWND hwnd,
    WPARAM wParam,
    LPARAM lParam,
    LRESULT& result) noexcept {
    RAWINPUT raw{};
    UINT rawSize = sizeof(raw);
    const UINT bytes = GetRawInputData(
        reinterpret_cast<HRAWINPUT>(lParam),
        RID_INPUT,
        &raw,
        &rawSize,
        sizeof(RAWINPUTHEADER));
    if (bytes == UINT(-1) || bytes < sizeof(RAWINPUTHEADER) ||
        raw.header.dwType != RIM_TYPEKEYBOARD || raw.data.keyboard.VKey != VK_ESCAPE) {
        return false;
    }

    const bool down = (raw.data.keyboard.Flags & RI_KEY_BREAK) == 0;
    if (down) {
        if (!g_rawEscapeOwned) {
            if (!FocusLeaseManager::ownsEscape()) {
                return false;
            }
            g_rawEscapeOwned = true;
            g_rawEscapeDocument = FocusLeaseManager::focusedDocument();
            if (g_rawEscapeDocument == 0 ||
                !DocumentRegistry::instance().hasDocumentKeyEventHandler(g_rawEscapeDocument)) {
                g_rawEscapeOwned = false;
                g_rawEscapeDocument = 0;
                return false;
            }
        }

        const bool repeat = g_suppressLegacyEscape;
        g_suppressLegacyEscape = true;
        ActiveDeviceState::noteKeyboardKey(true);
        if (FocusLeaseManager::focusedDocument() == g_rawEscapeDocument) {
            (void)DocumentRegistry::instance().routeDocumentKeyEvent(input::KeyEvent{
                .type = input::KeyEventType::Down,
                .keyCode = input::KeyEscape,
                .modifiers = inputModifiers(),
                .repeat = repeat,
            });
        }
    } else {
        if (!g_rawEscapeOwned) {
            return false;
        }

        ActiveDeviceState::noteKeyboardKey(false);
        if (FocusLeaseManager::focusedDocument() == g_rawEscapeDocument) {
            (void)DocumentRegistry::instance().routeDocumentKeyEvent(input::KeyEvent{
                .type = input::KeyEventType::Up,
                .keyCode = input::KeyEscape,
                .modifiers = inputModifiers(),
                .repeat = false,
            });
        }
        g_rawEscapeOwned = false;
        g_rawEscapeDocument = 0;
    }

    if (GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT) {
        result = DefWindowProcW(hwnd, WM_INPUT, wParam, lParam);
    } else {
        result = 0;
    }
    return true;
}

[[nodiscard]] bool routeUtf16Text(WPARAM wParam) noexcept {
    if (wParam > 0xFFFFu) {
        g_textDecoder.clear();
        return false;
    }

    auto& registry = DocumentRegistry::instance();
    const auto event = g_textDecoder.push(static_cast<char16_t>(wParam));
    if (!event.has_value()) {
        return registry.routeTextEvent(input::TextEvent{});
    }
    return registry.routeTextEvent(*event);
}

[[nodiscard]] bool retainedCaretClientPoints(HWND hwnd, POINT& top, POINT& bottom) noexcept {
    auto& registry = DocumentRegistry::instance();
    Rect caret{};
    if (!registry.focusedTextInputCaret(caret)) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }

    const Size viewport = registry.inputViewportSize();
    const float clientWidth = static_cast<float>(client.right - client.left);
    const float clientHeight = static_cast<float>(client.bottom - client.top);
    if (viewport.width <= 0.0f || viewport.height <= 0.0f ||
        clientWidth <= 0.0f || clientHeight <= 0.0f) {
        return false;
    }

    const float x = caret.x * clientWidth / viewport.width;
    const float y = caret.y * clientHeight / viewport.height;
    const float bottomY = caret.bottom() * clientHeight / viewport.height;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(bottomY)) {
        return false;
    }

    const auto clientX = static_cast<LONG>(std::lround(std::clamp(x, 0.0f, clientWidth)));
    const auto clientY = static_cast<LONG>(std::lround(std::clamp(y, 0.0f, clientHeight)));
    const auto clientBottom = static_cast<LONG>(std::lround(std::clamp(bottomY, 0.0f, clientHeight)));
    top = POINT{clientX, clientY};
    bottom = POINT{clientX, clientBottom};
    return true;
}

void positionImeForFocusedTextInput(HWND hwnd) noexcept {
    POINT caretTop{};
    POINT caretBottom{};
    if (!retainedCaretClientPoints(hwnd, caretTop, caretBottom)) {
        return;
    }

    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return;
    }

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = caretTop;
    (void)ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = caretBottom;
    (void)ImmSetCandidateWindow(context, &candidate);

    (void)ImmReleaseContext(hwnd, context);
}

void failClosed(const char* reason, DWORD expected, DWORD actual) noexcept {
    g_ready.store(false, std::memory_order_release);
    g_failed.store(true, std::memory_order_release);
    g_tasks.failClosed();
    spdlog::critical(
        "UIRepo_F4 dispatcher: {} (expected thread {}, actual {}); game-thread callbacks disabled",
        reason,
        expected,
        actual);
}

[[nodiscard]] bool postWake() noexcept {
    const HWND hwnd = g_window.load(std::memory_order_acquire);
    const UINT message = messageId();
    if (hwnd == nullptr || message == 0) {
        return false;
    }
    if (PostMessageW(hwnd, message, 0, 0)) {
        return true;
    }

    spdlog::critical("UIRepo_F4 dispatcher: PostMessageW failed (GLE {})", GetLastError());
    failClosed(
        "could not post wake message",
        g_gameThreadId.load(std::memory_order_acquire),
        GetCurrentThreadId());
    return false;
}

[[nodiscard]] bool drainWake(HWND hwnd, UINT message) noexcept {
    const UINT registered = messageId();
    if (registered == 0 || message != registered) {
        return false;
    }

    const DWORD expectedThread = g_gameThreadId.load(std::memory_order_acquire);
    const DWORD currentThread = GetCurrentThreadId();
    const HWND liveWindow = g_window.load(std::memory_order_acquire);
    if (!isReady() || hwnd != liveWindow) {
        return true;
    }
    if (currentThread != expectedThread) {
        failClosed("wake arrived on an unverified thread", expectedThread, currentThread);
        return true;
    }

    thread_local bool insideDrain = false;
    if (insideDrain) {
        return true;
    }

    struct InsideDrainScope {
        bool& value;
        explicit InsideDrainScope(bool& flag) : value(flag) { value = true; }
        ~InsideDrainScope() { value = false; }
    } insideScope{insideDrain};

    struct DrainThreadScope {
        explicit DrainThreadScope(DWORD thread) {
            g_drainThreadId.store(thread, std::memory_order_release);
        }
        ~DrainThreadScope() {
            g_drainThreadId.store(0, std::memory_order_release);
        }
    } drainThread{currentThread};

    {
        struct DrainSession {
            DrainSession() { g_tasks.beginDrain(); }
            ~DrainSession() { g_tasks.endDrain(); }
        } session;

        const auto deadline = std::chrono::steady_clock::now() + kWakeBudget;
        std::size_t ran = 0;
        while (ran < dispatch::kMaxTasksPerWake) {
            if (ran > 0 && std::chrono::steady_clock::now() >= deadline) {
                break;
            }

            auto task = g_tasks.takeNext();
            if (!task) {
                break;
            }
            ++ran;

            struct FinishTask {
                ~FinishTask() { g_tasks.finishExecuting(); }
            } finish;

            try {
                if (task->function) {
                    task->function();
                }
            } catch (const std::exception& exception) {
                spdlog::error("UIRepo_F4 dispatcher callback threw: {}", exception.what());
            } catch (...) {
                spdlog::error("UIRepo_F4 dispatcher callback threw an unknown exception");
            }
        }
    }

    if (g_tasks.takeWakeRearm()) {
        (void)postWake();
    }
    return true;
}

LRESULT CALLBACK subclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR) {
    if (drainWake(hwnd, message)) {
        return 0;
    }
    if (message == WM_UNICHAR && wParam == UNICODE_NOCHAR) {
        return TRUE;
    }
    if (message == WM_INPUT) {
        LRESULT rawResult{};
        if (routeOwnedRawEscape(hwnd, wParam, lParam, rawResult)) {
            return rawResult;
        }
    }
    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && FocusLeaseManager::ownsCursor()) {
        // Hide the OS arrow; UIRepo draws its own cursor sprite over the composited UI (see the
        // Runtime cursor overlay). An engine-rendered cursor would be occluded by UIRepo panels.
        SetCursor(nullptr);
        return TRUE;
    }

    bool handled = false;
    switch (message) {
    case WM_MOUSEMOVE:
        armMouseLeaveTracking(hwnd);
        handled = routePointer(hwnd, input::PointerEventType::Move, input::PointerButton::None, lParam);
        syncPointerCapture(hwnd);
        break;
    case WM_MOUSELEAVE:
        // One-shot: disarmed by Win32 after firing, so allow re-arming on the next move.
        g_mouseLeaveArmed = false;
        routePointerLeave();
        break;
    // The double-click messages carry the second press of a fast double-click; if the window class
    // has CS_DBLCLKS the OS sends these instead of a second *BUTTONDOWN, so route them as a normal
    // button-down rather than dropping the press to the game.
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        handled = routePointerButtonDown(hwnd, input::PointerButton::Left, lParam);
        break;
    case WM_LBUTTONUP:
        handled = routePointerButtonUp(hwnd, input::PointerButton::Left, lParam);
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        handled = routePointerButtonDown(hwnd, input::PointerButton::Right, lParam);
        break;
    case WM_RBUTTONUP:
        handled = routePointerButtonUp(hwnd, input::PointerButton::Right, lParam);
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        handled = routePointerButtonDown(hwnd, input::PointerButton::Middle, lParam);
        break;
    case WM_MBUTTONUP:
        handled = routePointerButtonUp(hwnd, input::PointerButton::Middle, lParam);
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK: {
        const auto button = xButtonFromWParam(wParam);
        if (button != input::PointerButton::None && routePointerButtonDown(hwnd, button, lParam)) {
            return TRUE;
        }
        break;
    }
    case WM_XBUTTONUP: {
        const auto button = xButtonFromWParam(wParam);
        if (button != input::PointerButton::None && routePointerButtonUp(hwnd, button, lParam)) {
            return TRUE;
        }
        break;
    }
    case WM_MOUSEWHEEL:
        handled = routePointer(
            hwnd,
            input::PointerEventType::Wheel,
            input::PointerButton::None,
            lParam,
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
        break;
    case WM_MOUSEHWHEEL:
        handled = routePointer(
            hwnd,
            input::PointerEventType::HorizontalWheel,
            input::PointerButton::None,
            lParam,
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        handled = routeKey(message, wParam, lParam);
        break;
    case WM_CHAR:
        handled = routeUtf16Text(wParam);
        break;
    case WM_UNICHAR:
        if (wParam <= 0x10FFFFu) {
            g_textDecoder.clear();
            handled = DocumentRegistry::instance().routeTextEvent(
                input::TextEvent{static_cast<char32_t>(wParam)});
        }
        break;
    case WM_IME_STARTCOMPOSITION:
        positionImeForFocusedTextInput(hwnd);
        break;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) {
            g_pointerCapturedByUIRepo.store(false, std::memory_order_release);
            const bool flushed = flushOwnedPointerSequences();
            if (!flushed) {
                DocumentRegistry::instance().cancelPointerCapture();
            }
        }
        break;
    case WM_CANCELMODE:
    case WM_KILLFOCUS:
        resetInputOwnership(hwnd);
        FocusLeaseManager::releaseAll();
        break;
    case WM_ACTIVATEAPP:
        if (wParam == FALSE) {
            resetInputOwnership(hwnd);
            FocusLeaseManager::releaseAll();
        }
        break;
    default:
        break;
    }

    if (handled) {
        return 0;
    }

    if (message == WM_NCDESTROY) {
        detachWindow(rexWindow(hwnd));
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace

std::uint32_t messageId() noexcept {
    static const UINT id = RegisterWindowMessageW(L"UIRepo_F4.GameThreadDispatch.v1");
    return id;
}

void captureCurrentThread() noexcept {
    const DWORD current = GetCurrentThreadId();
    DWORD expected = 0;
    if (g_gameThreadId.compare_exchange_strong(expected, current, std::memory_order_acq_rel)) {
        spdlog::info("UIRepo_F4 dispatcher: captured Fallout render thread {}", current);
        return;
    }

    if (expected != current) {
        failClosed("render-thread identity changed", expected, current);
    }
}

void queueGameThreadBootstrap(std::function<void()> task) noexcept {
    if (!task || g_bootstrapPending.load(std::memory_order_acquire) ||
        g_bootstrapDone.load(std::memory_order_acquire)) {
        return;
    }

    g_bootstrapTask = std::move(task);
    g_bootstrapPending.store(true, std::memory_order_release);
}

void runGameThreadBootstrap() noexcept {
    if (!g_bootstrapPending.load(std::memory_order_acquire) ||
        g_bootstrapDone.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    auto task = std::move(g_bootstrapTask);
    g_bootstrapTask = nullptr;
    g_bootstrapPending.store(false, std::memory_order_release);
    if (!task) {
        return;
    }

    try {
        task();
    } catch (const std::exception& exception) {
        spdlog::error("UIRepo_F4 dispatcher: game-thread bootstrap threw: {}", exception.what());
    } catch (...) {
        spdlog::error("UIRepo_F4 dispatcher: game-thread bootstrap threw an unknown exception");
    }
}

bool attachWindow(REX::W32::HWND rexHwnd) noexcept {
    const HWND hwnd = nativeWindow(rexHwnd);
    if (hwnd == nullptr || g_failed.load(std::memory_order_acquire)) {
        return false;
    }

    const DWORD expectedThread = g_gameThreadId.load(std::memory_order_acquire);
    if (messageId() == 0) {
        failClosed("RegisterWindowMessageW failed", expectedThread, 0);
        return false;
    }

    DWORD processId = 0;
    const DWORD ownerThread = GetWindowThreadProcessId(hwnd, &processId);
    if (expectedThread == 0 || ownerThread == 0 || processId != GetCurrentProcessId()) {
        failClosed("could not verify Fallout window ownership", expectedThread, ownerThread);
        return false;
    }
    if (ownerThread != expectedThread) {
        failClosed(
            "Fallout window thread differs from F4SE plugin-load thread",
            expectedThread,
            ownerThread);
        return false;
    }

    const HWND existing = g_window.load(std::memory_order_acquire);
    if (existing == hwnd && g_ready.load(std::memory_order_acquire)) {
        return true;
    }

    if (existing != nullptr && existing != hwnd) {
        applyCursorLease(false);
        resetInputOwnership(existing);
        RemoveWindowSubclass(existing, subclassProc, kSubclassId);
        g_tasks.detach();
        g_ready.store(false, std::memory_order_release);
    }

    if (!SetWindowSubclass(hwnd, subclassProc, kSubclassId, 0)) {
        spdlog::critical("UIRepo_F4 dispatcher: SetWindowSubclass failed (GLE {})", GetLastError());
        failClosed("could not install wake subclass", expectedThread, GetCurrentThreadId());
        return false;
    }

    g_cursorLeaseApplied = false;
    g_mouseLeaveArmed = false;
    ActiveDeviceState::resetPointerBaseline();
    g_window.store(hwnd, std::memory_order_release);
    g_tasks.attach();
    g_ready.store(true, std::memory_order_release);
    spdlog::info(
        "UIRepo_F4 dispatcher: verified HWND {:p} on Fallout thread {}",
        static_cast<void*>(hwnd),
        ownerThread);
    return true;
}

void detachWindow(REX::W32::HWND rexHwnd) noexcept {
    const HWND hwnd = nativeWindow(rexHwnd);
    HWND expected = hwnd;
    if (!g_window.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
        return;
    }

    g_cursorLeaseApplied = false;
    resetInputOwnership(hwnd);
    RemoveWindowSubclass(hwnd, subclassProc, kSubclassId);
    g_ready.store(false, std::memory_order_release);
    g_tasks.detach();
}

bool dispatch(std::function<void()> task, OwnerHandle owner) {
    if (!task || g_failed.load(std::memory_order_acquire)) {
        return false;
    }

    const auto queued = g_tasks.tryEnqueue(std::move(task), owner);
    if (!queued.accepted) {
        return false;
    }
    if (!queued.needsWake) {
        return true;
    }
    return postWake();
}

bool dispatchSafety(std::function<void()> task) {
    if (!task || g_failed.load(std::memory_order_acquire)) {
        return false;
    }

    const auto queued = g_tasks.tryEnqueue(std::move(task), 0, true);
    if (!queued.accepted) {
        return false;
    }
    if (!queued.needsWake) {
        return true;
    }
    return postWake();
}

void applyCursorLease(bool owned) noexcept {
    const HWND hwnd = g_window.load(std::memory_order_acquire);
    if (hwnd == nullptr || g_cursorLeaseApplied == owned) {
        return;
    }

    g_cursorLeaseApplied = owned;
    // UIRepo draws its own cursor sprite as the last thing in the compositor (see the Runtime cursor
    // overlay); the engine cursor is not used because it composites before UIRepo and would be
    // occluded by UIRepo's own panels.
    if (owned) {
        // Clear any stale Fallout cursor clip and hide the OS arrow; our sprite stands in for it.
        ClipCursor(nullptr);
        SetCursor(nullptr);
        return;
    }

    PostMessageW(
        hwnd,
        WM_SETCURSOR,
        reinterpret_cast<WPARAM>(hwnd),
        MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

void cancelOwner(OwnerHandle owner) noexcept {
    if (owner == 0) {
        return;
    }

    g_tasks.cancelOwner(owner);
    const DWORD drainThread = g_drainThreadId.load(std::memory_order_acquire);
    if (drainThread != 0 && GetCurrentThreadId() == drainThread) {
        return;
    }

    while (!g_tasks.waitUntilNotExecutingFor(owner, std::chrono::seconds(5))) {
        spdlog::critical(
            "UIRepo_F4 dispatcher: owner {} destruction is waiting for an in-flight callback; userdata is not safe to free",
            owner);
    }
}

bool isReady() noexcept {
    return g_ready.load(std::memory_order_acquire) && !g_failed.load(std::memory_order_acquire);
}

bool isGameThread() noexcept {
    if (!isReady()) {
        return false;
    }
    return GetCurrentThreadId() == g_gameThreadId.load(std::memory_order_acquire);
}

} // namespace uirepo::fo4::GameThreadDispatcher
