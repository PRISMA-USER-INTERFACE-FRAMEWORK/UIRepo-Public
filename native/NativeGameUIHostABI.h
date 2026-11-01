#pragma once

#include <stdint.h>

#if defined(_WIN32)
#  define NGUI_CALL __cdecl
#else
#  define NGUI_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NGUI_ABI_VERSION 1u

typedef int32_t NguiStatus;
enum {
    NGUI_STATUS_OK = 0,
    NGUI_STATUS_INVALID_ARGUMENT = 1,
    NGUI_STATUS_UNSUPPORTED_VERSION = 2,
    NGUI_STATUS_NOT_INITIALIZED = 3,
    NGUI_STATUS_ALREADY_INITIALIZED = 4,
    NGUI_STATUS_FAULT = 5
};

typedef void (NGUI_CALL *NguiLogUtf8Fn)(int32_t level, const uint8_t* data, uintptr_t length);

/*
 * The host table is copied by value during ngui_initialize. The callback targets and borrowed
 * D3D11 interfaces must remain valid until ngui_shutdown returns.
 */
typedef struct NguiHostApiV1 {
    uint32_t size;
    uint32_t version;
    void* d3d11_device;
    void* d3d11_immediate_context;
    NguiLogUtf8Fn log_utf8;
} NguiHostApiV1;

typedef struct NguiFrameV1 {
    uint32_t size;
    uint32_t version;
    uint64_t frame_index;
    float delta_seconds;
    uint32_t viewport_width;
    uint32_t viewport_height;
    void* back_buffer_texture;
    void* render_target_view;
} NguiFrameV1;

typedef struct NguiPointerEventV1 {
    uint32_t size;
    uint32_t version;
    int32_t type;
    int32_t button;
    float x;
    float y;
    float wheel_x;
    float wheel_y;
    uint32_t modifiers;
} NguiPointerEventV1;

typedef struct NguiKeyEventV1 {
    uint32_t size;
    uint32_t version;
    int32_t type;
    int32_t virtual_key;
    int32_t scan_code;
    uint32_t modifiers;
    uint32_t character;
    int32_t is_repeat;
} NguiKeyEventV1;

NguiStatus NGUI_CALL ngui_initialize(const NguiHostApiV1* host);
NguiStatus NGUI_CALL ngui_begin_frame(const NguiFrameV1* frame);
NguiStatus NGUI_CALL ngui_resize(uint32_t width, uint32_t height);
NguiStatus NGUI_CALL ngui_pointer_event(const NguiPointerEventV1* event_data);
NguiStatus NGUI_CALL ngui_key_event(const NguiKeyEventV1* event_data);
NguiStatus NGUI_CALL ngui_shutdown(void);

#ifdef __cplusplus
}
#endif
