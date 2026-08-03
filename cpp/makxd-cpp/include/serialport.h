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
#include <array>
#include <memory>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <future>
#include <unordered_map>
#include <functional>
#include <thread>
#include <deque>
#include <chrono>
#include <span>
#include <stop_token>
#include "makxd_protocol.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// Define ssize_t for Windows compatibility
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fstream>
#endif

namespace makxd {

    class TransportEncryption;
    struct ConnectionConfig;
    struct ConnectionIoState;

    struct MakxdSerialPortInfo {
        std::string port;
        uint16_t pid;
    };

    struct PendingCommand {
        std::promise<std::string> promise;
        std::chrono::steady_clock::time_point timestamp;
        std::chrono::milliseconds timeout;
        int command_id;
        bool encrypted;
        uint8_t mak_api_opcode;
        std::array<uint8_t, 12> transaction_nonce{};

        PendingCommand(int id, std::chrono::milliseconds to,
            bool encrypted_transport, const std::array<uint8_t, 12>& nonce,
            uint8_t opcode)
            : timeout(to), command_id(id), encrypted(encrypted_transport),
              mak_api_opcode(opcode), transaction_nonce(nonce) {
            timestamp = std::chrono::steady_clock::now();
        }
    };

    class MAKXD_API SerialPort {
    public:
        SerialPort(bool encryptionEnabled = false,
            std::string_view encryptionKey = {});
        ~SerialPort();

        [[nodiscard]] bool open(const std::string& port, uint32_t baudRate);
        [[nodiscard]] bool open(const ConnectionConfig& connection);
        void close();
        [[nodiscard]] bool isOpen() const noexcept;
        [[nodiscard]] bool isActuallyConnected() const;

        [[nodiscard]] std::string getPortName() const;

        [[nodiscard]] std::future<std::string> sendTrackedMakApi(
            ApiOpcode opcode,
            std::span<const uint8_t> payload = {},
            std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
        [[nodiscard]] bool sendMakApi(
            ApiOpcode opcode,
            std::span<const uint8_t> payload = {});

        [[nodiscard]] bool flush();

        // Optimized timeout control
        void setTimeout(uint32_t timeoutMs);
        [[nodiscard]] uint32_t getTimeout() const noexcept;

        // Port enumeration
        [[nodiscard]] static std::vector<std::string> getAvailablePorts();
        [[nodiscard]] static std::vector<MakxdSerialPortInfo> findMakxdPortInfo();
        [[nodiscard]] static std::vector<std::string> findMakxdPorts();

        // Button callback support
        using ButtonCallback = std::function<void(uint8_t, bool)>;
        void setButtonCallback(ButtonCallback callback);

        // Get last platform error
        std::string getLastError();

    private:
        std::string m_portName;
        std::atomic<uint32_t> m_baudRate;
        std::atomic<uint32_t> m_timeout;
        std::atomic<bool> m_isOpen;
        std::unique_ptr<TransportEncryption> m_transportEncryption;
        std::unique_ptr<ConnectionIoState> m_connectionIo;
        mutable std::mutex m_mutex;
        mutable std::mutex m_nativeHandleMutex;

#ifdef _WIN32
        HANDLE m_handle;
        DCB m_dcb;
        COMMTIMEOUTS m_timeouts;
#else
        int m_fd;
        struct termios m_oldTermios;
        struct termios m_newTermios;
#endif

        // Command tracking system
        uint32_t m_commandCounter{ 0 };
        std::unordered_map<int, std::unique_ptr<PendingCommand>> m_pendingCommands;
        std::deque<int> m_pendingCommandOrder;
        std::mutex m_commandMutex;

        // High-performance listener thread
        std::jthread m_listenerThread;

        // Button data processing
        ButtonCallback m_buttonCallback;
        mutable std::mutex m_buttonCallbackMutex;
        std::atomic<uint8_t> m_lastButtonMask{ 0 };

        // Optimized parsing buffers
        static constexpr size_t BUFFER_SIZE = 4096;
        static constexpr size_t LINE_BUFFER_SIZE = 256;

        bool configurePort() { return platformConfigurePort(); }
        void updateTimeouts() { platformUpdateTimeouts(); }
        void listenerLoop(std::stop_token stopToken);
        void processIncomingData();
        void handleButtonData(uint8_t data);
        void processMakApiResponse(
            std::span<const uint8_t> response,
            const std::array<uint8_t, 12>* transactionNonce = nullptr);
        void cleanupTimedOutCommands();
        int generateCommandId();

        // Platform abstraction helpers for unified logic
        bool platformOpen(const std::string& devicePath);
        void platformClose();
        bool platformConfigurePort();
        void platformUpdateTimeouts();
        void platformUpdateTimeoutsUnlocked();
        ssize_t platformWrite(
            const void* data, size_t length, bool responseExpected);
        ssize_t platformRead(void* buffer, size_t maxBytes);
        size_t platformBytesAvailable() const;
        bool platformFlush();
        std::string getLastPlatformError();

        // Disable copy
        SerialPort(const SerialPort&) = delete;
        SerialPort& operator=(const SerialPort&) = delete;
    };

} // namespace makxd
