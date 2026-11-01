#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(UIREPO_NATIVE_API_BUILD)
#define UIREPO_NATIVE_API_EXPORT __declspec(dllexport)
#else
#define UIREPO_NATIVE_API_EXPORT
#endif

/* Compatibility model, in full:

   - A published struct never changes. To add fields, declare a new struct
     (UIRepoStyleV2) and a new table entry; do not grow UIRepoStyleV1. Every
     entry point therefore requires an exact `size`, and a mismatch is refused.
   - The function table only ever grows at the end. Existing entries keep their
     offsets, so a consumer reading the prefix it knows stays correct against a
     newer framework.
   - UIRepo_GetNativeApi accepts any requested version up to
     UIREPO_NATIVE_API_VERSION and refuses anything newer, because a newer
     consumer would read table entries this build does not have. Every
     accepted version receives the same, largest table.
   - Feature presence is advertised by capability bits, not by version number.
     Test the bit; do not infer support from the version.

   docs/abi-compatibility.md carries the reasoning. The rules are enforced by
   NativeApiContractTests.cpp and tests/validate-abi-compatibility.sh. */
#define UIREPO_NATIVE_API_VERSION_V1 1u
#define UIREPO_NATIVE_API_VERSION_V2 2u
#define UIREPO_NATIVE_API_VERSION 2u

typedef uint64_t UIRepoDocumentHandle;
typedef uint64_t UIRepoNodeHandle;
typedef uint64_t UIRepoSurfaceHandle;

enum UIRepoNativeCapabilityV1 {
    UIREPO_CAP_RETAINED_DOCUMENTS = UINT64_C(1) << 0,
    UIREPO_CAP_NATIVE_D3D11 = UINT64_C(1) << 1,
    UIREPO_CAP_DIRECTWRITE_TEXT = UINT64_C(1) << 2,
    UIREPO_CAP_ENGINE_COMPOSITION = UINT64_C(1) << 3,
    UIREPO_CAP_RENDERER_RECOVERY = UINT64_C(1) << 4,
    UIREPO_CAP_DXGI_FALLBACK = UINT64_C(1) << 5,
    UIREPO_CAP_POINTER_INPUT = UINT64_C(1) << 6,
    UIREPO_CAP_KEYBOARD_INPUT = UINT64_C(1) << 7,
    UIREPO_CAP_SCROLL_CONTAINERS = UINT64_C(1) << 8,
    UIREPO_CAP_POINTER_EVENTS = UINT64_C(1) << 9,
    UIREPO_CAP_NATIVE_SETTINGS_CONTROLS = UINT64_C(1) << 10,
    UIREPO_CAP_FOCUS_LEASE = UINT64_C(1) << 11,
    UIREPO_CAP_DOCUMENT_KEY_EVENTS = UINT64_C(1) << 12,
    UIREPO_CAP_TEXT_LAYOUT = UINT64_C(1) << 13,
    UIREPO_CAP_OFFSCREEN_SURFACES = UINT64_C(1) << 14,
    UIREPO_CAP_RESULT_CODES = UINT64_C(1) << 15,
    UIREPO_CAP_DOCUMENT_STATE = UINT64_C(1) << 16,
    UIREPO_CAP_DOCUMENT_ENUMERATION = UINT64_C(1) << 17,
    UIREPO_CAP_GAME_THREAD_DISPATCH = UINT64_C(1) << 18,
};

/* Why the most recent call failed.

   Every entry point still returns 0/1 (or a zero handle) - that is frozen and
   will not change. These codes answer the question the boolean cannot: a zero
   from createOffscreenSurface has half a dozen possible causes, and a consumer
   that cannot tell "wrong thread" from "budget exceeded" cannot react
   correctly to either.

   Read with getLastResult() immediately after a call that failed. The value is
   per-thread and is overwritten by the next call on that thread, so capture it
   before doing anything else. */
