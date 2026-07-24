#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Export macros for C API
#ifdef _WIN32
    #ifdef MAKXD_EXPORTS
        // Building shared library - export symbols
        #define MAKXD_C_API __declspec(dllexport)
    #elif defined(MAKXD_SHARED)
        // Using shared library - import symbols
        #define MAKXD_C_API __declspec(dllimport)
    #else
        // Using static library - no decoration needed
        #define MAKXD_C_API
    #endif
#else
    // Non-Windows platforms
    #ifdef __GNUC__
        #define MAKXD_C_API __attribute__((visibility("default")))
    #else
        #define MAKXD_C_API
    #endif
#endif

// Forward declarations - opaque types
typedef struct makxd_device makxd_device_t;
typedef struct makxd_batch_builder makxd_batch_builder_t;

// Enums (C-compatible)
typedef enum {
    MAKXD_MOUSE_LEFT = 0,
    MAKXD_MOUSE_RIGHT = 1,
    MAKXD_MOUSE_MIDDLE = 2,
    MAKXD_MOUSE_SIDE1 = 3,
    MAKXD_MOUSE_SIDE2 = 4,
    MAKXD_MOUSE_UNKNOWN = 255
} makxd_mouse_button_t;

typedef enum {
    MAKXD_STATUS_DISCONNECTED = 0,
    MAKXD_STATUS_CONNECTING = 1,
    MAKXD_STATUS_CONNECTED = 2,
    MAKXD_STATUS_CONNECTION_ERROR = 3
} makxd_connection_status_t;

// Simple structs (C-compatible)
typedef struct {
    char port[256];
    char description[256];
    uint16_t vid;
    uint16_t pid;
    bool is_connected;
} makxd_device_info_t;

typedef struct {
    bool left;
    bool right;
    bool middle;
    bool side1;
    bool side2;
} makxd_mouse_button_states_t;

// Callback function pointers
typedef void (*makxd_mouse_button_callback_t)(makxd_mouse_button_t button, bool pressed, void* user_data);
typedef void (*makxd_connection_callback_t)(bool connected, void* user_data);

// Error handling
typedef enum {
    MAKXD_SUCCESS = 0,
    MAKXD_ERROR_INVALID_DEVICE = 1,
    MAKXD_ERROR_CONNECTION_FAILED = 2,
    MAKXD_ERROR_COMMAND_FAILED = 3,
    MAKXD_ERROR_TIMEOUT = 4,
    MAKXD_ERROR_INVALID_PARAMETER = 5,
    MAKXD_ERROR_OUT_OF_MEMORY = 6
} makxd_error_t;

// Get error message string
MAKXD_C_API const char* makxd_error_string(makxd_error_t error);

// Device management
MAKXD_C_API makxd_device_t* makxd_device_create(void);
MAKXD_C_API void makxd_device_destroy(makxd_device_t* device);

// Static device discovery
MAKXD_C_API int makxd_find_devices(makxd_device_info_t* devices, int max_devices);
MAKXD_C_API makxd_error_t makxd_find_first_device(char* port, size_t port_size);

// Connection management
MAKXD_C_API makxd_error_t makxd_connect(makxd_device_t* device, const char* port);
MAKXD_C_API void makxd_disconnect(makxd_device_t* device);
MAKXD_C_API bool makxd_is_connected(makxd_device_t* device);
MAKXD_C_API makxd_connection_status_t makxd_get_status(makxd_device_t* device);

// Device information
MAKXD_C_API makxd_error_t makxd_get_device_info(makxd_device_t* device, makxd_device_info_t* info);
MAKXD_C_API makxd_error_t makxd_get_version(makxd_device_t* device, char* version, size_t version_size);

