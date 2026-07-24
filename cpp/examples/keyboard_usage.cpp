#include "makxd.h"

#include <cstdint>
#include <string>

int main() {
    makxd::Device device;

    auto batch = device.createBatch();
    batch.keyboardDown("ctrl")
        .keyboardPress(std::uint8_t{4}, 50, 10)
        .keyboardUp(std::string{"ctrl"})
        .keyboardString("Hello")
        .keyboardInit()
        .keyboardMask(std::uint8_t{4}, true)
        .keyboardRemap(std::string{"a"}, std::string{"b"});

    return 0;
}
