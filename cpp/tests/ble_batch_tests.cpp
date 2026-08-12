#include "makxd.h"
#include "serialport.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <iostream>
#include <latch>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace {
struct FakeBle {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::vector<uint8_t>> notifications;
    size_t largestBatch{};

    bool write(std::span<const uint8_t> packet) {
        std::vector<uint8_t> notification;
        if (packet.size() >= 9u &&
            std::equal(packet.begin(), packet.begin() + 4, "MBAT")) {
            const uint8_t count = packet[8];
            largestBatch = (std::max)(largestBatch, static_cast<size_t>(count));
            notification = {'M', 'B', 'A', 'R', 1u, 11u,
                packet[6], packet[7], 0u, count, count};
            size_t offset = 9u;
            for (uint8_t index = 0u; index < count; index++) {
                const size_t bytes = packet[offset++];
                const uint8_t opcode = packet[offset];
                offset += bytes;
                notification.insert(notification.end(),
                    {0u, 5u, opcode, 1u, 0u, 0u, 0u});
            }
        } else if (!packet.empty()) {
            notification = {packet[0], 1u, 0u, 0u, 0u};
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            notifications.push_back(std::move(notification));
        }
        ready.notify_one();
        return true;
    }

    size_t read(std::span<uint8_t> output) {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait_for(lock, std::chrono::milliseconds(10), [&] {
            return !notifications.empty();
        });
        if (notifications.empty()) {
            return 0u;
        }
        auto packet = std::move(notifications.front());
        notifications.pop_front();
        const size_t count = (std::min)(output.size(), packet.size());
        std::copy_n(packet.begin(), count, output.begin());
        return count;
    }
};
}

int main() {
    FakeBle fake;
    makxd::SerialPort port;
    auto config = makxd::ConnectionConfig::ble(
        {},
        [](std::string_view) { return true; },
        [&](std::span<const uint8_t> bytes) { return fake.write(bytes); },
        [&](std::span<uint8_t> bytes) { return fake.read(bytes); },
        [] {});
    if (!port.open(config)) {
        return 1;
    }
    auto initial = port.sendTrackedMakApi(
        makxd::ApiOpcode::FIRMWARE_VERSION, {},
        std::chrono::milliseconds(1000));
    if (initial.wait_for(std::chrono::seconds(2)) !=
        std::future_status::ready) {
        std::cerr << "initial direct response timed out; notifications="
                  << fake.notifications.size() << "\n";
        return 4;
    }
    (void)initial.get();
    std::vector<std::future<std::string>> replies;
    std::vector<std::jthread> callers;
    std::mutex repliesMutex;
    std::latch start(33);
    for (size_t index = 0u; index < 32u; index++) {
        callers.emplace_back([&] {
            start.arrive_and_wait();
            auto reply = port.sendTrackedMakApi(
                makxd::ApiOpcode::FIRMWARE_VERSION, {},
                std::chrono::milliseconds(1000));
            std::lock_guard<std::mutex> lock(repliesMutex);
            replies.push_back(std::move(reply));
        });
    }
    start.arrive_and_wait();
    callers.clear();
    size_t replyIndex = 0u;
    for (auto& reply : replies) {
        if (reply.wait_for(std::chrono::seconds(2)) !=
            std::future_status::ready) {
            const size_t readyCount = static_cast<size_t>(std::count_if(
                replies.begin(), replies.end(), [](auto& candidate) {
                    return candidate.wait_for(std::chrono::milliseconds(0)) ==
                        std::future_status::ready;
                }));
            std::cerr << "reply timeout; largest batch=" << fake.largestBatch
                      << " queued notifications=" << fake.notifications.size()
                      << " ready=" << readyCount
                      << " reply index=" << replyIndex
                      << " replies=" << replies.size() << '\n';
            return 2;
        }
        (void)reply.get();
        replyIndex++;
    }
    port.close();
    return fake.largestBatch > 1u ? 0 : 3;
}
