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
    MAKXD_CONTROLLER_SOUTH = 0,
    MAKXD_CONTROLLER_EAST = 1,
    MAKXD_CONTROLLER_WEST = 2,
    MAKXD_CONTROLLER_NORTH = 3,
    MAKXD_CONTROLLER_DPAD_UP = 4,
    MAKXD_CONTROLLER_DPAD_DOWN = 5,
    MAKXD_CONTROLLER_DPAD_LEFT = 6,
    MAKXD_CONTROLLER_DPAD_RIGHT = 7,
    MAKXD_CONTROLLER_LEFT_SHOULDER = 8,
    MAKXD_CONTROLLER_RIGHT_SHOULDER = 9,
    MAKXD_CONTROLLER_LEFT_TRIGGER = 10,
    MAKXD_CONTROLLER_RIGHT_TRIGGER = 11,
    MAKXD_CONTROLLER_LEFT_STICK_X = 12,
    MAKXD_CONTROLLER_LEFT_STICK_Y = 13,
    MAKXD_CONTROLLER_RIGHT_STICK_X = 14,
    MAKXD_CONTROLLER_RIGHT_STICK_Y = 15,
    MAKXD_CONTROLLER_LEFT_STICK_BUTTON = 16,
    MAKXD_CONTROLLER_RIGHT_STICK_BUTTON = 17,
    MAKXD_CONTROLLER_SELECT = 18,
    MAKXD_CONTROLLER_START = 19,
    MAKXD_CONTROLLER_MODE = 20,
    MAKXD_CONTROLLER_GRIP_LEFT = 21,
    MAKXD_CONTROLLER_GRIP_RIGHT = 22,
    MAKXD_CONTROLLER_EXTRA_1 = 23,
    MAKXD_CONTROLLER_EXTRA_2 = 24,
    MAKXD_CONTROLLER_EXTRA_3 = 25,
    MAKXD_CONTROLLER_EXTRA_4 = 26,
    MAKXD_CONTROLLER_EXTRA_5 = 27,
    MAKXD_CONTROLLER_EXTRA_6 = 28,
    MAKXD_CONTROLLER_EXTRA_7 = 29,
    MAKXD_CONTROLLER_EXTRA_8 = 30,
    MAKXD_CONTROLLER_EXTRA_9 = 31,
    MAKXD_CONTROLLER_EXTRA_10 = 32,
    MAKXD_CONTROLLER_EXTRA_11 = 33,
    MAKXD_CONTROLLER_EXTRA_12 = 34,
    MAKXD_CONTROLLER_EXTRA_13 = 35,
    MAKXD_CONTROLLER_EXTRA_14 = 36,
    MAKXD_CONTROLLER_EXTRA_15 = 37,
    MAKXD_CONTROLLER_EXTRA_16 = 38,
    MAKXD_CONTROLLER_EXTRA_17 = 39,
    MAKXD_CONTROLLER_EXTRA_18 = 40,
    MAKXD_CONTROLLER_EXTRA_19 = 41,
    MAKXD_CONTROLLER_EXTRA_20 = 42,
    MAKXD_CONTROLLER_EXTRA_21 = 43,
    MAKXD_CONTROLLER_EXTRA_22 = 44,
    MAKXD_CONTROLLER_EXTRA_23 = 45,
    MAKXD_CONTROLLER_EXTRA_24 = 46,
    MAKXD_CONTROLLER_EXTRA_25 = 47,
    MAKXD_CONTROLLER_EXTRA_26 = 48,
    MAKXD_CONTROLLER_EXTRA_27 = 49,
    MAKXD_CONTROLLER_EXTRA_28 = 50,
    MAKXD_CONTROLLER_EXTRA_29 = 51,
    MAKXD_CONTROLLER_EXTRA_30 = 52,
    MAKXD_CONTROLLER_EXTRA_31 = 53,
    MAKXD_CONTROLLER_EXTRA_32 = 54
} makxd_controller_control_t;

typedef enum {
    MAKXD_CONTROLLER_MASK_DISABLED = 0,
    MAKXD_CONTROLLER_MASK_COMPLETE = 1,
    MAKXD_CONTROLLER_MASK_NEGATIVE = 2,
    MAKXD_CONTROLLER_MASK_POSITIVE = 3,
    MAKXD_CONTROLLER_MASK_BOTH = 4
} makxd_controller_mask_mode_t;

