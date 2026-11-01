/* An interactive in-game test harness driven entirely through the public
   UIRepo ABI.

   This is the first consumer of the ABI that a human actually clicks on. Like
   the static sample before it, everything it knows about UIRepo comes from two
   headers - NativeApi.h and NativeApiLoader.h - and it links nothing: the table
   is resolved at runtime from the already-loaded framework plugin. Nothing here
   reaches into the framework; if a mod author can build and drive this, they can
   build and drive their own UI.

   It exists to exercise the parts CI cannot reach at all: a real OS cursor
   engaging through the focus lease, real WM mouse and keyboard events routing
   across the module boundary into node handlers this DLL registered, and clean
   return of control to the game when the panel closes. Those paths only run in
   a live process against a live renderer, so the acquire sequence below has to
   be correct by construction - there is no local test that can catch a wrong
   order.

   THE ACQUIRE SEQUENCE, and why this exact order (verified against the
   framework's own focus/input routing):

     1. Build the document and its controls, and register every handler first,
        while nothing is visible or focused. Handlers registered after focus
        would miss the first frame's events.
     2. setDocumentFocusRequirements(cursor=1, input=1, escape=1) - this only
        records what the document wants; it does not engage anything yet. The
        lease is driven per-frame from whichever document is *focused*.
     3. setDocumentInputEnabled(1) and setDocumentVisible(1) - a document is
        "input eligible" only when it is both visible and input-enabled, and
        focusNode refuses a node in an ineligible document. So these must come
        before the focus call, not after.
     4. focusNode(firstControl) - this sets the focused document. On the next
        rendered frame the framework's per-frame sync sees a focused document
        that wants a cursor and engages the OS cursor lease. That is the moment
        the cursor appears; there is no separate "show cursor" call.

   Pointer move/enter/leave/button events reach a node's pointer handler only
   while the document stays visible AND input-enabled, and only for nodes that
   registered a handler. Escape reaches the document key handler only while a
   node is focused; the escape lease is what stops the game from swallowing it
   into the pause menu first.

   TEARDOWN drops focus and clears the requirements, which releases the cursor,
   input and escape leases on the next frame, then hides and destroys the
   document - so the player is never left with a captured cursor and no way out.
   Close button, Escape, and kPreLoadGame all funnel through the same teardown.

   CommonLibF4 must be included before anything that pulls in the Windows API,
   and NativeApiLoader.h pulls in windows.h. Include order is load-bearing here:
   reverse these two and CommonLib stops the build with "Windows API detected".
   See the SDK README. */
#include <F4SE/F4SE.h>
#include <spdlog/spdlog.h>

#include "uirepo/NativeApiLoader.h"

#include <cstdint>
#include <cstdio>

