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

    makxd_device(bool encryption_enabled = false,
                 const char* encryption_key_hex = nullptr) :
        cpp_device(std::make_unique<makxd::Device>(
            encryption_enabled,
            encryption_key_hex ? encryption_key_hex : "")),
        callback_state(std::make_shared<makxd_callback_state>()),
        lifetime_token(std::make_shared<std::atomic<bool>>(true)) {}
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

static makxd::ConnectionConfig convert_connection_config(
    const makxd_connection_config_t& source) {
    const std::string key =
        source.aes128_key_hex ? source.aes128_key_hex : "";
    if (source.method == MAKXD_CONNECTION_COM) {
        return makxd::ConnectionConfig::com(
            source.com_port ? source.com_port : "", key);
    }
    if (source.method == MAKXD_CONNECTION_UDP) {
        return makxd::ConnectionConfig::udp(
            source.udp_host ? source.udp_host : "",
            source.udp_port == 0u ? 8080u : source.udp_port,
            source.udp_mode == MAKXD_UDP_RAW
                ? makxd::UdpWireMode::RAW
                : makxd::UdpWireMode::HOST,
            key,
            source.udp_bind_address ? source.udp_bind_address : "",
            source.udp_interface ? source.udp_interface : "",
            source.vlan_id);
    }
    return makxd::ConnectionConfig::ble(
        source.ble_address ? source.ble_address : "",
        [connect = source.ble_connect, user = source.ble_user_data](
            std::string_view address) {
            const std::string addressText(address);
            return connect != nullptr &&
                connect(addressText.c_str(), user);
        },
        [write = source.ble_write, user = source.ble_user_data](
            std::span<const uint8_t> bytes) {
            return write != nullptr &&
                write(bytes.data(), bytes.size(), user);
        },
        [read = source.ble_read, user = source.ble_user_data](
            std::span<uint8_t> bytes) {
            return read == nullptr
                ? size_t{0}
                : read(bytes.data(), bytes.size(), user);
        },
        [close = source.ble_close, user = source.ble_user_data]() {
            if (close != nullptr) {
                close(user);
            }
        });
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

extern "C" {

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

makxd_error_t makxd_connect_config(
    makxd_device_t* device,
    const makxd_connection_config_t* connection) {
    if (!device || !connection) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    if (connection->method == MAKXD_CONNECTION_BLE &&
        connection->aes128_key_hex != nullptr &&
        connection->aes128_key_hex[0] != '\0') {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->connect(
            convert_connection_config(*connection))
                ? MAKXD_SUCCESS
                : MAKXD_ERROR_CONNECTION_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_get_device_kinds(
    makxd_device_t* device, makxd_device_kinds_t* kinds) {
    if (!device || !kinds) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        const auto result = device->cpp_device->device();
        if (!result) {
            return device->cpp_device->isConnected()
                ? MAKXD_ERROR_COMMAND_FAILED
                : MAKXD_ERROR_CONNECTION_FAILED;
        }
        kinds->kinds = result->kinds;
        return MAKXD_SUCCESS;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_firmware_version(
    makxd_device_t* device, uint32_t* version) {
    if (!device || !version) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        const auto result = device->cpp_device->firmwareVersion();
        if (!result) {
            return device->cpp_device->isConnected()
                ? MAKXD_ERROR_COMMAND_FAILED
                : MAKXD_ERROR_CONNECTION_FAILED;
        }
        *version = *result;
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

makxd_error_t makxd_mouse_down_dt(
    makxd_device_t* device,
    makxd_mouse_button_t button,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        return device->cpp_device->mouseDown(cpp_button, dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
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

makxd_error_t makxd_mouse_up_dt(
    makxd_device_t* device,
    makxd_mouse_button_t button,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        return device->cpp_device->mouseUp(cpp_button, dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
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

makxd_error_t makxd_mouse_button_mask(
    makxd_device_t* device,
    makxd_mouse_button_t button,
    bool enabled) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    try {
        makxd::MouseButton cpp_button{};
        if (!try_convert_mouse_button(button, cpp_button)) {
            return MAKXD_ERROR_INVALID_PARAMETER;
        }
        return device->cpp_device->mouseButtonMask(cpp_button, enabled)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_move_mask(
    makxd_device_t* device,
    bool left,
    bool right,
    bool down,
    bool up) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    try {
        return device->cpp_device->mouseMoveMask(left, right, down, up)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_mouse_wheel_mask(
    makxd_device_t* device,
    bool down,
    bool up) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    try {
        return device->cpp_device->mouseWheelMask(down, up)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
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

makxd_error_t makxd_mouse_move_dt(
    makxd_device_t* device,
    int32_t x,
    int32_t y,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->mouseMove(x, y, dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_device_t* makxd_device_create_with_transport(
    bool encryption_enabled, const char* encryption_key_hex) {
    if (encryption_enabled && !encryption_key_hex) {
        return nullptr;
    }
    try {
        return new makxd_device(encryption_enabled, encryption_key_hex);
    } catch (...) {
        return nullptr;
    }
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

makxd_error_t makxd_mouse_wheel_dt(
    makxd_device_t* device,
    int32_t delta,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->mouseWheel(delta, dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_keyboard_down(makxd_device_t* device, uint8_t key) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (key == 0u) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->keyboardDown(makxd::KeyboardKey{key})
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_keyboard_down_dt(
    makxd_device_t* device,
    uint8_t key,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (key == 0u || dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->keyboardDown(
            makxd::KeyboardKey{key}, dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_keyboard_up(makxd_device_t* device, uint8_t key) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (key == 0u) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->keyboardUp(makxd::KeyboardKey{key})
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_keyboard_up_dt(
    makxd_device_t* device,
    uint8_t key,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (key == 0u || dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->keyboardUp(
            makxd::KeyboardKey{key}, dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_keyboard_init(makxd_device_t* device) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    try {
        return device->cpp_device->keyboardInit()
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_keyboard_init_dt(
    makxd_device_t* device,
    uint16_t dt_uframes) {
    if (!device) {
        return MAKXD_ERROR_INVALID_DEVICE;
    }
    if (dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->keyboardInit(dt_uframes)
            ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) {
        return handle_exception();
    }
}

makxd_error_t makxd_controller_control_get(
    makxd_device_t* device, makxd_controller_control_t control, int32_t* value) {
    const auto id = static_cast<int>(control);
    if (!device || !value || id < 0 || id > 54) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        const auto result = device->cpp_device->controllerControl(
            static_cast<makxd::ControllerControl>(static_cast<uint8_t>(id)));
        if (!result) return MAKXD_ERROR_COMMAND_FAILED;
        *value = *result;
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_controller_control(
    makxd_device_t* device, makxd_controller_control_t control, int32_t value) {
    return makxd_controller_control_dt(device, control, value, 0u);
}

makxd_error_t makxd_controller_control_dt(
    makxd_device_t* device, makxd_controller_control_t control,
    int32_t value, uint16_t dt_uframes) {
    const auto id = static_cast<int>(control);
    if (!device || id < 0 || id > 54 || dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->controllerControl(
            static_cast<makxd::ControllerControl>(static_cast<uint8_t>(id)),
            value, dt_uframes)
                ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_controller_mask(
    makxd_device_t* device, makxd_controller_control_t control,
    makxd_controller_mask_mode_t mode) {
    const auto id = static_cast<int>(control);
    const auto mask = static_cast<int>(mode);
    if (!device || id < 0 || id > 54 || mask < 0 || mask > 4) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->controllerMask(
            static_cast<makxd::ControllerControl>(static_cast<uint8_t>(id)),
            static_cast<makxd::ControllerMaskMode>(static_cast<uint8_t>(mask)))
                ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

makxd_error_t makxd_controller_state_get(
    makxd_device_t* device, makxd_controller_state_t* state) {
    if (!device || !state) return MAKXD_ERROR_INVALID_PARAMETER;
    try {
        const auto result = device->cpp_device->controllerState();
        if (!result) return MAKXD_ERROR_COMMAND_FAILED;
        state->digital =
            static_cast<uint64_t>(result->digitalLow) |
            (static_cast<uint64_t>(result->digitalHigh) << 32u);
        state->left_trigger = result->leftTrigger;
        state->right_trigger = result->rightTrigger;
        state->left_stick_x = result->leftStickX;
        state->left_stick_y = result->leftStickY;
        state->right_stick_x = result->rightStickX;
        state->right_stick_y = result->rightStickY;
        return MAKXD_SUCCESS;
    } catch (...) { return handle_exception(); }
}

static makxd::ControllerState makxd_controller_state_read(
    const makxd_controller_state_t& state) {
    return {
        static_cast<uint32_t>(state.digital),
        static_cast<uint32_t>(state.digital >> 32u),
        state.left_trigger,
        state.right_trigger,
        state.left_stick_x,
        state.left_stick_y,
        state.right_stick_x,
        state.right_stick_y};
}

makxd_error_t makxd_controller_state_set(
    makxd_device_t* device, const makxd_controller_state_t* state) {
    return makxd_controller_state_set_dt(device, state, 0u);
}

makxd_error_t makxd_controller_state_set_dt(
    makxd_device_t* device, const makxd_controller_state_t* state,
    uint16_t dt_uframes) {
    if (!device || !state || dt_uframes > 0x3FFFu) {
        return MAKXD_ERROR_INVALID_PARAMETER;
    }
    try {
        return device->cpp_device->setControllerState(
            makxd_controller_state_read(*state), dt_uframes)
                ? MAKXD_SUCCESS : MAKXD_ERROR_COMMAND_FAILED;
    } catch (...) { return handle_exception(); }
}

bool makxd_controller_stream_decode(
    const uint8_t* values, size_t values_size,
    makxd_controller_stream_state_t* state) {
    if (!values || !state || values_size != 21u) {
        return false;
    }
    const auto u16 = [values](size_t offset) {
        return static_cast<uint16_t>(
            values[offset] | (static_cast<uint16_t>(values[offset + 1u]) << 8u));
    };
    state->buttons = static_cast<uint32_t>(
        values[0] | (static_cast<uint32_t>(values[1]) << 8u) |
        (static_cast<uint32_t>(values[2]) << 16u) |
        (static_cast<uint32_t>(values[3]) << 24u));
    state->hat = values[4];
    state->lt = u16(5u);
    state->rt = u16(7u);
    state->x = static_cast<int16_t>(u16(9u));
    state->y = static_cast<int16_t>(u16(11u));
    state->rx = static_cast<int16_t>(u16(13u));
    state->ry = static_cast<int16_t>(u16(15u));
    state->z = static_cast<int16_t>(u16(17u));
    state->rz = static_cast<int16_t>(u16(19u));
    return state->hat <= 8u;
}
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
