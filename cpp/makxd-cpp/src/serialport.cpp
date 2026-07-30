#include "../include/serialport.h"
#include "../include/makxd.h"
#include "../include/transport_encryption.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <cctype>
#include <chrono>
#include <exception>
#include <future>
#include <string_view>
#include <utility>
#include <limits>
#include <random>

#ifdef _WIN32
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "setupapi.lib")
#else
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fstream>
#include <regex>
#include <libudev.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace makxd {

struct ConnectionIoState {
	ConnectionConfig config{};
	std::deque<std::array<uint8_t, 8>> rawTransactions;
#ifdef _WIN32
	SOCKET udpSocket{INVALID_SOCKET};
	bool winsockStarted{false};
#else
	int udpSocket{-1};
#endif
};

namespace {
constexpr std::string_view ASCII_PROMPT = ">>> ";

std::string parseAsciiResponseValue(std::string_view body) {
	while (!body.empty() && (body.back() == '\r' || body.back() == '\n')) {
		body.remove_suffix(1);
	}

	const size_t separator = body.find_last_of("\r\n");
	if (separator == std::string_view::npos) {
		return std::string(body);
	}

	const size_t valueStart = body.find_first_not_of("\r\n", separator + 1);
	return valueStart == std::string_view::npos
	           ? std::string{}
	           : std::string(body.substr(valueStart));
}
} // namespace

SerialPort::SerialPort(bool encryptionEnabled, std::string_view encryptionKey)
	: m_baudRate(115200)
	, m_timeout(100)
	, m_isOpen(false)
	, m_transportEncryption(std::make_unique<TransportEncryption>(
	      encryptionEnabled, encryptionKey))
	, m_connectionIo(std::make_unique<ConnectionIoState>())
#ifdef _WIN32
	, m_handle(INVALID_HANDLE_VALUE)
#else
	, m_fd(-1)
#endif
{
#ifdef _WIN32
	memset(&m_dcb, 0, sizeof(m_dcb));
	memset(&m_timeouts, 0, sizeof(m_timeouts));
#endif
}

SerialPort::~SerialPort() {
	close();
}

bool SerialPort::open(const std::string& port, uint32_t baudRate) {
	for (;;) {
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_isOpen) {
			// Avoid re-entrant deadlock by releasing the lock before calling close().
			lock.unlock();
			close();
			continue;
		}

		m_portName = port;
		m_baudRate.store(baudRate, std::memory_order_relaxed);

		// Unified logic with platform abstraction
#ifdef _WIN32
		std::string devicePath = "\\\\.\\" + port;
#else
		std::string devicePath = "/dev/" + port;
#endif

		if (!platformOpen(devicePath)) {
			return false;
		}

		if (!platformConfigurePort()) {
			platformClose();
			return false;
		}

		m_isOpen = true;

		// Start high-performance listener thread (shared logic)
		m_listenerThread = std::jthread([this](std::stop_token stopToken) {
			listenerLoop(stopToken);
		});

		return true;
	}
}

void SerialPort::close() {
	// Stop listener thread BEFORE acquiring mutex to avoid deadlock.
	// The listener may call processResponse which acquires m_commandMutex,
	// while another thread might hold m_commandMutex and wait for m_mutex.
	if (m_listenerThread.joinable()) {
		m_listenerThread.request_stop();
		if (std::this_thread::get_id() != m_listenerThread.get_id()) {
			m_listenerThread.join();
		}
		// If on the listener thread, skip join — the jthread destructor
		// will join after the listener loop exits via the stop token.
	}

	std::lock_guard<std::mutex> lock(m_mutex);

	if (!m_isOpen.load(std::memory_order_acquire)) {
		return;
	}

	// Mark closed so no new I/O starts while we tear down.
	m_isOpen.store(false, std::memory_order_release);

	// Cancel all pending commands with double-checked locking for safety
	// First pass: mark all commands as cancelled to prevent new promise operations
	std::vector<std::unique_ptr<PendingCommand>> commandsToCancel;
	{
		std::lock_guard<std::mutex> cmdLock(m_commandMutex);
		commandsToCancel.reserve(m_pendingCommands.size());
		for (auto& [id, cmd] : m_pendingCommands) {
			commandsToCancel.push_back(std::move(cmd));
		}
		m_pendingCommands.clear();
		m_pendingCommandOrder.clear();
	}

	// Second pass: cancel commands outside of mutex to prevent deadlock
	for (auto& cmd : commandsToCancel) {
		try {
			cmd->promise.set_exception(std::make_exception_ptr(
			                               std::runtime_error("Connection closed")));
		}
		catch (...) {
			// Promise already set or moved - safe to ignore
		}
	}

	// Platform-specific cleanup
	platformClose();

	// Reset button state
	m_lastButtonMask.store(0, std::memory_order_release);
}

bool SerialPort::isOpen() const noexcept {
	return m_isOpen;
}

bool SerialPort::open(const ConnectionConfig& connection) {
	if (connection.method == ConnectionMethod::COM) {
		m_transportEncryption = std::make_unique<TransportEncryption>(
			!connection.aes128Key.empty(), connection.aes128Key);
		m_connectionIo->config = connection;
		return open(connection.comPort, 115200u);
	}
	if (connection.method == ConnectionMethod::UDP) {
		if (connection.udpHost.empty() || connection.udpPort == 0u ||
			connection.vlanId > 4094u ||
			(connection.vlanId != 0u &&
			 connection.udpBindAddress.empty() &&
			 connection.udpInterface.empty())) {
			return false;
		}
#ifdef _WIN32
		if (!connection.udpInterface.empty() &&
			connection.udpBindAddress.empty()) {
			return false;
		}
#endif
	} else if (connection.method == ConnectionMethod::BLE) {
		if (connection.bleAddress.empty() ||
			!connection.bleConnect ||
			!connection.bleWrite || !connection.bleRead ||
			!connection.aes128Key.empty()) {
			return false;
		}
	}

	close();
	m_transportEncryption = std::make_unique<TransportEncryption>(
		!connection.aes128Key.empty(), connection.aes128Key);
	m_connectionIo->config = connection;
	m_portName = connection.method == ConnectionMethod::UDP
		? "udp://" + connection.udpHost + ":" +
			std::to_string(connection.udpPort)
		: "ble://" + connection.bleAddress;

	if (connection.method == ConnectionMethod::UDP) {
#ifdef _WIN32
		WSADATA winsock{};
		if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
			return false;
		}
		m_connectionIo->winsockStarted = true;
#endif
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		addrinfo* remote = nullptr;
		const std::string port = std::to_string(connection.udpPort);
		if (getaddrinfo(
				connection.udpHost.c_str(), port.c_str(),
				&hints, &remote) != 0 || remote == nullptr) {
			platformClose();
			return false;
		}
		m_connectionIo->udpSocket = ::socket(
			remote->ai_family, remote->ai_socktype, remote->ai_protocol);
#ifdef _WIN32
		if (m_connectionIo->udpSocket == INVALID_SOCKET) {
#else
		if (m_connectionIo->udpSocket < 0) {
#endif
			freeaddrinfo(remote);
			platformClose();
			return false;
		}
#ifndef _WIN32
		if (!connection.udpInterface.empty() &&
			setsockopt(
				m_connectionIo->udpSocket,
				SOL_SOCKET,
				SO_BINDTODEVICE,
				connection.udpInterface.c_str(),
				static_cast<socklen_t>(connection.udpInterface.size() + 1u)) != 0) {
			freeaddrinfo(remote);
			platformClose();
			return false;
		}
#endif
		if (!connection.udpBindAddress.empty()) {
			sockaddr_in local{};
			local.sin_family = AF_INET;
			local.sin_port = 0;
			if (inet_pton(
					AF_INET, connection.udpBindAddress.c_str(),
					&local.sin_addr) != 1 ||
				::bind(
					m_connectionIo->udpSocket,
					reinterpret_cast<const sockaddr*>(&local),
					sizeof(local)) != 0) {
				freeaddrinfo(remote);
				platformClose();
				return false;
			}
		}
		const bool connected = ::connect(
			m_connectionIo->udpSocket,
			remote->ai_addr,
			static_cast<int>(remote->ai_addrlen)) == 0;
		freeaddrinfo(remote);
		if (!connected) {
			platformClose();
			return false;
		}
#ifdef _WIN32
		u_long nonblocking = 1;
		ioctlsocket(m_connectionIo->udpSocket, FIONBIO, &nonblocking);
#else
		const int flags = fcntl(m_connectionIo->udpSocket, F_GETFL, 0);
		fcntl(m_connectionIo->udpSocket, F_SETFL, flags | O_NONBLOCK);
#endif
	} else if (!connection.bleConnect(connection.bleAddress)) {
		platformClose();
		return false;
	}

	m_isOpen.store(true, std::memory_order_release);
	m_listenerThread = std::jthread([this](std::stop_token stopToken) {
		listenerLoop(stopToken);
	});
	return true;
}

