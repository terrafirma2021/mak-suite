#include "makxd_net.h"

#ifndef _WIN32
#error makxd-net-cpp currently supports Windows only
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t CMD_CONNECT = 0xAF3C2828u;
constexpr std::uint32_t CMD_MOUSE_MOVE = 0xAEDE7345u;
constexpr std::uint32_t CMD_MOUSE_LEFT = 0x9823AE8Du;
constexpr std::uint32_t CMD_MOUSE_MIDDLE = 0x97A3AE8Du;
constexpr std::uint32_t CMD_MOUSE_RIGHT = 0x238D8212u;
constexpr std::uint32_t CMD_MOUSE_WHEEL = 0xFFEEAD38u;
constexpr std::uint32_t CMD_KEYBOARD_ALL = 0x123C2C2Fu;
constexpr std::uint32_t CMD_REBOOT = 0xAA8855AAu;
constexpr std::uint32_t CMD_MONITOR = 0x27388020u;

constexpr std::size_t HEADER_SIZE = 16u;
constexpr std::size_t MOUSE_PAYLOAD_SIZE = 56u;
constexpr std::size_t KEYBOARD_PAYLOAD_SIZE = 12u;
constexpr std::size_t MONITOR_PACKET_SIZE = 20u;
constexpr DWORD RECEIVE_TIMEOUT_MS = 2000u;

struct MonitorSnapshot {
    std::uint8_t mouse_buttons = 0u;
    std::uint8_t modifiers = 0u;
    std::array<std::uint8_t, 10> keys{};
};

struct ClientState {
    std::mutex command_mutex;
    SOCKET command_socket = INVALID_SOCKET;
    sockaddr_in bridge{};
    bool winsock_started = false;
    std::uint32_t mac = 0u;
    std::uint32_t sequence = 0u;
    std::uint32_t random_state = 0u;
    std::uint32_t mouse_buttons = 0u;
    std::uint8_t keyboard_modifiers = 0u;
    std::array<std::uint8_t, 10> keyboard_keys{};

    std::mutex monitor_mutex;
    SOCKET monitor_socket = INVALID_SOCKET;
    bool monitor_enabled = false;
    MonitorSnapshot monitor{};
    std::jthread monitor_thread;
};

ClientState g_state;

void write_u32_le(std::uint8_t* destination, std::uint32_t value)
{
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8u);
    destination[2] = static_cast<std::uint8_t>(value >> 16u);
    destination[3] = static_cast<std::uint8_t>(value >> 24u);
}

std::uint32_t read_u32_le(const std::uint8_t* source)
{
    return static_cast<std::uint32_t>(source[0]) |
        (static_cast<std::uint32_t>(source[1]) << 8u) |
        (static_cast<std::uint32_t>(source[2]) << 16u) |
        (static_cast<std::uint32_t>(source[3]) << 24u);
}

bool endpoint_matches(const sockaddr_in& left, const sockaddr_in& right)
{
    return left.sin_family == right.sin_family &&
        left.sin_port == right.sin_port &&
        left.sin_addr.s_addr == right.sin_addr.s_addr;
}

bool parse_port(std::string_view text, std::uint16_t& result)
{
    if (text.empty()) {
        return false;
    }
    unsigned int value = 0u;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value == 0u || value > 65535u) {
        return false;
    }
    result = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_mac(std::string_view text, std::uint32_t& result)
{
    if (text.size() != 8u) {
        return false;
    }
    std::uint32_t value = 0u;
    for (const char character : text) {
        std::uint8_t nibble = 0u;
        if (character >= '0' && character <= '9') {
            nibble = static_cast<std::uint8_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = static_cast<std::uint8_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            nibble = static_cast<std::uint8_t>(character - 'A' + 10);
        } else {
            return false;
        }
        value = (value << 4u) | nibble;
    }
    result = value;
    return true;
}

std::uint32_t next_random_locked()
{
    if (g_state.random_state == 0u) {
        const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        g_state.random_state = static_cast<std::uint32_t>(ticks) ^
            static_cast<std::uint32_t>(GetCurrentProcessId());
        if (g_state.random_state == 0u) {
            g_state.random_state = 0x6D2B79F5u;
        }
    }
    std::uint32_t value = g_state.random_state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    g_state.random_state = value;
    return value;
}

