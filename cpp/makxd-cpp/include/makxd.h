#pragma once

// Export macros for shared library support across platforms
#ifdef _WIN32
    #ifdef MAKXD_EXPORTS
        #define MAKXD_API __declspec(dllexport)
    #elif defined(MAKXD_SHARED)
        #define MAKXD_API __declspec(dllimport)
    #else
        #define MAKXD_API
    #endif
#else
    #ifdef __GNUC__
        #define MAKXD_API __attribute__((visibility("default")))
    #else
        #define MAKXD_API
    #endif
#endif

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <exception>
#include <unordered_map>
#include <atomic>
#include <future>
#include <chrono>
#include <expected>
#include <variant>
#include <span>

#include "makxd_stream.h"
#include "makxd_protocol.h"

namespace makxd {

    struct ControllerState {
        uint32_t digitalLow{};
        uint32_t digitalHigh{};
        uint16_t leftTrigger{};
        uint16_t rightTrigger{};
        int16_t leftStickX{};
        int16_t leftStickY{};
        int16_t rightStickX{};
        int16_t rightStickY{};
    };


    // Forward declaration
    class SerialPort;

    // Enums
    enum class MouseButton : uint8_t {
        LEFT = 0,
        RIGHT = 1,
        MIDDLE = 2,
        SIDE1 = 3,
        SIDE2 = 4,
        UNKNOWN = 255
    };

    enum class ControllerControl : uint8_t {
        SOUTH = 0, EAST, WEST, NORTH,
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
        LEFT_SHOULDER, RIGHT_SHOULDER,
        LEFT_TRIGGER, RIGHT_TRIGGER,
        LEFT_STICK_X, LEFT_STICK_Y, RIGHT_STICK_X, RIGHT_STICK_Y,
        LEFT_STICK_BUTTON, RIGHT_STICK_BUTTON,
        SELECT, START, MODE, GRIP_LEFT, GRIP_RIGHT,
        EXTRA_1, EXTRA_2, EXTRA_3, EXTRA_4, EXTRA_5, EXTRA_6, EXTRA_7, EXTRA_8,
        EXTRA_9, EXTRA_10, EXTRA_11, EXTRA_12, EXTRA_13, EXTRA_14, EXTRA_15,
        EXTRA_16, EXTRA_17, EXTRA_18, EXTRA_19, EXTRA_20, EXTRA_21, EXTRA_22,
        EXTRA_23, EXTRA_24, EXTRA_25, EXTRA_26, EXTRA_27, EXTRA_28, EXTRA_29,
        EXTRA_30, EXTRA_31, EXTRA_32
    };

    enum class ControllerMaskMode : uint8_t {
        DISABLED = 0, COMPLETE = 1, NEGATIVE = 2, POSITIVE = 3, BOTH = 4
    };

