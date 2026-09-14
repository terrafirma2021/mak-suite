#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <stdexcept>
namespace makxd {
enum class StreamKind : std::uint8_t { Mouse = 1, Keyboard = 2, Controller = 3 };
inline constexpr std::uint8_t STREAM_COMMAND = 0x52u, STREAM_EVENT = 0x53u;
inline constexpr std::size_t STREAM_MAX_PAYLOAD_BYTES = 251u;
inline constexpr std::uint16_t STREAM_TRIGGER_MAX = 1023u;
inline constexpr std::uint16_t CONTROLLER_TRIGGER_MAX = 1023u;
struct StreamFrame { std::uint8_t command = 0; std::vector<std::uint8_t> payload; };
struct InputChange {
    StreamKind kind{}; std::uint8_t control = 0; std::uint16_t value = 0; bool overflow = false;
    [[nodiscard]] bool isTrigger() const { return kind == StreamKind::Controller && (control == 10 || control == 11); }
};
struct StreamRequest {
    StreamKind kind; std::optional<bool> enabled;
    [[nodiscard]] std::vector<std::uint8_t> encode() const {
        auto id = static_cast<std::uint8_t>(kind);
        if (id < 1 || id > 3) throw std::invalid_argument("invalid stream kind");
        std::vector<std::uint8_t> frame{0xde, 0xad, static_cast<std::uint8_t>(enabled ? 2 : 1), 0, STREAM_COMMAND, id};
        if (enabled) frame.push_back(*enabled ? 1 : 0);
        return frame;
    }
    [[nodiscard]] static StreamRequest mouse(bool enabled = true) { return {StreamKind::Mouse, enabled}; }
    [[nodiscard]] static StreamRequest keyboard(bool enabled = true) { return {StreamKind::Keyboard, enabled}; }
    [[nodiscard]] static StreamRequest controller(bool enabled = true) { return {StreamKind::Controller, enabled}; }
};
class StreamFrameDecoder {
public:
    void feed(std::span<const std::uint8_t> bytes)
    {
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::optional<StreamFrame> next()
    {
        while (m_buffer.size() >= 2u) {
            if (m_buffer[0] != 0xDEu || m_buffer[1] != 0xADu) {
                m_buffer.erase(m_buffer.begin());
                continue;
            }
            if (m_buffer.size() < 4u)
                return std::nullopt;
            const auto payload_size = static_cast<std::size_t>(
                m_buffer[2] | (static_cast<std::size_t>(m_buffer[3]) << 8u));
            if (payload_size > STREAM_MAX_PAYLOAD_BYTES) {
                m_buffer.erase(m_buffer.begin());
                continue;
            }
            const auto frame_size = 5u + payload_size;
            if (m_buffer.size() < frame_size)
                return std::nullopt;
            StreamFrame frame;
            frame.command = m_buffer[4u];
            frame.payload.assign(m_buffer.begin() + 5u,
                m_buffer.begin() + frame_size);
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + frame_size);
            return frame;
        }
        return std::nullopt;
    }

private:
    std::vector<std::uint8_t> m_buffer;
};


[[nodiscard]] inline std::optional<InputChange> decode_input_change(const StreamFrame& frame) {
    const auto& p = frame.payload;
    if (frame.command != STREAM_EVENT || p.size() < 3 || p.size() > 4 || p[0] < 1 || p[0] > 3) return std::nullopt;
    InputChange event{static_cast<StreamKind>(p[0]), p[1], p[2], false};
    if (p.size() == 3 && p[1] == 255 && p[2] == 255) { event.overflow = true; return event; }
    if (event.isTrigger()) {
        if (p.size() != 4) return std::nullopt;
        event.value = static_cast<std::uint16_t>(p[2] | (static_cast<std::uint16_t>(p[3]) << 8));
        if (event.value > STREAM_TRIGGER_MAX) return std::nullopt;
    } else if (p.size() != 3 || p[2] > 1 || (p[0] == 1 && p[1] > 31) ||
        (p[0] == 3 && (p[1] > 54 || (p[1] >= 12 && p[1] <= 15)))) return std::nullopt;
    return event;
}
} // namespace makxd