void encode_header(std::span<std::uint8_t> packet,
                   std::uint32_t random,
                   std::uint32_t sequence,
                   std::uint32_t command)
{
    write_u32_le(packet.data(), g_state.mac);
    write_u32_le(packet.data() + 4u, random);
    write_u32_le(packet.data() + 8u, sequence);
    write_u32_le(packet.data() + 12u, command);
}

int transact_locked(std::uint32_t command,
                    std::uint32_t random,
                    std::uint32_t sequence,
                    std::span<const std::uint8_t> payload)
{
    if (g_state.command_socket == INVALID_SOCKET) {
        return KMNET_ERR_CREATE_SOCKET;
    }

    std::vector<std::uint8_t> packet(HEADER_SIZE + payload.size(), 0u);
    encode_header(packet, random, sequence, command);
    if (!payload.empty()) {
        std::copy(payload.begin(), payload.end(), packet.begin() + HEADER_SIZE);
    }

    const int sent = sendto(g_state.command_socket,
                            reinterpret_cast<const char*>(packet.data()),
                            static_cast<int>(packet.size()),
                            0,
                            reinterpret_cast<const sockaddr*>(&g_state.bridge),
                            sizeof(g_state.bridge));
    if (sent != static_cast<int>(packet.size())) {
        return KMNET_ERR_SEND;
    }

    std::array<std::uint8_t, 64> response{};
    sockaddr_in source{};
    int source_size = sizeof(source);
    const int received = recvfrom(g_state.command_socket,
                                  reinterpret_cast<char*>(response.data()),
                                  static_cast<int>(response.size()),
                                  0,
                                  reinterpret_cast<sockaddr*>(&source),
                                  &source_size);
    if (received < 0) {
        return KMNET_ERR_RECEIVE_TIMEOUT;
    }
    if (received < static_cast<int>(HEADER_SIZE) || !endpoint_matches(source, g_state.bridge)) {
        return KMNET_ERR_COMMAND;
    }

    const std::uint32_t response_sequence = read_u32_le(response.data() + 8u);
    const std::uint32_t response_command = read_u32_le(response.data() + 12u);
    if (response_command != command) {
        return KMNET_ERR_COMMAND;
    }
    if (response_sequence != sequence) {
        return KMNET_ERR_SEQUENCE;
    }
    return KMNET_SUCCESS;
}

int send_command_locked(std::uint32_t command,
                        std::span<const std::uint8_t> payload,
                        std::uint32_t random)
{
    ++g_state.sequence;
    return transact_locked(command, random, g_state.sequence, payload);
}

int send_command_locked(std::uint32_t command, std::span<const std::uint8_t> payload)
{
    return send_command_locked(command, payload, next_random_locked());
}

std::array<std::uint8_t, MOUSE_PAYLOAD_SIZE> make_mouse_payload(
    std::uint32_t buttons, int x, int y, int wheel)
{
    std::array<std::uint8_t, MOUSE_PAYLOAD_SIZE> payload{};
    write_u32_le(payload.data(), buttons);
    write_u32_le(payload.data() + 4u, static_cast<std::uint32_t>(static_cast<std::int32_t>(x)));
    write_u32_le(payload.data() + 8u, static_cast<std::uint32_t>(static_cast<std::int32_t>(y)));
    write_u32_le(payload.data() + 12u, static_cast<std::uint32_t>(static_cast<std::int32_t>(wheel)));
    return payload;
}

std::array<std::uint8_t, KEYBOARD_PAYLOAD_SIZE> make_keyboard_payload(
    std::uint8_t modifiers, const std::array<std::uint8_t, 10>& keys)
{
    std::array<std::uint8_t, KEYBOARD_PAYLOAD_SIZE> payload{};
    payload[0] = modifiers;
    std::copy(keys.begin(), keys.end(), payload.begin() + 2u);
    return payload;
}

bool is_i16(int value)
{
    return value >= (std::numeric_limits<std::int16_t>::min)() &&
        value <= (std::numeric_limits<std::int16_t>::max)();
}

