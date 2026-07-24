#include <winsock2.h>
#include <ws2tcpip.h>

#include <makxd_net.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t CMD_CONNECT = 0xAF3C2828u;
constexpr std::uint32_t CMD_MOUSE_MOVE = 0xAEDE7345u;
constexpr std::uint32_t CMD_MOUSE_LEFT = 0x9823AE8Du;
constexpr std::uint32_t CMD_KEYBOARD_ALL = 0x123C2C2Fu;
constexpr std::uint32_t CMD_REBOOT = 0xAA8855AAu;
constexpr std::uint32_t CMD_MONITOR = 0x27388020u;

std::uint32_t read_u32_le(const std::uint8_t* source)
{
    return static_cast<std::uint32_t>(source[0]) |
        (static_cast<std::uint32_t>(source[1]) << 8u) |
        (static_cast<std::uint32_t>(source[2]) << 16u) |
        (static_cast<std::uint32_t>(source[3]) << 24u);
}

std::int32_t read_i32_le(const std::uint8_t* source)
{
    return static_cast<std::int32_t>(read_u32_le(source));
}

struct FakeBridge {
    SOCKET socket_handle = INVALID_SOCKET;
    std::uint16_t port = 0u;
    std::jthread worker;
    std::mutex packets_mutex;
    std::vector<std::vector<std::uint8_t>> packets;
    std::atomic<bool> failed = false;

    bool start()
    {
        socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) {
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            closesocket(socket_handle);
            socket_handle = INVALID_SOCKET;
            return false;
        }
        int address_size = sizeof(address);
        if (getsockname(socket_handle, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR) {
            closesocket(socket_handle);
            socket_handle = INVALID_SOCKET;
            return false;
        }
        port = ntohs(address.sin_port);
        const DWORD timeout = 4000u;
        (void)setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        worker = std::jthread([this](std::stop_token stop) {
            std::array<std::uint8_t, 2048> buffer{};
            while (!stop.stop_requested()) {
                sockaddr_in client{};
                int client_size = sizeof(client);
                const int received = recvfrom(socket_handle,
                                              reinterpret_cast<char*>(buffer.data()),
                                              static_cast<int>(buffer.size()),
                                              0,
                                              reinterpret_cast<sockaddr*>(&client),
                                              &client_size);
                if (received == SOCKET_ERROR) {
                    if (!stop.stop_requested()) {
                        failed = true;
                    }
                    break;
                }
                if (received < 16) {
                    failed = true;
                    continue;
                }

                std::vector<std::uint8_t> packet(buffer.begin(), buffer.begin() + received);
                {
                    std::scoped_lock lock(packets_mutex);
                    packets.push_back(packet);
                }

                const int acknowledged = sendto(socket_handle,
                                                 reinterpret_cast<const char*>(buffer.data()),
                                                 16,
                                                 0,
                                                 reinterpret_cast<const sockaddr*>(&client),
                                                 client_size);
                if (acknowledged != 16) {
                    failed = true;
                    break;
                }

                const std::uint32_t command = read_u32_le(buffer.data() + 12u);
                const std::uint32_t random = read_u32_le(buffer.data() + 4u);
                if (command == CMD_MONITOR && random != 0u) {
                    sockaddr_in monitor_client = client;
                    monitor_client.sin_port = htons(static_cast<std::uint16_t>(random));
                    std::array<std::uint8_t, 20> monitor{};
                    monitor[1] = 0x09u; // left + side1
                    monitor[9] = 0x02u; // left shift
                    monitor[10] = 0x04u; // HID A
                    const int sent = sendto(socket_handle,
                                            reinterpret_cast<const char*>(monitor.data()),
                                            static_cast<int>(monitor.size()),
                                            0,
                                            reinterpret_cast<const sockaddr*>(&monitor_client),
                                            sizeof(monitor_client));
                    if (sent != static_cast<int>(monitor.size())) {
                        failed = true;
                    }
                }
                if (command == CMD_REBOOT) {
                    break;
                }
            }
        });
        return true;
    }

    void wait()
    {
        if (worker.joinable()) {
            worker.join();
        }
    }

    void stop()
    {
        if (worker.joinable()) {
            worker.request_stop();
        }
        if (socket_handle != INVALID_SOCKET) {
            closesocket(socket_handle);
            socket_handle = INVALID_SOCKET;
        }
        if (worker.joinable()) {
            worker.join();
        }
    }

