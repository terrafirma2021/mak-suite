#include "makxd.h"
#include <array>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main() {
    std::mutex mutex;
    std::deque<std::vector<uint8_t>> replies;
    std::vector<std::vector<uint8_t>> commands;
    auto connection = makxd::ConnectionConfig::ble(
        {}, [](std::string_view) { return true; },
        [&](std::span<const uint8_t> packet) {
            std::lock_guard lock(mutex);
            if (packet.size() >= 9u &&
                std::equal(packet.begin(), packet.begin() + 4, "MBAT")) {
                const auto count = packet[8];
                std::vector<uint8_t> response{'M', 'B', 'A', 'R', 1u, 11u,
                    packet[6], packet[7], 0u, count, count};
                size_t offset = 9u;
                for (uint8_t i = 0; i < count; ++i) {
                    const size_t bytes = packet[offset++];
                    if (offset + bytes > packet.size()) return false;
                    commands.emplace_back(packet.begin() + offset, packet.begin() + offset + bytes);
                    offset += bytes;
                    response.insert(response.end(), {0u, 0u});
                }
                replies.push_back(std::move(response));
            } else {
                commands.emplace_back(packet.begin(), packet.end());
                if (packet.size() == 1u && packet[0] == 0x02u)
                    replies.push_back({0x02u, 0x43u});
                if (packet.size() == 1u && packet[0] == 0x41u) {
                    replies.push_back({0xde,0xad,4,0,0x53,3,10,255,3});
                    replies.push_back({0x41,1});
                }
                if (packet.size() == 2u && packet[0] == 0x52u) replies.push_back({0x52,1});
            }
            return true;
        },
        [&](std::span<uint8_t> output) -> size_t {
            std::lock_guard lock(mutex);
            if (replies.empty()) return 0;
            auto reply = std::move(replies.front());
            replies.pop_front();
            std::copy(reply.begin(), reply.end(), output.begin());
            return reply.size();
        }, [] {});
    makxd::Device device;
    if (!device.connect(connection)) return 1;
    bool ok = true;
    std::atomic<unsigned> eventCount{0};
    device.setInputCallback([&](const makxd::InputChange& event) {
        if (event.kind == makxd::StreamKind::Controller && event.control == 10 && event.value == 1023) ++eventCount;
    });
    ok &= device.mouseDown(makxd::MouseButton::LEFT);
    ok &= device.mouseUp(makxd::MouseButton::LEFT);
    ok &= device.mouseMove(12, -7);
    ok &= device.mouseWheel(-2);
    ok &= device.keyboardDown(makxd::KeyboardKey{uint8_t{4}});
    ok &= device.keyboardUp(makxd::KeyboardKey{uint8_t{4}});
    ok &= device.keyboardInit();
    ok &= device.controllerStream(true);
    ok &= device.inputStream(makxd::StreamKind::Mouse, true);
    ok &= device.inputStream(makxd::StreamKind::Keyboard, true);
    ok &= device.controllerStream(false);
    ok &= device.setControllerState(makxd::ControllerState{});
    ok &= device.keyboardPress(makxd::KeyboardKey{uint8_t{4}}, 10, 5);
    std::vector<std::vector<uint8_t>> expected{
        {0x02}, {0x11, 1}, {0x11, 0}, {0x18, 12, 0, 249, 255},
        {0x19, 254, 255}, {0x20, 4}, {0x21, 4}, {0x22},
        {0x41, 1}, {0x52, 1, 1}, {0x52, 2, 1}, {0x41, 0},
        {0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0x23, 4, 10, 0, 0, 0, 5, 0, 0, 0},
    };
    // Allow queued writes to reach the adapter before disconnecting.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        { std::lock_guard lock(mutex); if (commands.size() >= expected.size()) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ok &= device.controllerStream() == true;
    ok &= device.inputStream(makxd::StreamKind::Keyboard) == true;
    ok &= eventCount.load() == 1;
    expected.insert(expected.end(), {{0x41}, {0x52, 2}});
    makxd::StreamFrameDecoder decoder;
    const std::array<uint8_t, 9> trigger{0xde,0xad,4,0,0x53,3,11,0,2};
    for (auto byte : trigger) decoder.feed(std::span<const uint8_t>(&byte, 1));
    auto frame = decoder.next();
    ok &= frame && makxd::decode_input_change(*frame)->value == 512;
    ok &= !makxd::decode_input_change({0x53,{3,10,0,4}});
    ok &= !makxd::decode_input_change({0x53,{3,12,1}});
    device.disconnect();
    if (!ok || commands != expected) {
        std::cerr << "Input command records differ from the firmware contract; count="
                  << commands.size() << "\n";
        return 2;
    }
    return 0;
}