void stop_monitor_local()
{
    std::jthread thread;
    SOCKET socket_to_close = INVALID_SOCKET;
    {
        std::scoped_lock lock(g_state.monitor_mutex);
        g_state.monitor_enabled = false;
        g_state.monitor = {};
        socket_to_close = g_state.monitor_socket;
        g_state.monitor_socket = INVALID_SOCKET;
        if (g_state.monitor_thread.joinable()) {
            g_state.monitor_thread.request_stop();
            thread = std::move(g_state.monitor_thread);
        }
    }
    if (socket_to_close != INVALID_SOCKET) {
        closesocket(socket_to_close);
    }
    if (thread.joinable()) {
        thread.join();
    }
}

bool monitor_is_enabled()
{
    std::scoped_lock lock(g_state.monitor_mutex);
    return g_state.monitor_enabled;
}

void close_local(bool notify_monitor)
{
    if (notify_monitor && monitor_is_enabled()) {
        std::scoped_lock command_lock(g_state.command_mutex);
        if (g_state.command_socket != INVALID_SOCKET) {
            (void)send_command_locked(CMD_MONITOR, {}, 0u);
        }
    }
    stop_monitor_local();

    std::scoped_lock command_lock(g_state.command_mutex);
    if (g_state.command_socket != INVALID_SOCKET) {
        closesocket(g_state.command_socket);
        g_state.command_socket = INVALID_SOCKET;
    }
    g_state.bridge = {};
    g_state.mac = 0u;
    g_state.sequence = 0u;
    g_state.mouse_buttons = 0u;
    g_state.keyboard_modifiers = 0u;
    g_state.keyboard_keys.fill(0u);
    if (g_state.winsock_started) {
        WSACleanup();
        g_state.winsock_started = false;
    }
}

int set_mouse_button(std::uint32_t command, std::uint32_t bit, int is_down)
{
    if (is_down != 0 && is_down != 1) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.command_mutex);
    const std::uint32_t candidate = is_down != 0 ?
        (g_state.mouse_buttons | bit) : (g_state.mouse_buttons & ~bit);
    const auto payload = make_mouse_payload(candidate, 0, 0, 0);
    const int result = send_command_locked(command, payload);
    if (result == KMNET_SUCCESS) {
        g_state.mouse_buttons = candidate;
    }
    return result;
}

int monitor_mouse_button(std::uint8_t bit)
{
    std::scoped_lock lock(g_state.monitor_mutex);
    if (!g_state.monitor_enabled) {
        return -1;
    }
    return (g_state.monitor.mouse_buttons & bit) != 0u ? 1 : 0;
}

} // namespace

int kmNet_init(const char* ip, const char* port, const char* mac)
{
    if (ip == nullptr || port == nullptr || mac == nullptr) {
        return KMNET_ERR_ARGUMENT;
    }

    close_local(false);

    std::uint16_t parsed_port = 0u;
    std::uint32_t parsed_mac = 0u;
    if (!parse_port(port, parsed_port) || !parse_mac(mac, parsed_mac)) {
        return KMNET_ERR_ARGUMENT;
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return KMNET_ERR_CREATE_SOCKET;
    }
    g_state.winsock_started = true;
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        close_local(false);
        return KMNET_ERR_SOCKET_VERSION;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(parsed_port);
    if (InetPtonA(AF_INET, ip, &address.sin_addr) != 1) {
        close_local(false);
        return KMNET_ERR_ARGUMENT;
    }

    const SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        close_local(false);
        return KMNET_ERR_CREATE_SOCKET;
    }
    const DWORD timeout = RECEIVE_TIMEOUT_MS;
    if (setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR) {
        closesocket(socket_handle);
        close_local(false);
        return KMNET_ERR_CREATE_SOCKET;
    }

    int result = KMNET_ERR_CREATE_SOCKET;
    {
        std::scoped_lock lock(g_state.command_mutex);
        g_state.command_socket = socket_handle;
        g_state.bridge = address;
        g_state.mac = parsed_mac;
        g_state.sequence = 0u;
        g_state.mouse_buttons = 0u;
        g_state.keyboard_modifiers = 0u;
        g_state.keyboard_keys.fill(0u);
        result = transact_locked(CMD_CONNECT, next_random_locked(), 0u, {});
    }
    if (result != KMNET_SUCCESS) {
        close_local(false);
    }
    return result;
}