void SerialPort::setApiProtocol(ApiProtocol apiProtocol) noexcept {
	m_apiProtocol.store(apiProtocol, std::memory_order_release);
}

ApiProtocol SerialPort::getApiProtocol() const noexcept {
	return m_apiProtocol.load(std::memory_order_acquire);
}

bool SerialPort::isActuallyConnected() const {
	if (!m_isOpen) {
		return false;
	}
	if (m_connectionIo->config.method != ConnectionMethod::COM) {
		return true;
	}

#ifdef _WIN32
	// Windows: Check if handle is still valid
	if (m_handle == INVALID_HANDLE_VALUE) {
		return false;
	}

	// Try to get comm state to verify device is still there
	DCB dcb;
	return GetCommState(m_handle, &dcb) != 0;
#else
	// Linux: Check if file descriptor is still valid
	if (m_fd < 0) {
		return false;
	}

	// Use poll to check if device is still connected
	struct pollfd pfd;
	pfd.fd = m_fd;
	pfd.events = POLLERR | POLLHUP | POLLNVAL;
	pfd.revents = 0;

	int result = poll(&pfd, 1, 0);  // Non-blocking check

	if (result < 0) {
		return false;  // Error occurred
	}

	// If any error conditions are set, device is disconnected
	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
		return false;
	}

	return true;
#endif
}

std::future<std::string> SerialPort::sendTrackedCommand(const std::string& command,
        bool expectResponse,
        std::chrono::milliseconds timeout) {
	// Check port status with atomic load to prevent race conditions
	if (!m_isOpen.load(std::memory_order_acquire)) {
		std::promise<std::string> promise;
		promise.set_exception(std::make_exception_ptr(
		                          std::runtime_error("Port not open")));
		return promise.get_future();
	}

	// Command length validation
	constexpr size_t MAX_COMMAND_LENGTH = 512;
	if (command.length() > MAX_COMMAND_LENGTH) {
		std::promise<std::string> promise;
		promise.set_exception(std::make_exception_ptr(
		                          std::runtime_error("Command too long (max " + std::to_string(MAX_COMMAND_LENGTH) + " chars)")));
		return promise.get_future();
	}

	std::string trackedCommand = command + "\r\n";
	TransportEncodedCommand encodedCommand;
	try {
		encodedCommand = m_transportEncryption->encodeCommand(
		    std::span<const uint8_t>(
		        reinterpret_cast<const uint8_t*>(trackedCommand.data()),
		        trackedCommand.size()));
	}
	catch (...) {
		std::promise<std::string> promise;
		promise.set_exception(std::current_exception());
		return promise.get_future();
	}

	int cmdId = -1;
	std::future<std::string> future;

	// Store pending command under lock so ID generation cannot collide.
	{
		std::lock_guard<std::mutex> lock(m_commandMutex);
		cmdId = generateCommandId();
		if (cmdId <= 0) {
			std::promise<std::string> promise;
			promise.set_exception(std::make_exception_ptr(
			                          std::runtime_error("No command IDs available")));
			return promise.get_future();
		}

		auto pendingCmd = std::make_unique<PendingCommand>(
		    cmdId, command, expectResponse, timeout,
		    m_transportEncryption->enabled(), encodedCommand.transactionNonce);
		future = pendingCmd->promise.get_future();
		m_pendingCommands.emplace(cmdId, std::move(pendingCmd));
		m_pendingCommandOrder.push_back(cmdId);
	}

	// Unified write operation
	ssize_t bytesWritten = platformWrite(
	    encodedCommand.bytes.data(), encodedCommand.bytes.size(),
	    expectResponse);

	if (bytesWritten != static_cast<ssize_t>(encodedCommand.bytes.size())) {
		std::lock_guard<std::mutex> lock(m_commandMutex);
		auto it = m_pendingCommands.find(cmdId);
		if (it != m_pendingCommands.end()) {
			try {
				std::string errorMsg = "Write failed";
				if (bytesWritten < 0) {
					errorMsg += " (" + getLastPlatformError() + ")";
				}
				else {
					errorMsg += " (partial write: " + std::to_string(bytesWritten) +
					            "/" + std::to_string(encodedCommand.bytes.size()) + " bytes)";
				}
				it->second->promise.set_exception(std::make_exception_ptr(
				                                      std::runtime_error(errorMsg)));
			}
			catch (...) {
				// Promise already set
			}
			m_pendingCommands.erase(it);
			auto orderIt = std::find(m_pendingCommandOrder.begin(), m_pendingCommandOrder.end(), cmdId);
			if (orderIt != m_pendingCommandOrder.end()) {
				m_pendingCommandOrder.erase(orderIt);
			}
		}
	}

	// Unified flush operation
	platformFlush();

	return future;
}