    ~FakeBridge()
    {
        stop();
    }
};

std::uint16_t unused_udp_port()
{
    const SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) {
        return 0u;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(probe);
        return 0u;
    }
    int size = sizeof(address);
    if (getsockname(probe, reinterpret_cast<sockaddr*>(&address), &size) == SOCKET_ERROR) {
        closesocket(probe);
        return 0u;
    }
    const std::uint16_t port = ntohs(address.sin_port);
    closesocket(probe);
    return port;
}

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        std::cerr << "FAIL: WSAStartup\n";
        return 1;
    }

    bool ok = true;
    FakeBridge bridge;
    ok &= expect(bridge.start(), "fake Bridge starts");
    const std::uint16_t monitor_port = unused_udp_port();
    ok &= expect(monitor_port != 0u, "monitor port allocated");

    const std::string bridge_port = std::to_string(bridge.port);
    ok &= expect(kmNet_init("127.0.0.1", bridge_port.c_str(), "12345678") == KMNET_SUCCESS,
                 "connect acknowledgement");
    ok &= expect(kmNet_mouse_move(12, -7) == KMNET_SUCCESS, "mouse move acknowledgement");
    ok &= expect(kmNet_mouse_left(1) == KMNET_SUCCESS, "mouse button acknowledgement");
    ok &= expect(kmNet_keydown(0x04) == KMNET_SUCCESS, "keyboard acknowledgement");
    ok &= expect(kmNet_monitor(monitor_port) == KMNET_SUCCESS, "monitor enable acknowledgement");

    const auto monitor_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < monitor_deadline &&
           (kmNet_monitor_mouse_left() != 1 || kmNet_monitor_keyboard(0x04) != 1)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= expect(kmNet_monitor_mouse_left() == 1, "monitor left button");
    ok &= expect(kmNet_monitor_mouse_side1() == 1, "monitor side1 button");
    ok &= expect(kmNet_monitor_keyboard(0x04) == 1, "monitor normal key");
    ok &= expect(kmNet_monitor_keyboard(0xE1) == 1, "monitor modifier key");

    ok &= expect(kmNet_monitor(0) == KMNET_SUCCESS, "monitor disable acknowledgement");
    ok &= expect(kmNet_monitor_mouse_left() == -1, "monitor queries disabled");
    ok &= expect(kmNet_reboot() == KMNET_SUCCESS, "reboot acknowledgement");
    bridge.wait();
    ok &= expect(!bridge.failed.load(), "fake Bridge transport stayed healthy");

    std::vector<std::vector<std::uint8_t>> packets;
    {
        std::scoped_lock lock(bridge.packets_mutex);
        packets = bridge.packets;
    }
    ok &= expect(packets.size() == 7u, "expected command count");
    if (packets.size() == 7u) {
        ok &= expect(packets[0].size() == 16u && read_u32_le(packets[0].data() + 12u) == CMD_CONNECT,
                     "connect packet layout");
        ok &= expect(packets[1].size() == 72u &&
                     read_u32_le(packets[1].data() + 12u) == CMD_MOUSE_MOVE &&
                     read_i32_le(packets[1].data() + 20u) == 12 &&
                     read_i32_le(packets[1].data() + 24u) == -7,
                     "mouse packet layout");
        ok &= expect(read_u32_le(packets[2].data() + 12u) == CMD_MOUSE_LEFT &&
                     read_u32_le(packets[2].data() + 16u) == 1u,
                     "button shadow layout");
        ok &= expect(packets[3].size() == 28u &&
                     read_u32_le(packets[3].data() + 12u) == CMD_KEYBOARD_ALL &&
                     packets[3][18] == 0x04u,
                     "keyboard snapshot layout");
        ok &= expect(read_u32_le(packets[4].data() + 12u) == CMD_MONITOR &&
                     (read_u32_le(packets[4].data() + 4u) >> 16u) == 0xAA55u,
                     "monitor enable layout");
        ok &= expect(read_u32_le(packets[5].data() + 12u) == CMD_MONITOR &&
                     read_u32_le(packets[5].data() + 4u) == 0u,
                     "monitor disable layout");
        ok &= expect(read_u32_le(packets[6].data() + 12u) == CMD_REBOOT,
                     "reboot packet layout");
    }

    kmNet_close();
    bridge.stop();
    WSACleanup();
    return ok ? 0 : 1;
}
