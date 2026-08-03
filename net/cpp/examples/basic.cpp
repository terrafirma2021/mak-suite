#include <makxd_net.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    // kmNetMakxdBridge.exe must already be running on UDP port 8338.
    const int connected = kmNet_init("127.0.0.1", "8338", "12345678");
    if (connected != KMNET_SUCCESS) {
        std::cerr << "Bridge connection failed: " << connected << '\n';
        return 1;
    }

    if (kmNet_mouse_move(20, 0) != KMNET_SUCCESS ||
        kmNet_mouse_left(1) != KMNET_SUCCESS ||
        kmNet_mouse_left(0) != KMNET_SUCCESS) {
        std::cerr << "Mouse command failed\n";
        kmNet_close();
        return 1;
    }

    constexpr int hid_a = 0x04;
    if (kmNet_keydown(hid_a) == KMNET_SUCCESS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)kmNet_keyup(hid_a);
    }

    kmNet_close();
    return 0;
}