enum UIRepoResultV1 {
    UIREPO_RESULT_OK = 0,
    /* The call was refused but the framework did not classify why. Not an
       error condition of its own - treat it as "failed, cause unreported". */
    UIREPO_RESULT_UNKNOWN = 1,
    /* A null pointer, an out-of-range enum, or a non-finite float. */
    UIREPO_RESULT_INVALID_ARGUMENT = 2,
    /* A versioned struct's `size` field did not match. Almost always a caller
       that forgot to set it, or one built against a different published shape. */
    UIREPO_RESULT_INVALID_STRUCT_SIZE = 3,
    /* A document, node, or surface handle is unknown, stale, or belongs to
       something else. */
    UIREPO_RESULT_INVALID_HANDLE = 4,
    /* A GPU-touching call was made off the game's render thread. */
    UIREPO_RESULT_WRONG_THREAD = 5,
    /* The renderer or device is not up yet. Retry on a later frame; this is
       expected for several frames after load, not a failure to report. */
    UIREPO_RESULT_NOT_READY = 6,
    /* The capability backing this call is not present in this build. */
    UIREPO_RESULT_UNSUPPORTED = 7,
    /* A hard budget refused the request - per-surface dimensions, or the
       aggregate live byte budget. Asking for less may succeed. */
    UIREPO_RESULT_BUDGET_EXCEEDED = 8,
    /* A GPU resource could not be created, or the device failed. */
    UIREPO_RESULT_RESOURCE_FAILED = 9,
    /* The target is in a state this call forbids - notably a visible or
       input-active document asked to render offscreen. */
    UIREPO_RESULT_STATE_CONFLICT = 10,
    /* An allocation failed. */
    UIREPO_RESULT_OUT_OF_MEMORY = 11,
};

enum UIRepoDocumentHealthV1 {
    UIREPO_DOCUMENT_HEALTH_CREATING = 0,
    UIREPO_DOCUMENT_HEALTH_READY = 1,
    UIREPO_DOCUMENT_HEALTH_LIVE = 2,
    UIREPO_DOCUMENT_HEALTH_HIDDEN = 3,
    UIREPO_DOCUMENT_HEALTH_FAULTED = 4,
    UIREPO_DOCUMENT_HEALTH_DESTROYING = 5,
    UIREPO_DOCUMENT_HEALTH_DESTROYED = 6,
};

enum UIRepoDocumentRoleV1 {
    UIREPO_DOCUMENT_ROLE_HUD = 0,
    UIREPO_DOCUMENT_ROLE_PANEL = 1,
    UIREPO_DOCUMENT_ROLE_OVERLAY = 2,
    UIREPO_DOCUMENT_ROLE_WORLD_SURFACE = 3,
    UIREPO_DOCUMENT_ROLE_DIAGNOSTIC = 4,
};

enum UIRepoNodeTypeV1 {
    UIREPO_NODE_PANEL = 1,
    UIREPO_NODE_STACK = 2,
    UIREPO_NODE_TEXT = 3,
    UIREPO_NODE_BUTTON = 4,
    UIREPO_NODE_IMAGE = 5,
    UIREPO_NODE_VIDEO = 6,
    UIREPO_NODE_TOGGLE = 7,
    UIREPO_NODE_SLIDER = 8,
    UIREPO_NODE_STEPPER = 9,
    UIREPO_NODE_TEXT_INPUT = 10,
};

enum UIRepoAxisV1 {
    UIREPO_AXIS_ROW = 0,
    UIREPO_AXIS_COLUMN = 1,
};

enum UIRepoLengthKindV1 {
    UIREPO_LENGTH_AUTO = 0,
    UIREPO_LENGTH_PIXELS = 1,
    UIREPO_LENGTH_PERCENT = 2,
};

enum UIRepoAlignV1 {
    UIREPO_ALIGN_START = 0,
    UIREPO_ALIGN_CENTER = 1,
    UIREPO_ALIGN_END = 2,
    UIREPO_ALIGN_STRETCH = 3,
};