typedef enum {
    MAKXD_DEVICE_NONE = 0x00,
    MAKXD_DEVICE_MOUSE = 0x01,
    MAKXD_DEVICE_KEYBOARD = 0x02,
    MAKXD_DEVICE_GENERIC_HID = 0x04,
    MAKXD_DEVICE_DS4 = 0x08,
    MAKXD_DEVICE_DUALSENSE_DS5 = 0x10,
    MAKXD_DEVICE_DUALSENSE_EDGE = 0x20,
    MAKXD_DEVICE_XBOX_GIP = 0x40,
    MAKXD_DEVICE_XBOX_360_XINPUT = 0x80
} makxd_device_kind_t;

typedef enum {
    MAKXD_STATUS_DISCONNECTED = 0,
    MAKXD_STATUS_CONNECTING = 1,
    MAKXD_STATUS_CONNECTED = 2,
    MAKXD_STATUS_CONNECTION_ERROR = 3
} makxd_connection_status_t;

typedef enum {
    MAKXD_CONNECTION_COM = 0,
    MAKXD_CONNECTION_UDP = 1,
    MAKXD_CONNECTION_BLE = 2
} makxd_connection_method_t;

typedef enum {
    MAKXD_UDP_HOST = 0,
    MAKXD_UDP_RAW = 1
} makxd_udp_wire_mode_t;

typedef bool (*makxd_ble_connect_t)(
    const char* address, void* user_data);
typedef bool (*makxd_ble_write_t)(
    const uint8_t* bytes, size_t byte_count, void* user_data);
typedef size_t (*makxd_ble_read_t)(
    uint8_t* bytes, size_t byte_capacity, void* user_data);
typedef void (*makxd_ble_close_t)(void* user_data);

typedef struct {
    makxd_connection_method_t method;
    const char* aes128_key_hex;
    const char* com_port;
    const char* udp_host;
    uint16_t udp_port;
    makxd_udp_wire_mode_t udp_mode;
    const char* udp_bind_address;
    const char* udp_interface;
    uint16_t vlan_id;
    const char* ble_address;
    makxd_ble_connect_t ble_connect;
    makxd_ble_write_t ble_write;
    makxd_ble_read_t ble_read;
    makxd_ble_close_t ble_close;
    void* ble_user_data;
} makxd_connection_config_t;

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

typedef struct {
    uint64_t digital;
    uint16_t left_trigger;
    uint16_t right_trigger;
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
} makxd_controller_state_t;

typedef struct {
    uint32_t buttons;
    uint8_t hat;
    uint16_t lt;
    uint16_t rt;
    int16_t x;
    int16_t y;
    int16_t rx;
    int16_t ry;
    int16_t z;
    int16_t rz;
} makxd_controller_stream_state_t;

typedef struct {
    uint8_t kinds;
} makxd_device_kinds_t;

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
MAKXD_C_API makxd_device_t* makxd_device_create_with_transport(
    bool encryption_enabled, const char* encryption_key_hex);
MAKXD_C_API void makxd_device_destroy(makxd_device_t* device);

// Static device discovery
MAKXD_C_API int makxd_find_devices(makxd_device_info_t* devices, int max_devices);
MAKXD_C_API makxd_error_t makxd_find_first_device(char* port, size_t port_size);

// Connection management
MAKXD_C_API makxd_error_t makxd_connect(makxd_device_t* device, const char* port);
MAKXD_C_API makxd_error_t makxd_connect_config(
    makxd_device_t* device, const makxd_connection_config_t* connection);
MAKXD_C_API void makxd_disconnect(makxd_device_t* device);
MAKXD_C_API bool makxd_is_connected(makxd_device_t* device);
MAKXD_C_API makxd_connection_status_t makxd_get_status(makxd_device_t* device);

// Device information
MAKXD_C_API makxd_error_t makxd_get_device_info(makxd_device_t* device, makxd_device_info_t* info);
MAKXD_C_API makxd_error_t makxd_get_device_kinds(
    makxd_device_t* device, makxd_device_kinds_t* kinds);