std::future<std::string> SerialPort::sendTrackedMakApi(
        ApiOpcode opcode,
        ApiVerb verb,
        std::span<const uint8_t> payload,
        std::chrono::milliseconds timeout) {
	if (!m_isOpen.load(std::memory_order_acquire)) {
		std::promise<std::string> promise;
		promise.set_exception(std::make_exception_ptr(
			std::runtime_error("Port not open")));
		return promise.get_future();
	}
	if (payload.size() > 248u) {
		std::promise<std::string> promise;
		promise.set_exception(std::make_exception_ptr(
			std::runtime_error("MAK_API command exceeds the COM frame limit")));
		return promise.get_future();
	}

	std::vector<uint8_t> identified;
	identified.reserve(3u + payload.size());
	identified.push_back(0x00u);
	identified.push_back(static_cast<uint8_t>(opcode));
	identified.push_back(static_cast<uint8_t>(verb));
	identified.insert(identified.end(), payload.begin(), payload.end());

	TransportEncodedCommand encodedCommand;
	try {
		if (m_transportEncryption->enabled()) {
			encodedCommand = m_transportEncryption->encodeCommand(identified);
		} else {
			encodedCommand.bytes.resize(5u + identified.size());
			encodedCommand.bytes[0] = 0xDEu;
			encodedCommand.bytes[1] = 0xADu;
			encodedCommand.bytes[2] =
				static_cast<uint8_t>(identified.size());
			encodedCommand.bytes[3] =
				static_cast<uint8_t>(identified.size() >> 8u);
			encodedCommand.bytes[4] = 0x00u;
			std::copy(
				identified.begin(), identified.end(),
				encodedCommand.bytes.begin() + 5);
		}
	} catch (...) {
		std::promise<std::string> promise;
		promise.set_exception(std::current_exception());
		return promise.get_future();
	}

	int commandId = -1;
	std::future<std::string> future;
	{
		std::lock_guard<std::mutex> lock(m_commandMutex);
		commandId = generateCommandId();
		if (commandId <= 0) {
			std::promise<std::string> promise;
			promise.set_exception(std::make_exception_ptr(
				std::runtime_error("No command IDs available")));
			return promise.get_future();
		}
		auto pending = std::make_unique<PendingCommand>(
			commandId,
			"MAK_API",
			true,
			timeout,
			m_transportEncryption->enabled(),
			encodedCommand.transactionNonce,
			true,
			static_cast<uint8_t>(opcode));
		future = pending->promise.get_future();
		m_pendingCommands.emplace(commandId, std::move(pending));
		m_pendingCommandOrder.push_back(commandId);
	}

	const ssize_t bytesWritten = platformWrite(
		encodedCommand.bytes.data(), encodedCommand.bytes.size(), true);
	if (bytesWritten != static_cast<ssize_t>(encodedCommand.bytes.size())) {
		std::lock_guard<std::mutex> lock(m_commandMutex);
		auto pending = m_pendingCommands.find(commandId);
		if (pending != m_pendingCommands.end()) {
			try {
				pending->second->promise.set_exception(
					std::make_exception_ptr(
						std::runtime_error("MAK_API command write failed")));
			} catch (...) {
			}
			m_pendingCommands.erase(pending);
		}
		auto order = std::find(
			m_pendingCommandOrder.begin(),
			m_pendingCommandOrder.end(),
			commandId);
		if (order != m_pendingCommandOrder.end()) {
			m_pendingCommandOrder.erase(order);
		}
	}
	platformFlush();
	return future;
}

bool SerialPort::sendMakApi(
        ApiOpcode opcode,
        ApiVerb verb,
        std::span<const uint8_t> payload) {
	if (!m_isOpen.load(std::memory_order_acquire) || payload.size() > 248u) {
		return false;
	}

	std::vector<uint8_t> identified;
	identified.reserve(3u + payload.size());
	identified.push_back(0x00u);
	identified.push_back(static_cast<uint8_t>(opcode));
	identified.push_back(static_cast<uint8_t>(verb));
	identified.insert(identified.end(), payload.begin(), payload.end());

	TransportEncodedCommand encodedCommand;
	try {
		if (m_transportEncryption->enabled()) {
			encodedCommand = m_transportEncryption->encodeCommand(identified);
		} else {
			encodedCommand.bytes.resize(5u + identified.size());
			encodedCommand.bytes[0] = 0xDEu;
			encodedCommand.bytes[1] = 0xADu;
			encodedCommand.bytes[2] =
				static_cast<uint8_t>(identified.size());
			encodedCommand.bytes[3] =
				static_cast<uint8_t>(identified.size() >> 8u);
			encodedCommand.bytes[4] = 0x00u;
			std::copy(
				identified.begin(), identified.end(),
				encodedCommand.bytes.begin() + 5);
		}
	} catch (...) {
		return false;
	}

	const ssize_t bytesWritten = platformWrite(
		encodedCommand.bytes.data(), encodedCommand.bytes.size(), false);
	return bytesWritten == static_cast<ssize_t>(encodedCommand.bytes.size()) &&
		platformFlush();
}

bool SerialPort::sendCommand(const std::string& command) {
	// Check port status with atomic load to prevent race conditions
	if (!m_isOpen.load(std::memory_order_acquire)) {
		return false;
	}

	// Command length validation
	constexpr size_t MAX_COMMAND_LENGTH = 512;
	if (command.length() > MAX_COMMAND_LENGTH) {
#ifdef DEBUG
		std::cerr << "SerialPort: Command too long (" << command.length() << " > " << MAX_COMMAND_LENGTH << ")" << std::endl;
#endif
		return false;
	}

	std::string fullCommand = command + "\r\n";
	TransportEncodedCommand encodedCommand;
	try {
		encodedCommand = m_transportEncryption->encodeCommand(
		    std::span<const uint8_t>(
		        reinterpret_cast<const uint8_t*>(fullCommand.data()),
		        fullCommand.size()));
	}
	catch (...) {
		return false;
	}

	// Unified write and flush operation
	ssize_t bytesWritten = platformWrite(
	    encodedCommand.bytes.data(), encodedCommand.bytes.size(), false);
	if (bytesWritten == static_cast<ssize_t>(encodedCommand.bytes.size())) {
		return platformFlush();
	}

	return false;
}

