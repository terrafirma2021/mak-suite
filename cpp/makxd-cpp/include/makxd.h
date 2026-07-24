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

#include "makxd_stream.h"

namespace makxd {

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

    enum class ConnectionStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        CONNECTION_ERROR,
    };

    using KeyboardKey = std::variant<uint8_t, std::string>;

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
        Device();
        ~Device();

        // Static methods
        static std::vector<DeviceInfo> findDevices();
        static std::string findFirstDevice();

        // Connection with async support
        [[nodiscard]] bool connect(const std::string& port = "", bool highSpeed = true);
        void disconnect();
        [[nodiscard]] bool isConnected() const noexcept;
        [[nodiscard]] ConnectionStatus getStatus() const noexcept;

        // Async connection methods
        [[nodiscard]] std::future<bool> connectAsync(const std::string& port = "");
        [[nodiscard]] std::expected<void, ConnectionStatus> connectExpected(const std::string& port = "");

        // Device info
        [[nodiscard]] DeviceInfo getDeviceInfo() const;
        [[nodiscard]] std::string getVersion() const;
        [[nodiscard]] std::expected<std::string, ConnectionStatus> getVersionExpected() const;

        // Mouse button control; success requires Makxd's echoed command.
        [[nodiscard]] bool mouseDown(MouseButton button);
        [[nodiscard]] bool mouseUp(MouseButton button);
        [[nodiscard]] bool click(MouseButton button);  // Combined press+release
        [[nodiscard]] bool click(MouseButton button, uint32_t count, uint32_t delay_ms = 1);


        // Mouse button state queries (with caching)
        [[nodiscard]] bool mouseButtonState(MouseButton button);

        // Movement; success requires Makxd's echoed command.
        [[nodiscard]] bool mouseMove(int32_t x, int32_t y);
        [[nodiscard]] bool mouseSilentMove(int32_t x, int32_t y);
        [[nodiscard]] bool mouseMoveSmooth(int32_t x, int32_t y, uint32_t segments);
        [[nodiscard]] bool mouseMoveBezier(int32_t x, int32_t y, uint32_t segments,
            int32_t ctrl_x, int32_t ctrl_y);
        [[nodiscard]] bool mouseMoveControls(int32_t x, int32_t y, uint32_t segments,
            int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2);
        [[nodiscard]] bool mouseMoveTo(int32_t x, int32_t y, uint32_t segments = 1);
        [[nodiscard]] bool mouseMoveToControls(int32_t x, int32_t y, uint32_t segments,
            int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2);
        [[nodiscard]] std::string mousePosition();
        [[nodiscard]] bool setMouseScreen(int32_t width, int32_t height);
        [[nodiscard]] std::string getMouseScreen();
        [[nodiscard]] bool setAxisStream(const std::string& mode, uint32_t period_ms = 0);
        [[nodiscard]] std::string getAxisStream();
        [[nodiscard]] bool setMouseStream(const std::string& mode, uint32_t period_ms = 0);
        [[nodiscard]] std::string getMouseStream();
        [[nodiscard]] bool setButtonStream(const std::string& mode, uint32_t period_ms = 0);
        [[nodiscard]] std::string getButtonStream();
        [[nodiscard]] bool setEcho(bool enabled);
        [[nodiscard]] std::string getEcho();

        // High-performance drag operations
        [[nodiscard]] bool mouseDrag(MouseButton button, int32_t x, int32_t y);
        [[nodiscard]] bool mouseDragSmooth(MouseButton button, int32_t x, int32_t y, uint32_t segments = 10);
        [[nodiscard]] bool mouseDragBezier(MouseButton button, int32_t x, int32_t y, uint32_t segments = 20,
            int32_t ctrl_x = 0, int32_t ctrl_y = 0);


        // Mouse wheel
        [[nodiscard]] bool mouseWheel(int32_t delta);

        // Keyboard control
        [[nodiscard]] bool keyboardDown(const KeyboardKey& key);
        [[nodiscard]] bool keyboardUp(const KeyboardKey& key);
        [[nodiscard]] bool keyboardPress(const KeyboardKey& key);
        [[nodiscard]] bool keyboardPress(const KeyboardKey& key, uint32_t hold_ms);
        [[nodiscard]] bool keyboardPress(const KeyboardKey& key,
            uint32_t hold_ms, uint32_t rand_ms);
        [[nodiscard]] bool keyboardString(const std::string& text);
        [[nodiscard]] bool keyboardInit();
        [[nodiscard]] bool keyboardIsDown(const KeyboardKey& key);
        [[nodiscard]] bool keyboardMask(const KeyboardKey& key, bool enable);
        [[nodiscard]] bool keyboardRemap(const KeyboardKey& source,
            const KeyboardKey& target);
        [[nodiscard]] bool keyboardMultiDown(const std::vector<KeyboardKey>& keys);
        [[nodiscard]] bool keyboardMultiUp(const std::vector<KeyboardKey>& keys);
        [[nodiscard]] bool keyboardMultiPress(const std::vector<KeyboardKey>& keys);
        [[nodiscard]] std::string getKeyboardKeys();
        [[nodiscard]] bool setKeyboardKeys(bool enabled);

        // Mouse locking with state caching
        [[nodiscard]] bool lockMouseX(bool lock = true);
        [[nodiscard]] bool lockMouseY(bool lock = true);
        [[nodiscard]] bool lockMouseLeft(bool lock = true);
        [[nodiscard]] bool lockMouseMiddle(bool lock = true);
        [[nodiscard]] bool lockMouseRight(bool lock = true);
        [[nodiscard]] bool lockMouseSide1(bool lock = true);
        [[nodiscard]] bool lockMouseSide2(bool lock = true);

        // Fast lock state queries (cached)
        [[nodiscard]] bool isMouseXLocked() const;
        [[nodiscard]] bool isMouseYLocked() const;
        [[nodiscard]] bool isMouseLeftLocked() const;
        [[nodiscard]] bool isMouseMiddleLocked() const;
        [[nodiscard]] bool isMouseRightLocked() const;
        [[nodiscard]] bool isMouseSide1Locked() const;
        [[nodiscard]] bool isMouseSide2Locked() const;

        // Batch lock state query
        [[nodiscard]] std::unordered_map<std::string, bool> getAllLockStates() const;

        // Mouse input catching
        [[nodiscard]] uint8_t catchMouseLeft();
        [[nodiscard]] uint8_t catchMouseMiddle();
        [[nodiscard]] uint8_t catchMouseRight();
        [[nodiscard]] uint8_t catchMouseSide1();
        [[nodiscard]] uint8_t catchMouseSide2();
        [[nodiscard]] bool setCatchMouseLeft(uint8_t value);
        [[nodiscard]] bool setCatchMouseMiddle(uint8_t value);
        [[nodiscard]] bool setCatchMouseRight(uint8_t value);
        [[nodiscard]] bool setCatchMouseSide1(uint8_t value);
        [[nodiscard]] bool setCatchMouseSide2(uint8_t value);

        // Directional axis locks
        [[nodiscard]] bool lockMouseXPositive(bool lock = true);
        [[nodiscard]] bool lockMouseXNegative(bool lock = true);
        [[nodiscard]] bool lockMouseYPositive(bool lock = true);
        [[nodiscard]] bool lockMouseYNegative(bool lock = true);

        // Button monitoring with optimized processing
        [[nodiscard]] bool enableButtonMonitoring(bool enable = true);
        [[nodiscard]] bool isButtonMonitoringEnabled() const noexcept;
        [[nodiscard]] uint8_t getButtonMask() const noexcept;

        // Serial spoofing
        [[nodiscard]] std::string getMouseSerial();
        [[nodiscard]] bool setMouseSerial(const std::string& serial);
        [[nodiscard]] bool resetMouseSerial();


        // Device control
        /**
         * @brief Changes the MAKXD device baud rate using the proper protocol
         * 
         * Sends the MAKXD-specific baud rate change command and reconnects at the new rate.
         * Valid range: 115200 - 4000000 bps. Note: This change is NOT permanent and will
         * reset to 115200 when the MAKXD device is power cycled.
         * 
         * @param baudRate Target baud rate (will be clamped to valid range)
         * @param validateCommunication Whether to test communication after baud rate change (default: true)
         * @return true if baud rate was successfully changed and verified, false otherwise
         */
        [[nodiscard]] bool setBaudRate(uint32_t baudRate, bool validateCommunication = true);
        [[nodiscard]] std::string getBaudRate();

        // Callbacks
        void setMouseButtonCallback(MouseButtonCallback callback);
        void setConnectionCallback(ConnectionCallback callback);

        // High-level automation
        [[nodiscard]] bool clickSequence(const std::vector<MouseButton>& buttons,
            std::chrono::milliseconds delay = std::chrono::milliseconds(50));
        [[nodiscard]] bool movePattern(const std::vector<std::pair<int32_t, int32_t>>& points,
            bool smooth = true, uint32_t segments = 10);

        // Performance utilities
        void enableHighPerformanceMode(bool enable = true);
        [[nodiscard]] bool isHighPerformanceModeEnabled() const noexcept;

        // Command batching for maximum performance
        class MAKXD_API BatchCommandBuilder {
        public:
            BatchCommandBuilder& move(int32_t x, int32_t y);
            BatchCommandBuilder& moveSmooth(int32_t x, int32_t y, uint32_t segments = 10);
            BatchCommandBuilder& moveBezier(int32_t x, int32_t y, uint32_t segments = 20,
                int32_t ctrl_x = 0, int32_t ctrl_y = 0);
            BatchCommandBuilder& click(MouseButton button);
            BatchCommandBuilder& press(MouseButton button);
            BatchCommandBuilder& release(MouseButton button);
            BatchCommandBuilder& scroll(int32_t delta);
            BatchCommandBuilder& drag(MouseButton button, int32_t x, int32_t y);
            BatchCommandBuilder& dragSmooth(MouseButton button, int32_t x, int32_t y, uint32_t segments = 10);
            BatchCommandBuilder& dragBezier(MouseButton button, int32_t x, int32_t y, uint32_t segments = 20,
                int32_t ctrl_x = 0, int32_t ctrl_y = 0);
            BatchCommandBuilder& keyboardDown(const KeyboardKey& key);
            BatchCommandBuilder& keyboardUp(const KeyboardKey& key);
            BatchCommandBuilder& keyboardPress(const KeyboardKey& key);
            BatchCommandBuilder& keyboardPress(const KeyboardKey& key, uint32_t hold_ms);
            BatchCommandBuilder& keyboardPress(const KeyboardKey& key,
                uint32_t hold_ms, uint32_t rand_ms);
            BatchCommandBuilder& keyboardString(const std::string& text);
            BatchCommandBuilder& keyboardInit();
            BatchCommandBuilder& keyboardMask(const KeyboardKey& key, bool enable);
            BatchCommandBuilder& keyboardRemap(const KeyboardKey& source,
                const KeyboardKey& target);
            BatchCommandBuilder& keyboardMultiDown(const std::vector<KeyboardKey>& keys);
            BatchCommandBuilder& keyboardMultiUp(const std::vector<KeyboardKey>& keys);
            BatchCommandBuilder& keyboardMultiPress(const std::vector<KeyboardKey>& keys);
            BatchCommandBuilder& keyboardKeys(bool enabled);
            [[nodiscard]] bool execute();

        private:
            friend class Device;
            BatchCommandBuilder(Device* device, std::shared_ptr<std::atomic<bool>> deviceLifetime)
                : m_device(device), m_deviceLifetime(std::move(deviceLifetime)) {}
            [[nodiscard]] bool isDeviceAlive() const;
            Device* m_device;
            std::shared_ptr<std::atomic<bool>> m_deviceLifetime;
            std::vector<std::string> m_commands;
        };

        BatchCommandBuilder createBatch();

        // Legacy raw command interface (not recommended for performance)
        [[deprecated("Use typed Device methods (mouseMove/click/lock/etc.) instead of raw commands.")]]
        [[nodiscard]] bool sendRawCommand(const std::string& command) const;
        [[deprecated("Use typed Device methods and callbacks instead of raw response polling.")]]
        [[nodiscard]] std::string receiveRawResponse() const;

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
