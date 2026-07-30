#include "../include/makxd.h"
#include "../include/serialport.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace makxd {

    ConnectionConfig ConnectionConfig::com(
        std::string port,
        ApiProtocol protocol,
        std::string aes128Key) {
        ConnectionConfig config;
        config.method = ConnectionMethod::COM;
        config.apiProtocol = protocol;
        config.aes128Key = std::move(aes128Key);
        config.comPort = std::move(port);
        return config;
    }

    ConnectionConfig ConnectionConfig::udp(
        std::string host,
        uint16_t port,
        UdpWireMode mode,
        ApiProtocol protocol,
        std::string aes128Key,
        std::string bindAddress,
        std::string interfaceName,
        uint16_t vlanId) {
        ConnectionConfig config;
        config.method = ConnectionMethod::UDP;
        config.apiProtocol = protocol;
        config.aes128Key = std::move(aes128Key);
        config.udpHost = std::move(host);
        config.udpPort = port;
        config.udpMode = mode;
        config.udpBindAddress = std::move(bindAddress);
        config.udpInterface = std::move(interfaceName);
        config.vlanId = vlanId;
        return config;
    }

    ConnectionConfig ConnectionConfig::ble(
        std::string address,
        std::function<bool(std::string_view)> connect,
        std::function<bool(std::span<const uint8_t>)> write,
        std::function<size_t(std::span<uint8_t>)> read,
        std::function<void()> close,
        ApiProtocol protocol) {
        ConnectionConfig config;
        config.method = ConnectionMethod::BLE;
        config.apiProtocol = protocol;
        config.bleAddress = std::move(address);
        config.bleConnect = std::move(connect);
        config.bleWrite = std::move(write);
        config.bleRead = std::move(read);
        config.bleClose = std::move(close);
        return config;
    }

    namespace {
        bool equalsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) {
            if (lhs.size() != rhs.size()) {
                return false;
            }

            for (size_t i = 0; i < lhs.size(); ++i) {
                if (std::toupper(static_cast<unsigned char>(lhs[i])) !=
                    std::toupper(static_cast<unsigned char>(rhs[i]))) {
                    return false;
                }
            }

            return true;
        }

        std::optional<uint8_t> parseUint8Decimal(std::string_view valueText) {
            int parsedValue = 0;
            const char* begin = valueText.data();
            const char* end = begin + valueText.size();
            const auto [parseEnd, parseErr] = std::from_chars(begin, end, parsedValue);
            if (parseErr != std::errc{} || parseEnd != end) {
                return std::nullopt;
            }

            if (parsedValue < 0 || parsedValue > (std::numeric_limits<uint8_t>::max)()) {
                return std::nullopt;
            }

            return static_cast<uint8_t>(parsedValue);
        }

        std::string escapeSingleQuotedCommandString(std::string_view value) {
            constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

            std::string escaped;
            escaped.reserve(value.size());

            for (const unsigned char ch : value) {
                switch (ch) {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\'':
                    escaped += "\\'";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    if (std::iscntrl(ch)) {
                        escaped += "\\x";
                        escaped += HEX_DIGITS[(ch >> 4) & 0x0F];
                        escaped += HEX_DIGITS[ch & 0x0F];
                    } else {
                        escaped.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }

            return escaped;
        }

        std::string escapeDoubleQuotedCommandString(std::string_view value) {
            constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

            std::string escaped;
            escaped.reserve(value.size());

            for (const unsigned char ch : value) {
                switch (ch) {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    if (std::iscntrl(ch)) {
                        escaped += "\\x";
                        escaped += HEX_DIGITS[(ch >> 4) & 0x0F];
                        escaped += HEX_DIGITS[ch & 0x0F];
                    } else {
                        escaped.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }

            return escaped;
        }

        std::string keyboardKeyCommand(const KeyboardKey& key) {
            return std::visit([](const auto& value) -> std::string {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, uint8_t>) {
                    return std::to_string(value);
                } else {
                    if (value.empty()) {
                        return {};
                    }
                    return "'" + escapeSingleQuotedCommandString(value) + "'";
                }
            }, key);
        }

        std::optional<uint8_t> keyboardKeyCode(const KeyboardKey& key) {
            if (const auto* code = std::get_if<uint8_t>(&key)) {
                return *code;
            }
            const auto& source = std::get<std::string>(key);
            if (source.empty() || !std::ranges::all_of(
                    source, [](unsigned char byte) { return byte < 0x80u; })) {
                return std::nullopt;
            }
            std::string name(source);
            std::ranges::transform(name, name.begin(), [](unsigned char byte) {
                return static_cast<char>(std::tolower(byte));
            });
            if (name.size() == 1u) {
                if (name[0] >= 'a' && name[0] <= 'z') {
                    return static_cast<uint8_t>(4u + name[0] - 'a');
                }
                if (name[0] >= '1' && name[0] <= '9') {
                    return static_cast<uint8_t>(30u + name[0] - '1');
                }
                if (name[0] == '0') {
                    return 39u;
                }
            }
            if (name.size() >= 2u && name.size() <= 3u &&
                name[0] == 'f') {
                const auto function = parseUint8Decimal(
                    std::string_view(name).substr(1u));
                if (function && *function >= 1u && *function <= 12u) {
                    return static_cast<uint8_t>(57u + *function);
                }
            }
            if (name.size() == 3u &&
                (name.starts_with("kp") || name.starts_with("np")) &&
                name[2] >= '0' && name[2] <= '9') {
                return name[2] == '0'
                    ? 98u
                    : static_cast<uint8_t>(88u + name[2] - '0');
            }
            static constexpr std::pair<std::string_view, uint8_t> names[] = {
                {"enter",40},{"return",40},{"escape",41},{"esc",41},
                {"backspace",42},{"back",42},{"tab",43},{"space",44},
                {"spacebar",44},{"minus",45},{"dash",45},{"hyphen",45},
                {"equals",46},{"equal",46},{"leftbracket",47},{"lbracket",47},
                {"openbracket",47},{"rightbracket",48},{"rbracket",48},
                {"closebracket",48},{"backslash",49},{"bslash",49},
                {"nonus_hash",50},{"semicolon",51},{"semi",51},{"quote",52},
                {"apostrophe",52},{"singlequote",52},{"grave",53},
                {"backtick",53},{"tilde",53},{"comma",54},{"period",55},
                {"dot",55},{"slash",56},{"forwardslash",56},{"fslash",56},
                {"capslock",57},{"caps",57},{"printscreen",70},{"prtsc",70},
                {"print",70},{"scrolllock",71},{"scroll",71},{"pause",72},
                {"break",72},{"insert",73},{"ins",73},{"home",74},
                {"pageup",75},{"pgup",75},{"delete",76},{"del",76},
                {"end",77},{"pagedown",78},{"pgdown",78},{"pgdn",78},
                {"right",79},{"rightarrow",79},{"left",80},{"leftarrow",80},
                {"down",81},{"downarrow",81},{"up",82},{"uparrow",82},
                {"numlock",83},{"num",83},{"kpdivide",84},{"npdivide",84},
                {"kpmultiply",85},{"npmultiply",85},{"kpminus",86},
                {"npminus",86},{"kpplus",87},{"npplus",87},{"kpenter",88},
                {"npenter",88},{"kpperiod",99},{"kpdot",99},
                {"npperiod",99},{"npdot",99},{"leftctrl",224},{"lctrl",224},
                {"leftcontrol",224},{"lcontrol",224},{"ctrl",224},
                {"control",224},{"leftshift",225},{"lshift",225},{"shift",225},
                {"leftalt",226},{"lalt",226},{"alt",226},{"leftgui",227},
                {"lgui",227},{"leftwin",227},{"lwin",227},{"gui",227},
                {"win",227},{"windows",227},{"super",227},{"meta",227},
                {"cmd",227},{"command",227},{"rightctrl",228},{"rctrl",228},
                {"rightcontrol",228},{"rcontrol",228},{"rightshift",229},
                {"rshift",229},{"rightalt",230},{"ralt",230},
                {"rightgui",231},{"rgui",231},{"rightwin",231},{"rwin",231},
                {"rightwindows",231},
            };
            for (const auto& [candidate, code] : names) {
                if (name == candidate) {
                    return code;
                }
            }
            return std::nullopt;
        }

        void appendU16(std::vector<uint8_t>& payload, uint16_t value) {
            payload.push_back(static_cast<uint8_t>(value));
            payload.push_back(static_cast<uint8_t>(value >> 8u));
        }

        void appendI16(std::vector<uint8_t>& payload, int16_t value) {
            appendU16(payload, static_cast<uint16_t>(value));
        }

        void appendU32(std::vector<uint8_t>& payload, uint32_t value) {
            payload.push_back(static_cast<uint8_t>(value));
            payload.push_back(static_cast<uint8_t>(value >> 8u));
            payload.push_back(static_cast<uint8_t>(value >> 16u));
            payload.push_back(static_cast<uint8_t>(value >> 24u));
        }

        uint16_t readU16(std::string_view value, size_t offset) {
            return static_cast<uint16_t>(
                static_cast<uint8_t>(value[offset]) |
                (static_cast<uint16_t>(
                    static_cast<uint8_t>(value[offset + 1u])) << 8u));
        }

        uint32_t readU32(std::string_view value, size_t offset) {
            return static_cast<uint32_t>(
                static_cast<uint8_t>(value[offset]) |
                (static_cast<uint32_t>(
                    static_cast<uint8_t>(value[offset + 1u])) << 8u) |
                (static_cast<uint32_t>(
                    static_cast<uint8_t>(value[offset + 2u])) << 16u) |
                (static_cast<uint32_t>(
                    static_cast<uint8_t>(value[offset + 3u])) << 24u));
        }

        std::optional<DeviceRoute> parseKmDeviceRoute(std::string_view value) {
            if (!value.starts_with("R:")) {
                return std::nullopt;
            }
            const auto mouse = value.find(";M:");
            const auto keyboard = value.find("uf;K:");
            const auto controller = value.find("uf;C:");
            if (mouse == std::string_view::npos ||
                keyboard == std::string_view::npos ||
                controller == std::string_view::npos ||
                !value.ends_with("uf")) {
                return std::nullopt;
            }
            DeviceRoute route{};
            const auto routeText = value.substr(2u, mouse - 2u);
            route.routeMask =
                (routeText.find('M') != std::string_view::npos ? 0x01u : 0u) |
                (routeText.find('K') != std::string_view::npos ? 0x02u : 0u) |
                (routeText.find('C') != std::string_view::npos ? 0x04u : 0u);
            const auto parseU16 = [](std::string_view text)
                -> std::optional<uint16_t> {
                uint32_t parsed = 0u;
                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), parsed);
                if (error != std::errc{} ||
                    end != text.data() + text.size() ||
                    parsed > 0xFFFFu) {
                    return std::nullopt;
                }
                return static_cast<uint16_t>(parsed);
            };
            const auto mouseValue = parseU16(value.substr(
                mouse + 3u, keyboard - (mouse + 3u)));
            const auto keyboardValue = parseU16(value.substr(
                keyboard + 5u, controller - (keyboard + 5u)));
            const auto controllerValue = parseU16(value.substr(
                controller + 5u,
                value.size() - 2u - (controller + 5u)));
            if (!mouseValue || !keyboardValue || !controllerValue) {
                return std::nullopt;
            }
            route.mouseUframes = *mouseValue;
            route.keyboardUframes = *keyboardValue;
            route.controllerUframes = *controllerValue;
            return route;
        }

        std::string keyboardKeyListCommand(
            std::string_view commandName,
            const std::vector<KeyboardKey>& keys)
        {
            if (keys.empty()) {
                return {};
            }

            std::string command = std::string(commandName) + "(";
            for (size_t index = 0; index < keys.size(); ++index) {
                const auto keyCommand = keyboardKeyCommand(keys[index]);
                if (keyCommand.empty()) {
                    return {};
                }
                if (index != 0u) {
                    command += ',';
                }
                command += keyCommand;
            }
            command += ')';
            return command;
        }

        constexpr uint16_t DT_UFRAMES_MAX = 0x3FFFu;

        bool dtUframesValid(uint16_t dtUframes) {
            return dtUframes <= DT_UFRAMES_MAX;
        }

        std::string commandWithDt(std::string_view command, uint16_t dtUframes) {
            if (!dtUframesValid(dtUframes) ||
                command.empty() || command.back() != ')') {
                return {};
            }
            std::string timedCommand(command.substr(0u, command.size() - 1u));
            timedCommand += ',';
            timedCommand += std::to_string(dtUframes);
            timedCommand += ')';
            return timedCommand;
        }

        std::string controllerCommandBuild(
            std::string_view name,
            std::initializer_list<int64_t> values,
            std::optional<uint16_t> dtUframes = std::nullopt) {
            if (dtUframes.has_value() && !dtUframesValid(*dtUframes)) {
                return {};
            }
            std::string command("km.");
            command += name;
            command += '(';
            bool first = true;
            for (const auto value : values) {
                if (!first) {
                    command += ',';
                }
                first = false;
                command += std::to_string(value);
            }
            if (dtUframes.has_value()) {
                command += ',';
                command += std::to_string(*dtUframes);
            }
            command += ')';
            return command;
        }
    } // namespace

    // Constants
    constexpr uint16_t MAKXD_VID = 0x1A86;
    constexpr uint16_t MAKXD_PID_CH343 = 0x55D3;
    constexpr uint16_t MAKXD_PID_CH340 = 0x7523;
    constexpr const char* CH343_DESC = "USB-Enhanced-SERIAL CH343";
    constexpr const char* CH340_DESC = "USB-SERIAL CH340";
    constexpr std::array<uint32_t, 3> BAUD_CANDIDATES = {
        115200u, 1000000u, 4000000u
    };
    constexpr auto BAUD_OPEN_SETTLE = std::chrono::milliseconds(180);
    constexpr auto BAUD_CLOSE_SETTLE = std::chrono::milliseconds(120);
    constexpr auto BAUD_PROBE_TIMEOUT = std::chrono::milliseconds(750);

    // Static member definitions for PerformanceProfiler
    std::atomic<bool> PerformanceProfiler::s_enabled{ false };
    std::mutex PerformanceProfiler::s_mutex;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> PerformanceProfiler::s_stats;

    // Command cache for maximum performance - using constexpr arrays for zero-overhead lookups
    struct CommandCache {
        static constexpr size_t BUTTON_COUNT = 5; // LEFT, RIGHT, MIDDLE, SIDE1, SIDE2
        static constexpr size_t LOCK_TARGET_COUNT = 7; // X, Y, LEFT, RIGHT, MIDDLE, SIDE1, SIDE2

        // Pre-computed command strings indexed by MouseButton enum value
        std::array<std::string, BUTTON_COUNT> press_commands;
        std::array<std::string, BUTTON_COUNT> release_commands;

        // Lock/unlock/query commands indexed by LockTarget enum value
        std::array<std::string, LOCK_TARGET_COUNT> lock_commands;
        std::array<std::string, LOCK_TARGET_COUNT> unlock_commands;
        std::array<std::string, LOCK_TARGET_COUNT> query_commands;

        CommandCache() {
            // Pre-compute all button commands indexed by MouseButton
            press_commands[std::to_underlying(MouseButton::LEFT)] = "km.left(1)";
            press_commands[std::to_underlying(MouseButton::RIGHT)] = "km.right(1)";
            press_commands[std::to_underlying(MouseButton::MIDDLE)] = "km.middle(1)";
            press_commands[std::to_underlying(MouseButton::SIDE1)] = "km.side1(1)";
            press_commands[std::to_underlying(MouseButton::SIDE2)] = "km.side2(1)";

            release_commands[std::to_underlying(MouseButton::LEFT)] = "km.left(0)";
            release_commands[std::to_underlying(MouseButton::RIGHT)] = "km.right(0)";
            release_commands[std::to_underlying(MouseButton::MIDDLE)] = "km.middle(0)";
            release_commands[std::to_underlying(MouseButton::SIDE1)] = "km.side1(0)";
            release_commands[std::to_underlying(MouseButton::SIDE2)] = "km.side2(0)";

            // Pre-compute lock commands indexed by LockTarget
            lock_commands[0] = "km.lock_mx(1)"; // X
            lock_commands[1] = "km.lock_my(1)"; // Y
            lock_commands[2] = "km.lock_ml(1)"; // LEFT
            lock_commands[3] = "km.lock_mr(1)"; // RIGHT
            lock_commands[4] = "km.lock_mm(1)"; // MIDDLE
            lock_commands[5] = "km.lock_ms1(1)"; // SIDE1
            lock_commands[6] = "km.lock_ms2(1)"; // SIDE2

            unlock_commands[0] = "km.lock_mx(0)";
            unlock_commands[1] = "km.lock_my(0)";
            unlock_commands[2] = "km.lock_ml(0)";
            unlock_commands[3] = "km.lock_mr(0)";
            unlock_commands[4] = "km.lock_mm(0)";
            unlock_commands[5] = "km.lock_ms1(0)";
            unlock_commands[6] = "km.lock_ms2(0)";

            query_commands[0] = "km.lock_mx()";
            query_commands[1] = "km.lock_my()";
            query_commands[2] = "km.lock_ml()";
            query_commands[3] = "km.lock_mr()";
            query_commands[4] = "km.lock_mm()";
            query_commands[5] = "km.lock_ms1()";
            query_commands[6] = "km.lock_ms2()";
        }

        // Safe button command lookup — returns nullptr if out of range
        const std::string* getPressCommand(MouseButton button) const {
            auto idx = std::to_underlying(button);
            return idx < BUTTON_COUNT ? &press_commands[idx] : nullptr;
        }

        const std::string* getReleaseCommand(MouseButton button) const {
            auto idx = std::to_underlying(button);
            return idx < BUTTON_COUNT ? &release_commands[idx] : nullptr;
        }
    };

    // High-performance PIMPL implementation
    class Device::Impl {
    public:
        std::unique_ptr<SerialPort> serialPort;
        ApiProtocol apiProtocol;
        ConnectionConfig connection;
        DeviceInfo deviceInfo;
        std::atomic<ConnectionStatus> atomicStatus{ConnectionStatus::DISCONNECTED};
        std::atomic<bool> connected;
        std::atomic<bool> highPerformanceMode;
        mutable std::mutex mutex;
        static std::string lastError;

        // Command cache for ultra-fast lookups
        CommandCache commandCache;

        // State caching with bitwise operations (like Python v2.0)
        std::atomic<uint16_t> lockStateCache{ 0 };  // 16 bits for different lock states
        std::atomic<bool> lockStateCacheValid{ false };

        // Button state tracking
        std::atomic<uint8_t> currentButtonMask{ 0 };
        std::atomic<bool> buttonMonitoringEnabled{ false };

        // Callbacks
        Device::MouseButtonCallback mouseButtonCallback;
        Device::ConnectionCallback connectionCallback;
        mutable std::mutex callbackMutex;

        // Pre-allocated string buffers for different command types
        mutable std::string moveCommandBuffer;
        mutable std::string smoothCommandBuffer;
        mutable std::string bezierCommandBuffer;
        mutable std::string wheelCommandBuffer;
        mutable std::string generalCommandBuffer;
        mutable std::mutex commandBufferMutex;

        // Connection monitoring
        std::jthread monitoringThread;
        std::condition_variable monitoringCondition;
        std::mutex monitoringMutex;

        enum class LockTarget : uint8_t {
            X = 0,
            Y = 1,
            LEFT = 2,
            RIGHT = 3,
            MIDDLE = 4,
            SIDE1 = 5,
            SIDE2 = 6
        };
        
        // Safe thread cleanup
        void cleanupMonitoringThread() {
            if (!monitoringThread.joinable()) {
                return;
            }

            monitoringThread.request_stop();

            // Wake up the monitoring thread immediately.
            monitoringCondition.notify_all();

            if (std::this_thread::get_id() != monitoringThread.get_id()) {
                monitoringThread.join();
            }
            // If on the monitoring thread, skip join — the jthread destructor
            // will join after the monitoring loop exits via the stop token.
        }

        Impl(
            bool encryptionEnabled,
            std::string_view encryptionKey,
            ApiProtocol selectedApiProtocol)
            : serialPort(std::make_unique<SerialPort>(
                  encryptionEnabled, encryptionKey))
            , apiProtocol(selectedApiProtocol)
            , connection(ConnectionConfig::com(
                {}, selectedApiProtocol,
                encryptionEnabled ? std::string(encryptionKey) : std::string{}))
            , connected(false)
            , highPerformanceMode(false) {
          
            deviceInfo.isConnected = false;

            // Pre-allocate command buffers to avoid frequent allocations
            moveCommandBuffer.reserve(128);
            smoothCommandBuffer.reserve(128);
            bezierCommandBuffer.reserve(192);
            wheelCommandBuffer.reserve(64);
            generalCommandBuffer.reserve(256);

            // Set up button callback for serial port
            serialPort->setButtonCallback([this](uint8_t button, bool pressed) {
                handleButtonEvent(button, pressed);
                });
        }

        ~Impl() = default;

        static void setLastError(const std::string& error) {
            lastError = error;
        }

        bool initializeDevice() {
            if (!serialPort->isOpen()) {
                setLastError("Initialize device serial port open failed: " + serialPort->getLastError());
                return false;
            }

            // Small delay for device to be ready
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (connection.method != ConnectionMethod::COM) {
                return true;
            }

            try {
                if (apiProtocol == ApiProtocol::MAK_API) {
                    constexpr std::array<uint8_t, 1> enabled{1u};
                    serialPort->sendTrackedMakApi(
                        ApiOpcode::BUTTONS,
                        ApiVerb::SET,
                        enabled,
                        std::chrono::milliseconds(100)).get();
                    return true;
                }
                return serialPort->sendTrackedCommand(
                    "km.buttons(1)", true,
                    std::chrono::milliseconds(100)).get() == "km.buttons(1)";
            }
            catch (...) {
                return false;
            }
        }

        void handleButtonEvent(uint8_t button, bool pressed) {
            // Update button mask atomically using fetch_or/fetch_and
            // to avoid lost updates from concurrent button events
            const uint8_t bit = static_cast<uint8_t>(1u << button);
            if (pressed) {
                currentButtonMask.fetch_or(bit, std::memory_order_acq_rel);
            }
            else {
                currentButtonMask.fetch_and(static_cast<uint8_t>(~bit), std::memory_order_acq_rel);
            }

            // Call user callback if set
            if (button >= 5) {
                return;
            }

            Device::MouseButtonCallback callbackCopy;
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                callbackCopy = mouseButtonCallback;
            }

            if (!callbackCopy) {
                return;
            }

            const MouseButton mouseBtn = static_cast<MouseButton>(button);
            try {
                callbackCopy(mouseBtn, pressed);
            }
            catch (...) {
                // Ignore callback exceptions.
            }
        }

        void notifyConnectionChange(bool isConnected) {
            Device::ConnectionCallback callbackCopy;
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                callbackCopy = connectionCallback;
            }

            if (!callbackCopy) {
                return;
            }

            try {
                callbackCopy(isConnected);
            }
            catch (...) {
                // Ignore callback exceptions.
            }
        }

        void connectionMonitoringLoop(std::stop_token stopToken) {
            int pollInterval = 150;
            const int maxPollInterval = 500;
            const int pollIncrement = 50;
            
            while (!stopToken.stop_requested()) {
                // Double-check connection state with acquire semantics to ensure we see all updates
                bool currentlyConnected = connected.load(std::memory_order_acquire);
                if (!currentlyConnected) {
                    break;
                }
                
                // Check actual connection status using platform-specific methods
                // Use a local variable to avoid multiple calls during state updates
                bool actuallyConnected = serialPort->isActuallyConnected();
                
                if (!actuallyConnected) {
                    // Device disconnected - use compare_exchange to prevent race conditions
                    // Only update if we're still marked as connected
                    bool expectedConnected = true;
                    if (connected.compare_exchange_strong(expectedConnected, false, std::memory_order_acq_rel)) {
                        // We successfully changed from connected to disconnected
                        // Now update all other state atomically
                        atomicStatus.store(ConnectionStatus::DISCONNECTED, std::memory_order_release);
                        currentButtonMask.store(0, std::memory_order_release);
                        lockStateCacheValid.store(false, std::memory_order_release);
                        buttonMonitoringEnabled.store(false, std::memory_order_release);
                        
                        // Trigger callback after all state is updated
                        notifyConnectionChange(false);
                    }
                    
                    // Exit the loop regardless of who updated the state
                    break;
                }
                
                // Use condition variable for interruptible sleep with exponential backoff
                std::unique_lock<std::mutex> lock(monitoringMutex);
                if (monitoringCondition.wait_for(lock, std::chrono::milliseconds(pollInterval),
                    [&stopToken] { return stopToken.stop_requested(); })) {
                    // Condition was signaled (stop requested)
                    break;
                }
                
                // Exponential backoff to reduce CPU usage
                pollInterval = std::min<int>(maxPollInterval, pollInterval + pollIncrement);
            }
        }

        // High-performance command execution
        bool executeCommand(const std::string& command) {
            if (!connected.load(std::memory_order_acquire)) {
                return false;
            }
            if (apiProtocol == ApiProtocol::MAK_API) {
                setLastError("KM-only command is unavailable in MAK_API mode");
                return false;
            }

            auto start = std::chrono::high_resolution_clock::now();

            bool result = false;
            try {
                result = serialPort->sendTrackedCommand(
                    command, true, std::chrono::milliseconds(100)).get() == command;
            }
            catch (...) {
                result = false;
            }

            // Performance profiling
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            makxd::PerformanceProfiler::logCommandTiming(command, duration);

            return result;
        }

        bool executeApiCommand(
            const std::string& command,
            ApiOpcode opcode,
            ApiVerb verb,
            std::span<const uint8_t> payload = {}) {
            if (!connected.load(std::memory_order_acquire)) {
                return false;
            }
            if (apiProtocol == ApiProtocol::KM) {
                return executeCommand(command);
            }
            try {
                serialPort->sendTrackedMakApi(
                    opcode, verb, payload,
                    std::chrono::milliseconds(100)).get();
                return true;
            } catch (const std::exception& error) {
                setLastError(error.what());
                return false;
            }
        }

        std::optional<std::string> executeApiQuery(
            const std::string& command,
            ApiOpcode opcode,
            std::span<const uint8_t> payload = {}) {
            if (!connected.load(std::memory_order_acquire)) {
                return std::nullopt;
            }
            try {
                if (apiProtocol == ApiProtocol::KM) {
                    return serialPort->sendTrackedCommand(
                        command, true,
                        std::chrono::milliseconds(100)).get();
                }
                return serialPort->sendTrackedMakApi(
                    opcode, ApiVerb::GET, payload,
                    std::chrono::milliseconds(100)).get();
            } catch (const std::exception& error) {
                setLastError(error.what());
                return std::nullopt;
            }
        }


        // Optimized move command with buffer reuse and bounds checking
        bool executeMoveCommand(
            int32_t x,
            int32_t y,
            std::optional<uint16_t> dtUframes = std::nullopt) {
            // Validate coordinate ranges to prevent buffer overflow
            constexpr int32_t MAX_COORD = 32767;
            constexpr int32_t MIN_COORD = -32768;
            
            if (x < MIN_COORD || x > MAX_COORD || y < MIN_COORD || y > MAX_COORD ||
                (dtUframes.has_value() && !dtUframesValid(*dtUframes))) {
                #ifdef DEBUG
                std::cerr << "Move coordinates out of range: (" << x << "," << y << ")" << std::endl;
                #endif
                return false;
            }
            
            std::lock_guard<std::mutex> lock(commandBufferMutex);
            moveCommandBuffer.clear();

            moveCommandBuffer = "km.move(";
            moveCommandBuffer += std::to_string(x);
            moveCommandBuffer += ",";
            moveCommandBuffer += std::to_string(y);
            if (dtUframes.has_value()) {
                moveCommandBuffer += ",";
                moveCommandBuffer += std::to_string(*dtUframes);
            }
            moveCommandBuffer += ")";

            // Additional length check
            if (moveCommandBuffer.length() > 512) {
                return false;
            }

            std::array<uint8_t, 6> payload{};
            payload[0] = static_cast<uint8_t>(x);
            payload[1] = static_cast<uint8_t>(
                static_cast<uint16_t>(x) >> 8u);
            payload[2] = static_cast<uint8_t>(y);
            payload[3] = static_cast<uint8_t>(
                static_cast<uint16_t>(y) >> 8u);
            const size_t payloadBytes = dtUframes.has_value() ? 6u : 4u;
            if (dtUframes.has_value()) {
                payload[4] = static_cast<uint8_t>(*dtUframes);
                payload[5] = static_cast<uint8_t>(*dtUframes >> 8u);
            }
            return executeApiCommand(
                moveCommandBuffer,
                ApiOpcode::MOVE,
                ApiVerb::EXEC,
                std::span<const uint8_t>(payload.data(), payloadBytes));
        }

        // Optimized smooth move command with buffer reuse
        bool executeSmoothMoveCommand(int32_t x, int32_t y, uint32_t segments) {
            // Validate inputs
            constexpr int32_t MAX_COORD = 32767;
            constexpr int32_t MIN_COORD = -32768;
            
            if (x < MIN_COORD || x > MAX_COORD || y < MIN_COORD || y > MAX_COORD) {
                return false;
            }
            if (segments > 1000) { // Reasonable limit
                return false;
            }
            
            std::lock_guard<std::mutex> lock(commandBufferMutex);
            smoothCommandBuffer.clear();

            smoothCommandBuffer = "km.move(";
            smoothCommandBuffer += std::to_string(x);
            smoothCommandBuffer += ",";
            smoothCommandBuffer += std::to_string(y);
            smoothCommandBuffer += ",";
            smoothCommandBuffer += std::to_string(segments);
            smoothCommandBuffer += ")";

            return executeCommand(smoothCommandBuffer);
        }

        // Optimized bezier move command with buffer reuse
        bool executeBezierMoveCommand(int32_t x, int32_t y, uint32_t segments, int32_t ctrl_x, int32_t ctrl_y) {
            // Validate inputs
            constexpr int32_t MAX_COORD = 32767;
            constexpr int32_t MIN_COORD = -32768;
            
            if (x < MIN_COORD || x > MAX_COORD || y < MIN_COORD || y > MAX_COORD ||
                ctrl_x < MIN_COORD || ctrl_x > MAX_COORD || ctrl_y < MIN_COORD || ctrl_y > MAX_COORD) {
                return false;
            }
            if (segments > 1000) { // Reasonable limit
                return false;
            }
            
            std::lock_guard<std::mutex> lock(commandBufferMutex);
            bezierCommandBuffer.clear();

            bezierCommandBuffer = "km.move(";
            bezierCommandBuffer += std::to_string(x);
            bezierCommandBuffer += ",";
            bezierCommandBuffer += std::to_string(y);
            bezierCommandBuffer += ",";
            bezierCommandBuffer += std::to_string(segments);
            bezierCommandBuffer += ",";
            bezierCommandBuffer += std::to_string(ctrl_x);
            bezierCommandBuffer += ",";
            bezierCommandBuffer += std::to_string(ctrl_y);
            bezierCommandBuffer += ")";

            return executeCommand(bezierCommandBuffer);
        }

        // Optimized wheel command with buffer reuse
        bool executeWheelCommand(
            int32_t delta,
            std::optional<uint16_t> dtUframes = std::nullopt) {
            // Validate wheel delta range
            if (delta < -32768 || delta > 32767 ||
                (dtUframes.has_value() && !dtUframesValid(*dtUframes))) {
                return false;
            }
            
            std::lock_guard<std::mutex> lock(commandBufferMutex);
            wheelCommandBuffer.clear();

            wheelCommandBuffer = "km.wheel(";
            wheelCommandBuffer += std::to_string(delta);
            if (dtUframes.has_value()) {
                wheelCommandBuffer += ",";
                wheelCommandBuffer += std::to_string(*dtUframes);
            }
            wheelCommandBuffer += ")";

            std::array<uint8_t, 4> payload{};
            payload[0] = static_cast<uint8_t>(delta);
            payload[1] = static_cast<uint8_t>(
                static_cast<uint16_t>(delta) >> 8u);
            const size_t payloadBytes = dtUframes.has_value() ? 4u : 2u;
            if (dtUframes.has_value()) {
                payload[2] = static_cast<uint8_t>(*dtUframes);
                payload[3] = static_cast<uint8_t>(*dtUframes >> 8u);
            }
            return executeApiCommand(
                wheelCommandBuffer,
                ApiOpcode::WHEEL,
                ApiVerb::EXEC,
                std::span<const uint8_t>(payload.data(), payloadBytes));
        }

        static constexpr uint16_t lockBit(LockTarget target) {
            return static_cast<uint16_t>(1u << std::to_underlying(target));
        }

        // Cache-based lock state management using atomic RMW
        void updateLockStateCache(LockTarget target, bool locked) {
            const uint16_t bit = lockBit(target);
            if (locked) {
                lockStateCache.fetch_or(bit, std::memory_order_acq_rel);
            }
            else {
                lockStateCache.fetch_and(static_cast<uint16_t>(~bit), std::memory_order_acq_rel);
            }
            lockStateCacheValid.store(true, std::memory_order_release);
        }

        bool getLockStateFromCache(LockTarget target) const {
            if (!lockStateCacheValid.load(std::memory_order_acquire)) {
                return false; // Cache invalid
            }

            return (lockStateCache.load(std::memory_order_acquire) & lockBit(target)) != 0;
        }
    };

    // Device implementation
    Device::Device(
        bool encryptionEnabled,
        std::string_view encryptionKey,
        ApiProtocol apiProtocol)
        : m_impl(std::make_unique<Impl>(
            encryptionEnabled, encryptionKey, apiProtocol))
        , m_lifetimeToken(std::make_shared<std::atomic<bool>>(true)) {}

    Device::Device(ConnectionConfig connection)
        : m_impl(std::make_unique<Impl>(
            !connection.aes128Key.empty(),
            connection.aes128Key,
            connection.apiProtocol))
        , m_lifetimeToken(std::make_shared<std::atomic<bool>>(true)) {
        m_impl->connection = std::move(connection);
    }
    std::string Device::Impl::lastError = "";

    Device::~Device() {
        if (m_lifetimeToken) {
            m_lifetimeToken->store(false, std::memory_order_release);
        }
        disconnect();
    }

    std::vector<DeviceInfo> Device::findDevices() {
        std::vector<DeviceInfo> devices;
        auto ports = SerialPort::findMakxdPortInfo();

        for (const auto& portInfo : ports) {
            DeviceInfo info;
            info.port = portInfo.port;
            info.description = portInfo.pid == MAKXD_PID_CH343 ? CH343_DESC : CH340_DESC;
            info.vid = MAKXD_VID;
            info.pid = portInfo.pid;
            info.isConnected = false;
            devices.push_back(info);
        }

        return devices;
    }

    std::string Device::getLastError()
    {
        return m_impl->lastError;
    }

    std::string Device::findFirstDevice() {
        auto devices = findDevices();
        return devices.empty() ? "" : devices[0].port;
    }

    bool Device::connect() {
        return m_impl->connection.method == ConnectionMethod::COM
            ? connect(m_impl->connection.comPort)
            : connect(m_impl->connection);
    }

    bool Device::connect(const std::string& port) {
        std::unique_lock<std::mutex> lock(m_impl->mutex);

        if (m_impl->connected.load()) {
            return true;
        }
        m_impl->connection.method = ConnectionMethod::COM;
        m_impl->connection.comPort = port;

        std::vector<MakxdSerialPortInfo> candidates;
        if (port.empty()) {
            candidates = SerialPort::findMakxdPortInfo();
        } else {
            candidates.push_back({port, 0u});
            for (const auto& portInfo : SerialPort::findMakxdPortInfo()) {
                if (portInfo.port == port) {
                    candidates[0].pid = portInfo.pid;
                    break;
                }
            }
        }
        if (candidates.empty()) {
            m_impl->atomicStatus.store(ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            m_impl->setLastError("Invalid device port!");
            return false;
        }

        m_impl->atomicStatus.store(ConnectionStatus::CONNECTING, std::memory_order_release);
        m_impl->serialPort->setApiProtocol(ApiProtocol::KM);

        bool baudDetected = false;
        MakxdSerialPortInfo detectedPort{};
        for (const auto& candidate : candidates) {
            for (uint32_t baudRate : BAUD_CANDIDATES) {
                if (!m_impl->serialPort->open(candidate.port, baudRate)) {
                    std::this_thread::sleep_for(BAUD_CLOSE_SETTLE);
                    continue;
                }
                std::this_thread::sleep_for(BAUD_OPEN_SETTLE);
                try {
                    auto future = m_impl->serialPort->sendTrackedCommand(
                        "km.version()", true, BAUD_PROBE_TIMEOUT);
                    if ((future.wait_for(BAUD_PROBE_TIMEOUT) == std::future_status::ready) &&
                        (future.get() == "km.MAKXD")) {
                        baudDetected = true;
                        detectedPort = candidate;
                        break;
                    }
                }
                catch (...) {
                }
                m_impl->serialPort->close();
                std::this_thread::sleep_for(BAUD_CLOSE_SETTLE);
            }
            if (baudDetected) {
                break;
            }
        }

        if (!baudDetected) {
            m_impl->setLastError(
                "km.version() did not return km.MAKXD at any supported baud");
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            m_impl->deviceInfo.isConnected = false;
            return false;
        }

        if (!m_impl->serialPort->isOpen() ||
            !m_impl->serialPort->isActuallyConnected()) {
            m_impl->setLastError("Detected serial port is no longer connected!");
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            m_impl->deviceInfo.isConnected = false;
            return false;
        }

        m_impl->serialPort->setApiProtocol(m_impl->apiProtocol);

        // Initialize device
        if (!m_impl->initializeDevice()) {
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            m_impl->deviceInfo.isConnected = false;
            return false;
        }

        // Update device info first
        m_impl->deviceInfo.port = detectedPort.port;
        m_impl->deviceInfo.description =
            detectedPort.pid == MAKXD_PID_CH340 ? CH340_DESC : CH343_DESC;
        m_impl->deviceInfo.vid = MAKXD_VID;
        m_impl->deviceInfo.pid =
            detectedPort.pid == 0u ? MAKXD_PID_CH343 : detectedPort.pid;
        m_impl->deviceInfo.isConnected = true;

        // Atomically update all connection state before starting monitoring thread
        m_impl->atomicStatus.store(ConnectionStatus::CONNECTED, std::memory_order_release);
        m_impl->buttonMonitoringEnabled.store(true, std::memory_order_release);
        
        // Use acquire-release semantics to ensure all state is visible before connected flag is set
        std::atomic_thread_fence(std::memory_order_release);
        m_impl->connected.store(true, std::memory_order_release);
        
        // Start connection monitoring thread AFTER all state is established
        // This prevents the monitoring thread from seeing inconsistent state
        try {
            m_impl->monitoringThread = std::jthread([impl = m_impl.get()](std::stop_token stopToken) {
                impl->connectionMonitoringLoop(stopToken);
            });
        } catch (const std::system_error&) {
            // Thread creation failed - cleanup and return error
            m_impl->setLastError("Monitoring thread creation failure!");
            m_impl->connected.store(false, std::memory_order_release);
            m_impl->atomicStatus.store(ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            m_impl->deviceInfo.isConnected = false;
            m_impl->serialPort->close();
            return false;
        }

        lock.unlock();
        m_impl->notifyConnectionChange(true);

        return true;
    }

    bool Device::connect(const ConnectionConfig& connection) {
        if (m_impl->connected.load(std::memory_order_acquire)) {
            return true;
        }
        m_impl->serialPort = std::make_unique<SerialPort>(
            !connection.aes128Key.empty(), connection.aes128Key);
        m_impl->serialPort->setButtonCallback(
            [impl = m_impl.get()](uint8_t button, bool pressed) {
                impl->handleButtonEvent(button, pressed);
            });
        m_impl->apiProtocol = connection.apiProtocol;
        m_impl->connection = connection;
        if (connection.method == ConnectionMethod::COM) {
            return connect(connection.comPort);
        }

        std::unique_lock<std::mutex> lock(m_impl->mutex);
        m_impl->atomicStatus.store(
            ConnectionStatus::CONNECTING, std::memory_order_release);
        m_impl->serialPort->setApiProtocol(ApiProtocol::KM);
        if (!m_impl->serialPort->open(connection)) {
            m_impl->setLastError("Connection transport open failed");
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            return false;
        }
        try {
            auto version = m_impl->serialPort->sendTrackedCommand(
                "km.version()", true, BAUD_PROBE_TIMEOUT);
            if (version.wait_for(BAUD_PROBE_TIMEOUT) !=
                    std::future_status::ready ||
                version.get() != "km.MAKXD") {
                throw std::runtime_error(
                    "km.version() did not return km.MAKXD");
            }
        } catch (const std::exception& error) {
            m_impl->setLastError(error.what());
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            return false;
        }
        m_impl->serialPort->setApiProtocol(connection.apiProtocol);
        if (!m_impl->initializeDevice()) {
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            return false;
        }
        m_impl->deviceInfo.port = m_impl->serialPort->getPortName();
        m_impl->deviceInfo.description =
            connection.method == ConnectionMethod::UDP
                ? "MAKXD UDP"
                : "MAKXD BLE";
        m_impl->deviceInfo.vid = 0u;
        m_impl->deviceInfo.pid = 0u;
        m_impl->deviceInfo.isConnected = true;
        m_impl->atomicStatus.store(
            ConnectionStatus::CONNECTED, std::memory_order_release);
        m_impl->connected.store(true, std::memory_order_release);
        m_impl->monitoringThread = std::jthread(
            [impl = m_impl.get()](std::stop_token stopToken) {
                impl->connectionMonitoringLoop(stopToken);
            });
        lock.unlock();
        m_impl->notifyConnectionChange(true);
        return true;
    }

    std::future<bool> Device::connectAsync(const std::string& port) {
        // OPTIMIZED: Use immediate return for already connected state
        if (m_impl->connected.load(std::memory_order_acquire)) {
            // Create ready future more efficiently
            std::packaged_task<bool()> task([]() { return true; });
            auto future = task.get_future();
            task();
            return future;
        }
        
        // For actual connection, this is inherently I/O bound so thread is acceptable
        return std::async(std::launch::async, [this, port]() {
            return connect(port);
        });
    }

    std::expected<void, ConnectionStatus> Device::connectExpected(const std::string& port) {
        if (connect(port)) {
            return {};
        }

        return std::unexpected(getStatus());
    }

    void Device::disconnect() {
        bool shouldNotify = false;
        {
            std::unique_lock<std::mutex> lock(m_impl->mutex);

            // Always clean up monitoring thread first, regardless of connection state.
            m_impl->cleanupMonitoringThread();

            // Use compare_exchange to prevent race conditions with monitoring thread.
            bool expectedConnected = true;
            shouldNotify = m_impl->connected.compare_exchange_strong(
                expectedConnected, false, std::memory_order_acq_rel);

            // Ensure all disconnected state is consistent even if another thread
            // already marked the connection as down.
            m_impl->atomicStatus.store(ConnectionStatus::DISCONNECTED, std::memory_order_release);

            // Always close the serial port if it is still open.
            if (m_impl->serialPort->isOpen()) {
                m_impl->serialPort->close();
            }

            // Update remaining state after serial port is closed.
            m_impl->deviceInfo.isConnected = false;
            m_impl->currentButtonMask.store(0, std::memory_order_release);
            m_impl->lockStateCacheValid.store(false, std::memory_order_release);
            m_impl->buttonMonitoringEnabled.store(false, std::memory_order_release);
        }

        if (shouldNotify) {
            m_impl->notifyConnectionChange(false);
        }
    }


    bool Device::isConnected() const noexcept {
        return m_impl->connected.load(std::memory_order_acquire);
    }

    ConnectionStatus Device::getStatus() const noexcept {
        return m_impl->atomicStatus.load(std::memory_order_acquire);
    }

    DeviceInfo Device::getDeviceInfo() const {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        DeviceInfo info = m_impl->deviceInfo;
        info.isConnected = m_impl->connected.load(std::memory_order_acquire);
        return info;
    }

    std::string Device::getVersion() const {
        if (!m_impl->connected.load()) {
            return "";
        }

        // Retry with escalating timeouts to tolerate temporary serial instability.
        constexpr std::array<std::chrono::milliseconds, 3> timeouts = {
            std::chrono::milliseconds(75),
            std::chrono::milliseconds(150),
            std::chrono::milliseconds(300)
        };

        for (size_t attempt = 0; attempt < timeouts.size(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(attempt == 0 ? 10 : 20));

            try {
                std::string version =
                    m_impl->apiProtocol == ApiProtocol::MAK_API
                    ? m_impl->serialPort->sendTrackedMakApi(
                        ApiOpcode::VERSION,
                        ApiVerb::GET,
                        {},
                        timeouts[attempt]).get()
                    : m_impl->serialPort->sendTrackedCommand(
                        "km.version()", true, timeouts[attempt]).get();
                if (!version.empty()) {
                    return version;
                }
            }
            catch (...) {
                // Continue retry loop.
            }

            if (!m_impl->connected.load(std::memory_order_acquire)) {
                return "";
            }
        }

        return "";
    }

    std::expected<std::string, ConnectionStatus> Device::getVersionExpected() const {
        if (!m_impl->connected.load(std::memory_order_acquire)) {
            return std::unexpected(ConnectionStatus::DISCONNECTED);
        }

        const std::string version = getVersion();
        if (version.empty()) {
            return std::unexpected(getStatus());
        }

        return version;
    }

    ApiProtocol Device::getApiProtocol() const noexcept {
        return m_impl->apiProtocol;
    }

    std::expected<DeviceRoute, ConnectionStatus> Device::device() const {
        if (!m_impl->connected.load(std::memory_order_acquire)) {
            return std::unexpected(ConnectionStatus::DISCONNECTED);
        }
        const auto response = m_impl->executeApiQuery(
            "km.device()", ApiOpcode::DEVICE);
        if (!response) {
            return std::unexpected(getStatus());
        }
        if (m_impl->apiProtocol == ApiProtocol::KM) {
            const auto route = parseKmDeviceRoute(*response);
            return route
                ? std::expected<DeviceRoute, ConnectionStatus>(*route)
                : std::unexpected(getStatus());
        }
        if (response->size() != 11u) {
            return std::unexpected(getStatus());
        }
        DeviceRoute route{};
        route.routeMask = static_cast<uint8_t>((*response)[0]);
        route.mouseUframes = readU16(*response, 1u);
        route.keyboardUframes = readU16(*response, 3u);
        route.controllerUframes = readU16(*response, 5u);
        route.generation = readU32(*response, 7u);
        return route;
    }


    // High-performance mouse control methods
    bool Device::mouseDown(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto* cmd = m_impl->commandCache.getPressCommand(button);
        if (!cmd || std::to_underlying(button) >= 5u) {
            return false;
        }
        constexpr std::array<uint8_t, 1> payload{1u};
        return m_impl->executeApiCommand(
            *cmd,
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            ApiVerb::SET,
            payload);
    }

    bool Device::mouseDown(MouseButton button, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const auto* command = m_impl->commandCache.getPressCommand(button);
        const auto timedCommand =
            command ? commandWithDt(*command, dt_uframes) : std::string{};
        if (timedCommand.empty() || std::to_underlying(button) >= 5u) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            1u,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            timedCommand,
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            ApiVerb::SET,
            payload);
    }

    bool Device::mouseUp(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto* cmd = m_impl->commandCache.getReleaseCommand(button);
        if (!cmd || std::to_underlying(button) >= 5u) {
            return false;
        }
        constexpr std::array<uint8_t, 1> payload{0u};
        return m_impl->executeApiCommand(
            *cmd,
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            ApiVerb::SET,
            payload);
    }

    bool Device::mouseUp(MouseButton button, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const auto* command = m_impl->commandCache.getReleaseCommand(button);
        const auto timedCommand =
            command ? commandWithDt(*command, dt_uframes) : std::string{};
        if (timedCommand.empty() || std::to_underlying(button) >= 5u) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            0u,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            timedCommand,
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            ApiVerb::SET,
            payload);
    }

    bool Device::mouseButtonMask(MouseButton button, bool enabled) {
        if (!m_impl->connected.load() || std::to_underlying(button) >= 5u) {
            return false;
        }
        constexpr std::array<const char*, 5> commands{
            "left_mask", "right_mask", "middle_mask", "side1_mask", "side2_mask"};
        const auto index = std::to_underlying(button);
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enabled ? 1u : 0u)};
        return m_impl->executeApiCommand(
            "km." + std::string(commands[index]) + "(" +
                (enabled ? "1" : "0") + ")",
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT_MASK) + index),
            ApiVerb::SET,
            payload);
    }

    bool Device::mouseLeftMask(bool enabled) {
        return mouseButtonMask(MouseButton::LEFT, enabled);
    }

    bool Device::mouseRightMask(bool enabled) {
        return mouseButtonMask(MouseButton::RIGHT, enabled);
    }

    bool Device::mouseMiddleMask(bool enabled) {
        return mouseButtonMask(MouseButton::MIDDLE, enabled);
    }

    bool Device::mouseSide1Mask(bool enabled) {
        return mouseButtonMask(MouseButton::SIDE1, enabled);
    }

    bool Device::mouseSide2Mask(bool enabled) {
        return mouseButtonMask(MouseButton::SIDE2, enabled);
    }

    bool Device::mouseMoveMask(
        bool left, bool right, bool down, bool up) {
        if (!m_impl->connected.load()) {
            return false;
        }
        const std::array<uint8_t, 4> payload{
            static_cast<uint8_t>(left ? 1u : 0u),
            static_cast<uint8_t>(right ? 1u : 0u),
            static_cast<uint8_t>(down ? 1u : 0u),
            static_cast<uint8_t>(up ? 1u : 0u)};
        return m_impl->executeApiCommand(
            "km.move_mask(" + std::to_string(payload[0]) + "," +
                std::to_string(payload[1]) + "," +
                std::to_string(payload[2]) + "," +
                std::to_string(payload[3]) + ")",
            ApiOpcode::MOVE_MASK,
            ApiVerb::SET,
            payload);
    }

    bool Device::mouseWheelMask(bool down, bool up) {
        if (!m_impl->connected.load()) {
            return false;
        }
        const std::array<uint8_t, 2> payload{
            static_cast<uint8_t>(down ? 1u : 0u),
            static_cast<uint8_t>(up ? 1u : 0u)};
        return m_impl->executeApiCommand(
            "km.wheel_mask(" + std::to_string(payload[0]) + "," +
                std::to_string(payload[1]) + ")",
            ApiOpcode::WHEEL_MASK,
            ApiVerb::SET,
            payload);
    }

    bool Device::click(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        // For maximum performance, batch press+release
        const auto* pressCmd = m_impl->commandCache.getPressCommand(button);
        const auto* releaseCmd = m_impl->commandCache.getReleaseCommand(button);

        if (pressCmd && releaseCmd) {
            bool result1 = mouseDown(button);
            bool result2 = mouseUp(button);
            return result1 && result2;
        }
        return false;
    }

    bool Device::click(MouseButton button, uint32_t count, uint32_t delay_ms) {
        if (!m_impl->connected.load() || count == 0 || delay_ms == 0) {
            return false;
        }
        const auto buttonValue = std::to_underlying(button) + 1u;
        return m_impl->executeCommand(
            "km.click(" + std::to_string(buttonValue) + "," +
            std::to_string(count) + "," + std::to_string(delay_ms) + ")");
    }




    bool Device::mouseButtonState(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        // Use cached button state for performance
        uint8_t mask = m_impl->currentButtonMask.load();
        return (mask & (1u << std::to_underlying(button))) != 0;
    }


    // High-performance movement methods
    bool Device::mouseMove(int32_t x, int32_t y) {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeMoveCommand(x, y);
    }

    bool Device::mouseMove(int32_t x, int32_t y, uint16_t dt_uframes) {
        if (!m_impl->connected.load()) {
            return false;
        }
        return m_impl->executeMoveCommand(x, y, dt_uframes);
    }

    bool Device::mouseSilentMove(int32_t x, int32_t y) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand(
            "km.silent(" + std::to_string(x) + "," + std::to_string(y) + ")");
    }

    bool Device::mouseMoveSmooth(int32_t x, int32_t y, uint32_t segments) {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeSmoothMoveCommand(x, y, segments);
    }

    bool Device::mouseMoveBezier(int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeBezierMoveCommand(x, y, segments, ctrl_x, ctrl_y);
    }

    bool Device::mouseMoveControls(int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x1, int32_t ctrl_y1, int32_t ctrl_x2, int32_t ctrl_y2) {
        if (!m_impl->connected.load()) {
            return false;
        }
        return m_impl->executeCommand(
            "km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
            std::to_string(segments) + "," + std::to_string(ctrl_x1) + "," +
            std::to_string(ctrl_y1) + "," + std::to_string(ctrl_x2) + "," +
            std::to_string(ctrl_y2) + ")");
    }

    bool Device::setAxisStream(const std::string& mode, uint32_t period_ms) {
        if (!m_impl->connected.load() || mode.empty()) return false;
        const std::string suffix = period_ms == 0 ? ")" : "," + std::to_string(period_ms) + ")";
        return m_impl->executeCommand("km.axis(" + mode + suffix);
    }

    std::string Device::getAxisStream() {
        if (!m_impl->connected.load()) return {};
        try {
            return m_impl->serialPort->sendTrackedCommand(
                "km.axis()", true, std::chrono::milliseconds(100)).get();
        }
        catch (...) { return {}; }
    }

    bool Device::setMouseStream(const std::string& mode, uint32_t period_ms) {
        if (!m_impl->connected.load() || mode.empty()) return false;
        const std::string suffix = period_ms == 0 ? ")" : "," + std::to_string(period_ms) + ")";
        return m_impl->executeCommand("km.mouse(" + mode + suffix);
    }

    std::string Device::getMouseStream() {
        if (!m_impl->connected.load()) return {};
        try {
            return m_impl->serialPort->sendTrackedCommand(
                "km.mouse()", true, std::chrono::milliseconds(100)).get();
        }
        catch (...) { return {}; }
    }

    bool Device::setButtonStream(const std::string& mode, uint32_t period_ms) {
        if (!m_impl->connected.load() || mode.empty()) return false;
        const std::string suffix = period_ms == 0 ? ")" : "," + std::to_string(period_ms) + ")";
        const bool result = m_impl->executeCommand("km.buttons(" + mode + suffix);
        if (result && mode == "0") {
            m_impl->buttonMonitoringEnabled.store(false, std::memory_order_release);
        } else if (result) {
            m_impl->buttonMonitoringEnabled.store(true, std::memory_order_release);
        }
        return result;
    }

    std::string Device::getButtonStream() {
        if (!m_impl->connected.load()) return {};
        try {
            return m_impl->serialPort->sendTrackedCommand(
                "km.buttons()", true, std::chrono::milliseconds(100)).get();
        }
        catch (...) { return {}; }
    }

    bool Device::setEcho(bool enabled) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.echo(" + std::to_string(enabled ? 1 : 0) + ")");
    }

    std::string Device::getEcho() {
        if (!m_impl->connected.load()) return {};
        try {
            return m_impl->serialPort->sendTrackedCommand(
                "km.echo()", true, std::chrono::milliseconds(100)).get();
        }
        catch (...) { return {}; }
    }

    // High-performance drag operations
    bool Device::mouseDrag(MouseButton button, int32_t x, int32_t y) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto* pressCmd = m_impl->commandCache.getPressCommand(button);
        const auto* releaseCmd = m_impl->commandCache.getReleaseCommand(button);
        if (!pressCmd || !releaseCmd) return false;

        // Execute drag sequence: press -> move -> release
        bool result1 = mouseDown(button);
        bool result2 = m_impl->executeMoveCommand(x, y);
        bool result3 = mouseUp(button);

        return result1 && result2 && result3;
    }

    bool Device::mouseDragSmooth(MouseButton button, int32_t x, int32_t y, uint32_t segments) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto* pressCmd = m_impl->commandCache.getPressCommand(button);
        const auto* releaseCmd = m_impl->commandCache.getReleaseCommand(button);
        if (!pressCmd || !releaseCmd) return false;

        // Execute smooth drag sequence: press -> smooth move -> release
        bool result1 = m_impl->executeCommand(*pressCmd);
        bool result2 = m_impl->executeSmoothMoveCommand(x, y, segments);
        bool result3 = m_impl->executeCommand(*releaseCmd);

        return result1 && result2 && result3;
    }

    bool Device::mouseDragBezier(MouseButton button, int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto* pressCmd = m_impl->commandCache.getPressCommand(button);
        const auto* releaseCmd = m_impl->commandCache.getReleaseCommand(button);
        if (!pressCmd || !releaseCmd) return false;

        // Execute bezier drag sequence: press -> bezier move -> release
        bool result1 = m_impl->executeCommand(*pressCmd);
        bool result2 = m_impl->executeBezierMoveCommand(x, y, segments, ctrl_x, ctrl_y);
        bool result3 = m_impl->executeCommand(*releaseCmd);

        return result1 && result2 && result3;
    }




    bool Device::mouseWheel(int32_t delta) {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeWheelCommand(delta);
    }

    bool Device::mouseWheel(int32_t delta, uint16_t dt_uframes) {
        if (!m_impl->connected.load()) {
            return false;
        }
        return m_impl->executeWheelCommand(delta, dt_uframes);
    }

    // Keyboard control methods
    bool Device::keyboardDown(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        const std::array<uint8_t, 1> payload{*keyCode};
        return m_impl->executeApiCommand(
            "km.down(" + keyCommand + ")",
            ApiOpcode::KEY_DOWN,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardDown(
        const KeyboardKey& key, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            *keyCode,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            "km.down(" + keyCommand + "," +
                std::to_string(dt_uframes) + ")",
            ApiOpcode::KEY_DOWN,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardUp(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        const std::array<uint8_t, 1> payload{*keyCode};
        return m_impl->executeApiCommand(
            "km.up(" + keyCommand + ")",
            ApiOpcode::KEY_UP,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardUp(
        const KeyboardKey& key, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            *keyCode,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            "km.up(" + keyCommand + "," +
                std::to_string(dt_uframes) + ")",
            ApiOpcode::KEY_UP,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardPress(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        const std::array<uint8_t, 1> payload{*keyCode};
        return m_impl->executeApiCommand(
            "km.press(" + keyCommand + ")",
            ApiOpcode::KEY_PRESS,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardPress(const KeyboardKey& key, uint32_t hold_ms) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        std::vector<uint8_t> payload{*keyCode};
        appendU32(payload, hold_ms);
        return m_impl->executeApiCommand(
            "km.press(" + keyCommand + "," +
                std::to_string(hold_ms) + ")",
            ApiOpcode::KEY_PRESS,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardPress(
        const KeyboardKey& key, uint32_t hold_ms, uint32_t rand_ms)
    {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        std::vector<uint8_t> payload{*keyCode};
        appendU32(payload, hold_ms);
        appendU32(payload, rand_ms);
        return m_impl->executeApiCommand(
            "km.press(" + keyCommand + "," +
                std::to_string(hold_ms) + "," +
                std::to_string(rand_ms) + ")",
            ApiOpcode::KEY_PRESS,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardString(const std::string& text) {
        if (!m_impl->connected.load() || text.size() > 256u ||
            !std::ranges::all_of(text, [](unsigned char byte) {
                return byte < 0x80u;
            }) ||
            (m_impl->apiProtocol == ApiProtocol::MAK_API &&
                text.size() > 248u)) {
            return false;
        }

        return m_impl->executeApiCommand(
            "km.string(\"" + escapeDoubleQuotedCommandString(text) + "\")",
            ApiOpcode::KEY_STRING,
            ApiVerb::EXEC,
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(text.data()),
                text.size()));
    }

    bool Device::keyboardInit() {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeApiCommand(
            "km.init()", ApiOpcode::KEY_INIT, ApiVerb::EXEC);
    }

    bool Device::keyboardInit(uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const std::array<uint8_t, 2> payload{
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            "km.init(" + std::to_string(dt_uframes) + ")",
            ApiOpcode::KEY_INIT,
            ApiVerb::EXEC,
            payload);
    }

    bool Device::keyboardIsDown(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }

        const std::array<uint8_t, 1> payload{*keyCode};
        const auto response = m_impl->executeApiQuery(
            "km.isdown(" + keyCommand + ")",
            ApiOpcode::KEY_IS_DOWN,
            payload);
        try {
            if (!response) {
                return false;
            }
            if (m_impl->apiProtocol == ApiProtocol::MAK_API) {
                return response->size() == 1u &&
                    static_cast<uint8_t>((*response)[0]) != 0u;
            }
            const auto parsed = parseUint8Decimal(*response);
            return parsed.has_value() && parsed.value() != 0u;
        }
        catch (...) {
            return false;
        }
    }

    bool Device::keyboardMask(const KeyboardKey& key, bool enable) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCommand = keyboardKeyCommand(key);
        const auto keyCode = keyboardKeyCode(key);
        if (keyCommand.empty() || !keyCode) {
            return false;
        }
        const std::array<uint8_t, 2> payload{
            *keyCode, static_cast<uint8_t>(enable ? 1u : 0u)};
        return m_impl->executeApiCommand(
            "km.mask(" + keyCommand + "," + (enable ? "1" : "0") + ")",
            ApiOpcode::KEY_MASK,
            ApiVerb::SET,
            payload);
    }

    bool Device::keyboardRemap(
        const KeyboardKey& source, const KeyboardKey& target)
    {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto sourceCommand = keyboardKeyCommand(source);
        const auto targetCommand = keyboardKeyCommand(target);
        const auto sourceCode = keyboardKeyCode(source);
        const auto targetCode = keyboardKeyCode(target);
        if (sourceCommand.empty() || targetCommand.empty() ||
            !sourceCode || !targetCode) {
            return false;
        }
        const std::array<uint8_t, 2> payload{*sourceCode, *targetCode};
        return m_impl->executeApiCommand(
            "km.remap(" + sourceCommand + "," + targetCommand + ")",
            ApiOpcode::KEY_REMAP,
            ApiVerb::SET,
            payload);
    }

    bool Device::keyboardMultiDown(const std::vector<KeyboardKey>& keys) {
        if (!m_impl->connected.load()) return false;
        const auto command = keyboardKeyListCommand("km.multidown", keys);
        std::vector<uint8_t> payload;
        payload.reserve(keys.size());
        for (const auto& key : keys) {
            const auto code = keyboardKeyCode(key);
            if (!code) return false;
            payload.push_back(*code);
        }
        return !command.empty() && m_impl->executeApiCommand(
            command, ApiOpcode::KEY_MULTI_DOWN, ApiVerb::EXEC, payload);
    }

    bool Device::keyboardMultiUp(const std::vector<KeyboardKey>& keys) {
        if (!m_impl->connected.load()) return false;
        const auto command = keyboardKeyListCommand("km.multiup", keys);
        std::vector<uint8_t> payload;
        payload.reserve(keys.size());
        for (const auto& key : keys) {
            const auto code = keyboardKeyCode(key);
            if (!code) return false;
            payload.push_back(*code);
        }
        return !command.empty() && m_impl->executeApiCommand(
            command, ApiOpcode::KEY_MULTI_UP, ApiVerb::EXEC, payload);
    }

    bool Device::keyboardMultiPress(const std::vector<KeyboardKey>& keys) {
        if (!m_impl->connected.load()) return false;
        const auto command = keyboardKeyListCommand("km.multipress", keys);
        std::vector<uint8_t> payload;
        payload.reserve(keys.size());
        for (const auto& key : keys) {
            const auto code = keyboardKeyCode(key);
            if (!code) return false;
            payload.push_back(*code);
        }
        return !command.empty() && m_impl->executeApiCommand(
            command, ApiOpcode::KEY_MULTI_PRESS, ApiVerb::EXEC, payload);
    }

    bool Device::controllerState(const ControllerState& state) {
        if (state.hat > 8u) {
            return false;
        }
        const auto command = controllerCommandBuild("controller", {
            state.buttons, state.hat, state.lt, state.rt, state.x, state.y,
            state.rx, state.ry, state.z, state.rz});
        std::vector<uint8_t> payload;
        payload.reserve(21u);
        appendU32(payload, state.buttons);
        payload.push_back(state.hat);
        appendU16(payload, state.lt);
        appendU16(payload, state.rt);
        appendI16(payload, state.x);
        appendI16(payload, state.y);
        appendI16(payload, state.rx);
        appendI16(payload, state.ry);
        appendI16(payload, state.z);
        appendI16(payload, state.rz);
        return !command.empty() && m_impl->executeApiCommand(
            command, ApiOpcode::CONTROLLER_STATE, ApiVerb::SET, payload);
    }

    bool Device::controllerState(
        const ControllerState& state, uint16_t dt_uframes) {
        if (state.hat > 8u) {
            return false;
        }
        const auto command = controllerCommandBuild("controller", {
            state.buttons, state.hat, state.lt, state.rt, state.x, state.y,
            state.rx, state.ry, state.z, state.rz}, dt_uframes);
        std::vector<uint8_t> payload;
        payload.reserve(23u);
        appendU32(payload, state.buttons);
        payload.push_back(state.hat);
        appendU16(payload, state.lt);
        appendU16(payload, state.rt);
        appendI16(payload, state.x);
        appendI16(payload, state.y);
        appendI16(payload, state.rx);
        appendI16(payload, state.ry);
        appendI16(payload, state.z);
        appendI16(payload, state.rz);
        appendU16(payload, dt_uframes);
        return !command.empty() && m_impl->executeApiCommand(
            command, ApiOpcode::CONTROLLER_STATE, ApiVerb::SET, payload);
    }

#define MAKXD_CONTROLLER_VALUE_METHODS( \
    method, command_name, opcode_name, type, append_value) \
    bool Device::method(type value) { \
        const auto command = controllerCommandBuild(command_name, {value}); \
        std::vector<uint8_t> payload; \
        append_value(payload, value); \
        return !command.empty() && m_impl->executeApiCommand( \
            command, ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    } \
    bool Device::method(type value, uint16_t dt_uframes) { \
        const auto command = controllerCommandBuild( \
            command_name, {value}, dt_uframes); \
        std::vector<uint8_t> payload; \
        append_value(payload, value); \
        appendU16(payload, dt_uframes); \
        return !command.empty() && m_impl->executeApiCommand( \
            command, ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    }

    MAKXD_CONTROLLER_VALUE_METHODS(
        controllerLeftTrigger, "controller_lt", CONTROLLER_LT,
        uint16_t, appendU16)
    MAKXD_CONTROLLER_VALUE_METHODS(
        controllerRightTrigger, "controller_rt", CONTROLLER_RT,
        uint16_t, appendU16)
#undef MAKXD_CONTROLLER_VALUE_METHODS

    std::optional<bool> Device::controllerButton(ControllerButton button) {
        const auto value = static_cast<uint8_t>(button);
        if (value < 1u || value > 32u) return std::nullopt;
        const auto response = m_impl->executeApiQuery(
            "km.controller_button" + std::to_string(value) + "()",
            static_cast<ApiOpcode>(0x5Fu + value));
        if (!response) return std::nullopt;
        if (m_impl->apiProtocol == ApiProtocol::MAK_API) {
            return response->size() == 1u &&
                static_cast<uint8_t>((*response)[0]) == 1u;
        }
        return *response == "1";
    }

    bool Device::controllerButton(
        ControllerButton button, bool pressed) {
        const auto value = static_cast<uint8_t>(button);
        if (value < 1u || value > 32u) return false;
        const auto command =
            "km.controller_button" + std::to_string(value) +
            "(" + (pressed ? "1)" : "0)");
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(pressed ? 1u : 0u)};
        return m_impl->executeApiCommand(
            command, static_cast<ApiOpcode>(0x5Fu + value),
            ApiVerb::SET, payload);
    }

    bool Device::controllerButton(
        ControllerButton button, bool pressed, uint16_t dt_uframes) {
        const auto value = static_cast<uint8_t>(button);
        if (value < 1u || value > 32u) return false;
        const auto command =
            "km.controller_button" + std::to_string(value) +
            "(" + (pressed ? "1," : "0,") +
            std::to_string(dt_uframes) + ")";
        const std::array<uint8_t, 3> payload{
            static_cast<uint8_t>(pressed ? 1u : 0u),
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            command, static_cast<ApiOpcode>(0x5Fu + value),
            ApiVerb::SET, payload);
    }

#define MAKXD_CONTROLLER_HAT_METHODS(method, command_name, opcode_name) \
    std::optional<bool> Device::method() { \
        const auto response = m_impl->executeApiQuery( \
            "km." command_name "()", ApiOpcode::opcode_name); \
        if (!response) return std::nullopt; \
        if (m_impl->apiProtocol == ApiProtocol::MAK_API) { \
            return response->size() == 1u && \
                static_cast<uint8_t>((*response)[0]) == 1u; \
        } \
        return *response == "1"; \
    } \
    bool Device::method(bool pressed) { \
        const std::array<uint8_t, 1> payload{ \
            static_cast<uint8_t>(pressed ? 1u : 0u)}; \
        return m_impl->executeApiCommand( \
            "km." command_name "(" + std::string(pressed ? "1)" : "0)"), \
            ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    } \
    bool Device::method(bool pressed, uint16_t dt_uframes) { \
        const std::array<uint8_t, 3> payload{ \
            static_cast<uint8_t>(pressed ? 1u : 0u), \
            static_cast<uint8_t>(dt_uframes), \
            static_cast<uint8_t>(dt_uframes >> 8u)}; \
        return m_impl->executeApiCommand( \
            "km." command_name "(" + std::string(pressed ? "1," : "0,") + \
                std::to_string(dt_uframes) + ")", \
            ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    }

    MAKXD_CONTROLLER_HAT_METHODS(
        controllerHatLeft, "controller_hat_left", CONTROLLER_HAT_LEFT)
    MAKXD_CONTROLLER_HAT_METHODS(
        controllerHatRight, "controller_hat_right", CONTROLLER_HAT_RIGHT)
    MAKXD_CONTROLLER_HAT_METHODS(
        controllerHatDown, "controller_hat_down", CONTROLLER_HAT_DOWN)
    MAKXD_CONTROLLER_HAT_METHODS(
        controllerHatUp, "controller_hat_up", CONTROLLER_HAT_UP)

#undef MAKXD_CONTROLLER_HAT_METHODS

#define MAKXD_CONTROLLER_PAIR_METHODS( \
    method, command_name, opcode_name, type, append_pair) \
    bool Device::method(type first, type second) { \
        const auto command = controllerCommandBuild( \
            command_name, {first, second}); \
        std::vector<uint8_t> payload; \
        append_pair; \
        return !command.empty() && m_impl->executeApiCommand( \
            command, ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    } \
    bool Device::method(type first, type second, uint16_t dt_uframes) { \
        const auto command = controllerCommandBuild( \
            command_name, {first, second}, dt_uframes); \
        std::vector<uint8_t> payload; \
        append_pair; \
        appendU16(payload, dt_uframes); \
        return !command.empty() && m_impl->executeApiCommand( \
            command, ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    }

    MAKXD_CONTROLLER_PAIR_METHODS(
        controllerLeftStick, "controller_left_stick", CONTROLLER_LEFT_STICK,
        int16_t, appendI16(payload, first); appendI16(payload, second))
    MAKXD_CONTROLLER_PAIR_METHODS(
        controllerRightStick, "controller_right_stick",
        CONTROLLER_RIGHT_STICK, int16_t,
        appendI16(payload, first); appendI16(payload, second))
    MAKXD_CONTROLLER_PAIR_METHODS(
        controllerAux, "controller_aux", CONTROLLER_AUX, int16_t,
        appendI16(payload, first); appendI16(payload, second))
#undef MAKXD_CONTROLLER_PAIR_METHODS

#define MAKXD_CONTROLLER_MASK_METHODS(method, command_name, opcode_name) \
    bool Device::method(bool enabled) { \
        const auto command = controllerCommandBuild( \
            command_name, {enabled ? 1 : 0}); \
        const std::array<uint8_t, 1> payload{ \
            static_cast<uint8_t>(enabled ? 1u : 0u)}; \
        return !command.empty() && m_impl->executeApiCommand( \
            command, ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    }

    MAKXD_CONTROLLER_MASK_METHODS(
        controllerLeftTriggerMask, "controller_lt_mask", CONTROLLER_LT_MASK)
    MAKXD_CONTROLLER_MASK_METHODS(
        controllerRightTriggerMask, "controller_rt_mask", CONTROLLER_RT_MASK)

    bool Device::controllerButtonMask(
        ControllerButton button, bool enabled) {
        const auto value = static_cast<uint8_t>(button);
        if (value < 1u || value > 32u) return false;
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enabled ? 1u : 0u)};
        return m_impl->executeApiCommand(
            "km.controller_button" + std::to_string(value) + "_mask(" +
                (enabled ? "1)" : "0)"),
            static_cast<ApiOpcode>(0x7Fu + value), ApiVerb::SET, payload);
    }

    MAKXD_CONTROLLER_MASK_METHODS(
        controllerHatLeftMask, "controller_hat_left_mask",
        CONTROLLER_HAT_LEFT_MASK)
    MAKXD_CONTROLLER_MASK_METHODS(
        controllerHatRightMask, "controller_hat_right_mask",
        CONTROLLER_HAT_RIGHT_MASK)
    MAKXD_CONTROLLER_MASK_METHODS(
        controllerHatDownMask, "controller_hat_down_mask",
        CONTROLLER_HAT_DOWN_MASK)
    MAKXD_CONTROLLER_MASK_METHODS(
        controllerHatUpMask, "controller_hat_up_mask",
        CONTROLLER_HAT_UP_MASK)

#undef MAKXD_CONTROLLER_MASK_METHODS

#define MAKXD_CONTROLLER_DIRECTION_MASK_METHODS( \
    method, command_name, opcode_name) \
    bool Device::method( \
        bool first_negative, bool first_positive, \
        bool second_negative, bool second_positive) { \
        const auto command = controllerCommandBuild(command_name, { \
            first_negative ? 1 : 0, first_positive ? 1 : 0, \
            second_negative ? 1 : 0, second_positive ? 1 : 0}); \
        const std::array<uint8_t, 4> payload{ \
            static_cast<uint8_t>(first_negative ? 1u : 0u), \
            static_cast<uint8_t>(first_positive ? 1u : 0u), \
            static_cast<uint8_t>(second_negative ? 1u : 0u), \
            static_cast<uint8_t>(second_positive ? 1u : 0u)}; \
        return !command.empty() && m_impl->executeApiCommand( \
            command, ApiOpcode::opcode_name, ApiVerb::SET, payload); \
    }

    MAKXD_CONTROLLER_DIRECTION_MASK_METHODS(
        controllerLeftStickMask, "controller_left_stick_mask",
        CONTROLLER_LEFT_STICK_MASK)
    MAKXD_CONTROLLER_DIRECTION_MASK_METHODS(
        controllerRightStickMask, "controller_right_stick_mask",
        CONTROLLER_RIGHT_STICK_MASK)
    MAKXD_CONTROLLER_DIRECTION_MASK_METHODS(
        controllerAuxMask, "controller_aux_mask", CONTROLLER_AUX_MASK)

#undef MAKXD_CONTROLLER_DIRECTION_MASK_METHODS

    std::string Device::getKeyboardKeys() {
        if (!m_impl->connected.load()) return {};
        const auto response = m_impl->executeApiQuery(
            "km.keys()", ApiOpcode::KEY_KEYS);
        if (!response) return {};
        if (m_impl->apiProtocol == ApiProtocol::MAK_API) {
            return response->size() == 1u
                ? std::to_string(static_cast<uint8_t>((*response)[0]))
                : std::string{};
        }
        return *response;
    }

    bool Device::setKeyboardKeys(bool enabled) {
        if (!m_impl->connected.load()) return false;
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enabled ? 1u : 0u)};
        return m_impl->executeApiCommand(
            "km.keys(" + std::to_string(enabled ? 1 : 0) + ")",
            ApiOpcode::KEY_KEYS,
            ApiVerb::SET,
            payload);
    }


    // Mouse locking methods with caching
    bool Device::lockMouseX(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::X);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::X, lock);
        }
        return result;
    }

    bool Device::lockMouseY(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::Y);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::Y, lock);
        }
        return result;
    }

    bool Device::lockMouseLeft(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::LEFT);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::LEFT, lock);
        }
        return result;
    }

    bool Device::lockMouseMiddle(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::MIDDLE);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::MIDDLE, lock);
        }
        return result;
    }

    bool Device::lockMouseRight(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::RIGHT);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::RIGHT, lock);
        }
        return result;
    }

    bool Device::lockMouseSide1(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::SIDE1);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::SIDE1, lock);
        }
        return result;
    }

    bool Device::lockMouseSide2(bool lock) {
        if (!m_impl->connected.load()) return false;

        auto idx = std::to_underlying(Impl::LockTarget::SIDE2);
        const std::string& command = lock ?
            m_impl->commandCache.lock_commands[idx] :
            m_impl->commandCache.unlock_commands[idx];

        bool result = m_impl->executeCommand(command);
        if (result) {
            m_impl->updateLockStateCache(Impl::LockTarget::SIDE2, lock);
        }
        return result;
    }

    // Fast cached lock state queries
    bool Device::isMouseXLocked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::X);
    }

    bool Device::isMouseYLocked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::Y);
    }

    bool Device::isMouseLeftLocked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::LEFT);
    }

    bool Device::isMouseMiddleLocked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::MIDDLE);
    }

    bool Device::isMouseRightLocked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::RIGHT);
    }

    bool Device::isMouseSide1Locked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::SIDE1);
    }

    bool Device::isMouseSide2Locked() const {
        return m_impl->getLockStateFromCache(Impl::LockTarget::SIDE2);
    }

    std::unordered_map<std::string, bool> Device::getAllLockStates() const {
        return {
            {"X", isMouseXLocked()},
            {"Y", isMouseYLocked()},
            {"LEFT", isMouseLeftLocked()},
            {"RIGHT", isMouseRightLocked()},
            {"MIDDLE", isMouseMiddleLocked()},
            {"SIDE1", isMouseSide1Locked()},
            {"SIDE2", isMouseSide2Locked()}
        };
    }

    // Mouse input catching methods
    uint8_t Device::catchMouseLeft() {
        if (!m_impl->connected.load()) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_ml()", true,
            std::chrono::milliseconds(50));
        try {
            const std::string response = future.get();
            const auto parsed = parseUint8Decimal(response);
            return parsed.value_or(0);
        }
        catch (...) {
            return 0;
        }
    }

    uint8_t Device::catchMouseMiddle() {
        if (!m_impl->connected.load()) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_mm()", true,
            std::chrono::milliseconds(50));
        try {
            const std::string response = future.get();
            const auto parsed = parseUint8Decimal(response);
            return parsed.value_or(0);
        }
        catch (...) {
            return 0;
        }
    }

    uint8_t Device::catchMouseRight() {
        if (!m_impl->connected.load()) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_mr()", true,
            std::chrono::milliseconds(50));
        try {
            const std::string response = future.get();
            const auto parsed = parseUint8Decimal(response);
            return parsed.value_or(0);
        }
        catch (...) {
            return 0;
        }
    }

    uint8_t Device::catchMouseSide1() {
        if (!m_impl->connected.load()) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_ms1()", true,
            std::chrono::milliseconds(50));
        try {
            const std::string response = future.get();
            const auto parsed = parseUint8Decimal(response);
            return parsed.value_or(0);
        }
        catch (...) {
            return 0;
        }
    }

    uint8_t Device::catchMouseSide2() {
        if (!m_impl->connected.load()) return 0;

        auto future = m_impl->serialPort->sendTrackedCommand("km.catch_ms2()", true,
            std::chrono::milliseconds(50));
        try {
            const std::string response = future.get();
            const auto parsed = parseUint8Decimal(response);
            return parsed.value_or(0);
        }
        catch (...) {
            return 0;
        }
    }

    bool Device::setCatchMouseLeft(uint8_t value) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.catch_ml(" + std::to_string(value) + ")");
    }

    bool Device::setCatchMouseMiddle(uint8_t value) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.catch_mm(" + std::to_string(value) + ")");
    }

    bool Device::setCatchMouseRight(uint8_t value) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.catch_mr(" + std::to_string(value) + ")");
    }

    bool Device::setCatchMouseSide1(uint8_t value) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.catch_ms1(" + std::to_string(value) + ")");
    }

    bool Device::setCatchMouseSide2(uint8_t value) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.catch_ms2(" + std::to_string(value) + ")");
    }

    bool Device::lockMouseXPositive(bool lock) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.lock_mx+(" + std::to_string(lock ? 1 : 0) + ")");
    }

    bool Device::lockMouseXNegative(bool lock) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.lock_mx-(" + std::to_string(lock ? 1 : 0) + ")");
    }

    bool Device::lockMouseYPositive(bool lock) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.lock_my+(" + std::to_string(lock ? 1 : 0) + ")");
    }

    bool Device::lockMouseYNegative(bool lock) {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.lock_my-(" + std::to_string(lock ? 1 : 0) + ")");
    }

    // Button monitoring methods
    bool Device::enableButtonMonitoring(bool enable) {
        if (!m_impl->connected.load(std::memory_order_acquire)) {
            return false;
        }

        std::string command = enable ? "km.buttons(1)" : "km.buttons(0)";
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enable ? 1u : 0u)};
        bool result = m_impl->executeApiCommand(
            command, ApiOpcode::BUTTONS, ApiVerb::SET, payload);
        if (result) {
            m_impl->buttonMonitoringEnabled.store(enable, std::memory_order_release);
        }
        return result;
    }

    bool Device::isButtonMonitoringEnabled() const noexcept {
        return m_impl->buttonMonitoringEnabled.load(std::memory_order_acquire);
    }

    uint8_t Device::getButtonMask() const noexcept {
        return m_impl->currentButtonMask.load();
    }

    // Serial spoofing methods
    std::string Device::getMouseSerial() {
        if (!m_impl->connected.load()) return "";

        // Small delay to ensure any pending responses are cleared
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto future = m_impl->serialPort->sendTrackedCommand("km.serial()", true,
            std::chrono::milliseconds(50));
        try {
            return future.get();
        }
        catch (...) {
            return "";
        }
    }

    bool Device::setMouseSerial(const std::string& serial) {
        if (!m_impl->connected.load()) return false;

        std::string command = "km.serial('";
        command += escapeSingleQuotedCommandString(serial);
        command += "')";
        return m_impl->executeCommand(command);
    }

    bool Device::resetMouseSerial() {
        if (!m_impl->connected.load()) return false;
        return m_impl->executeCommand("km.serial(0)");
    }

    void Device::setMouseButtonCallback(MouseButtonCallback callback) {
        std::lock_guard<std::mutex> lock(m_impl->callbackMutex);
        m_impl->mouseButtonCallback = std::move(callback);
    }

    void Device::setConnectionCallback(ConnectionCallback callback) {
        std::lock_guard<std::mutex> lock(m_impl->callbackMutex);
        m_impl->connectionCallback = std::move(callback);
    }

    // High-level automation methods
    bool Device::clickSequence(const std::vector<MouseButton>& buttons,
        std::chrono::milliseconds delay) {
        if (!m_impl->connected.load()) {
            return false;
        }

        for (const auto& button : buttons) {
            if (!click(button)) {
                return false;
            }
            if (delay.count() > 0) {
                std::this_thread::sleep_for(delay);
            }
        }
        return true;
    }


    bool Device::movePattern(const std::vector<std::pair<int32_t, int32_t>>& points,
        bool smooth, uint32_t segments) {
        if (!m_impl->connected.load()) {
            return false;
        }

        for (const auto& [x, y] : points) {
            if (smooth) {
                if (!mouseMoveSmooth(x, y, segments)) {
                    return false;
                }
            }
            else {
                if (!mouseMove(x, y)) {
                    return false;
                }
            }
        }
        return true;
    }

    void Device::enableHighPerformanceMode(bool enable) {
        m_impl->highPerformanceMode.store(enable);
    }

    bool Device::isHighPerformanceModeEnabled() const noexcept {
        return m_impl->highPerformanceMode.load();
    }

    // Batch command builder implementation
    Device::BatchCommandBuilder Device::createBatch() {
        return BatchCommandBuilder(this, m_lifetimeToken);
    }

    bool Device::BatchCommandBuilder::isDeviceAlive() const {
        return m_device != nullptr &&
               m_deviceLifetime &&
               m_deviceLifetime->load(std::memory_order_acquire);
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::move(int32_t x, int32_t y) {
        if (!isDeviceAlive()) {
            return *this;
        }
        m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::move(
        int32_t x, int32_t y, uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        if (!dtUframesValid(dt_uframes)) {
            m_valid = false;
            return *this;
        }
        m_commands.push_back(
            "km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
            std::to_string(dt_uframes) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::moveSmooth(int32_t x, int32_t y, uint32_t segments) {
        if (!isDeviceAlive()) {
            return *this;
        }
        m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(segments) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::moveBezier(int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        if (!isDeviceAlive()) {
            return *this;
        }
        m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
            std::to_string(segments) + "," + std::to_string(ctrl_x) + "," + std::to_string(ctrl_y) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::click(MouseButton button) {
        if (!isDeviceAlive()) {
            return *this;
        }
        auto& cache = m_device->m_impl->commandCache;
        const auto* pressCmd = cache.getPressCommand(button);
        const auto* releaseCmd = cache.getReleaseCommand(button);

        if (pressCmd && releaseCmd) {
            m_commands.push_back(*pressCmd);
            m_commands.push_back(*releaseCmd);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::press(MouseButton button) {
        if (!isDeviceAlive()) {
            return *this;
        }
        auto& cache = m_device->m_impl->commandCache;
        const auto* cmd = cache.getPressCommand(button);
        if (cmd) {
            m_commands.push_back(*cmd);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::press(
        MouseButton button, uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto* command =
            m_device->m_impl->commandCache.getPressCommand(button);
        const auto timedCommand =
            command ? commandWithDt(*command, dt_uframes) : std::string{};
        if (timedCommand.empty()) {
            m_valid = false;
        } else {
            m_commands.push_back(timedCommand);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::release(MouseButton button) {
        if (!isDeviceAlive()) {
            return *this;
        }
        auto& cache = m_device->m_impl->commandCache;
        const auto* cmd = cache.getReleaseCommand(button);
        if (cmd) {
            m_commands.push_back(*cmd);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::release(
        MouseButton button, uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto* command =
            m_device->m_impl->commandCache.getReleaseCommand(button);
        const auto timedCommand =
            command ? commandWithDt(*command, dt_uframes) : std::string{};
        if (timedCommand.empty()) {
            m_valid = false;
        } else {
            m_commands.push_back(timedCommand);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::scroll(int32_t delta) {
        if (!isDeviceAlive()) {
            return *this;
        }
        m_commands.push_back("km.wheel(" + std::to_string(delta) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::scroll(
        int32_t delta, uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        if (!dtUframesValid(dt_uframes)) {
            m_valid = false;
            return *this;
        }
        m_commands.push_back(
            "km.wheel(" + std::to_string(delta) + "," +
            std::to_string(dt_uframes) + ")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::drag(MouseButton button, int32_t x, int32_t y) {
        if (!isDeviceAlive()) {
            return *this;
        }
        auto& cache = m_device->m_impl->commandCache;
        const auto* pressCmd = cache.getPressCommand(button);
        const auto* releaseCmd = cache.getReleaseCommand(button);

        if (pressCmd && releaseCmd) {
            // Add press, move, release commands to batch (consistent with normal mouseDrag format)
            m_commands.push_back(*pressCmd);
            m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + ")");
            m_commands.push_back(*releaseCmd);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::dragSmooth(MouseButton button, int32_t x, int32_t y, uint32_t segments) {
        if (!isDeviceAlive()) {
            return *this;
        }
        auto& cache = m_device->m_impl->commandCache;
        const auto* pressCmd = cache.getPressCommand(button);
        const auto* releaseCmd = cache.getReleaseCommand(button);

        if (pressCmd && releaseCmd) {
            // Add press, smooth move, release commands to batch
            m_commands.push_back(*pressCmd);
            m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(segments) + ")");
            m_commands.push_back(*releaseCmd);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::dragBezier(MouseButton button, int32_t x, int32_t y, uint32_t segments,
        int32_t ctrl_x, int32_t ctrl_y) {
        if (!isDeviceAlive()) {
            return *this;
        }
        auto& cache = m_device->m_impl->commandCache;
        const auto* pressCmd = cache.getPressCommand(button);
        const auto* releaseCmd = cache.getReleaseCommand(button);

        if (pressCmd && releaseCmd) {
            // Add press, bezier move, release commands to batch
            m_commands.push_back(*pressCmd);
            m_commands.push_back("km.move(" + std::to_string(x) + "," + std::to_string(y) + "," +
                std::to_string(segments) + "," + std::to_string(ctrl_x) + "," + std::to_string(ctrl_y) + ")");
            m_commands.push_back(*releaseCmd);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardDown(
        const KeyboardKey& key)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (!keyCommand.empty()) {
            m_commands.push_back("km.down(" + keyCommand + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardDown(
        const KeyboardKey& key, uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (keyCommand.empty() || !dtUframesValid(dt_uframes)) {
            m_valid = false;
        } else {
            m_commands.push_back(
                "km.down(" + keyCommand + "," +
                std::to_string(dt_uframes) + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardUp(
        const KeyboardKey& key)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (!keyCommand.empty()) {
            m_commands.push_back("km.up(" + keyCommand + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardUp(
        const KeyboardKey& key, uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (keyCommand.empty() || !dtUframesValid(dt_uframes)) {
            m_valid = false;
        } else {
            m_commands.push_back(
                "km.up(" + keyCommand + "," +
                std::to_string(dt_uframes) + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardPress(
        const KeyboardKey& key)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (!keyCommand.empty()) {
            m_commands.push_back("km.press(" + keyCommand + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardPress(
        const KeyboardKey& key, uint32_t hold_ms)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (!keyCommand.empty()) {
            m_commands.push_back(
                "km.press(" + keyCommand + "," +
                std::to_string(hold_ms) + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardPress(
        const KeyboardKey& key, uint32_t hold_ms, uint32_t rand_ms)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (!keyCommand.empty()) {
            m_commands.push_back(
                "km.press(" + keyCommand + "," +
                std::to_string(hold_ms) + "," +
                std::to_string(rand_ms) + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardString(
        const std::string& text)
    {
        if (!isDeviceAlive() || text.size() > 256u) {
            return *this;
        }
        m_commands.push_back(
            "km.string(\"" + escapeDoubleQuotedCommandString(text) + "\")");
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardInit()
    {
        if (isDeviceAlive()) {
            m_commands.push_back("km.init()");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardInit(
        uint16_t dt_uframes) {
        if (!isDeviceAlive()) {
            return *this;
        }
        if (!dtUframesValid(dt_uframes)) {
            m_valid = false;
        } else {
            m_commands.push_back(
                "km.init(" + std::to_string(dt_uframes) + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardMultiDown(
        const std::vector<KeyboardKey>& keys)
    {
        if (isDeviceAlive()) {
            const auto command = keyboardKeyListCommand("km.multidown", keys);
            if (!command.empty()) m_commands.push_back(command);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardMultiUp(
        const std::vector<KeyboardKey>& keys)
    {
        if (isDeviceAlive()) {
            const auto command = keyboardKeyListCommand("km.multiup", keys);
            if (!command.empty()) m_commands.push_back(command);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardMultiPress(
        const std::vector<KeyboardKey>& keys)
    {
        if (isDeviceAlive()) {
            const auto command = keyboardKeyListCommand("km.multipress", keys);
            if (!command.empty()) m_commands.push_back(command);
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardKeys(bool enabled)
    {
        if (isDeviceAlive()) {
            m_commands.push_back("km.keys(" + std::to_string(enabled ? 1 : 0) + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardMask(
        const KeyboardKey& key, bool enable)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto keyCommand = keyboardKeyCommand(key);
        if (!keyCommand.empty()) {
            m_commands.push_back(
                "km.mask(" + keyCommand + "," +
                (enable ? "1" : "0") + ")");
        }
        return *this;
    }

    Device::BatchCommandBuilder& Device::BatchCommandBuilder::keyboardRemap(
        const KeyboardKey& source, const KeyboardKey& target)
    {
        if (!isDeviceAlive()) {
            return *this;
        }
        const auto sourceCommand = keyboardKeyCommand(source);
        const auto targetCommand = keyboardKeyCommand(target);
        if (!sourceCommand.empty() && !targetCommand.empty()) {
            m_commands.push_back(
                "km.remap(" + sourceCommand + "," + targetCommand + ")");
        }
        return *this;
    }

    bool Device::BatchCommandBuilder::execute() {
        if (!isDeviceAlive() || !m_valid) {
            return false;
        }

        if (!m_device->m_impl->connected.load()) {
            return false;
        }

        for (const auto& command : m_commands) {
            if (!m_device->m_impl->executeCommand(command)) {
                return false;
            }
        }
        return true;
    }

    // Legacy raw command interface (not recommended)
    bool Device::sendRawCommand(const std::string& command) const {
        if (!m_impl->connected.load()) {
            return false;
        }

        try {
            m_impl->serialPort->sendTrackedCommand(
                command, true, std::chrono::milliseconds(100)).get();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    std::string Device::receiveRawResponse() const {
        if (!m_impl->connected.load(std::memory_order_acquire)) {
            return "";
        }

        return m_impl->serialPort->readString();
    }


    // Utility functions
    std::string mouseButtonToString(MouseButton button) {
        switch (button) {
        case MouseButton::LEFT: return "LEFT";
        case MouseButton::RIGHT: return "RIGHT";
        case MouseButton::MIDDLE: return "MIDDLE";
        case MouseButton::SIDE1: return "SIDE1";
        case MouseButton::SIDE2: return "SIDE2";
        case MouseButton::UNKNOWN: return "UNKNOWN";
        }
        return "UNKNOWN";
    }

    MouseButton stringToMouseButton(const std::string& buttonName) {
        const std::string_view name{buttonName};

        if (equalsIgnoreAsciiCase(name, "LEFT")) return MouseButton::LEFT;
        if (equalsIgnoreAsciiCase(name, "RIGHT")) return MouseButton::RIGHT;
        if (equalsIgnoreAsciiCase(name, "MIDDLE")) return MouseButton::MIDDLE;
        if (equalsIgnoreAsciiCase(name, "SIDE1")) return MouseButton::SIDE1;
        if (equalsIgnoreAsciiCase(name, "SIDE2")) return MouseButton::SIDE2;

        return MouseButton::UNKNOWN;
    }

} // namespace makxd
