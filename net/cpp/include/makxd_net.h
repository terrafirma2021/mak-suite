#pragma once

#include <cstdint>

// Lightweight C++ client for kmNetMakxdBridge.exe.
// This API talks UDP to the Bridge. It does not open a MAKXD COM port.

enum kmNet_result : int {
    KMNET_SUCCESS = 0,
    KMNET_ERR_CREATE_SOCKET = -9000,
    KMNET_ERR_SOCKET_VERSION = -8999,
    KMNET_ERR_SEND = -8998,
    KMNET_ERR_RECEIVE_TIMEOUT = -8997,
    KMNET_ERR_COMMAND = -8996,
    KMNET_ERR_SEQUENCE = -8995,
    KMNET_ERR_ARGUMENT = -8994,
};

// ip is an IPv4 address, port is the Bridge UDP command port, and mac must be
// exactly eight hexadecimal characters. The Bridge must already be running.
[[nodiscard]] int kmNet_init(const char* ip, const char* port, const char* mac);

// Stops local monitoring and closes this client's sockets. If monitoring is
// active, the client first makes a best-effort request to disable it.
void kmNet_close();

[[nodiscard]] int kmNet_mouse_move(int x, int y);
[[nodiscard]] int kmNet_mouse_left(int is_down);
[[nodiscard]] int kmNet_mouse_middle(int is_down);
[[nodiscard]] int kmNet_mouse_right(int is_down);
[[nodiscard]] int kmNet_mouse_wheel(int wheel);
[[nodiscard]] int kmNet_mouse_all(std::uint32_t buttons, int x, int y, int wheel);

// Keys are unsigned USB HID keyboard usages. Modifier usages are 0xE0..0xE7.
[[nodiscard]] int kmNet_keydown(int hid_usage);
[[nodiscard]] int kmNet_keyup(int hid_usage);

// port == 0 disables monitoring. A non-zero port starts the Bridge's combined
// mouse/keyboard stream and binds a second local UDP socket on that port.
[[nodiscard]] int kmNet_monitor(int port);
[[nodiscard]] int kmNet_monitor_mouse_left();
[[nodiscard]] int kmNet_monitor_mouse_middle();
[[nodiscard]] int kmNet_monitor_mouse_right();
[[nodiscard]] int kmNet_monitor_mouse_side1();
[[nodiscard]] int kmNet_monitor_mouse_side2();
[[nodiscard]] int kmNet_monitor_keyboard(int hid_usage);

// Resets the Bridge's KM Client session. The local command socket is closed;
// call kmNet_init() before sending another command.
[[nodiscard]] int kmNet_reboot();