enum UIRepoTextWrappingV1 {
    UIREPO_TEXT_NO_WRAP = 0,
    UIREPO_TEXT_WRAP = 1,
};

enum UIRepoTextAlignmentV1 {
    UIREPO_TEXT_ALIGN_START = 0,
    UIREPO_TEXT_ALIGN_CENTER = 1,
    UIREPO_TEXT_ALIGN_END = 2,
};

enum UIRepoTextOverflowV1 {
    UIREPO_TEXT_OVERFLOW_CLIP = 0,
    UIREPO_TEXT_OVERFLOW_ELLIPSIS = 1,
};

enum UIRepoVideoPlaybackStateV1 {
    UIREPO_VIDEO_STOPPED = 0,
    UIREPO_VIDEO_PLAYING = 1,
    UIREPO_VIDEO_PAUSED = 2,
};

enum UIRepoNodeEventTypeV1 {
    UIREPO_EVENT_ACTIVATE = 1,
    UIREPO_EVENT_FOCUS_GAINED = 2,
    UIREPO_EVENT_FOCUS_LOST = 3,
    UIREPO_EVENT_VALUE_CHANGED = 4,
};

enum UIRepoPointerEventTypeV1 {
    UIREPO_POINTER_MOVE = 1,
    UIREPO_POINTER_BUTTON_DOWN = 2,
    UIREPO_POINTER_BUTTON_UP = 3,
    UIREPO_POINTER_WHEEL = 4,
    UIREPO_POINTER_HORIZONTAL_WHEEL = 5,
};

enum UIRepoNodePointerEventTypeV1 {
    UIREPO_NODE_POINTER_ENTER = 1,
    UIREPO_NODE_POINTER_LEAVE = 2,
    UIREPO_NODE_POINTER_MOVE = 3,
    UIREPO_NODE_POINTER_BUTTON_DOWN = 4,
    UIREPO_NODE_POINTER_BUTTON_UP = 5,
    UIREPO_NODE_POINTER_WHEEL = 6,
    UIREPO_NODE_POINTER_DRAG_BEGIN = 7,
    UIREPO_NODE_POINTER_DRAG_MOVE = 8,
    UIREPO_NODE_POINTER_DRAG_END = 9,
    UIREPO_NODE_POINTER_CANCEL = 10,
};

enum UIRepoPointerButtonV1 {
    UIREPO_POINTER_BUTTON_NONE = 0,
    UIREPO_POINTER_BUTTON_LEFT = 1,
    UIREPO_POINTER_BUTTON_RIGHT = 2,
    UIREPO_POINTER_BUTTON_MIDDLE = 3,
    UIREPO_POINTER_BUTTON_X1 = 4,
    UIREPO_POINTER_BUTTON_X2 = 5,
};

enum UIRepoKeyEventTypeV1 {
    UIREPO_KEY_DOWN = 1,
    UIREPO_KEY_UP = 2,
};

enum UIRepoInputModifierV1 {
    UIREPO_MODIFIER_SHIFT = 1u << 0,
    UIREPO_MODIFIER_CONTROL = 1u << 1,
    UIREPO_MODIFIER_ALT = 1u << 2,
};

typedef void (*UIRepoNodeEventCallback)(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    uint32_t eventType,
    void* userData);

typedef struct UIRepoLengthV1 {
    uint32_t kind;
    float value;
} UIRepoLengthV1;

typedef struct UIRepoThicknessV1 {
    float left;
    float top;
    float right;
    float bottom;
} UIRepoThicknessV1;

typedef struct UIRepoColorV1 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} UIRepoColorV1;

