#include "makxd_c.h"
#include "makxd.h"
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <functional>
#include <mutex>
#include <atomic>
#include <cctype>

extern "C" {

// Internal wrapper structures
struct makxd_callback_state {
    std::mutex mutex;
    makxd_mouse_button_callback_t mouse_callback = nullptr;
    void* mouse_callback_user_data = nullptr;
    makxd_connection_callback_t connection_callback = nullptr;
    void* connection_callback_user_data = nullptr;
};

struct makxd_device {
    std::unique_ptr<makxd::Device> cpp_device;
    std::shared_ptr<makxd_callback_state> callback_state;
    std::shared_ptr<std::atomic<bool>> lifetime_token;

    makxd_device() :
        cpp_device(std::make_unique<makxd::Device>()),
        callback_state(std::make_shared<makxd_callback_state>()),
        lifetime_token(std::make_shared<std::atomic<bool>>(true)) {}
};

struct makxd_batch_builder {
    std::unique_ptr<makxd::Device::BatchCommandBuilder> cpp_batch;
    std::shared_ptr<std::atomic<bool>> owner_lifetime;
    
    makxd_batch_builder(makxd::Device::BatchCommandBuilder&& batch,
                        std::shared_ptr<std::atomic<bool>> lifetime) :
        cpp_batch(std::make_unique<makxd::Device::BatchCommandBuilder>(std::move(batch))),
        owner_lifetime(std::move(lifetime)) {}
};

// Helper functions
static bool try_convert_mouse_button(makxd_mouse_button_t button, makxd::MouseButton& out_button) {
    switch (button) {
        case MAKXD_MOUSE_LEFT:
            out_button = makxd::MouseButton::LEFT;
            return true;
        case MAKXD_MOUSE_RIGHT:
            out_button = makxd::MouseButton::RIGHT;
            return true;
        case MAKXD_MOUSE_MIDDLE:
            out_button = makxd::MouseButton::MIDDLE;
            return true;
        case MAKXD_MOUSE_SIDE1:
            out_button = makxd::MouseButton::SIDE1;
            return true;
        case MAKXD_MOUSE_SIDE2:
            out_button = makxd::MouseButton::SIDE2;
            return true;
        case MAKXD_MOUSE_UNKNOWN:
            return false;
    }
    return false;
}

static makxd_mouse_button_t convert_mouse_button_to_c(makxd::MouseButton button) {
    switch (button) {
        case makxd::MouseButton::LEFT: return MAKXD_MOUSE_LEFT;
        case makxd::MouseButton::RIGHT: return MAKXD_MOUSE_RIGHT;
        case makxd::MouseButton::MIDDLE: return MAKXD_MOUSE_MIDDLE;
        case makxd::MouseButton::SIDE1: return MAKXD_MOUSE_SIDE1;
        case makxd::MouseButton::SIDE2: return MAKXD_MOUSE_SIDE2;
        case makxd::MouseButton::UNKNOWN: return MAKXD_MOUSE_UNKNOWN;
    }
    return MAKXD_MOUSE_UNKNOWN;
}

static makxd_connection_status_t convert_connection_status(makxd::ConnectionStatus status) {
    switch (status) {
        case makxd::ConnectionStatus::DISCONNECTED: return MAKXD_STATUS_DISCONNECTED;
        case makxd::ConnectionStatus::CONNECTING: return MAKXD_STATUS_CONNECTING;
        case makxd::ConnectionStatus::CONNECTED: return MAKXD_STATUS_CONNECTED;
        case makxd::ConnectionStatus::CONNECTION_ERROR: return MAKXD_STATUS_CONNECTION_ERROR;
    }
    return MAKXD_STATUS_DISCONNECTED;
}

static makxd_error_t handle_exception() {
    if (!std::current_exception()) return MAKXD_ERROR_COMMAND_FAILED;
    try {
        throw;
    } catch (const makxd::ConnectionException&) {
        return MAKXD_ERROR_CONNECTION_FAILED;
    } catch (const makxd::CommandException&) {
        return MAKXD_ERROR_COMMAND_FAILED;
    } catch (const makxd::TimeoutException&) {
        return MAKXD_ERROR_TIMEOUT;
    } catch (const makxd::MakxdException&) {
        return MAKXD_ERROR_COMMAND_FAILED;
    } catch (const std::bad_alloc&) {
        return MAKXD_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return MAKXD_ERROR_COMMAND_FAILED;
    }
}

static void safe_copy_string(char* dest, size_t dest_size, const std::string& src) {
    if (dest && dest_size > 0) {
        size_t copy_size = std::min(dest_size - 1, src.size());
        strncpy(dest, src.c_str(), copy_size);
        dest[copy_size] = '\0';
    }
}

static makxd_error_t validate_batch_builder(const makxd_batch_builder_t* batch) {
    if (!batch || !batch->cpp_batch) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    if (!batch->owner_lifetime ||
        !batch->owner_lifetime->load(std::memory_order_acquire)) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    return MAKXD_SUCCESS;
}

static bool equals_ignore_ascii_case(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) {
        return false;
    }

    while (*lhs != '\0' && *rhs != '\0') {
        if (std::toupper(static_cast<unsigned char>(*lhs)) !=
            std::toupper(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
}

// Error handling
const char* makxd_error_string(makxd_error_t error) {
    switch (error) {
        case MAKXD_SUCCESS: return "Success";
        case MAKXD_ERROR_INVALID_DEVICE: return "Invalid device";
        case MAKXD_ERROR_CONNECTION_FAILED: return "Connection failed";
        case MAKXD_ERROR_COMMAND_FAILED: return "Command failed";
        case MAKXD_ERROR_TIMEOUT: return "Timeout";
        case MAKXD_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case MAKXD_ERROR_OUT_OF_MEMORY: return "Out of memory";
    }
    return "Unknown error";
}

// Device management
makxd_device_t* makxd_device_create(void) {
    try {
        return new makxd_device();
    } catch (...) {
        return nullptr;
    }
}

void makxd_device_destroy(makxd_device_t* device) {
    if (device && device->lifetime_token) {
        device->lifetime_token->store(false, std::memory_order_release);
    }
    delete device;
}

// Static device discovery
int makxd_find_devices(makxd_device_info_t* devices, int max_devices) {
    if (!devices || max_devices <= 0) {
        return 0;
    }
    
    try {
        auto cpp_devices = makxd::Device::findDevices();
        int count = std::min(max_devices, static_cast<int>(cpp_devices.size()));
        
        for (int i = 0; i < count; i++) {
            safe_copy_string(devices[i].port, sizeof(devices[i].port), cpp_devices[i].port);
            safe_copy_string(devices[i].description, sizeof(devices[i].description), cpp_devices[i].description);
            devices[i].vid = cpp_devices[i].vid;
            devices[i].pid = cpp_devices[i].pid;
            devices[i].is_connected = cpp_devices[i].isConnected;
        }
        
        return count;
    } catch (...) {
        return 0;
    }
}

makxd_error_t makxd_find_first_device(char* port, size_t port_size) {
    if (!port || port_size == 0) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    
    try {
        auto first_port = makxd::Device::findFirstDevice();
        if (first_port.empty()) {
            return MAKXD_ERROR_CONNECTION_FAILED;
        }
        safe_copy_string(port, port_size, first_port);
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

// Connection management
makxd_error_t makxd_connect(makxd_device_t* device, const char* port) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        std::string port_str = port ? port : "";
        bool success = device->cpp_device->connect(port_str);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_CONNECTION_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

void makxd_disconnect(makxd_device_t* device) {
    if (device) {
        try {
            device->cpp_device->disconnect();
        } catch (...) {
            // Ignore exceptions on disconnect
        }
    }
}

bool makxd_is_connected(makxd_device_t* device) {
    if (!device) {
        return false;
    }
    
    try {
        return device->cpp_device->isConnected();
    } catch (...) {
        return false;
    }
}

makxd_connection_status_t makxd_get_status(makxd_device_t* device) {
    if (!device) {
        return MAKXD_STATUS_DISCONNECTED;
    }
    
    try {
        return convert_connection_status(device->cpp_device->getStatus());
    } catch (...) {
        return MAKXD_STATUS_CONNECTION_ERROR;
    }
}

// Device information
makxd_error_t makxd_get_device_info(makxd_device_t* device, makxd_device_info_t* info) {
    if (!device || !info) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    
    try {
        auto cpp_info = device->cpp_device->getDeviceInfo();
        safe_copy_string(info->port, sizeof(info->port), cpp_info.port);
        safe_copy_string(info->description, sizeof(info->description), cpp_info.description);
        info->vid = cpp_info.vid;
        info->pid = cpp_info.pid;
        info->is_connected = cpp_info.isConnected;
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_get_version(makxd_device_t* device, char* version, size_t version_size) {
    if (!device || !version || version_size == 0) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    
    try {
        auto cpp_version = device->cpp_device->getVersion();
        if (cpp_version.empty()) {
            return device->cpp_device->isConnected()
                ? MAKXD_ERROR_COMMAND_FAILED
                : MAKXD_ERROR_CONNECTION_FAILED;
        }
        safe_copy_string(version, version_size, cpp_version);
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

// Mouse button control
makxd_error_t makxd_mouse_down(makxd_device_t* device, makxd_mouse_button_t button) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        bool success = device->cpp_device->mouseDown(cpp_button);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_up(makxd_device_t* device, makxd_mouse_button_t button) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        bool success = device->cpp_device->mouseUp(cpp_button);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_click(makxd_device_t* device, makxd_mouse_button_t button) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        bool success = device->cpp_device->click(cpp_button);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

// Mouse button state queries
makxd_error_t makxd_mouse_button_state(makxd_device_t* device, makxd_mouse_button_t button, bool* state) {
    if (!device || !state) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        *state = device->cpp_device->mouseButtonState(cpp_button);
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

// Mouse movement
makxd_error_t makxd_mouse_move(makxd_device_t* device, int32_t x, int32_t y) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        bool success = device->cpp_device->mouseMove(x, y);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_silent_move(makxd_device_t* device, int32_t x, int32_t y) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return device->cpp_device->mouseSilentMove(x, y) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_mouse_move_smooth(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        bool success = device->cpp_device->mouseMoveSmooth(x, y, segments);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_move_bezier(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        bool success = device->cpp_device->mouseMoveBezier(x, y, segments, ctrl_x, ctrl_y);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_move_controls(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return device->cpp_device->mouseMoveControls(x, y, segments, ctrl_x1, ctrl_y1, ctrl_x2, ctrl_y2) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_mouse_move_to(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return device->cpp_device->mouseMoveTo(x, y, segments) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_mouse_move_to_controls(makxd_device_t* device, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return device->cpp_device->mouseMoveToControls(x, y, segments, ctrl_x1, ctrl_y1, ctrl_x2, ctrl_y2) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_mouse_click_count(makxd_device_t* device, makxd_mouse_button_t button, uint32_t count, uint32_t delay_ms) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) return MAKXD_ERROR_INVALID_PARAMETER;
        return device->cpp_device->click(cpp_button, count, delay_ms) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

static makxd_error_t copy_extra_response(makxd_device_t* device, char* response, size_t response_size, const std::string& value) {
    if (!device || !response || response_size == 0) return MAKXD_ERROR_INVALID_PARAMETER;
    safe_copy_string(response, response_size, value);
    return value.empty() ? MAKXD_ERROR_COMMAND_FAILED : MAKXD_SUCCESS;
}

makxd_error_t makxd_get_mouse_position(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->mousePosition()); }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_set_mouse_screen(makxd_device_t* device, int32_t width, int32_t height) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return device->cpp_device->setMouseScreen(width, height) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_mouse_screen(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->getMouseScreen()); }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_set_axis_stream(makxd_device_t* device, const char* mode, uint32_t period_ms) {
    if (!device || !mode) return MAKXD_ERROR_INVALID_PARAMETER;
    try { return device->cpp_device->setAxisStream(mode, period_ms) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_axis_stream(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->getAxisStream()); }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_set_mouse_stream(makxd_device_t* device, const char* mode, uint32_t period_ms) {
    if (!device || !mode) return MAKXD_ERROR_INVALID_PARAMETER;
    try { return device->cpp_device->setMouseStream(mode, period_ms) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_mouse_stream(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->getMouseStream()); }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_set_button_stream(makxd_device_t* device, const char* mode, uint32_t period_ms) {
    if (!device || !mode) return MAKXD_ERROR_INVALID_PARAMETER;
    try { return device->cpp_device->setButtonStream(mode, period_ms) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_button_stream(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->getButtonStream()); }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_set_echo(makxd_device_t* device, bool enabled) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return device->cpp_device->setEcho(enabled) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; }
    catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_echo(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->getEcho()); }
    catch (...) { return handle_exception(); }
}

// Mouse drag operations
makxd_error_t makxd_mouse_drag(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        bool success = device->cpp_device->mouseDrag(cpp_button, x, y);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_drag_smooth(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        bool success = device->cpp_device->mouseDragSmooth(cpp_button, x, y, segments);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_drag_bezier(makxd_device_t* device, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }

        bool success = device->cpp_device->mouseDragBezier(cpp_button, x, y, segments, ctrl_x, ctrl_y);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

// Mouse wheel
makxd_error_t makxd_mouse_wheel(makxd_device_t* device, int32_t delta) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    
    try {
        bool success = device->cpp_device->mouseWheel(delta);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

// Mouse locking functions
makxd_error_t makxd_lock_mouse_x(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseX(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_lock_mouse_y(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseY(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_lock_mouse_left(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseLeft(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_lock_mouse_middle(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseMiddle(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_lock_mouse_right(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseRight(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_lock_mouse_side1(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseSide1(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_lock_mouse_side2(makxd_device_t* device, bool lock) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->lockMouseSide2(lock);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

// Lock state queries
makxd_error_t makxd_is_mouse_x_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseXLocked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_mouse_y_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseYLocked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_mouse_left_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseLeftLocked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_mouse_middle_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseMiddleLocked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_mouse_right_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseRightLocked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_mouse_side1_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseSide1Locked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_mouse_side2_locked(makxd_device_t* device, bool* locked) {
    if (!device || !locked) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *locked = device->cpp_device->isMouseSide2Locked();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

// Mouse input catching
makxd_error_t makxd_catch_mouse_left(makxd_device_t* device, uint8_t* result) {
    if (!device || !result) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *result = device->cpp_device->catchMouseLeft();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_catch_mouse_middle(makxd_device_t* device, uint8_t* result) {
    if (!device || !result) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *result = device->cpp_device->catchMouseMiddle();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_catch_mouse_right(makxd_device_t* device, uint8_t* result) {
    if (!device || !result) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *result = device->cpp_device->catchMouseRight();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_catch_mouse_side1(makxd_device_t* device, uint8_t* result) {
    if (!device || !result) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *result = device->cpp_device->catchMouseSide1();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_catch_mouse_side2(makxd_device_t* device, uint8_t* result) {
    if (!device || !result) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *result = device->cpp_device->catchMouseSide2();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

#define MAKXD_SET_CATCH(name, method) \
makxd_error_t name(makxd_device_t* device, uint8_t value) { \
    if (!device) return MAKXD_ERROR_INVALID_DEVICE; \
    try { return device->cpp_device->method(value) ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED; } \
    catch (...) { return handle_exception(); } \
}

MAKXD_SET_CATCH(makxd_set_catch_mouse_left, setCatchMouseLeft)
MAKXD_SET_CATCH(makxd_set_catch_mouse_middle, setCatchMouseMiddle)
MAKXD_SET_CATCH(makxd_set_catch_mouse_right, setCatchMouseRight)
MAKXD_SET_CATCH(makxd_set_catch_mouse_side1, setCatchMouseSide1)
MAKXD_SET_CATCH(makxd_set_catch_mouse_side2, setCatchMouseSide2)
#undef MAKXD_SET_CATCH

// Button monitoring
makxd_error_t makxd_enable_button_monitoring(makxd_device_t* device, bool enable) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->enableButtonMonitoring(enable);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_button_monitoring_enabled(makxd_device_t* device, bool* enabled) {
    if (!device || !enabled) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *enabled = device->cpp_device->isButtonMonitoringEnabled();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_button_mask(makxd_device_t* device, uint8_t* mask) {
    if (!device || !mask) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *mask = device->cpp_device->getButtonMask();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

// Serial spoofing
makxd_error_t makxd_get_mouse_serial(makxd_device_t* device, char* serial, size_t serial_size) {
    if (!device || !serial || serial_size == 0) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        auto cpp_serial = device->cpp_device->getMouseSerial();
        safe_copy_string(serial, serial_size, cpp_serial);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_set_mouse_serial(makxd_device_t* device, const char* serial) {
    if (!device || !serial) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        bool success = device->cpp_device->setMouseSerial(serial);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_reset_mouse_serial(makxd_device_t* device) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->resetMouseSerial();
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

// Device control
makxd_error_t makxd_set_baud_rate(makxd_device_t* device, uint32_t baud_rate) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        bool success = device->cpp_device->setBaudRate(baud_rate);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_get_baud_rate(makxd_device_t* device, char* response, size_t response_size) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try { return copy_extra_response(device, response, response_size, device->cpp_device->getBaudRate()); }
    catch (...) { return handle_exception(); }
}

// Callbacks
makxd_error_t makxd_set_mouse_button_callback(makxd_device_t* device, makxd_mouse_button_callback_t callback, void* user_data) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    
    try {
        const auto state = device->callback_state;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->mouse_callback = callback;
            state->mouse_callback_user_data = user_data;
        }
        
        if (callback) {
            device->cpp_device->setMouseButtonCallback([state](makxd::MouseButton button, bool pressed) {
                makxd_mouse_button_callback_t callbackFn = nullptr;
                void* callbackUserData = nullptr;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    callbackFn = state->mouse_callback;
                    callbackUserData = state->mouse_callback_user_data;
                }

                if (callbackFn) {
                    callbackFn(convert_mouse_button_to_c(button), pressed, callbackUserData);
                }
            });
        } else {
            device->cpp_device->setMouseButtonCallback(nullptr);
        }
        
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_set_connection_callback(makxd_device_t* device, makxd_connection_callback_t callback, void* user_data) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    
    try {
        const auto state = device->callback_state;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->connection_callback = callback;
            state->connection_callback_user_data = user_data;
        }
        
        if (callback) {
            device->cpp_device->setConnectionCallback([state](bool connected) {
                makxd_connection_callback_t callbackFn = nullptr;
                void* callbackUserData = nullptr;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    callbackFn = state->connection_callback;
                    callbackUserData = state->connection_callback_user_data;
                }

                if (callbackFn) {
                    callbackFn(connected, callbackUserData);
                }
            });
        } else {
            device->cpp_device->setConnectionCallback(nullptr);
        }
        
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

// High-level automation
makxd_error_t makxd_click_sequence(makxd_device_t* device, const makxd_mouse_button_t* buttons, size_t count, uint32_t delay_ms) {
    if (!device || !buttons) return MAKXD_ERROR_INVALID_PARAMETER;
    
    try {
        std::vector<makxd::MouseButton> cpp_buttons;
        cpp_buttons.reserve(count);
        
        for (size_t i = 0; i < count; i++) {
            makxd::MouseButton cpp_button{};
            if (!try_convert_mouse_button(buttons[i], cpp_button)) {
                return MAKXD_ERROR_INVALID_PARAMETER;
            }
            cpp_buttons.push_back(cpp_button);
        }
        
        bool success = device->cpp_device->clickSequence(cpp_buttons, std::chrono::milliseconds(delay_ms));
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_move_pattern(makxd_device_t* device, const makxd_point_t* points, size_t count, bool smooth, uint32_t segments) {
    if (!device || !points) return MAKXD_ERROR_INVALID_PARAMETER;
    
    try {
        std::vector<std::pair<int32_t, int32_t>> cpp_points;
        cpp_points.reserve(count);
        
        for (size_t i = 0; i < count; i++) {
            cpp_points.emplace_back(points[i].x, points[i].y);
        }
        
        bool success = device->cpp_device->movePattern(cpp_points, smooth, segments);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

// Performance mode
makxd_error_t makxd_enable_high_performance_mode(makxd_device_t* device, bool enable) {
    if (!device) return MAKXD_ERROR_INVALID_DEVICE;
    try {
        device->cpp_device->enableHighPerformanceMode(enable);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_is_high_performance_mode_enabled(makxd_device_t* device, bool* enabled) {
    if (!device || !enabled) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        *enabled = device->cpp_device->isHighPerformanceModeEnabled();
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

// Batch operations
makxd_batch_builder_t* makxd_create_batch(makxd_device_t* device) {
    if (!device) return nullptr;
    
    try {
        auto cpp_batch = device->cpp_device->createBatch();
        return new makxd_batch_builder(std::move(cpp_batch), device->lifetime_token);
    } catch (...) {
        return nullptr;
    }
}

void makxd_batch_destroy(makxd_batch_builder_t* batch) {
    delete batch;
}

makxd_error_t makxd_batch_move(makxd_batch_builder_t* batch, int32_t x, int32_t y) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        batch->cpp_batch->move(x, y);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_move_smooth(makxd_batch_builder_t* batch, int32_t x, int32_t y, uint32_t segments) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        batch->cpp_batch->moveSmooth(x, y, segments);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_move_bezier(makxd_batch_builder_t* batch, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        batch->cpp_batch->moveBezier(x, y, segments, ctrl_x, ctrl_y);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_click(makxd_batch_builder_t* batch, makxd_mouse_button_t button) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        batch->cpp_batch->click(cpp_button);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_press(makxd_batch_builder_t* batch, makxd_mouse_button_t button) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        batch->cpp_batch->press(cpp_button);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_release(makxd_batch_builder_t* batch, makxd_mouse_button_t button) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        batch->cpp_batch->release(cpp_button);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_scroll(makxd_batch_builder_t* batch, int32_t delta) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        batch->cpp_batch->scroll(delta);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_drag(makxd_batch_builder_t* batch, makxd_mouse_button_t button, int32_t x, int32_t y) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        batch->cpp_batch->drag(cpp_button, x, y);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_drag_smooth(makxd_batch_builder_t* batch, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        batch->cpp_batch->dragSmooth(cpp_button, x, y, segments);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_drag_bezier(makxd_batch_builder_t* batch, makxd_mouse_button_t button, int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        batch->cpp_batch->dragBezier(cpp_button, x, y, segments, ctrl_x, ctrl_y);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_batch_execute(makxd_batch_builder_t* batch) {
    makxd_error_t validation = validate_batch_builder(batch);
    if (validation != MAKXD_SUCCESS) return validation;
    try {
        bool success = batch->cpp_batch->execute();
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

// Raw command interface
makxd_error_t makxd_send_raw_command(makxd_device_t* device, const char* command) {
    if (!device || !command) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        bool success = device->cpp_device->sendRawCommand(command);
        return success ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_receive_raw_response(makxd_device_t* device, char* response, size_t response_size) {
    if (!device || !response || response_size == 0) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        auto cpp_response = device->cpp_device->receiveRawResponse();
        safe_copy_string(response, response_size, cpp_response);
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

// Utility functions
const char* makxd_mouse_button_to_string(makxd_mouse_button_t button) {
    switch (button) {
        case MAKXD_MOUSE_LEFT: return "LEFT";
        case MAKXD_MOUSE_RIGHT: return "RIGHT";
        case MAKXD_MOUSE_MIDDLE: return "MIDDLE";
        case MAKXD_MOUSE_SIDE1: return "SIDE1";
        case MAKXD_MOUSE_SIDE2: return "SIDE2";
        case MAKXD_MOUSE_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

makxd_mouse_button_t makxd_string_to_mouse_button(const char* button_name) {
    if (!button_name) return MAKXD_MOUSE_UNKNOWN;
    if (equals_ignore_ascii_case(button_name, "LEFT")) return MAKXD_MOUSE_LEFT;
    if (equals_ignore_ascii_case(button_name, "RIGHT")) return MAKXD_MOUSE_RIGHT;
    if (equals_ignore_ascii_case(button_name, "MIDDLE")) return MAKXD_MOUSE_MIDDLE;
    if (equals_ignore_ascii_case(button_name, "SIDE1")) return MAKXD_MOUSE_SIDE1;
    if (equals_ignore_ascii_case(button_name, "SIDE2")) return MAKXD_MOUSE_SIDE2;
    return MAKXD_MOUSE_UNKNOWN;
}

// Performance profiling
void makxd_profiler_enable(bool enable) {
    makxd::PerformanceProfiler::enableProfiling(enable);
}

void makxd_profiler_reset_stats(void) {
    makxd::PerformanceProfiler::resetStats();
}

int makxd_profiler_get_stats(makxd_perf_stat_t* stats, int max_stats) {
    if (!stats || max_stats <= 0) return 0;
    
    try {
        auto cpp_stats = makxd::PerformanceProfiler::getStats();
        int count = std::min(max_stats, static_cast<int>(cpp_stats.size()));
        
        int i = 0;
        for (const auto& [command, data] : cpp_stats) {
            if (i >= count) break;
            
            safe_copy_string(stats[i].command_name, sizeof(stats[i].command_name), command);
            stats[i].call_count = data.first;
            stats[i].total_microseconds = data.second;
            i++;
        }
        
        return count;
    } catch (...) {
        return 0;
    }
}

} // extern "C"