// Mouse button control
MAKXD_C_API makxd_error_t makxd_mouse_down(makxd_device_t* device, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_mouse_up(makxd_device_t* device, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_mouse_click(makxd_device_t* device, makxd_mouse_button_t button);

// Mouse button state queries
MAKXD_C_API makxd_error_t makxd_mouse_button_state(makxd_device_t* device, makxd_mouse_button_t button, bool* state);

// Mouse movement
MAKXD_C_API makxd_error_t makxd_mouse_move(makxd_device_t* device, int32_t x, int32_t y);
MAKXD_C_API makxd_error_t makxd_mouse_silent_move(makxd_device_t* device, int32_t x, int32_t y);
MAKXD_C_API makxd_error_t makxd_mouse_move_smooth(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments);
MAKXD_C_API makxd_error_t makxd_mouse_move_bezier(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y);
MAKXD_C_API makxd_error_t makxd_mouse_move_controls(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2);
MAKXD_C_API makxd_error_t makxd_mouse_move_to(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments);
MAKXD_C_API makxd_error_t makxd_mouse_move_to_controls(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2);
MAKXD_C_API makxd_error_t makxd_mouse_click_count(makxd_device_t* device, makxd_mouse_button_t button, uint32_t count, uint32_t delay_ms);
MAKXD_C_API makxd_error_t makxd_get_mouse_position(makxd_device_t* device, char* response, size_t response_size);
MAKXD_C_API makxd_error_t makxd_set_mouse_screen(makxd_device_t* device, int32_t width, int32_t height);
MAKXD_C_API makxd_error_t makxd_get_mouse_screen(makxd_device_t* device, char* response, size_t response_size);
MAKXD_C_API makxd_error_t makxd_set_axis_stream(makxd_device_t* device, const char* mode, uint32_t period_ms);
MAKXD_C_API makxd_error_t makxd_get_axis_stream(makxd_device_t* device, char* response, size_t response_size);
MAKXD_C_API makxd_error_t makxd_set_mouse_stream(makxd_device_t* device, const char* mode, uint32_t period_ms);
MAKXD_C_API makxd_error_t makxd_get_mouse_stream(makxd_device_t* device, char* response, size_t response_size);
MAKXD_C_API makxd_error_t makxd_set_button_stream(makxd_device_t* device, const char* mode, uint32_t period_ms);
MAKXD_C_API makxd_error_t makxd_get_button_stream(makxd_device_t* device, char* response, size_t response_size);
MAKXD_C_API makxd_error_t makxd_set_echo(makxd_device_t* device, bool enabled);
MAKXD_C_API makxd_error_t makxd_get_echo(makxd_device_t* device, char* response, size_t response_size);

// Mouse drag operations
MAKXD_C_API makxd_error_t makxd_mouse_drag(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y);
MAKXD_C_API makxd_error_t makxd_mouse_drag_smooth(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments);
MAKXD_C_API makxd_error_t makxd_mouse_drag_bezier(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y);

// Mouse wheel
MAKXD_C_API makxd_error_t makxd_mouse_wheel(makxd_device_t* device, int32_t delta);

// Mouse locking
MAKXD_C_API makxd_error_t makxd_lock_mouse_x(makxd_device_t* device, bool lock);
MAKXD_C_API makxd_error_t makxd_lock_mouse_y(makxd_device_t* device, bool lock);
MAKXD_C_API makxd_error_t makxd_lock_mouse_left(makxd_device_t* device, bool lock);
MAKXD_C_API makxd_error_t makxd_lock_mouse_middle(makxd_device_t* device, bool lock);
MAKXD_C_API makxd_error_t makxd_lock_mouse_right(makxd_device_t* device, bool lock);
MAKXD_C_API makxd_error_t makxd_lock_mouse_side1(makxd_device_t* device, bool lock);
MAKXD_C_API makxd_error_t makxd_lock_mouse_side2(makxd_device_t* device, bool lock);

// Lock state queries
MAKXD_C_API makxd_error_t makxd_is_mouse_x_locked(makxd_device_t* device, bool* locked);
MAKXD_C_API makxd_error_t makxd_is_mouse_y_locked(makxd_device_t* device, bool* locked);
MAKXD_C_API makxd_error_t makxd_is_mouse_left_locked(makxd_device_t* device, bool* locked);
MAKXD_C_API makxd_error_t makxd_is_mouse_middle_locked(makxd_device_t* device, bool* locked);
MAKXD_C_API makxd_error_t makxd_is_mouse_right_locked(makxd_device_t* device, bool* locked);
MAKXD_C_API makxd_error_t makxd_is_mouse_side1_locked(makxd_device_t* device, bool* locked);
MAKXD_C_API makxd_error_t makxd_is_mouse_side2_locked(makxd_device_t* device, bool* locked);

// Mouse input catching
MAKXD_C_API makxd_error_t makxd_catch_mouse_left(makxd_device_t* device, uint8_t* result);
MAKXD_C_API makxd_error_t makxd_catch_mouse_middle(makxd_device_t* device, uint8_t* result);
MAKXD_C_API makxd_error_t makxd_catch_mouse_right(makxd_device_t* device, uint8_t* result);
MAKXD_C_API makxd_error_t makxd_catch_mouse_side1(makxd_device_t* device, uint8_t* result);
MAKXD_C_API makxd_error_t makxd_catch_mouse_side2(makxd_device_t* device, uint8_t* result);
MAKXD_C_API makxd_error_t makxd_set_catch_mouse_left(makxd_device_t* device, uint8_t value);
MAKXD_C_API makxd_error_t makxd_set_catch_mouse_middle(makxd_device_t* device, uint8_t value);
MAKXD_C_API makxd_error_t makxd_set_catch_mouse_right(makxd_device_t* device, uint8_t value);
MAKXD_C_API makxd_error_t makxd_set_catch_mouse_side1(makxd_device_t* device, uint8_t value);
MAKXD_C_API makxd_error_t makxd_set_catch_mouse_side2(makxd_device_t* device, uint8_t value);

// Button monitoring
MAKXD_C_API makxd_error_t makxd_enable_button_monitoring(makxd_device_t* device, bool enable);
MAKXD_C_API makxd_error_t makxd_is_button_monitoring_enabled(makxd_device_t* device, bool* enabled);
MAKXD_C_API makxd_error_t makxd_get_button_mask(makxd_device_t* device, uint8_t* mask);

// Serial spoofing
MAKXD_C_API makxd_error_t makxd_get_mouse_serial(makxd_device_t* device, char* serial, size_t serial_size);
MAKXD_C_API makxd_error_t makxd_set_mouse_serial(makxd_device_t* device, const char* serial);
MAKXD_C_API makxd_error_t makxd_reset_mouse_serial(makxd_device_t* device);

// Device control
MAKXD_C_API makxd_error_t makxd_set_baud_rate(makxd_device_t* device, uint32_t baud_rate);
MAKXD_C_API makxd_error_t makxd_get_baud_rate(makxd_device_t* device, char* response, size_t response_size);

// Callbacks
MAKXD_C_API makxd_error_t makxd_set_mouse_button_callback(makxd_device_t* device, makxd_mouse_button_callback_t callback, void* user_data);
MAKXD_C_API makxd_error_t makxd_set_connection_callback(makxd_device_t* device, makxd_connection_callback_t callback, void* user_data);

// High-level automation
MAKXD_C_API makxd_error_t makxd_click_sequence(makxd_device_t* device, const makxd_mouse_button_t* buttons, size_t count, uint32_t delay_ms);

// Move pattern - simplified version
typedef struct {
    int32_t x;
    int32_t y;
} makxd_point_t;

MAKXD_C_API makxd_error_t makxd_move_pattern(makxd_device_t* device, const makxd_point_t* points, size_t count, bool smooth, uint32_t segments);

// Performance mode
MAKXD_C_API makxd_error_t makxd_enable_high_performance_mode(makxd_device_t* device, bool enable);
MAKXD_C_API makxd_error_t makxd_is_high_performance_mode_enabled(makxd_device_t* device, bool* enabled);

// Batch operations
MAKXD_C_API makxd_batch_builder_t* makxd_create_batch(makxd_device_t* device);
MAKXD_C_API void makxd_batch_destroy(makxd_batch_builder_t* batch);
MAKXD_C_API makxd_error_t makxd_batch_move(makxd_batch_builder_t* batch, int32_t x, int32_t y);
MAKXD_C_API makxd_error_t makxd_batch_move_smooth(makxd_batch_builder_t* batch, int32_t x, int32_t y, uint32_t segments);
MAKXD_C_API makxd_error_t makxd_batch_move_bezier(makxd_batch_builder_t* batch, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y);
MAKXD_C_API makxd_error_t makxd_batch_click(makxd_batch_builder_t* batch, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_batch_press(makxd_batch_builder_t* batch, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_batch_release(makxd_batch_builder_t* batch, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_batch_scroll(makxd_batch_builder_t* batch, int32_t delta);
MAKXD_C_API makxd_error_t makxd_batch_drag(makxd_batch_builder_t* batch, makxd_mouse_button_t button, int32_t x, int32_t y);
MAKXD_C_API makxd_error_t makxd_batch_drag_smooth(makxd_batch_builder_t* batch, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments);
MAKXD_C_API makxd_error_t makxd_batch_drag_bezier(makxd_batch_builder_t* batch, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y);
MAKXD_C_API makxd_error_t makxd_batch_execute(makxd_batch_builder_t* batch);

// Raw command interface
MAKXD_C_API makxd_error_t makxd_send_raw_command(makxd_device_t* device, const char* command);
MAKXD_C_API makxd_error_t makxd_receive_raw_response(makxd_device_t* device, char* response, size_t response_size);

// Utility functions
MAKXD_C_API const char* makxd_mouse_button_to_string(makxd_mouse_button_t button);
MAKXD_C_API makxd_mouse_button_t makxd_string_to_mouse_button(const char* button_name);

// Performance profiling
MAKXD_C_API void makxd_profiler_enable(bool enable);
MAKXD_C_API void makxd_profiler_reset_stats(void);

// Performance stats result structure
typedef struct {
    char command_name[64];
    uint64_t call_count;
    uint64_t total_microseconds;
} makxd_perf_stat_t;

MAKXD_C_API int makxd_profiler_get_stats(makxd_perf_stat_t* stats, int max_stats);

#ifdef __cplusplus
}
#endif