void SerialPort::listenerLoop(std::stop_token stopToken) {
	// Optimized read buffers (shared logic)
	std::vector<uint8_t> readBuffer(BUFFER_SIZE);
	std::string responseBuffer;
	responseBuffer.reserve(BUFFER_SIZE);
	std::vector<uint8_t> makApiBuffer;
	makApiBuffer.reserve(BUFFER_SIZE);
	constexpr std::array<uint8_t, 2> makApiMagic{0xDEu, 0xADu};
	enum class ButtonPrefixState : uint8_t {
		NONE,
		K,
		KM,
		KM_DOT
	};
	ButtonPrefixState buttonPrefixState = ButtonPrefixState::NONE;

	auto lastCleanup = std::chrono::steady_clock::now();
	constexpr auto cleanupInterval = std::chrono::milliseconds(50);

	auto appendTextByte = [&](uint8_t byte) {
		if (responseBuffer.size() >= BUFFER_SIZE - 1) {
#ifdef DEBUG
			std::cerr << "SerialPort: response buffer overflow, discarding response" << std::endl;
#endif
			responseBuffer.clear();
			buttonPrefixState = ButtonPrefixState::NONE;
			return;
		}

		responseBuffer.push_back(static_cast<char>(byte));
		if (responseBuffer.size() >= ASCII_PROMPT.size() &&
		    responseBuffer.compare(responseBuffer.size() - ASCII_PROMPT.size(),
		                           ASCII_PROMPT.size(), ASCII_PROMPT) == 0) {
			const size_t bodySize = responseBuffer.size() - ASCII_PROMPT.size();
			std::string body(responseBuffer.data(), bodySize);
			responseBuffer.clear();
			processResponse(body);
		}
	};

	while (!stopToken.stop_requested() && m_isOpen.load(std::memory_order_acquire)) {
		try {
			// Unified bytes available check
			size_t bytesAvailable = platformBytesAvailable();
			if (bytesAvailable == 0) {
				std::this_thread::sleep_for(std::chrono::microseconds(500));
				continue;
			}

			// Unified read operation
			size_t bytesToRead = std::min<size_t>(bytesAvailable, static_cast<size_t>(BUFFER_SIZE));
			ssize_t bytesRead = platformRead(readBuffer.data(), bytesToRead);

			if (bytesRead <= 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			if (m_transportEncryption->enabled()) {
				for (const auto& decoded : m_transportEncryption->decodeResponses(
				         std::span<const uint8_t>(
				             readBuffer.data(), static_cast<size_t>(bytesRead)))) {
					if (getApiProtocol() == ApiProtocol::MAK_API) {
						processMakApiResponse(
							decoded.plaintext,
							&decoded.transactionNonce);
						continue;
					}
					const std::string plaintext(
					    reinterpret_cast<const char*>(decoded.plaintext.data()),
					    decoded.plaintext.size());
					if (plaintext.size() < ASCII_PROMPT.size() ||
					    plaintext.compare(
					        plaintext.size() - ASCII_PROMPT.size(),
					        ASCII_PROMPT.size(),
					        ASCII_PROMPT) != 0) {
						throw std::runtime_error(
						    "encrypted response is missing the command prompt");
					}
					processResponse(
					    plaintext.substr(0, plaintext.size() - ASCII_PROMPT.size()),
					    &decoded.transactionNonce);
				}
				continue;
			}

			if (getApiProtocol() == ApiProtocol::MAK_API) {
				makApiBuffer.insert(
					makApiBuffer.end(),
					readBuffer.begin(),
					readBuffer.begin() + bytesRead);
				for (;;) {
					const auto marker = std::search(
						makApiBuffer.begin(), makApiBuffer.end(),
						makApiMagic.begin(), makApiMagic.end());
					if (marker == makApiBuffer.end()) {
						const bool keepDe =
							!makApiBuffer.empty() &&
							makApiBuffer.back() == 0xDEu;
						makApiBuffer.clear();
						if (keepDe) {
							makApiBuffer.push_back(0xDEu);
						}
						break;
					}
					if (marker != makApiBuffer.begin()) {
						makApiBuffer.erase(makApiBuffer.begin(), marker);
					}
					if (makApiBuffer.size() < 5u) {
						break;
					}
					const size_t payloadBytes =
						static_cast<size_t>(makApiBuffer[2]) |
						(static_cast<size_t>(makApiBuffer[3]) << 8u);
					if (payloadBytes == 0u || payloadBytes > 251u) {
						makApiBuffer.erase(makApiBuffer.begin());
						continue;
					}
					const size_t frameBytes = 5u + payloadBytes;
					if (makApiBuffer.size() < frameBytes) {
						break;
					}
					if (makApiBuffer[4] == 0x00u) {
						processMakApiResponse(std::span<const uint8_t>(
							makApiBuffer.data() + 5u, payloadBytes));
					}
					makApiBuffer.erase(
						makApiBuffer.begin(),
						makApiBuffer.begin() + frameBytes);
				}
				continue;
			}

			// Shared byte processing logic
			for (ssize_t i = 0; i < bytesRead; ++i) {
				const uint8_t byte = readBuffer[i];
				bool consumed = false;

				auto flushPrefixAsText = [&]() {
					switch (buttonPrefixState) {
						case ButtonPrefixState::K:
							appendTextByte('k');
							break;
						case ButtonPrefixState::KM:
							appendTextByte('k');
							appendTextByte('m');
							break;
						case ButtonPrefixState::KM_DOT:
							appendTextByte('k');
							appendTextByte('m');
							appendTextByte('.');
							break;
						case ButtonPrefixState::NONE:
						default:
							break;
					}
					buttonPrefixState = ButtonPrefixState::NONE;
				};

				// Detect standalone button prefix "km." only when no response is buffered.
				if (responseBuffer.empty() || buttonPrefixState != ButtonPrefixState::NONE) {
					switch (buttonPrefixState) {
						case ButtonPrefixState::NONE:
							if (byte == 'k') {
								buttonPrefixState = ButtonPrefixState::K;
								consumed = true;
							}
							break;
						case ButtonPrefixState::K:
							if (byte == 'm') {
								buttonPrefixState = ButtonPrefixState::KM;
								consumed = true;
							}
							else {
								flushPrefixAsText();
							}
							break;
						case ButtonPrefixState::KM:
							if (byte == '.') {
								buttonPrefixState = ButtonPrefixState::KM_DOT;
								consumed = true;
							}
							else {
								flushPrefixAsText();
							}
							break;
						case ButtonPrefixState::KM_DOT:
							if (byte < 32) {
								handleButtonData(byte);
								buttonPrefixState = ButtonPrefixState::NONE;
								consumed = true;
							}
							else {
								flushPrefixAsText();
							}
							break;
					}
				}

				if (consumed) {
					continue;
				}

				appendTextByte(byte);
			}

			// Periodic cleanup of timed-out commands (shared logic)
			auto now = std::chrono::steady_clock::now();
			if (now - lastCleanup > cleanupInterval) {
				cleanupTimedOutCommands();
				lastCleanup = now;
			}

		}
		catch (const std::exception& e) {
			// Log specific exception for debugging but continue running
			// In production, you might want to use a proper logging framework
#ifdef DEBUG
			std::cerr << "SerialPort listener exception: " << e.what() << std::endl;
#endif

			// Brief pause to prevent tight exception loops
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

			// Check if port is still open after exception
			if (!m_isOpen.load(std::memory_order_acquire)) {
				// Port was closed, exit gracefully
				break;
			}
		}
		catch (...) {
			// Unknown exception - be more cautious
#ifdef DEBUG
			std::cerr << "SerialPort listener unknown exception" << std::endl;
#endif

			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			// Check if port is still open after unknown exception
			if (!m_isOpen.load(std::memory_order_acquire)) {
				// Port was closed, exit gracefully
				break;
			}
		}
	}
}

void SerialPort::handleButtonData(uint8_t data) {
	const uint8_t lastMask = m_lastButtonMask.load(std::memory_order_acquire);
	if (data == lastMask) {
		return; // No change
	}

	m_lastButtonMask.store(data, std::memory_order_release);

	ButtonCallback callbackCopy;
	{
		std::lock_guard<std::mutex> lock(m_buttonCallbackMutex);
		callbackCopy = m_buttonCallback;
	}
	if (!callbackCopy) {
		return;
	}

	// Only process changed bits.
	const uint8_t changedBits = static_cast<uint8_t>(data ^ lastMask);
	for (int bit = 0; bit < 5; ++bit) {
		if (changedBits & (1 << bit)) {
			const bool isPressed = (data & (1 << bit)) != 0;
			try {
				callbackCopy(bit, isPressed);
			}
			catch (...) {
				// Ignore callback exceptions.
			}
		}
	}
}

void SerialPort::processResponse(
    const std::string& response,
    const std::array<uint8_t, 12>* transactionNonce) {
	const std::string value = parseAsciiResponseValue(response);

	// Responses are prompt-delimited and complete in command order. The command
	// itself is not modified with a tracking suffix; KM only accepts plain ASCII
	// command lines.
	std::lock_guard<std::mutex> lock(m_commandMutex);
	if (m_transportEncryption->enabled()) {
		if (transactionNonce == nullptr) {
			return;
		}
		auto orderIt = std::find_if(
		    m_pendingCommandOrder.begin(),
		    m_pendingCommandOrder.end(),
		    [&](int pendingId) {
			    const auto pendingIt = m_pendingCommands.find(pendingId);
			    return pendingIt != m_pendingCommands.end() &&
			           !pendingIt->second->makApi &&
			           pendingIt->second->encrypted &&
			           pendingIt->second->transaction_nonce == *transactionNonce;
		    });
		if (orderIt == m_pendingCommandOrder.end()) {
			return;
		}
		const int cmdId = *orderIt;
		m_pendingCommandOrder.erase(orderIt);
		auto it = m_pendingCommands.find(cmdId);
		if (it == m_pendingCommands.end()) {
			return;
		}
		try {
			it->second->promise.set_value(value);
		}
		catch (...) {
		}
		m_pendingCommands.erase(it);
		return;
	}

	while (!m_pendingCommandOrder.empty()) {
		int cmdId = m_pendingCommandOrder.front();
		m_pendingCommandOrder.pop_front();
		auto it = m_pendingCommands.find(cmdId);
		if (it == m_pendingCommands.end()) {
			continue;
		}
		if (it->second->makApi) {
			m_pendingCommandOrder.push_back(cmdId);
			return;
		}
		try {
			it->second->promise.set_value(value);
		}
		catch (...) {
			// Promise already set
		}
		m_pendingCommands.erase(it);
		break;
	}
}

void SerialPort::processMakApiResponse(
    std::span<const uint8_t> response,
    const std::array<uint8_t, 12>* transactionNonce) {
	if (response.size() < 3u || response[0] != 0x00u) {
		return;
	}
	const uint8_t opcode = response[1];
	const uint8_t status = response[2];

	std::lock_guard<std::mutex> lock(m_commandMutex);
	auto order = std::find_if(
		m_pendingCommandOrder.begin(),
		m_pendingCommandOrder.end(),
		[&](int pendingId) {
			const auto pending = m_pendingCommands.find(pendingId);
			if (pending == m_pendingCommands.end() ||
				!pending->second->makApi ||
				pending->second->mak_api_opcode != opcode) {
				return false;
			}
			return !m_transportEncryption->enabled() ||
				(transactionNonce != nullptr &&
				 pending->second->encrypted &&
				 pending->second->transaction_nonce == *transactionNonce);
		});
	if (order == m_pendingCommandOrder.end()) {
		return;
	}
	const int commandId = *order;
	m_pendingCommandOrder.erase(order);
	auto pending = m_pendingCommands.find(commandId);
	if (pending == m_pendingCommands.end()) {
		return;
	}
	try {
		if (status != 0x01u) {
			pending->second->promise.set_exception(
				std::make_exception_ptr(
					std::runtime_error("MAK_API returned error status")));
		} else {
			pending->second->promise.set_value(std::string(
				reinterpret_cast<const char*>(response.data() + 3u),
				response.size() - 3u));
		}
	} catch (...) {
	}
	m_pendingCommands.erase(pending);
}

void SerialPort::cleanupTimedOutCommands() {
	auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(m_commandMutex);
	auto it = m_pendingCommands.begin();
	while (it != m_pendingCommands.end()) {
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		                   now - it->second->timestamp);

		if (elapsed > it->second->timeout) {
			int timedOutId = it->first;
			try {
				it->second->promise.set_exception(std::make_exception_ptr(
				                                      std::runtime_error("Command timeout")));
			}
			catch (...) {
				// Promise already set
			}
			it = m_pendingCommands.erase(it);
			auto orderIt = std::find(m_pendingCommandOrder.begin(), m_pendingCommandOrder.end(), timedOutId);
			if (orderIt != m_pendingCommandOrder.end()) {
				m_pendingCommandOrder.erase(orderIt);
			}
		}
		else {
			++it;
		}
	}
}