namespace {

using uirepo::sdk::Api;

/* Every handle and the running click count live at file scope. The framework
   invokes our callbacks on the game/render thread with no context of its own
   beyond the handles it passes, so the harness keeps its state here and the
   callbacks stay small, non-throwing, and reentrancy-safe. */
const UIRepoNativeApiV1* g_api = nullptr;   // stable table pointer, safe to cache
UIRepoDocumentHandle g_document = 0;
UIRepoNodeHandle g_clickButton = 0;
UIRepoNodeHandle g_counterText = 0;
UIRepoNodeHandle g_statusText = 0;
UIRepoNodeHandle g_toggle = 0;
UIRepoNodeHandle g_slider = 0;

unsigned g_clickCount = 0;
bool g_altButtonColor = false;   // flipped each click for visible feedback

/* VK_ESCAPE. windows.h is already in scope via NativeApiLoader.h, but the ABI
   speaks in raw virtual-key codes, so name the value we compare against. */
constexpr std::uint32_t kKeyEscape = 0x1Bu;

/* Two resting colors for the click button, swapped on every click, plus a
   brighter hover tint applied while the cursor is over it. */
constexpr UIRepoColorV1 kButtonColorA{62, 82, 138, 255};
constexpr UIRepoColorV1 kButtonColorB{72, 128, 108, 255};
constexpr UIRepoColorV1 kButtonHover{86, 110, 172, 255};

/* Every refusal is reported with its cause. This is the whole reason the result
   model exists: a bare zero would leave a mod author guessing between a bad
   handle, a wrong thread, and a renderer that simply is not up yet. */
void reportRefusal(const UIRepoNativeApiV1* api, const char* what) {
    if (api != nullptr && (api->capabilities & UIREPO_CAP_RESULT_CODES) != 0 &&
        api->getLastResult != nullptr && api->describeResult != nullptr) {
        const std::uint32_t why = api->getLastResult();
        spdlog::error("{} refused: {} (code {})", what, api->describeResult(why), why);
    } else {
        spdlog::error("{} refused; this framework build does not report result codes", what);
    }
}

/* Re-applies the click button's full style with a chosen background. setNodeStyle
   replaces the whole style struct, so the button's shape is rebuilt each time
   rather than patched - the color is the only thing that varies. */
void applyButtonBackground(const UIRepoNativeApiV1* api, UIRepoColorV1 background) {
    if (api == nullptr || g_document == 0 || g_clickButton == 0) {
        return;
    }
    UIRepoStyleV1 style{};
    style.size = sizeof(style);
    style.padding = UIRepoThicknessV1{18.0f, 12.0f, 18.0f, 12.0f};
    style.background = background;
    style.opacity = 1.0f;
    style.cornerRadius = 8.0f;
    if (api->setNodeStyle(g_document, g_clickButton, &style) == 0) {
        reportRefusal(api, "setNodeStyle(button)");
    }
}

[[nodiscard]] UIRepoColorV1 currentButtonRestColor() {
    return g_altButtonColor ? kButtonColorB : kButtonColorA;
}

/* Rebuilds the toggle/slider status line from the live control values. Reading
   through the getters proves the values crossed the ABI, rather than trusting a
   copy this side kept. */
void refreshStatus(const UIRepoNativeApiV1* api) {
    if (api == nullptr || g_document == 0 || g_statusText == 0) {
        return;
    }

    std::uint8_t toggleValue = 0;
    if (g_toggle != 0 && api->getToggleValue != nullptr) {
        (void)api->getToggleValue(g_document, g_toggle, &toggleValue);
    }
    float sliderValue = 0.0f;
    if (g_slider != 0 && api->getSliderValue != nullptr) {
        (void)api->getSliderValue(g_document, g_slider, &sliderValue);
    }

    char status[96]{};
    std::snprintf(
        status,
        sizeof(status),
        "Toggle: %s    Slider: %.2f",
        toggleValue != 0 ? "ON" : "OFF",
        static_cast<double>(sliderValue));
    if (api->setText(g_document, g_statusText, status) == 0) {
        reportRefusal(api, "setText(status)");
    }
}

/* ---- callbacks, all invoked by the framework on the game/render thread ---- */

/* The click button is driven by ACTIVATE rather than a raw button-up: activate
   fires on a completed left click even after a small drag, as long as the
   cursor is still over the control on release, so it is the robust "was
   clicked" signal. Pointer enter/leave (below) only paint the hover tint. */
void onClickActivate(UIRepoDocumentHandle document, UIRepoNodeHandle, std::uint32_t eventType, void*) {
    if (g_api == nullptr || document != g_document) {
        return;
    }
    /* One handler receives ALL node events (activate, focus gained/lost, value
       changed). Only a real activate is a click; focusing this button at acquire
       time fires FOCUS_GAINED into the same handler, so without this guard the
       counter would tick before anyone clicked. */
    if (eventType != UIREPO_EVENT_ACTIVATE) {
        return;
    }
    ++g_clickCount;
    g_altButtonColor = !g_altButtonColor;

    char counter[24]{};
    std::snprintf(counter, sizeof(counter), "%u", g_clickCount);
    if (g_counterText != 0 && g_api->setText(g_document, g_counterText, counter) == 0) {
        reportRefusal(g_api, "setText(counter)");
    }
    /* Cursor is still over the button after a click, so keep the hover tint;
       the rest color underneath has already flipped for the next leave. */
    applyButtonBackground(g_api, kButtonHover);
    spdlog::info("UIRepo harness: click {} on the test button", g_clickCount);
}

/* Hover feedback: brighten on enter, drop back to the current resting color on
   leave. Purely cosmetic, and the clearest proof that real pointer-move events
   are crossing the ABI into a handler this DLL registered. */
void onButtonPointer(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle,
    const UIRepoNodePointerEventV1* event,
    void*) {
    if (g_api == nullptr || document != g_document || event == nullptr) {
        return;
    }
    if (event->type == UIREPO_NODE_POINTER_ENTER) {
        applyButtonBackground(g_api, kButtonHover);
    } else if (event->type == UIREPO_NODE_POINTER_LEAVE) {
        applyButtonBackground(g_api, currentButtonRestColor());
    }
}

void onToggleChanged(UIRepoDocumentHandle document, UIRepoNodeHandle, std::uint32_t eventType, void*) {
    if (g_api == nullptr || document != g_document || eventType != UIREPO_EVENT_VALUE_CHANGED) {
        return;
    }
    refreshStatus(g_api);
    std::uint8_t value = 0;
    if (g_toggle != 0 && g_api->getToggleValue != nullptr) {
        (void)g_api->getToggleValue(g_document, g_toggle, &value);
    }
    spdlog::info("UIRepo harness: toggle is now {}", value != 0 ? "ON" : "OFF");
}

void onSliderChanged(UIRepoDocumentHandle document, UIRepoNodeHandle, std::uint32_t eventType, void*) {
    if (g_api == nullptr || document != g_document || eventType != UIREPO_EVENT_VALUE_CHANGED) {
        return;
    }
    refreshStatus(g_api);
    float value = 0.0f;
    if (g_slider != 0 && g_api->getSliderValue != nullptr) {
        (void)g_api->getSliderValue(g_document, g_slider, &value);
    }
    spdlog::info("UIRepo harness: slider is now {:.2f}", value);
}

/* Forward declaration: the Close button and the Escape key both tear the
   harness down, and teardown resolves the table itself for the message-thread
   path, so the callbacks below hand it the cached table pointer. */
void teardownHarness(const UIRepoNativeApiV1* api);

void onCloseActivate(UIRepoDocumentHandle document, UIRepoNodeHandle, std::uint32_t eventType, void*) {
    if (g_api == nullptr || document != g_document || eventType != UIREPO_EVENT_ACTIVATE) {
        return;
    }
    spdlog::info("UIRepo harness: Close button pressed; releasing the lease");
    teardownHarness(g_api);
}

/* Escape reaches this handler only because the document holds the escape lease;
   without it the game would open the pause menu first. Returning 1 marks the key
   handled so it stops here. Any other key falls through to the game (return 0). */
std::uint8_t onDocumentKey(
    UIRepoDocumentHandle document,
    const UIRepoKeyEventV1* event,
    void*) {
    if (g_api == nullptr || document != g_document || event == nullptr) {
        return 0;
    }
    if (event->type == UIREPO_KEY_DOWN && event->keyCode == kKeyEscape) {
        spdlog::info("UIRepo harness: Escape pressed; releasing the lease");
        teardownHarness(g_api);
        return 1;
    }
    return 0;
}

/* ---- build / acquire / teardown ---- */

/* Builds the panel and every control, and registers all handlers, but does NOT
   yet make anything visible or focused. Returns false and unwinds on any hard
   refusal. */
bool buildHarness(const UIRepoNativeApiV1* api) {
    const UIRepoDocumentHandle document = api->createDocument("uirepo-input-harness");
    if (document == 0) {
        reportRefusal(api, "createDocument");
        return false;
    }
    g_document = document;

    const UIRepoNodeHandle root = api->getRootNode(document);
    if (root == 0) {
        reportRefusal(api, "getRootNode");
        return false;
    }

    constexpr UIRepoColorV1 kBg{18, 20, 26, 250};
    constexpr UIRepoColorV1 kHeaderBg{27, 31, 41, 255};
    constexpr UIRepoColorV1 kTileBg{31, 35, 46, 255};
    constexpr UIRepoColorV1 kAccent{240, 200, 120, 255};
    constexpr UIRepoColorV1 kAccentBar{240, 200, 120, 150};
    constexpr UIRepoColorV1 kTextMuted{130, 138, 150, 255};
    constexpr UIRepoColorV1 kTextLabel{192, 200, 210, 255};
    constexpr UIRepoColorV1 kDivider{255, 255, 255, 20};
    constexpr UIRepoColorV1 kCloseBg{120, 52, 60, 255};

    const UIRepoNodeHandle card =
        api->createStack(document, root, "harness-card", UIREPO_AXIS_COLUMN);
    if (card == 0) {
        reportRefusal(api, "createStack(card)");
        return false;
    }

    UIRepoStyleV1 cardStyle{};
    cardStyle.size = sizeof(cardStyle);
    cardStyle.width = UIRepoLengthV1{UIREPO_LENGTH_PIXELS, 480.0f};
    cardStyle.height = UIRepoLengthV1{UIREPO_LENGTH_AUTO, 0.0f};
    cardStyle.margin = UIRepoThicknessV1{0.0f, 120.0f, 0.0f, 0.0f};
    cardStyle.alignItems = UIREPO_ALIGN_STRETCH;
    cardStyle.background = kBg;
    cardStyle.opacity = 1.0f;
    cardStyle.cornerRadius = 12.0f;
    cardStyle.clipToBounds = 1;
    if (api->setNodeStyle(document, card, &cardStyle) == 0) {
        reportRefusal(api, "setNodeStyle(card)");
        return false;
    }

    const auto makeText = [&](UIRepoNodeHandle parent, const char* id, const char* value,
                              float size, UIRepoColorV1 color) -> UIRepoNodeHandle {
        const UIRepoNodeHandle n = api->createText(document, parent, id, value);
        if (n != 0) {
            (void)api->setTextFontSize(document, n, size);
            (void)api->setTextColor(document, n, color);
        }
        return n;
    };
    const auto makeStack = [&](UIRepoNodeHandle parent, const char* id, std::uint32_t axis,
                               const UIRepoStyleV1& s) -> UIRepoNodeHandle {
        const UIRepoNodeHandle n = api->createStack(document, parent, id, axis);
        if (n != 0) {
            (void)api->setNodeStyle(document, n, &s);
        }
        return n;
    };
    const auto makeDivider = [&](UIRepoNodeHandle parent, const char* id, UIRepoColorV1 color) {
        const UIRepoNodeHandle n = api->createPanel(document, parent, id);
        if (n != 0) {
            UIRepoStyleV1 s{};
            s.size = sizeof(s);
            s.height = UIRepoLengthV1{UIREPO_LENGTH_PIXELS, 2.0f};
            s.background = color;
            s.opacity = 1.0f;
            (void)api->setNodeStyle(document, n, &s);
        }
    };
    const auto growSpacer = [&](UIRepoNodeHandle node) {
        if (node != 0) {
            UIRepoStyleV1 s{};
            s.size = sizeof(s);
            s.flexGrow = 1.0f;
            s.opacity = 1.0f;
            (void)api->setNodeStyle(document, node, &s);
        }
    };

    UIRepoStyleV1 headerStyle{};
    headerStyle.size = sizeof(headerStyle);
    headerStyle.padding = UIRepoThicknessV1{22.0f, 16.0f, 22.0f, 14.0f};
    headerStyle.gap = 2.0f;
    headerStyle.alignItems = UIREPO_ALIGN_START;
    headerStyle.background = kHeaderBg;
    headerStyle.opacity = 1.0f;
    const UIRepoNodeHandle header = makeStack(card, "harness-header", UIREPO_AXIS_COLUMN, headerStyle);
    (void)makeText(header, "harness-title", "UIRepo", 26.0f, kAccent);
    (void)makeText(header, "harness-subtitle", "native UI runtime  -  input test", 12.0f, kTextMuted);

    makeDivider(card, "harness-accent", kAccentBar);

    UIRepoStyleV1 bodyStyle{};
    bodyStyle.size = sizeof(bodyStyle);
    bodyStyle.padding = UIRepoThicknessV1{22.0f, 18.0f, 22.0f, 18.0f};
    bodyStyle.gap = 16.0f;
    bodyStyle.alignItems = UIREPO_ALIGN_STRETCH;
    const UIRepoNodeHandle body = makeStack(card, "harness-body", UIREPO_AXIS_COLUMN, bodyStyle);
    if (body == 0) {
        reportRefusal(api, "createStack(body)");
        return false;
    }

    UIRepoStyleV1 tileStyle{};
    tileStyle.size = sizeof(tileStyle);
    tileStyle.padding = UIRepoThicknessV1{16.0f, 12.0f, 16.0f, 12.0f};
    tileStyle.gap = 2.0f;
    tileStyle.alignItems = UIREPO_ALIGN_START;
    tileStyle.background = kTileBg;
    tileStyle.opacity = 1.0f;
    tileStyle.cornerRadius = 8.0f;
    const UIRepoNodeHandle tile = makeStack(body, "harness-tile", UIREPO_AXIS_COLUMN, tileStyle);
    (void)makeText(tile, "harness-counter-label", "CLICKS", 11.0f, kTextMuted);
    g_counterText = makeText(tile, "harness-counter", "0", 30.0f, kAccent);
    if (g_counterText == 0) {
        reportRefusal(api, "createText(counter)");
        return false;
    }

    g_clickButton = api->createButton(document, body, "harness-click", "Click me");
    if (g_clickButton == 0) {
        reportRefusal(api, "createButton(click)");
        return false;
    }
    applyButtonBackground(api, kButtonColorA);
    if (api->setNodeEventHandler(document, g_clickButton, &onClickActivate, nullptr) == 0) {
        reportRefusal(api, "setNodeEventHandler(click)");
    }
    if (api->setNodePointerEventHandler(document, g_clickButton, &onButtonPointer, nullptr) == 0) {
        reportRefusal(api, "setNodePointerEventHandler(click)");
    }

    UIRepoStyleV1 rowStyle{};
    rowStyle.size = sizeof(rowStyle);
    rowStyle.gap = 12.0f;
    rowStyle.alignItems = UIREPO_ALIGN_CENTER;
    const UIRepoNodeHandle toggleRow = makeStack(body, "harness-toggle-row", UIREPO_AXIS_ROW, rowStyle);
    growSpacer(makeText(toggleRow, "harness-toggle-label", "Enable feature", 14.0f, kTextLabel));
    g_toggle = api->createToggle(document, toggleRow, "harness-toggle", 0);
    if (g_toggle == 0) {
        reportRefusal(api, "createToggle");
        return false;
    }
    if (api->setNodeEventHandler(document, g_toggle, &onToggleChanged, nullptr) == 0) {
        reportRefusal(api, "setNodeEventHandler(toggle)");
    }

    UIRepoStyleV1 sliderSectionStyle{};
    sliderSectionStyle.size = sizeof(sliderSectionStyle);
    sliderSectionStyle.gap = 8.0f;
    sliderSectionStyle.alignItems = UIREPO_ALIGN_STRETCH;
    const UIRepoNodeHandle sliderSection =
        makeStack(body, "harness-slider-section", UIREPO_AXIS_COLUMN, sliderSectionStyle);
    (void)makeText(sliderSection, "harness-slider-label", "Volume", 14.0f, kTextLabel);
    g_slider = api->createSlider(document, sliderSection, "harness-slider", 0.5f, 0.0f, 1.0f, 0.05f);
    if (g_slider == 0) {
        reportRefusal(api, "createSlider");
        return false;
    }
    if (api->setNodeEventHandler(document, g_slider, &onSliderChanged, nullptr) == 0) {
        reportRefusal(api, "setNodeEventHandler(slider)");
    }

    g_statusText = makeText(body, "harness-status", "Toggle: OFF    Slider: 0.50", 13.0f, kTextMuted);
    if (g_statusText == 0) {
        reportRefusal(api, "createText(status)");
        return false;
    }

    makeDivider(card, "harness-footer-divider", kDivider);

    UIRepoStyleV1 footerStyle{};
    footerStyle.size = sizeof(footerStyle);
    footerStyle.padding = UIRepoThicknessV1{22.0f, 14.0f, 22.0f, 16.0f};
    footerStyle.gap = 10.0f;
    footerStyle.alignItems = UIREPO_ALIGN_CENTER;
    const UIRepoNodeHandle footer = makeStack(card, "harness-footer", UIREPO_AXIS_ROW, footerStyle);
    growSpacer(makeText(footer, "harness-hint", "Esc or Close to exit", 12.0f, kTextMuted));

    const UIRepoNodeHandle closeButton = api->createButton(document, footer, "harness-close", "Close");
    if (closeButton == 0) {
        reportRefusal(api, "createButton(close)");
        return false;
    }
    UIRepoStyleV1 closeStyle{};
    closeStyle.size = sizeof(closeStyle);
    closeStyle.padding = UIRepoThicknessV1{18.0f, 9.0f, 18.0f, 9.0f};
    closeStyle.background = kCloseBg;
    closeStyle.opacity = 1.0f;
    closeStyle.cornerRadius = 6.0f;
    (void)api->setNodeStyle(document, closeButton, &closeStyle);
    if (api->setNodeEventHandler(document, closeButton, &onCloseActivate, nullptr) == 0) {
        reportRefusal(api, "setNodeEventHandler(close)");
    }

    /* The document-level key handler owns Escape for the whole panel; it does
       not depend on which node is focused, only that some node is. */
    if (api->setDocumentKeyEventHandler(document, &onDocumentKey, nullptr) == 0) {
        reportRefusal(api, "setDocumentKeyEventHandler");
    }

    spdlog::info("UIRepo harness: document {} built with interactive controls", document);
    return true;
}

/* Engages the lease in the one order that works (see the file header): record
   requirements, become input-eligible (input + visible), then focus a control -
   which is what makes the per-frame sync engage the OS cursor. */
bool acquireHarness(const UIRepoNativeApiV1* api) {
    if ((api->capabilities & UIREPO_CAP_FOCUS_LEASE) != 0) {
        UIRepoFocusRequirementsV1 requirements{};
        requirements.size = sizeof(requirements);
        requirements.inputMask = 1;   // route game input away while we own focus
        requirements.cursor = 1;      // ask the framework to show the OS cursor
        requirements.escape = 1;      // let our key handler see Escape first
        requirements.pause = 0;       // leave the game running underneath
        if (api->setDocumentFocusRequirements(g_document, &requirements) == 0) {
            reportRefusal(api, "setDocumentFocusRequirements");
        }
    } else {
        spdlog::warn(
            "UIRepo harness: framework build has no focus lease; the cursor will not be "
            "engaged and clicks may not route. Controls are drawn but may be inert.");
    }

    /* Input-enabled AND visible together are what make the document "input
       eligible", and focusNode refuses a node in an ineligible document, so both
       must land before the focus call. */
    if (api->setDocumentInputEnabled(g_document, 1) == 0) {
        reportRefusal(api, "setDocumentInputEnabled");
        return false;
    }
    if (api->setDocumentVisible(g_document, 1) == 0) {
        reportRefusal(api, "setDocumentVisible");
        return false;
    }

    /* Focusing the first control is the trigger: on the next rendered frame the
       framework's per-frame focus sync sees a focused document that wants a
       cursor and engages the lease. This is what puts the cursor on screen. */
    if (api->focusNode(g_document, g_clickButton) == 0) {
        reportRefusal(api, "focusNode");
        /* Not fatal on its own - the panel is still up and pointer clicks can
           re-establish focus - but the cursor will not appear until something
           is focused, so it is worth shouting about. */
        spdlog::error(
            "UIRepo harness: could not focus the first control; the cursor may not appear "
            "until a control is clicked");
    }

    spdlog::info("UIRepo harness: lease acquired; the cursor and controls should be live");
    return true;
}

void teardownHarness(const UIRepoNativeApiV1* api) {
    if (g_document == 0) {
        return;
    }
    const UIRepoDocumentHandle document = g_document;

    if (api != nullptr) {
        /* Drop focus and clear the requirements first: with no focused document
           the per-frame sync releases the cursor, input and escape leases on the
           next frame, so control is already back with the game before the
           document is even destroyed. */
        if (api->clearDocumentFocus != nullptr) {
            (void)api->clearDocumentFocus(document);
        }
        if (api->setDocumentFocusRequirements != nullptr) {
            UIRepoFocusRequirementsV1 inactive{};
            inactive.size = sizeof(inactive);   // all fields zero = wants nothing
            (void)api->setDocumentFocusRequirements(document, &inactive);
        }
        if (api->setDocumentVisible != nullptr) {
            (void)api->setDocumentVisible(document, 0);
        }
        if (api->destroyDocument != nullptr && api->destroyDocument(document) == 0) {
            reportRefusal(api, "destroyDocument");
        }
    }

    /* Whether or not the framework is still around, our handles are now
       meaningless; dropping them is the only correct move. Callbacks that fire
       after this all guard on g_document != 0. */
    g_document = 0;
    g_clickButton = 0;
    g_counterText = 0;
    g_statusText = 0;
    g_toggle = 0;
    g_slider = 0;
    g_clickCount = 0;
    g_altButtonColor = false;
    spdlog::info("UIRepo harness: torn down; control returned to the game");
}

void showHarness() {
    const Api api = uirepo::sdk::loadNativeApi();
    if (!api) {
        spdlog::warn("UIRepo unavailable: {}", uirepo::sdk::describe(api.status()));
        return;
    }

    /* Expected for several frames after load; not an error worth shouting about.
       We retry on the next lifecycle event, exactly as the static sample did. */
    if (api->isRendererReady() == 0) {
        spdlog::info("renderer is not ready yet; will retry on the next session event");
        return;
    }

    if (g_document != 0) {
        return;   // already up
    }

    spdlog::info(
        "resolved UIRepo ABI v{} (capabilities 0x{:x})",
        api.providerVersion(),
        api->capabilities);

    /* Retained documents are the floor: without them there is nothing to build. */
    if (!api.has(UIREPO_CAP_RETAINED_DOCUMENTS)) {
        spdlog::error("this framework build does not offer retained documents; cannot run the harness");
        return;
    }
    /* Pointer events and the focus lease are what make the harness interactive.
       Degrade loudly rather than crash if either is missing: the panel is still
       built and drawn, but the log makes clear why it may not respond. */
    if (!api.has(UIREPO_CAP_POINTER_EVENTS)) {
        spdlog::warn("framework build has no pointer events; hover and click routing will not work");
    }
    if (!api.has(UIREPO_CAP_FOCUS_LEASE)) {
        spdlog::warn("framework build has no focus lease; the OS cursor will not be engaged");
    }
    if (!api.has(UIREPO_CAP_DOCUMENT_KEY_EVENTS)) {
        spdlog::warn("framework build has no document key events; Escape-to-close will not work");
    }

    g_api = api.table();
    if (!buildHarness(g_api)) {
        /* Unwind a partial build so a later event can retry cleanly. */
        teardownHarness(g_api);
        return;
    }
    if (!acquireHarness(g_api)) {
        teardownHarness(g_api);
        return;
    }
    spdlog::info("UIRepo harness is on screen and interactive");
}

void teardownFromMessage() {
    const Api api = uirepo::sdk::loadNativeApi();
    if (api) {
        teardownHarness(api.table());
    } else {
        /* The framework went away first. The handles are already meaningless. */
        teardownHarness(nullptr);
    }
    g_api = nullptr;
}

void onF4SEMessage(F4SE::MessagingInterface::Message* message) {
    if (message == nullptr) {
        return;
    }

    switch (message->type) {
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame:
        /* By this point the framework's own log shows the renderer up and a
           probe frame rendered, which is why this is the documented moment to
           build a document rather than plugin load. */
        showHarness();
        break;
    case F4SE::MessagingInterface::kPreLoadGame:
        /* Tear the harness down before the world unloads so the player is never
           carried into a load with a captured cursor and a live lease. */
        teardownFromMessage();
        break;
    default:
        break;
    }
}

} // namespace

