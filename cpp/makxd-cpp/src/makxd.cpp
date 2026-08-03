#include "../include/makxd.h"
#include "../include/serialport.h"
#include <iostream>
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
#include <utility>

namespace makxd {

    ConnectionConfig ConnectionConfig::com(
        std::string port,
        std::string aes128Key) {
        ConnectionConfig config;
        config.method = ConnectionMethod::COM;
        config.aes128Key = std::move(aes128Key);
        config.comPort = std::move(port);
        return config;
    }

    ConnectionConfig ConnectionConfig::udp(
        std::string host,
        uint16_t port,
        UdpWireMode mode,
        std::string aes128Key,
        std::string bindAddress,
        std::string interfaceName,
        uint16_t vlanId) {
        ConnectionConfig config;
        config.method = ConnectionMethod::UDP;
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
        std::function<void()> close) {
        ConnectionConfig config;
        config.method = ConnectionMethod::BLE;
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

        constexpr uint16_t DT_UFRAMES_MAX = 0x3FFFu;

        bool dtUframesValid(uint16_t dtUframes) {
            return dtUframes <= DT_UFRAMES_MAX;
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

    // High-performance PIMPL implementation
    class Device::Impl {
    public:
        std::unique_ptr<SerialPort> serialPort;
        ConnectionConfig connection;
        DeviceInfo deviceInfo;
        std::optional<DeviceKinds> deviceKinds;
        std::optional<uint8_t> probedKinds;
        std::atomic<ConnectionStatus> atomicStatus{ConnectionStatus::DISCONNECTED};
        std::atomic<bool> connected;
        std::atomic<bool> highPerformanceMode;
        mutable std::mutex mutex;
        static std::string lastError;

        // Button state tracking
        std::atomic<uint8_t> currentButtonMask{ 0 };
        std::atomic<bool> buttonMonitoringEnabled{ false };

        // Callbacks
        Device::MouseButtonCallback mouseButtonCallback;
        Device::ConnectionCallback connectionCallback;
        mutable std::mutex callbackMutex;

        // Connection monitoring
        std::jthread monitoringThread;
        std::condition_variable monitoringCondition;
        std::mutex monitoringMutex;

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
            std::string_view encryptionKey)
            : serialPort(std::make_unique<SerialPort>(
                  encryptionEnabled, encryptionKey))
            , connection(ConnectionConfig::com(
                {},
                encryptionEnabled ? std::string(encryptionKey) : std::string{}))
            , connected(false)
            , highPerformanceMode(false) {
          
            deviceInfo.isConnected = false;

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

            try {
                std::string response;
                if (probedKinds) {
                    response.push_back(static_cast<char>(*probedKinds));
                    probedKinds.reset();
                } else {
                    response = serialPort->sendTrackedMakApi(
                        ApiOpcode::DEVICE,
                        {},
                        std::chrono::milliseconds(100)).get();
                    if (response.size() != 1u) {
                        return false;
                    }
                }
                DeviceKinds learned{};
                learned.kinds = static_cast<uint8_t>(response[0]);
                deviceKinds = learned;
                if (connection.method == ConnectionMethod::COM) {
                    constexpr std::array<uint8_t, 1> enabled{1u};
                    if (!serialPort->sendMakApi(
                            ApiOpcode::BUTTONS,
                            enabled)) {
                        return false;
                    }
                }
                return true;
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

        bool executeApiCommand(
            ApiOpcode opcode,
            std::span<const uint8_t> payload = {}) {
            if (!connected.load(std::memory_order_acquire)) {
                return false;
            }
            try {
                return serialPort->sendMakApi(opcode, payload);
            } catch (const std::exception& error) {
                setLastError(error.what());
                return false;
            }
        }

        std::optional<std::string> executeApiQuery(
            ApiOpcode opcode,
            std::span<const uint8_t> payload = {}) {
            if (!connected.load(std::memory_order_acquire)) {
                return std::nullopt;
            }
            try {
                return serialPort->sendTrackedMakApi(
                    opcode, payload,
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
                ApiOpcode::MOVE,
                std::span<const uint8_t>(payload.data(), payloadBytes));
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
                ApiOpcode::WHEEL,
                std::span<const uint8_t>(payload.data(), payloadBytes));
        }

    };

    // Device implementation
    Device::Device(
        bool encryptionEnabled,
        std::string_view encryptionKey)
        : m_impl(std::make_unique<Impl>(
            encryptionEnabled, encryptionKey))
        , m_lifetimeToken(std::make_shared<std::atomic<bool>>(true)) {}

    Device::Device(ConnectionConfig connection)
        : m_impl(std::make_unique<Impl>(
            !connection.aes128Key.empty(),
            connection.aes128Key))
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
                    auto future = m_impl->serialPort->sendTrackedMakApi(
                        ApiOpcode::DEVICE, {}, BAUD_PROBE_TIMEOUT);
                    if (future.wait_for(BAUD_PROBE_TIMEOUT) == std::future_status::ready) {
                        const auto response = future.get();
                        if (response.size() != 1u) {
                            throw std::runtime_error("Invalid DEVICE response");
                        }
                        m_impl->probedKinds = static_cast<uint8_t>(response[0]);
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
                "Device identity probe failed at every supported baud");
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            m_impl->deviceInfo.isConnected = false;
            m_impl->deviceKinds.reset();
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
        m_impl->connection = connection;
        if (connection.method == ConnectionMethod::COM) {
            return connect(connection.comPort);
        }

        std::unique_lock<std::mutex> lock(m_impl->mutex);
        m_impl->atomicStatus.store(
            ConnectionStatus::CONNECTING, std::memory_order_release);
        if (!m_impl->serialPort->open(connection)) {
            m_impl->setLastError("Connection transport open failed");
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            return false;
        }
        try {
            auto identity = m_impl->serialPort->sendTrackedMakApi(
                ApiOpcode::DEVICE, {}, BAUD_PROBE_TIMEOUT);
            if (identity.wait_for(BAUD_PROBE_TIMEOUT) !=
                    std::future_status::ready) {
                throw std::runtime_error(
                    "Device identity probe failed");
            }
            const auto response = identity.get();
            if (response.size() != 1u) {
                throw std::runtime_error("Device identity probe failed");
            }
            m_impl->probedKinds = static_cast<uint8_t>(response[0]);
        } catch (const std::exception& error) {
            m_impl->setLastError(error.what());
            m_impl->serialPort->close();
            m_impl->atomicStatus.store(
                ConnectionStatus::CONNECTION_ERROR, std::memory_order_release);
            return false;
        }
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
            m_impl->deviceKinds.reset();
            m_impl->currentButtonMask.store(0, std::memory_order_release);
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

    std::expected<DeviceKinds, ConnectionStatus> Device::device() const {
        if (!m_impl->connected.load(std::memory_order_acquire)) {
            return std::unexpected(ConnectionStatus::DISCONNECTED);
        }
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->deviceKinds) {
            return std::unexpected(getStatus());
        }
        return *m_impl->deviceKinds;
    }


    // High-performance mouse control methods
    bool Device::mouseDown(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        if (std::to_underlying(button) >= 5u) {
            return false;
        }
        constexpr std::array<uint8_t, 1> payload{1u};
        return m_impl->executeApiCommand(
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            payload);
    }

    bool Device::mouseDown(MouseButton button, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        if (std::to_underlying(button) >= 5u) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            1u,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            payload);
    }

    bool Device::mouseUp(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        if (std::to_underlying(button) >= 5u) {
            return false;
        }
        constexpr std::array<uint8_t, 1> payload{0u};
        return m_impl->executeApiCommand(
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            payload);
    }

    bool Device::mouseUp(MouseButton button, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        if (std::to_underlying(button) >= 5u) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            0u,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT) +
                std::to_underlying(button)),
            payload);
    }

    bool Device::mouseButtonMask(MouseButton button, bool enabled) {
        if (!m_impl->connected.load() || std::to_underlying(button) >= 5u) {
            return false;
        }
        const auto index = std::to_underlying(button);
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enabled ? 1u : 0u)};
        return m_impl->executeApiCommand(
            static_cast<ApiOpcode>(
                static_cast<uint8_t>(ApiOpcode::LEFT_MASK) + index),
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
            ApiOpcode::MOVE_MASK,
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
            ApiOpcode::WHEEL_MASK,
            payload);
    }

    bool Device::click(MouseButton button) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const bool pressed = mouseDown(button);
        const bool released = mouseUp(button);
        return pressed && released;
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

    // High-performance drag operations
    bool Device::mouseDrag(MouseButton button, int32_t x, int32_t y) {
        if (!m_impl->connected.load()) {
            return false;
        }

        if (std::to_underlying(button) > std::to_underlying(MouseButton::SIDE2)) {
            return false;
        }

        // Execute drag sequence: press -> move -> release
        bool result1 = mouseDown(button);
        bool result2 = m_impl->executeMoveCommand(x, y);
        bool result3 = mouseUp(button);

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

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        const std::array<uint8_t, 1> payload{*keyCode};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_DOWN,
            payload);
    }

    bool Device::keyboardDown(
        const KeyboardKey& key, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            *keyCode,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_DOWN,
            payload);
    }

    bool Device::keyboardUp(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        const std::array<uint8_t, 1> payload{*keyCode};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_UP,
            payload);
    }

    bool Device::keyboardUp(
        const KeyboardKey& key, uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        const std::array<uint8_t, 3> payload{
            *keyCode,
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_UP,
            payload);
    }

    bool Device::keyboardPress(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        const std::array<uint8_t, 1> payload{*keyCode};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_PRESS,
            payload);
    }

    bool Device::keyboardPress(const KeyboardKey& key, uint32_t hold_ms) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        std::vector<uint8_t> payload{*keyCode};
        appendU32(payload, hold_ms);
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_PRESS,
            payload);
    }

    bool Device::keyboardPress(
        const KeyboardKey& key, uint32_t hold_ms, uint32_t rand_ms)
    {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        std::vector<uint8_t> payload{*keyCode};
        appendU32(payload, hold_ms);
        appendU32(payload, rand_ms);
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_PRESS,
            payload);
    }

    bool Device::keyboardString(const std::string& text) {
        if (!m_impl->connected.load() || text.size() > 256u ||
            !std::ranges::all_of(text, [](unsigned char byte) {
                return byte < 0x80u;
            }) ||
            text.size() > 248u) {
            return false;
        }

        return m_impl->executeApiCommand(
            ApiOpcode::KEY_STRING,
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(text.data()),
                text.size()));
    }

    bool Device::keyboardInit() {
        if (!m_impl->connected.load()) {
            return false;
        }

        return m_impl->executeApiCommand(
            ApiOpcode::KEY_INIT);
    }

    bool Device::keyboardInit(uint16_t dt_uframes) {
        if (!m_impl->connected.load() || !dtUframesValid(dt_uframes)) {
            return false;
        }
        const std::array<uint8_t, 2> payload{
            static_cast<uint8_t>(dt_uframes),
            static_cast<uint8_t>(dt_uframes >> 8u)};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_INIT,
            payload);
    }

    bool Device::keyboardIsDown(const KeyboardKey& key) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }

        const std::array<uint8_t, 1> payload{*keyCode};
        const auto response = m_impl->executeApiQuery(
            ApiOpcode::KEY_IS_DOWN,
            payload);
        try {
            if (!response) {
                return false;
            }
            return response->size() == 1u &&
                static_cast<uint8_t>((*response)[0]) != 0u;
        }
        catch (...) {
            return false;
        }
    }

    bool Device::keyboardMask(const KeyboardKey& key, bool enable) {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto keyCode = keyboardKeyCode(key);
        if (!keyCode) {
            return false;
        }
        const std::array<uint8_t, 2> payload{
            *keyCode, static_cast<uint8_t>(enable ? 1u : 0u)};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_MASK,
            payload);
    }

    bool Device::keyboardRemap(
        const KeyboardKey& source, const KeyboardKey& target)
    {
        if (!m_impl->connected.load()) {
            return false;
        }

        const auto sourceCode = keyboardKeyCode(source);
        const auto targetCode = keyboardKeyCode(target);
        if (!sourceCode || !targetCode) {
            return false;
        }
        const std::array<uint8_t, 2> payload{*sourceCode, *targetCode};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_REMAP,
            payload);
    }

    bool Device::keyboardMultiDown(const std::vector<KeyboardKey>& keys) {
        if (!m_impl->connected.load() || keys.empty() || keys.size() > 14u) return false;
        std::vector<uint8_t> payload;
        payload.reserve(keys.size());
        for (const auto& key : keys) {
            const auto code = keyboardKeyCode(key);
            if (!code) return false;
            payload.push_back(*code);
        }
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_MULTI_DOWN, payload);
    }

    bool Device::keyboardMultiUp(const std::vector<KeyboardKey>& keys) {
        if (!m_impl->connected.load() || keys.empty() || keys.size() > 14u) return false;
        std::vector<uint8_t> payload;
        payload.reserve(keys.size());
        for (const auto& key : keys) {
            const auto code = keyboardKeyCode(key);
            if (!code) return false;
            payload.push_back(*code);
        }
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_MULTI_UP, payload);
    }

    bool Device::keyboardMultiPress(const std::vector<KeyboardKey>& keys) {
        if (!m_impl->connected.load() || keys.empty() || keys.size() > 14u) return false;
        std::vector<uint8_t> payload;
        payload.reserve(keys.size());
        for (const auto& key : keys) {
            const auto code = keyboardKeyCode(key);
            if (!code) return false;
            payload.push_back(*code);
        }
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_MULTI_PRESS, payload);
    }

    namespace {
        bool controllerValueValid(ControllerControl control, int32_t value) {
            const auto id = std::to_underlying(control);
            if (id >= 55u) return false;
            if (control == ControllerControl::LEFT_TRIGGER ||
                control == ControllerControl::RIGHT_TRIGGER) {
                return value >= 0 && value <= 65535;
            }
            if (id >= std::to_underlying(ControllerControl::LEFT_STICK_X) &&
                id <= std::to_underlying(ControllerControl::RIGHT_STICK_Y)) {
                return value >= -32768 && value <= 32767;
            }
            return value == 0 || value == 1;
        }
    }

    std::optional<int32_t> Device::controllerControl(ControllerControl control) {
        if (std::to_underlying(control) >= 55u) return std::nullopt;
        const std::array<uint8_t, 1> payload{std::to_underlying(control)};
        const auto response = m_impl->executeApiQuery(
            ApiOpcode::CONTROLLER_CONTROL, payload);
        if (!response) return std::nullopt;
        if (response->size() != 5u ||
            static_cast<uint8_t>((*response)[0]) != std::to_underlying(control)) {
            return std::nullopt;
        }
        return static_cast<int32_t>(readU32(*response, 1u));
    }

    bool Device::controllerControl(ControllerControl control, int32_t value) {
        return controllerControl(control, value, 0u);
    }

    bool Device::controllerControl(
        ControllerControl control, int32_t value, uint16_t dt_uframes) {
        if (!controllerValueValid(control, value) ||
            dt_uframes > 0x3FFFu) return false;
        std::vector<uint8_t> payload;
        payload.reserve(7u);
        payload.push_back(std::to_underlying(control));
        appendU32(payload, static_cast<uint32_t>(value));
        appendU16(payload, dt_uframes);
        return m_impl->executeApiCommand(
            ApiOpcode::CONTROLLER_CONTROL, payload);
    }

    bool Device::controllerMask(
        ControllerControl control, ControllerMaskMode mode) {
        if (std::to_underlying(control) >= 55u ||
            std::to_underlying(mode) > 4u) return false;
        std::vector<uint8_t> payload{
            std::to_underlying(control), std::to_underlying(mode)};
        return m_impl->executeApiCommand(
            ApiOpcode::CONTROLLER_MASK, payload);
    }

    std::optional<ControllerState> Device::controllerState() {
        const auto response = m_impl->executeApiQuery(
            ApiOpcode::CONTROLLER_STATE);
        if (!response) return std::nullopt;
        ControllerState state{};
        if (response->size() != 20u) return std::nullopt;
        state.digitalLow = readU32(*response, 0u);
        state.digitalHigh = readU32(*response, 4u);
        state.leftTrigger = readU16(*response, 8u);
        state.rightTrigger = readU16(*response, 10u);
        state.leftStickX = static_cast<int16_t>(readU16(*response, 12u));
        state.leftStickY = static_cast<int16_t>(readU16(*response, 14u));
        state.rightStickX = static_cast<int16_t>(readU16(*response, 16u));
        state.rightStickY = static_cast<int16_t>(readU16(*response, 18u));
        return state;
    }

    bool Device::setControllerState(const ControllerState& state) {
        return setControllerState(state, 0u);
    }

    bool Device::setControllerState(
        const ControllerState& state, uint16_t dt_uframes) {
        if (dt_uframes > 0x3FFFu) return false;
        std::vector<uint8_t> payload;
        payload.reserve(22u);
        appendU32(payload, state.digitalLow);
        appendU32(payload, state.digitalHigh);
        appendU16(payload, state.leftTrigger);
        appendU16(payload, state.rightTrigger);
        appendI16(payload, state.leftStickX);
        appendI16(payload, state.leftStickY);
        appendI16(payload, state.rightStickX);
        appendI16(payload, state.rightStickY);
        appendU16(payload, dt_uframes);
        return m_impl->executeApiCommand(
            ApiOpcode::CONTROLLER_STATE, payload);
    }

    std::string Device::getKeyboardKeys() {
        if (!m_impl->connected.load()) return {};
        const auto response = m_impl->executeApiQuery(
            ApiOpcode::KEY_KEYS);
        if (!response) return {};
        return response->size() == 1u
            ? std::to_string(static_cast<uint8_t>((*response)[0]))
            : std::string{};
    }

    bool Device::setKeyboardKeys(bool enabled) {
        if (!m_impl->connected.load()) return false;
        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enabled ? 1u : 0u)};
        return m_impl->executeApiCommand(
            ApiOpcode::KEY_KEYS,
            payload);
    }


    // Button monitoring methods
    bool Device::enableButtonMonitoring(bool enable) {
        if (!m_impl->connected.load(std::memory_order_acquire)) {
            return false;
        }

        const std::array<uint8_t, 1> payload{
            static_cast<uint8_t>(enable ? 1u : 0u)};
        bool result = m_impl->executeApiCommand(
            ApiOpcode::BUTTONS, payload);
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


    void Device::enableHighPerformanceMode(bool enable) {
        m_impl->highPerformanceMode.store(enable);
    }

    bool Device::isHighPerformanceModeEnabled() const noexcept {
        return m_impl->highPerformanceMode.load();
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