void kmNet_close()
{
    close_local(true);
}

int kmNet_mouse_move(int x, int y)
{
    if (!is_i16(x) || !is_i16(y)) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.command_mutex);
    const auto payload = make_mouse_payload(g_state.mouse_buttons, x, y, 0);
    return send_command_locked(CMD_MOUSE_MOVE, payload);
}

int kmNet_mouse_left(int is_down)
{
    return set_mouse_button(CMD_MOUSE_LEFT, 0x01u, is_down);
}

int kmNet_mouse_middle(int is_down)
{
    return set_mouse_button(CMD_MOUSE_MIDDLE, 0x04u, is_down);
}

int kmNet_mouse_right(int is_down)
{
    return set_mouse_button(CMD_MOUSE_RIGHT, 0x02u, is_down);
}

int kmNet_mouse_wheel(int wheel)
{
    if (!is_i16(wheel)) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.command_mutex);
    const auto payload = make_mouse_payload(g_state.mouse_buttons, 0, 0, wheel);
    return send_command_locked(CMD_MOUSE_WHEEL, payload);
}

int kmNet_mouse_all(std::uint32_t buttons, int x, int y, int wheel)
{
    if ((buttons & ~0x1Fu) != 0u || !is_i16(x) || !is_i16(y) || !is_i16(wheel)) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.command_mutex);
    const auto payload = make_mouse_payload(buttons, x, y, wheel);
    const int result = send_command_locked(CMD_MOUSE_WHEEL, payload);
    if (result == KMNET_SUCCESS) {
        g_state.mouse_buttons = buttons;
    }
    return result;
}

int kmNet_keydown(int hid_usage)
{
    if (hid_usage <= 0 || hid_usage > 0xFF) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.command_mutex);
    std::uint8_t modifiers = g_state.keyboard_modifiers;
    auto keys = g_state.keyboard_keys;
    const auto hid = static_cast<std::uint8_t>(hid_usage);

    if (hid >= 0xE0u && hid <= 0xE7u) {
        modifiers = static_cast<std::uint8_t>(modifiers | (1u << (hid - 0xE0u)));
    } else if (std::find(keys.begin(), keys.end(), hid) == keys.end()) {
        const auto empty = std::find(keys.begin(), keys.end(), 0u);
        if (empty != keys.end()) {
            *empty = hid;
        } else {
            std::move(keys.begin() + 1, keys.end(), keys.begin());
            keys.back() = hid;
        }
    }

    const auto payload = make_keyboard_payload(modifiers, keys);
    const int result = send_command_locked(CMD_KEYBOARD_ALL, payload);
    if (result == KMNET_SUCCESS) {
        g_state.keyboard_modifiers = modifiers;
        g_state.keyboard_keys = keys;
    }
    return result;
}

int kmNet_keyup(int hid_usage)
{
    if (hid_usage <= 0 || hid_usage > 0xFF) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.command_mutex);
    std::uint8_t modifiers = g_state.keyboard_modifiers;
    auto keys = g_state.keyboard_keys;
    const auto hid = static_cast<std::uint8_t>(hid_usage);

    if (hid >= 0xE0u && hid <= 0xE7u) {
        modifiers = static_cast<std::uint8_t>(modifiers & ~(1u << (hid - 0xE0u)));
    } else {
        const auto found = std::find(keys.begin(), keys.end(), hid);
        if (found != keys.end()) {
            std::move(found + 1, keys.end(), found);
            keys.back() = 0u;
        }
    }

    const auto payload = make_keyboard_payload(modifiers, keys);
    const int result = send_command_locked(CMD_KEYBOARD_ALL, payload);
    if (result == KMNET_SUCCESS) {
        g_state.keyboard_modifiers = modifiers;
        g_state.keyboard_keys = keys;
    }
    return result;
}