int SerialPort::generateCommandId() {
	// Must be called with m_commandMutex held.
	constexpr uint32_t MAX_ATTEMPTS = 65536;
	for (uint32_t attempts = 0; attempts < MAX_ATTEMPTS; ++attempts) {
		constexpr uint32_t MAX_COMMAND_ID = static_cast<uint32_t>((std::numeric_limits<int>::max)());
		if (m_commandCounter >= MAX_COMMAND_ID) {
			m_commandCounter = 0;
		}

		++m_commandCounter;
		const int candidateId = static_cast<int>(m_commandCounter);
		if (m_pendingCommands.find(candidateId) == m_pendingCommands.end()) {
			return candidateId;
		}
	}

	return -1;
}


void SerialPort::setButtonCallback(ButtonCallback callback) {
	std::lock_guard<std::mutex> lock(m_buttonCallbackMutex);
	m_buttonCallback = std::move(callback);
}

std::string SerialPort::getPortName() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_portName;
}

bool SerialPort::write(std::span<const uint8_t> data) {
	if (!m_isOpen.load(std::memory_order_acquire)) {
		return false;
	}

	if (data.empty()) {
		return true;
	}

	// Raw MAK_API write path: do not append CRLF.
	ssize_t bytesWritten = platformWrite(data.data(), data.size(), true);
	if (bytesWritten != static_cast<ssize_t>(data.size())) {
		return false;
	}

	return platformFlush();
}

bool SerialPort::write(const std::vector<uint8_t>& data) {
	return write(std::span<const uint8_t>(data.data(), data.size()));
}

bool SerialPort::write(const std::string& data) {
	return sendCommand(data);
}

std::vector<uint8_t> SerialPort::read(size_t maxBytes) {
	// This is a legacy method - not recommended for high performance
	std::vector<uint8_t> buffer;
	if (!m_isOpen || maxBytes == 0) {
		return buffer;
	}

	// Unified read operation
	buffer.resize(maxBytes);
	ssize_t bytesRead = platformRead(buffer.data(), maxBytes);
	if (bytesRead > 0) {
		buffer.resize(bytesRead);
	}
	else {
		buffer.clear();
	}

	return buffer;
}

std::string SerialPort::readString(size_t maxBytes) {
	auto data = read(maxBytes);
	return std::string(data.begin(), data.end());
}

size_t SerialPort::available() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_isOpen) {
		return 0;
	}

	// Unified bytes available check
	return platformBytesAvailable();
}

bool SerialPort::flush() {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_isOpen) {
		return false;
	}

	// Unified flush operation
	return platformFlush();
}

void SerialPort::setTimeout(uint32_t timeoutMs) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_timeout.store(timeoutMs, std::memory_order_relaxed);
	if (m_isOpen.load(std::memory_order_acquire)) {
		platformUpdateTimeouts();
	}
}

uint32_t SerialPort::getTimeout() const noexcept {
    return m_timeout.load(std::memory_order_relaxed);
}

std::string SerialPort::getLastError() {
    return getLastPlatformError();
}