    enum class ConnectionStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        CONNECTION_ERROR,
    };

    using KeyboardKey = std::variant<uint8_t, std::string>;

    struct ConnectionConfig {
        ConnectionMethod method{ConnectionMethod::COM};
        std::string aes128Key;
        std::string comPort;
        std::string udpHost;
        uint16_t udpPort{8080};
        UdpWireMode udpMode{UdpWireMode::HOST};
        std::string udpBindAddress;
        std::string udpInterface;
        uint16_t vlanId{0};
        std::string bleAddress;
        std::function<bool(std::string_view)> bleConnect;
        std::function<bool(std::span<const uint8_t>)> bleWrite;
        std::function<size_t(std::span<uint8_t>)> bleRead;
        std::function<void()> bleClose;

        [[nodiscard]] static ConnectionConfig com(
            std::string port = {},
            std::string aes128Key = {});
        [[nodiscard]] static ConnectionConfig udp(
            std::string host,
            uint16_t port = 8080,
            UdpWireMode mode = UdpWireMode::HOST,
            std::string aes128Key = {},
            std::string bindAddress = {},
            std::string interfaceName = {},
            uint16_t vlanId = 0);
        [[nodiscard]] static ConnectionConfig ble(
            std::string address,
            std::function<bool(std::string_view)> connect,
            std::function<bool(std::span<const uint8_t>)> write,
            std::function<size_t(std::span<uint8_t>)> read,
            std::function<void()> close);
    };

    // Simple structs
    struct DeviceInfo {
        std::string port;
        std::string description;
        uint16_t vid;
        uint16_t pid;
        bool isConnected;
    };

    struct MouseButtonStates {
        bool left;
        bool right;
        bool middle;
        bool side1;
        bool side2;

        MouseButtonStates() : left(false), right(false), middle(false), side1(false), side2(false) {}

        bool operator[](MouseButton button) const {
            switch (button) {
            case MouseButton::LEFT: return left;
            case MouseButton::RIGHT: return right;
            case MouseButton::MIDDLE: return middle;
            case MouseButton::SIDE1: return side1;
            case MouseButton::SIDE2: return side2;
            case MouseButton::UNKNOWN: return false;
            }
            return false;
        }

        void set(MouseButton button, bool state) {
            switch (button) {
            case MouseButton::LEFT: left = state; break;
            case MouseButton::RIGHT: right = state; break;
            case MouseButton::MIDDLE: middle = state; break;
            case MouseButton::SIDE1: side1 = state; break;
            case MouseButton::SIDE2: side2 = state; break;
            case MouseButton::UNKNOWN: break;
            }
        }
    };

    // Exception classes
    class MAKXD_API MakxdException : public std::exception {
    public:
        explicit MakxdException(const std::string& message) : m_message(message) {}
        const char* what() const noexcept override { return m_message.c_str(); }
    private:
        std::string m_message;
    };

    class MAKXD_API ConnectionException : public MakxdException {
    public:
        explicit ConnectionException(const std::string& message)
            : MakxdException("Connection error: " + message) {
        }
    };

    class MAKXD_API CommandException : public MakxdException {
    public:
        explicit CommandException(const std::string& message)
            : MakxdException("Command error: " + message) {
        }
    };

    class MAKXD_API TimeoutException : public MakxdException {
    public:
        explicit TimeoutException(const std::string& message)
            : MakxdException("Timeout error: " + message) {
        }
    };

    // Main Device class - High Performance MAKXD Mouse Controller
    class MAKXD_API Device {
    public:
        // Callback types
        using MouseButtonCallback = std::function<void(MouseButton, bool)>;
        using ConnectionCallback = std::function<void(bool)>;

        // Constructor and destructor
        Device(bool encryptionEnabled = false,
            std::string_view encryptionKey = {});
        explicit Device(ConnectionConfig connection);
        ~Device();

        // Static methods
        static std::vector<DeviceInfo> findDevices();
        static std::string findFirstDevice();

        // Connection with async support
        [[nodiscard]] bool connect();
        [[nodiscard]] bool connect(const std::string& port);
        [[nodiscard]] bool connect(const ConnectionConfig& connection);
        void disconnect();
        [[nodiscard]] bool isConnected() const noexcept;
        [[nodiscard]] ConnectionStatus getStatus() const noexcept;

        // Async connection methods
        [[nodiscard]] std::future<bool> connectAsync(const std::string& port = "");
        [[nodiscard]] std::expected<void, ConnectionStatus> connectExpected(const std::string& port = "");

        // Device info
        [[nodiscard]] DeviceInfo getDeviceInfo() const;
        [[nodiscard]] std::expected<DeviceKinds, ConnectionStatus> device() const;

        // Mouse button control; success requires Makxd's echoed command.
        [[nodiscard]] bool mouseDown(MouseButton button);
        [[nodiscard]] bool mouseDown(MouseButton button, uint16_t dt_uframes);
        [[nodiscard]] bool mouseUp(MouseButton button);
        [[nodiscard]] bool mouseUp(MouseButton button, uint16_t dt_uframes);
        [[nodiscard]] bool click(MouseButton button);  // Combined press+release
        [[nodiscard]] bool mouseButtonMask(MouseButton button, bool enabled);
        [[nodiscard]] bool mouseLeftMask(bool enabled);
        [[nodiscard]] bool mouseRightMask(bool enabled);
        [[nodiscard]] bool mouseMiddleMask(bool enabled);
        [[nodiscard]] bool mouseSide1Mask(bool enabled);
        [[nodiscard]] bool mouseSide2Mask(bool enabled);
        [[nodiscard]] bool mouseMoveMask(
            bool left, bool right, bool down, bool up);
        [[nodiscard]] bool mouseWheelMask(bool down, bool up);


        // Mouse button state queries (with caching)
        [[nodiscard]] bool mouseButtonState(MouseButton button);

        // Movement; success requires Makxd's echoed command.
        [[nodiscard]] bool mouseMove(int32_t x, int32_t y);
        [[nodiscard]] bool mouseMove(int32_t x, int32_t y, uint16_t dt_uframes);

        // High-performance drag operations
        [[nodiscard]] bool mouseDrag(MouseButton button, int32_t x, int32_t y);


        // Mouse wheel
        [[nodiscard]] bool mouseWheel(int32_t delta);
        [[nodiscard]] bool mouseWheel(int32_t delta, uint16_t dt_uframes);

        // Keyboard control
        [[nodiscard]] bool keyboardDown(const KeyboardKey& key);
        [[nodiscard]] bool keyboardDown(const KeyboardKey& key, uint16_t dt_uframes);
        [[nodiscard]] bool keyboardUp(const KeyboardKey& key);
        [[nodiscard]] bool keyboardUp(const KeyboardKey& key, uint16_t dt_uframes);
        [[nodiscard]] bool keyboardPress(const KeyboardKey& key);
        [[nodiscard]] bool keyboardPress(const KeyboardKey& key, uint32_t hold_ms);
        [[nodiscard]] bool keyboardPress(const KeyboardKey& key,
            uint32_t hold_ms, uint32_t rand_ms);
        [[nodiscard]] bool keyboardString(const std::string& text);
        [[nodiscard]] bool keyboardInit();
        [[nodiscard]] bool keyboardInit(uint16_t dt_uframes);
        [[nodiscard]] bool keyboardIsDown(const KeyboardKey& key);
        [[nodiscard]] bool keyboardMask(const KeyboardKey& key, bool enable);
        [[nodiscard]] bool keyboardRemap(const KeyboardKey& source,
            const KeyboardKey& target);
        [[nodiscard]] bool keyboardMultiDown(const std::vector<KeyboardKey>& keys);
        [[nodiscard]] bool keyboardMultiUp(const std::vector<KeyboardKey>& keys);
        [[nodiscard]] bool keyboardMultiPress(const std::vector<KeyboardKey>& keys);
        [[nodiscard]] std::string getKeyboardKeys();
        [[nodiscard]] bool setKeyboardKeys(bool enabled);

        // Unified semantic controller API.
        [[nodiscard]] std::optional<int32_t> controllerControl(
            ControllerControl control);
        [[nodiscard]] bool controllerControl(
            ControllerControl control, int32_t value);
        [[nodiscard]] bool controllerControl(
            ControllerControl control, int32_t value, uint16_t dt_uframes);
        [[nodiscard]] bool controllerMask(
            ControllerControl control, ControllerMaskMode mode);
        [[nodiscard]] std::optional<ControllerState> controllerState();
        [[nodiscard]] bool setControllerState(const ControllerState& state);
        [[nodiscard]] bool setControllerState(
            const ControllerState& state, uint16_t dt_uframes);

        // Button monitoring with optimized processing
        [[nodiscard]] bool enableButtonMonitoring(bool enable = true);
        [[nodiscard]] bool isButtonMonitoringEnabled() const noexcept;
        [[nodiscard]] uint8_t getButtonMask() const noexcept;

        // Callbacks
        void setMouseButtonCallback(MouseButtonCallback callback);
        void setConnectionCallback(ConnectionCallback callback);

        // High-level automation
        [[nodiscard]] bool clickSequence(const std::vector<MouseButton>& buttons,
            std::chrono::milliseconds delay = std::chrono::milliseconds(50));

        // Performance utilities
        void enableHighPerformanceMode(bool enable = true);
        [[nodiscard]] bool isHighPerformanceModeEnabled() const noexcept;

        // Error handling
        std::string getLastError();

    private:
        // Implementation details with caching and optimization
        class Impl;
        std::unique_ptr<Impl> m_impl;
        std::shared_ptr<std::atomic<bool>> m_lifetimeToken;

        // Disable copy
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
    };

    // Utility functions
    [[nodiscard]] MAKXD_API std::string mouseButtonToString(MouseButton button);
    [[nodiscard]] MAKXD_API MouseButton stringToMouseButton(const std::string& buttonName);

    // Performance profiling utilities
    class MAKXD_API PerformanceProfiler {
    private:
        static std::atomic<bool> s_enabled;
        static std::mutex s_mutex;
        static std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> s_stats;

    public:
        static void enableProfiling(bool enable = true) {
            s_enabled.store(enable);
        }

        static void logCommandTiming(std::string_view command, std::chrono::microseconds duration) {
            if (!s_enabled.load()) return;

            std::lock_guard<std::mutex> lock(s_mutex);
            auto& [count, total_us] = s_stats[std::string{command}];
            count++;
            total_us += duration.count();
        }

        static std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> getStats() {
            std::lock_guard<std::mutex> lock(s_mutex);
            return s_stats;
        }

        static void resetStats() {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_stats.clear();
        }
    };

} // namespace makxd