int kmNet_monitor(int port)
{
    if (port < 0 || port > 65535) {
        return KMNET_ERR_ARGUMENT;
    }

    if (port == 0) {
        int result = KMNET_ERR_CREATE_SOCKET;
        {
            std::scoped_lock lock(g_state.command_mutex);
            result = send_command_locked(CMD_MONITOR, {}, 0u);
        }
        stop_monitor_local();
        return result;
    }

    stop_monitor_local();

    const SOCKET monitor_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (monitor_socket == INVALID_SOCKET) {
        return KMNET_ERR_CREATE_SOCKET;
    }
    sockaddr_in listen_address{};
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(monitor_socket, reinterpret_cast<const sockaddr*>(&listen_address),
             sizeof(listen_address)) == SOCKET_ERROR) {
        closesocket(monitor_socket);
        return KMNET_ERR_CREATE_SOCKET;
    }
    const DWORD timeout = 250u;
    (void)setsockopt(monitor_socket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::uint32_t bridge_ip = 0u;
    int result = KMNET_ERR_CREATE_SOCKET;
    {
        std::scoped_lock lock(g_state.command_mutex);
        bridge_ip = g_state.bridge.sin_addr.s_addr;
        const std::uint32_t random = 0xAA550000u | static_cast<std::uint16_t>(port);
        result = send_command_locked(CMD_MONITOR, {}, random);
    }
    if (result != KMNET_SUCCESS) {
        closesocket(monitor_socket);
        return result;
    }

    {
        std::scoped_lock lock(g_state.monitor_mutex);
        g_state.monitor_socket = monitor_socket;
        g_state.monitor_enabled = true;
        g_state.monitor = {};
        g_state.monitor_thread = std::jthread([monitor_socket, bridge_ip](std::stop_token stop) {
            std::array<std::uint8_t, 256> packet{};
            while (!stop.stop_requested()) {
                sockaddr_in source{};
                int source_size = sizeof(source);
                const int received = recvfrom(monitor_socket,
                                              reinterpret_cast<char*>(packet.data()),
                                              static_cast<int>(packet.size()),
                                              0,
                                              reinterpret_cast<sockaddr*>(&source),
                                              &source_size);
                if (received == SOCKET_ERROR) {
                    const int error = WSAGetLastError();
                    if (stop.stop_requested() || error == WSAENOTSOCK) {
                        break;
                    }
                    if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                        continue;
                    }
                    continue;
                }
                if (received != static_cast<int>(MONITOR_PACKET_SIZE) ||
                    source.sin_addr.s_addr != bridge_ip) {
                    continue;
                }

                std::scoped_lock snapshot_lock(g_state.monitor_mutex);
                if (!g_state.monitor_enabled) {
                    continue;
                }
                g_state.monitor.mouse_buttons = packet[1];
                g_state.monitor.modifiers = packet[9];
                std::copy_n(packet.begin() + 10u, 10u, g_state.monitor.keys.begin());
            }
        });
    }
    return KMNET_SUCCESS;
}

int kmNet_monitor_mouse_left()
{
    return monitor_mouse_button(0x01u);
}

int kmNet_monitor_mouse_middle()
{
    return monitor_mouse_button(0x04u);
}

int kmNet_monitor_mouse_right()
{
    return monitor_mouse_button(0x02u);
}

int kmNet_monitor_mouse_side1()
{
    return monitor_mouse_button(0x08u);
}

int kmNet_monitor_mouse_side2()
{
    return monitor_mouse_button(0x10u);
}

int kmNet_monitor_keyboard(int hid_usage)
{
    if (hid_usage <= 0 || hid_usage > 0xFF) {
        return KMNET_ERR_ARGUMENT;
    }
    std::scoped_lock lock(g_state.monitor_mutex);
    if (!g_state.monitor_enabled) {
        return -1;
    }
    const auto hid = static_cast<std::uint8_t>(hid_usage);
    if (hid >= 0xE0u && hid <= 0xE7u) {
        return (g_state.monitor.modifiers & (1u << (hid - 0xE0u))) != 0u ? 1 : 0;
    }
    return std::find(g_state.monitor.keys.begin(), g_state.monitor.keys.end(), hid) !=
        g_state.monitor.keys.end() ? 1 : 0;
}

int kmNet_reboot()
{
    int result = KMNET_ERR_CREATE_SOCKET;
    {
        std::scoped_lock lock(g_state.command_mutex);
        result = send_command_locked(CMD_REBOOT, {});
    }
    close_local(false);
    return result;
}