typedef struct UIRepoStyleV1 {
    uint32_t size;
    UIRepoLengthV1 width;
    UIRepoLengthV1 height;
    float minWidth;
    float minHeight;
    float maxWidth;
    float maxHeight;
    UIRepoThicknessV1 padding;
    UIRepoThicknessV1 margin;
    float gap;
    float flexGrow;
    uint32_t alignItems;
    UIRepoColorV1 background;
    float opacity;
    int32_t zIndex;
    float cornerRadius;
    uint8_t clipToBounds;
    uint8_t reserved[3];
} UIRepoStyleV1;

typedef struct UIRepoTextLayoutV1 {
    uint32_t size;
    uint32_t wrapping;
    uint32_t alignment;
    uint32_t overflow;
} UIRepoTextLayoutV1;

typedef struct UIRepoPointerEventV1 {
    uint32_t size;
    uint32_t type;
    uint32_t button;
    uint32_t modifiers;
    float x;
    float y;
    float wheelDelta;
} UIRepoPointerEventV1;

typedef struct UIRepoNodePointerEventV1 {
    uint32_t size;
    uint32_t type;
    uint32_t button;
    uint32_t modifiers;
    float x;
    float y;
    float wheelDelta;
} UIRepoNodePointerEventV1;

typedef void (*UIRepoNodePointerEventCallback)(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoNodePointerEventV1* event,
    void* userData);

typedef struct UIRepoKeyEventV1 {
    uint32_t size;
    uint32_t type;
    uint32_t keyCode;
    uint32_t modifiers;
    uint8_t repeat;
    uint8_t reserved[3];
} UIRepoKeyEventV1;

typedef uint8_t (*UIRepoDocumentKeyEventCallback)(
    UIRepoDocumentHandle document,
    const UIRepoKeyEventV1* event,
    void* userData);

/* Offscreen surfaces are experimental. Consumers only ever name a surface by
   handle; no D3D11 device, context, texture, or view is exposed across this
   ABI, and presentation of a surface stays inside the framework. Every
   offscreen entry point must be called on the game's render thread and
   returns failure rather than acting when it is not. */
typedef struct UIRepoSurfaceDescV1 {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    UIRepoColorV1 clearColor;
} UIRepoSurfaceDescV1;

typedef struct UIRepoSurfaceInfoV1 {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    UIRepoColorV1 clearColor;
    /* Advances whenever the surface contents were discarded, which happens on
       resize and on device recreation. Re-render when it differs from the
       value observed after the last render. */
    uint64_t generation;
    uint64_t byteSize;
} UIRepoSurfaceInfoV1;

typedef struct UIRepoSurfaceStatsV1 {
    uint32_t size;
    uint32_t reserved;
    uint64_t liveCount;
    uint64_t liveBytes;
    uint64_t pooledCount;
    uint64_t pooledBytes;
    uint64_t allocations;
    uint64_t reuses;
    uint64_t resizes;
    uint64_t deviceRebuilds;
    uint64_t rejections;
    uint64_t poolDrains;
} UIRepoSurfaceStatsV1;

typedef struct UIRepoFocusRequirementsV1 {
    uint32_t size;
    uint8_t inputMask;
    uint8_t cursor;
    uint8_t escape;
    uint8_t pause;
} UIRepoFocusRequirementsV1;

typedef struct UIRepoDocumentStateV1 {
    uint32_t size;
    uint8_t visible;
    uint8_t inputEnabled;
    uint8_t focused;
    uint8_t reserved0;
    uint32_t health;
    uint32_t role;
    int32_t order;
    uint32_t reserved1;
    uint64_t ownerId;
    uint64_t generation;
} UIRepoDocumentStateV1;

typedef void (*UIRepoDocumentEnumerateCallback)(
    UIRepoDocumentHandle document,
    const UIRepoDocumentStateV1* state,
    void* userData);

typedef void (*UIRepoGameThreadCallback)(void* userData);