std::vector<std::string> SerialPort::getAvailablePorts() {
	std::vector<std::string> ports;

#ifdef _WIN32
	HKEY hKey;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
	                  0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		char valueName[256];
		char data[256];
		DWORD valueNameSize, dataSize, dataType;
		DWORD index = 0;

		while (true) {
			valueNameSize = sizeof(valueName);
			dataSize = sizeof(data);

			LONG result = RegEnumValueA(hKey, index++, valueName, &valueNameSize,
			                            nullptr, &dataType,
			                            reinterpret_cast<BYTE*>(data), &dataSize);

			if (result == ERROR_NO_MORE_ITEMS) {
				break;
			}

			if (result == ERROR_SUCCESS && dataType == REG_SZ) {
				ports.emplace_back(data);
			}
		}

		RegCloseKey(hKey);
	}
#else
	// Linux implementation - scan /dev for tty devices
	DIR* dir = opendir("/dev");
	if (dir) {
		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr) {
			std::string name(entry->d_name);
			if (name.substr(0, 6) == "ttyUSB" || name.substr(0, 6) == "ttyACM") {
				ports.push_back(name);
			}
		}
		closedir(dir);
	}
#endif

	std::sort(ports.begin(), ports.end());
	return ports;
}

std::vector<MakxdSerialPortInfo> SerialPort::findMakxdPortInfo() {
	std::vector<MakxdSerialPortInfo> ch343Ports;
	std::vector<MakxdSerialPortInfo> ch340Ports;

#ifdef _WIN32
	HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS,
	                         nullptr, nullptr, DIGCF_PRESENT);
	if (deviceInfoSet == INVALID_HANDLE_VALUE) {
		return {};
	}

	SP_DEVINFO_DATA deviceInfoData;
	deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
		char hardwareId[1024] = { 0 };
		char portName[256] = { 0 };

		if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData,
		                                      SPDRP_HARDWAREID, nullptr,
		                                      reinterpret_cast<BYTE*>(hardwareId),
		                                      sizeof(hardwareId), nullptr)) {
			std::string id(hardwareId);
			std::transform(id.begin(), id.end(), id.begin(),
			               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			const bool isCh343 = id.find("VID_1A86&PID_55D3") != std::string::npos;
			const bool isCh340 = id.find("VID_1A86&PID_7523") != std::string::npos;
			if (!isCh343 && !isCh340) {
				continue;
			}

			HKEY hDeviceKey = SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfoData,
			                                       DICS_FLAG_GLOBAL, 0,
			                                       DIREG_DEV, KEY_READ);
			if (hDeviceKey != INVALID_HANDLE_VALUE) {
				DWORD portNameSize = sizeof(portName);
				if (RegQueryValueExA(hDeviceKey, "PortName", nullptr, nullptr,
				                     reinterpret_cast<BYTE*>(portName),
				                     &portNameSize) == ERROR_SUCCESS) {
					auto& ports = isCh343 ? ch343Ports : ch340Ports;
					ports.push_back({portName, isCh343 ? uint16_t{0x55D3} : uint16_t{0x7523}});
				}
				RegCloseKey(hDeviceKey);
			}
		}
	}

	SetupDiDestroyDeviceInfoList(deviceInfoSet);
#else
	// Linux implementation using udev to find MAKXD devices
	struct udev* udev = udev_new();
	if (!udev) {
		return {};
	}

	struct udev_enumerate* enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry* entry;

	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* dev = udev_device_new_from_syspath(udev, path);

		if (dev) {
			struct udev_device* parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
			if (parent) {
				const char* idVendor = udev_device_get_sysattr_value(parent, "idVendor");
				const char* idProduct = udev_device_get_sysattr_value(parent, "idProduct");

				const bool isCh343 = idVendor && idProduct &&
					strcmp(idVendor, "1a86") == 0 && strcmp(idProduct, "55d3") == 0;
				const bool isCh340 = idVendor && idProduct &&
					strcmp(idVendor, "1a86") == 0 && strcmp(idProduct, "7523") == 0;
				if (isCh343 || isCh340) {
					const char* devNode = udev_device_get_devnode(dev);
					if (devNode) {
						std::string portName = std::string(devNode).substr(5); // Remove "/dev/" prefix
						auto& ports = isCh343 ? ch343Ports : ch340Ports;
						ports.push_back({portName, isCh343 ? uint16_t{0x55D3} : uint16_t{0x7523}});
					}
				}
			}
			udev_device_unref(dev);
		}
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);
#endif

	const auto portOrder = [](const MakxdSerialPortInfo& lhs, const MakxdSerialPortInfo& rhs) {
		return lhs.port < rhs.port;
	};
	const auto samePort = [](const MakxdSerialPortInfo& lhs, const MakxdSerialPortInfo& rhs) {
		return lhs.port == rhs.port;
	};
	std::sort(ch343Ports.begin(), ch343Ports.end(), portOrder);
	ch343Ports.erase(std::unique(ch343Ports.begin(), ch343Ports.end(), samePort), ch343Ports.end());
	std::sort(ch340Ports.begin(), ch340Ports.end(), portOrder);
	ch340Ports.erase(std::unique(ch340Ports.begin(), ch340Ports.end(), samePort), ch340Ports.end());
	ch343Ports.insert(ch343Ports.end(), ch340Ports.begin(), ch340Ports.end());
	return ch343Ports;
}

std::vector<std::string> SerialPort::findMakxdPorts() {
	std::vector<std::string> ports;
	for (const auto& info : findMakxdPortInfo()) {
		ports.push_back(info.port);
	}
	return ports;
}

// Platform abstraction helper implementations
bool SerialPort::platformOpen(const std::string& devicePath) {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	m_connectionIo->config.method = ConnectionMethod::COM;
#ifdef _WIN32
	m_handle = CreateFileA(
	               devicePath.c_str(),
	               GENERIC_READ | GENERIC_WRITE,
	               0,
	               nullptr,
	               OPEN_EXISTING,
	               FILE_ATTRIBUTE_NORMAL,
	               nullptr
	           );
	return m_handle != INVALID_HANDLE_VALUE;
#else
	m_fd = ::open(devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	return m_fd >= 0;
#endif
}

void SerialPort::platformClose() {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	if (m_connectionIo->config.method == ConnectionMethod::BLE) {
		if (m_connectionIo->config.bleClose) {
			m_connectionIo->config.bleClose();
		}
		return;
	}
	if (m_connectionIo->config.method == ConnectionMethod::UDP) {
#ifdef _WIN32
		if (m_connectionIo->udpSocket != INVALID_SOCKET) {
			closesocket(m_connectionIo->udpSocket);
			m_connectionIo->udpSocket = INVALID_SOCKET;
		}
		if (m_connectionIo->winsockStarted) {
			WSACleanup();
			m_connectionIo->winsockStarted = false;
		}
#else
		if (m_connectionIo->udpSocket >= 0) {
			::close(m_connectionIo->udpSocket);
			m_connectionIo->udpSocket = -1;
		}
#endif
		m_connectionIo->rawTransactions.clear();
		return;
	}
#ifdef _WIN32
	if (m_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(m_handle);
		m_handle = INVALID_HANDLE_VALUE;
	}
#else
	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}
#endif
}