// Mouse button control
MAKXD_C_API makxd_error_t makxd_mouse_down(makxd_device_t* device, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_mouse_down_dt(makxd_device_t* device, makxd_mouse_button_t button, uint16_t dt_uframes);
MAKXD_C_API makxd_error_t makxd_mouse_up(makxd_device_t* device, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_mouse_up_dt(makxd_device_t* device, makxd_mouse_button_t button, uint16_t dt_uframes);
MAKXD_C_API makxd_error_t makxd_mouse_click(makxd_device_t* device, makxd_mouse_button_t button);
MAKXD_C_API makxd_error_t makxd_mouse_button_mask(
    makxd_device_t* device, makxd_mouse_button_t button, bool enabled);
MAKXD_C_API makxd_error_t makxd_mouse_move_mask(
    makxd_device_t* device, bool left, bool right, bool down, bool up);
MAKXD_C_API makxd_error_t makxd_mouse_wheel_mask(
    makxd_device_t* device, bool down, bool up);

// Mouse button state queries
MAKXD_C_API makxd_error_t makxd_mouse_button_state(makxd_device_t* device, makxd_mouse_button_t button, bool* state);

// Mouse movement
MAKXD_C_API makxd_error_t makxd_mouse_move(makxd_device_t* device, int32_t x, int32_t y);
MAKXD_C_API makxd_error_t makxd_mouse_move_dt(makxd_device_t* device, int32_t x, int32_t y, uint16_t dt_uframes);

// Mouse drag operations
MAKXD_C_API makxd_error_t makxd_mouse_drag(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y);

// Mouse wheel
MAKXD_C_API makxd_error_t makxd_mouse_wheel(makxd_device_t* device, int32_t delta);
MAKXD_C_API makxd_error_t makxd_mouse_wheel_dt(makxd_device_t* device, int32_t delta, uint16_t dt_uframes);

// Keyboard control by USB HID usage.
MAKXD_C_API makxd_error_t makxd_keyboard_down(makxd_device_t* device, uint8_t key);
MAKXD_C_API makxd_error_t makxd_keyboard_down_dt(makxd_device_t* device, uint8_t key, uint16_t dt_uframes);
MAKXD_C_API makxd_error_t makxd_keyboard_up(makxd_device_t* device, uint8_t key);
MAKXD_C_API makxd_error_t makxd_keyboard_up_dt(makxd_device_t* device, uint8_t key, uint16_t dt_uframes);
MAKXD_C_API makxd_error_t makxd_keyboard_init(makxd_device_t* device);
MAKXD_C_API makxd_error_t makxd_keyboard_init_dt(makxd_device_t* device, uint16_t dt_uframes);

MAKXD_C_API makxd_error_t makxd_controller_control_get(
    makxd_device_t* device, makxd_controller_control_t control, int32_t* value);
MAKXD_C_API makxd_error_t makxd_controller_control(
    makxd_device_t* device, makxd_controller_control_t control, int32_t value);
MAKXD_C_API makxd_error_t makxd_controller_control_dt(
    makxd_device_t* device, makxd_controller_control_t control,
    int32_t value, uint16_t dt_uframes);
MAKXD_C_API makxd_error_t makxd_controller_mask(
    makxd_device_t* device, makxd_controller_control_t control,
    makxd_controller_mask_mode_t mode);
MAKXD_C_API makxd_error_t makxd_controller_state_get(
    makxd_device_t* device, makxd_controller_state_t* state);
MAKXD_C_API makxd_error_t makxd_controller_state_set(
    makxd_device_t* device, const makxd_controller_state_t* state);
MAKXD_C_API makxd_error_t makxd_controller_state_set_dt(
    makxd_device_t* device, const makxd_controller_state_t* state,
    uint16_t dt_uframes);
MAKXD_C_API bool makxd_controller_stream_decode(
    const uint8_t* values, size_t values_size,
    makxd_controller_stream_state_t* state);

// Button monitoring
MAKXD_C_API makxd_error_t makxd_enable_button_monitoring(makxd_device_t* device, bool enable);
MAKXD_C_API makxd_error_t makxd_is_button_monitoring_enabled(makxd_device_t* device, bool* enabled);
MAKXD_C_API makxd_error_t makxd_get_button_mask(makxd_device_t* device, uint8_t* mask);

// Callbacks
MAKXD_C_API makxd_error_t makxd_set_mouse_button_callback(makxd_device_t* device, makxd_mouse_button_callback_t callback, void* user_data);
MAKXD_C_API makxd_error_t makxd_set_connection_callback(makxd_device_t* device, makxd_connection_callback_t callback, void* user_data);

// High-level automation
MAKXD_C_API makxd_error_t makxd_click_sequence(makxd_device_t* device, const makxd_mouse_button_t* buttons, size_t count, uint32_t delay_ms);

// Performance mode
MAKXD_C_API makxd_error_t makxd_enable_high_performance_mode(makxd_device_t* device, bool enable);
MAKXD_C_API makxd_error_t makxd_is_high_performance_mode_enabled(makxd_device_t* device, bool* enabled);

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