typedef struct UIRepoNativeApiV1 {
    uint32_t size;
    uint32_t abiVersion;
    uint64_t capabilities;

    uint8_t (*isRendererReady)(void);
    UIRepoDocumentHandle (*createDocument)(const char* rootId);
    uint8_t (*destroyDocument)(UIRepoDocumentHandle document);
    uint8_t (*setDocumentVisible)(UIRepoDocumentHandle document, uint8_t visible);
    uint8_t (*setDocumentOrder)(UIRepoDocumentHandle document, int32_t order);
    uint8_t (*setDocumentInputEnabled)(UIRepoDocumentHandle document, uint8_t enabled);
    UIRepoNodeHandle (*getRootNode)(UIRepoDocumentHandle document);

    UIRepoNodeHandle (*createPanel)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id);
    UIRepoNodeHandle (*createStack)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, uint32_t axis);
    UIRepoNodeHandle (*createText)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, const char* text);
    UIRepoNodeHandle (*createButton)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, const char* text);
    UIRepoNodeHandle (*createImage)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, const char* source, float naturalWidth, float naturalHeight);
    UIRepoNodeHandle (*createVideo)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, const char* source, float naturalWidth, float naturalHeight);

    uint8_t (*setNodeVisible)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint8_t visible);
    uint8_t (*setNodeEnabled)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint8_t enabled);
    uint8_t (*setNodeStyle)(UIRepoDocumentHandle document, UIRepoNodeHandle node, const UIRepoStyleV1* style);
    uint8_t (*setNodeEventHandler)(UIRepoDocumentHandle document, UIRepoNodeHandle node, UIRepoNodeEventCallback callback, void* userData);
    uint8_t (*focusNode)(UIRepoDocumentHandle document, UIRepoNodeHandle node);
    uint8_t (*clearDocumentFocus)(UIRepoDocumentHandle document);
    uint8_t (*setText)(UIRepoDocumentHandle document, UIRepoNodeHandle node, const char* text);
    uint8_t (*setTextFontSize)(UIRepoDocumentHandle document, UIRepoNodeHandle node, float fontSize);
    uint8_t (*setTextColor)(UIRepoDocumentHandle document, UIRepoNodeHandle node, UIRepoColorV1 color);
    uint8_t (*setMediaSource)(UIRepoDocumentHandle document, UIRepoNodeHandle node, const char* source);
    uint8_t (*setVideoPlayback)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint32_t state, uint8_t loop, float volume);

    uint8_t (*routePointerEvent)(const UIRepoPointerEventV1* event);
    uint8_t (*routeKeyEvent)(const UIRepoKeyEventV1* event);

    uint8_t (*setStackScrollable)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint8_t scrollable);
    uint8_t (*setNodePointerEventHandler)(UIRepoDocumentHandle document, UIRepoNodeHandle node, UIRepoNodePointerEventCallback callback, void* userData);

    UIRepoNodeHandle (*createToggle)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, uint8_t value);
    UIRepoNodeHandle (*createSlider)(UIRepoDocumentHandle document, UIRepoNodeHandle parent, const char* id, float value, float minimum, float maximum, float step);
    uint8_t (*setToggleValue)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint8_t value);
    uint8_t (*getToggleValue)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint8_t* value);
    uint8_t (*setSliderValue)(UIRepoDocumentHandle document, UIRepoNodeHandle node, float value);
    uint8_t (*getSliderValue)(UIRepoDocumentHandle document, UIRepoNodeHandle node, float* value);

    UIRepoNodeHandle (*createStepper)(
        UIRepoDocumentHandle document,
        UIRepoNodeHandle parent,
        const char* id,
        const char* const* options,
        uint32_t optionCount,
        uint32_t index);
    uint8_t (*setStepperIndex)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint32_t index);
    uint8_t (*getStepperIndex)(UIRepoDocumentHandle document, UIRepoNodeHandle node, uint32_t* index);

    UIRepoNodeHandle (*createTextInput)(
        UIRepoDocumentHandle document,
        UIRepoNodeHandle parent,
        const char* id,
        const char* value,
        uint32_t maxBytes);
    uint8_t (*setTextInputValue)(UIRepoDocumentHandle document, UIRepoNodeHandle node, const char* value);
    uint8_t (*getTextInputValue)(
        UIRepoDocumentHandle document,
        UIRepoNodeHandle node,
        char* buffer,
        uint32_t capacity,
        uint32_t* required);

    uint8_t (*setDocumentKeyEventHandler)(
        UIRepoDocumentHandle document,
        UIRepoDocumentKeyEventCallback callback,
        void* userData);
    uint8_t (*setDocumentFocusRequirements)(
        UIRepoDocumentHandle document,
        const UIRepoFocusRequirementsV1* requirements);

    uint8_t (*setTextLayout)(
        UIRepoDocumentHandle document,
        UIRepoNodeHandle node,
        const UIRepoTextLayoutV1* layout);

    /* Experimental offscreen surfaces. Guarded by
       UIREPO_CAP_OFFSCREEN_SURFACES; check the capability bit before calling.
       A surface belongs to exactly one document and is released when that
       document is destroyed. Only a hidden, input-inactive, healthy document
       can be rendered to a surface. */
    UIRepoSurfaceHandle (*createOffscreenSurface)(
        UIRepoDocumentHandle document,
        const UIRepoSurfaceDescV1* desc);
    uint8_t (*destroyOffscreenSurface)(UIRepoSurfaceHandle surface);
    uint8_t (*resizeOffscreenSurface)(
        UIRepoSurfaceHandle surface,
        uint32_t width,
        uint32_t height);
    uint8_t (*setOffscreenSurfaceClearColor)(
        UIRepoSurfaceHandle surface,
        UIRepoColorV1 color);
    uint8_t (*renderDocumentToSurface)(
        UIRepoDocumentHandle document,
        UIRepoSurfaceHandle surface);
    uint8_t (*getOffscreenSurfaceInfo)(
        UIRepoSurfaceHandle surface,
        UIRepoSurfaceInfoV1* info);
    uint8_t (*getOffscreenSurfaceStats)(UIRepoSurfaceStatsV1* stats);

    /* Result codes. Guarded by UIREPO_CAP_RESULT_CODES.

       getLastResult returns why the most recent call on the calling thread
       failed, as a UIRepoResultV1. It is only meaningful immediately after a
       call that reported failure.

       describeResult returns a short static English string for a code and
       never returns null, including for a code this build does not know. */
    uint32_t (*getLastResult)(void);
    const char* (*describeResult)(uint32_t result);
} UIRepoNativeApiV1;