bool SerialPort::platformConfigurePort() {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	if (m_connectionIo->config.method != ConnectionMethod::COM) {
		return true;
	}
#ifdef _WIN32
	m_dcb.DCBlength = sizeof(DCB);

	if (!GetCommState(m_handle, &m_dcb)) {
		return false;
	}

	m_dcb.BaudRate = m_baudRate.load(std::memory_order_relaxed);
	m_dcb.ByteSize = 8;
	m_dcb.Parity = NOPARITY;
	m_dcb.StopBits = ONESTOPBIT;
	m_dcb.fBinary = TRUE;
	m_dcb.fParity = FALSE;
	m_dcb.fOutxCtsFlow = FALSE;
	m_dcb.fOutxDsrFlow = FALSE;
	m_dcb.fDtrControl = DTR_CONTROL_DISABLE;
	m_dcb.fDsrSensitivity = FALSE;
	m_dcb.fTXContinueOnXoff = FALSE;
	m_dcb.fOutX = FALSE;
	m_dcb.fInX = FALSE;
	m_dcb.fErrorChar = FALSE;
	m_dcb.fNull = FALSE;
	m_dcb.fRtsControl = RTS_CONTROL_DISABLE;
	m_dcb.fAbortOnError = FALSE;

	if (!SetCommState(m_handle, &m_dcb)) {
		return false;
	}

	platformUpdateTimeoutsUnlocked();
	return true;
#else
	// Linux implementation using termios
	if (tcgetattr(m_fd, &m_oldTermios) != 0) {
		return false;
	}

	m_newTermios = m_oldTermios;

	// Configure serial port settings to match Windows DCB equivalent
	// Control flags - match Windows DCB settings
	m_newTermios.c_cflag &= ~PARENB;    // No parity (DCB.fParity = FALSE)
	m_newTermios.c_cflag &= ~CSTOPB;    // One stop bit (DCB.StopBits = ONESTOPBIT)
	m_newTermios.c_cflag &= ~CSIZE;     // Clear data size bits
	m_newTermios.c_cflag |= CS8;        // 8 data bits (DCB.ByteSize = 8)
	m_newTermios.c_cflag &= ~CRTSCTS;   // No hardware flow control (DCB.fRtsControl/fOutxCtsFlow = FALSE)
	m_newTermios.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem lines (DCB.fDtrControl = DISABLE)

	// Local flags - raw input processing like Windows binary mode
	m_newTermios.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw input (DCB.fBinary = TRUE equivalent)
	m_newTermios.c_lflag &= ~(ECHOK | ECHONL | IEXTEN);      // Additional echo/processing disable

	// Output flags - raw output like Windows
	m_newTermios.c_oflag &= ~OPOST;     // Raw output (no post-processing)
	m_newTermios.c_oflag &= ~(ONLCR | OCRNL | ONOCR | ONLRET); // No line ending conversions

	// Input flags - match Windows flow control settings
	m_newTermios.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control (DCB.fOutX/fInX = FALSE)
	m_newTermios.c_iflag &= ~(INLCR | ICRNL | IGNCR); // No line ending conversion
	m_newTermios.c_iflag &= ~(ISTRIP | INPCK);        // No parity stripping (DCB.fParity = FALSE)
	m_newTermios.c_iflag &= ~(BRKINT | IGNBRK);       // Break handling like Windows

	// Set timeouts - gaming-optimized to match Windows (10ms equivalent)
	m_newTermios.c_cc[VMIN] = 0;        // Non-blocking read
	m_newTermios.c_cc[VTIME] = 1;       // 0.1 second timeout (minimum granularity)

	// Set baud rate
	speed_t speed;
	switch (m_baudRate.load(std::memory_order_relaxed)) {
	case 9600:
		speed = B9600;
		break;
	case 19200:
		speed = B19200;
		break;
	case 38400:
		speed = B38400;
		break;
	case 57600:
		speed = B57600;
		break;
	case 115200:
		speed = B115200;
		break;
	case 230400:
		speed = B230400;
		break;
	case 460800:
		speed = B460800;
		break;
	case 921600:
		speed = B921600;
		break;
	case 1000000:
		speed = B1000000;
		break;
	case 1152000:
		speed = B1152000;
		break;
	case 1500000:
		speed = B1500000;
		break;
	case 2000000:
		speed = B2000000;
		break;
	case 2500000:
		speed = B2500000;
		break;
	case 3000000:
		speed = B3000000;
		break;
	case 3500000:
		speed = B3500000;
		break;
	case 4000000:
		speed = B4000000;
		break;
	default:
		speed = B115200;
		break;
	}

	cfsetispeed(&m_newTermios, speed);
	cfsetospeed(&m_newTermios, speed);

	if (tcsetattr(m_fd, TCSANOW, &m_newTermios) != 0) {
		return false;
	}

	// Flush any existing data
	tcflush(m_fd, TCIOFLUSH);

	return true;
#endif
}

void SerialPort::platformUpdateTimeouts() {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	platformUpdateTimeoutsUnlocked();
}

void SerialPort::platformUpdateTimeoutsUnlocked() {
	if (m_connectionIo->config.method != ConnectionMethod::COM) {
		return;
	}
#ifdef _WIN32
	// Gaming-optimized timeouts - much faster than original
	m_timeouts.ReadIntervalTimeout = 1;          // 1ms between bytes
	m_timeouts.ReadTotalTimeoutConstant = 10;    // 10ms total read timeout
	m_timeouts.ReadTotalTimeoutMultiplier = 1;   // 1ms per byte
	m_timeouts.WriteTotalTimeoutConstant = 10;   // 10ms write timeout
	m_timeouts.WriteTotalTimeoutMultiplier = 1;  // 1ms per byte

	SetCommTimeouts(m_handle, &m_timeouts);
#else
	// Linux gaming-optimized timeouts - match Windows performance
	if (m_isOpen.load(std::memory_order_acquire) && m_fd >= 0) {
		struct termios currentTermios;
		if (tcgetattr(m_fd, &currentTermios) == 0) {
			// Update timeout settings to match current m_timeout value
			// VTIME is in deciseconds (0.1s units), so convert from ms
			uint8_t vtime = std::min(255, std::max(1, static_cast<int>(m_timeout.load(std::memory_order_relaxed) / 100)));
			currentTermios.c_cc[VTIME] = vtime;
			currentTermios.c_cc[VMIN] = 0;  // Non-blocking
			tcsetattr(m_fd, TCSANOW, &currentTermios);
		}
	}
#endif
}