/* No F4SE_PLUGIN_VERSION here: the commonlibf4.plugin xmake rule generates it
   from the target's name, author and version.

   That generated struct is the Next-Gen style, and it is NOT enough on its own.
   On the OG runtime (1.10.163, F4SE 0.6.23) F4SE uses the older Query + Load
   interface and ignores the version struct entirely - a plugin exporting only
   Load is rejected with "does not appear to be an F4SE plugin", which looks
   nothing like a missing export. Any consumer targeting OG needs this. */
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info) {
    if (f4se == nullptr || info == nullptr) {
        return false;
    }

    const auto* data = F4SE::PluginVersionData::GetSingleton();
    if (data == nullptr) {
        return false;
    }

    info->infoVersion = F4SE::PluginInfo::kVersion;
    info->name = data->GetPluginName().data();
    info->version = data->GetPluginVersion().pack();

    if (f4se->IsEditor()) {
        return false;
    }

    return f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* interface) {
    /* This CommonLibF4 sets logging up for us: name the log and it lands in
       Documents/My Games/Fallout4/F4SE. No hand-rolled sink required. */
    F4SE::Init(interface, F4SE::InitInfo{
        .logName = "UIRepoSample_F4",
    });

    const auto* messaging = F4SE::GetMessagingInterface();
    if (messaging == nullptr || !messaging->RegisterListener(onF4SEMessage)) {
        spdlog::critical("could not register the F4SE messaging listener");
        return false;
    }

    spdlog::info("UIRepo interactive test harness loaded");
    return true;
}