/* V1 stays the first member, so V1 offsets and sizeof(UIRepoNativeApiV1) never move. */
typedef struct UIRepoNativeApiV2 {
    UIRepoNativeApiV1 v1;

    /* UIREPO_CAP_DOCUMENT_STATE. Any thread. The caller sets out->size. */
    uint8_t (*getDocumentState)(UIRepoDocumentHandle document, UIRepoDocumentStateV1* out);

    /* UIREPO_CAP_DOCUMENT_ENUMERATION. Any thread. Callbacks run on the calling thread, outside every framework lock. */
    uint8_t (*enumerateDocuments)(UIRepoDocumentEnumerateCallback callback, void* userData);

    /* UIREPO_CAP_GAME_THREAD_DISPATCH. Any thread. Never records a result. */
    uint8_t (*isGameThread)(void);

    /* UIREPO_CAP_GAME_THREAD_DISPATCH. Returns 1 only when queued; the callback never runs inline. */
    uint8_t (*dispatchToGameThread)(UIRepoGameThreadCallback callback, void* userData);
} UIRepoNativeApiV2;

#ifdef __cplusplus
extern "C" UIREPO_NATIVE_API_EXPORT const UIRepoNativeApiV1* UIRepo_GetNativeApi(uint32_t requestedVersion) noexcept;
#else
UIREPO_NATIVE_API_EXPORT const UIRepoNativeApiV1* UIRepo_GetNativeApi(uint32_t requestedVersion);
#endif