ssize_t SerialPort::platformWrite(
	const void* data, size_t length, bool responseExpected) {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	if (m_connectionIo->config.method != ConnectionMethod::COM) {
		if (data == nullptr || length == 0u) {
			return -1;
		}
		const auto* input = static_cast<const uint8_t*>(data);
		std::vector<uint8_t> wire;
		if (length >= 5u && input[0] == 0xDEu && input[1] == 0xADu) {
			const size_t applicationOffset = input[4] == 0x00u ? 5u : 4u;
			wire.assign(input + applicationOffset, input + length);
		} else {
			wire.assign(input, input + length);
		}
		if (m_connectionIo->config.method == ConnectionMethod::UDP &&
			m_connectionIo->config.udpMode == UdpWireMode::RAW &&
			wire[0] != 0x03u) {
			std::array<uint8_t, 8> transaction{};
			std::random_device random;
			for (auto& byte : transaction) {
				byte = static_cast<uint8_t>(random());
			}
			if (responseExpected) {
				m_connectionIo->rawTransactions.push_back(transaction);
			}
			std::vector<uint8_t> raw;
			raw.reserve(9u + wire.size());
			raw.push_back(0x55u);
			raw.insert(raw.end(), transaction.begin(), transaction.end());
			raw.insert(raw.end(), wire.begin(), wire.end());
			wire.swap(raw);
		}
		bool success = false;
		if (m_connectionIo->config.method == ConnectionMethod::UDP) {
			success = ::send(
				m_connectionIo->udpSocket,
				reinterpret_cast<const char*>(wire.data()),
				static_cast<int>(wire.size()), 0) ==
				static_cast<int>(wire.size());
		} else {
			success = wire.size() <= 64u &&
				m_connectionIo->config.bleWrite(wire);
		}
		return success ? static_cast<ssize_t>(length) : -1;
	}
#ifdef _WIN32
	DWORD bytesWritten = 0;
	bool success = WriteFile(m_handle, data, static_cast<DWORD>(length), &bytesWritten, nullptr);
	return success ? static_cast<ssize_t>(bytesWritten) : -1;
#else
	if (length == 0) {
		return 0;
	}
	if (m_fd < 0 || data == nullptr) {
		return -1;
	}

	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	size_t totalWritten = 0;

	while (totalWritten < length) {
		ssize_t written = ::write(m_fd, bytes + totalWritten, length - totalWritten);
		if (written > 0) {
			totalWritten += static_cast<size_t>(written);
			continue;
		}

		if (written == 0) {
			break;
		}

		if (errno == EINTR) {
			continue;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			struct pollfd pfd {};
			pfd.fd = m_fd;
			pfd.events = POLLOUT;

			const uint32_t clampedTimeout =
			    std::min<uint32_t>(m_timeout.load(std::memory_order_relaxed), static_cast<uint32_t>((std::numeric_limits<int>::max)()));
			int pollResult = 0;
			do {
				pollResult = poll(&pfd, 1, static_cast<int>(clampedTimeout));
			} while (pollResult < 0 && errno == EINTR);

			if (pollResult > 0 && (pfd.revents & POLLOUT)) {
				continue;
			}

			if (pollResult == 0) {
				errno = ETIMEDOUT;
			}
			else if (pollResult > 0) {
				errno = EIO;
			}
			break;
		}

		break;
	}

	if (totalWritten == 0) {
		return -1;
	}

	return static_cast<ssize_t>(totalWritten);
#endif
}

ssize_t SerialPort::platformRead(void* buffer, size_t maxBytes) {
	if (m_connectionIo->config.method != ConnectionMethod::COM) {
		std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
		if (buffer == nullptr || maxBytes == 0u) {
			return -1;
		}
		std::array<uint8_t, 512> incoming{};
		size_t incomingBytes = 0u;
		if (m_connectionIo->config.method == ConnectionMethod::UDP) {
			const int received = ::recv(
				m_connectionIo->udpSocket,
				reinterpret_cast<char*>(incoming.data()),
				static_cast<int>(incoming.size()), 0);
			if (received <= 0) {
				return 0;
			}
			incomingBytes = static_cast<size_t>(received);
		} else {
			incomingBytes = m_connectionIo->config.bleRead(incoming);
			if (incomingBytes == 0u || incomingBytes > 64u) {
				return 0;
			}
		}
		size_t offset = 0u;
		if (m_connectionIo->config.method == ConnectionMethod::UDP &&
			m_connectionIo->config.udpMode == UdpWireMode::RAW &&
			incoming[0] == 0x55u) {
			if (incomingBytes < 10u) {
				return 0;
			}
			const auto transaction = std::find_if(
				m_connectionIo->rawTransactions.begin(),
				m_connectionIo->rawTransactions.end(),
				[&](const std::array<uint8_t, 8>& expected) {
					return std::equal(
						expected.begin(), expected.end(),
						incoming.begin() + 1u);
				});
			if (transaction == m_connectionIo->rawTransactions.end()) {
				return 0;
			}
			m_connectionIo->rawTransactions.erase(transaction);
			offset = 9u;
		}
		const size_t bodyBytes = incomingBytes - offset;
		std::vector<uint8_t> normalized;
		if (bodyBytes != 0u &&
			(incoming[offset] == 0x00u || incoming[offset] == 0x03u)) {
			const size_t payloadBytes =
				incoming[offset] == 0x03u ? bodyBytes - 1u : bodyBytes;
			normalized.reserve(5u + payloadBytes);
			normalized.push_back(0xDEu);
			normalized.push_back(0xADu);
			normalized.push_back(static_cast<uint8_t>(payloadBytes));
			normalized.push_back(static_cast<uint8_t>(payloadBytes >> 8u));
			if (incoming[offset] == 0x00u) {
				normalized.push_back(0x00u);
			}
			normalized.insert(
				normalized.end(), incoming.begin() + offset, incoming.begin() + incomingBytes);
		} else {
			normalized.assign(
				incoming.begin() + offset, incoming.begin() + incomingBytes);
		}
		const size_t copyBytes = (std::min)(maxBytes, normalized.size());
		std::memcpy(buffer, normalized.data(), copyBytes);
		return static_cast<ssize_t>(copyBytes);
	}
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
#ifdef _WIN32
	DWORD bytesRead = 0;
	bool success = ReadFile(m_handle, buffer, static_cast<DWORD>(maxBytes), &bytesRead, nullptr);
	return success ? static_cast<ssize_t>(bytesRead) : -1;
#else
	return ::read(m_fd, buffer, maxBytes);
#endif
}

size_t SerialPort::platformBytesAvailable() const {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	if (m_connectionIo->config.method == ConnectionMethod::BLE) {
		return 1u;
	}
	if (m_connectionIo->config.method == ConnectionMethod::UDP) {
#ifdef _WIN32
		u_long bytesAvailable = 0;
		return ioctlsocket(
			m_connectionIo->udpSocket, FIONREAD, &bytesAvailable) == 0
				? static_cast<size_t>(bytesAvailable)
				: 0u;
#else
		int bytesAvailable = 0;
		return ioctl(
			m_connectionIo->udpSocket, FIONREAD, &bytesAvailable) == 0
				? static_cast<size_t>(bytesAvailable)
				: 0u;
#endif
	}
#ifdef _WIN32
	COMSTAT comStat;
	DWORD errors;
	if (ClearCommError(m_handle, &errors, &comStat)) {
		return comStat.cbInQue;
	}
	return 0;
#else
	int bytesAvailable = 0;
	if (ioctl(m_fd, FIONREAD, &bytesAvailable) >= 0) {
		return static_cast<size_t>(bytesAvailable);
	}
	return 0;
#endif
}

bool SerialPort::platformFlush() {
	std::lock_guard<std::mutex> nativeLock(m_nativeHandleMutex);
	if (m_connectionIo->config.method != ConnectionMethod::COM) {
		return true;
	}
#ifdef _WIN32
	return FlushFileBuffers(m_handle) != 0;
#else
	return tcdrain(m_fd) == 0;
#endif
}

std::string SerialPort::getLastPlatformError() {
#ifdef _WIN32
	DWORD error = GetLastError();
	return "Windows error: " + std::to_string(error);
#else
	return "errno: " + std::to_string(errno);
#endif
}

} // namespace makxd
