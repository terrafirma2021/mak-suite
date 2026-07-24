#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace makxd {

enum class StreamKind : std::uint8_t {
    Mouse = 1,
    Keyboard = 2,
    Controller = 3,
};

inline constexpr std::uint8_t STREAM_MASK_MOUSE = 1u << 0u;
inline constexpr std::uint8_t STREAM_MASK_KEYBOARD = 1u << 1u;
inline constexpr std::uint8_t STREAM_MASK_CONTROLLER = 1u << 2u;
inline constexpr std::uint8_t STREAM_MASK_ALL =
    STREAM_MASK_MOUSE | STREAM_MASK_KEYBOARD | STREAM_MASK_CONTROLLER;
inline constexpr std::uint8_t STREAM_COMMAND_INPUT = 0x01u;
inline constexpr std::size_t STREAM_MAX_BODY_BYTES = 252u;
inline constexpr std::size_t STREAM_MAX_PAYLOAD_BYTES =
    STREAM_MAX_BODY_BYTES - 1u;

enum class StreamOperation : std::uint8_t {
    Start = 1,
    Stop = 2,
    Status = 3,
};

struct StreamTiming {
    std::uint16_t raw = 0u;
    std::uint16_t dt_uframes = 0u;
    bool baseline = false;
    bool invalid = false;
};

struct StreamFrame {
    std::uint8_t command = 0u;
    std::vector<std::uint8_t> payload;
};

struct StreamControl {
    StreamOperation operation = StreamOperation::Status;
    std::uint8_t status = 0u;
    std::uint8_t active_mask = 0u;
};

struct StreamInputRecord {
    StreamKind kind = static_cast<StreamKind>(0u);
    std::uint32_t sequence = 0u;
    StreamTiming timing{};
    std::vector<std::uint8_t> values;
};

class StreamRequest {
public:
    StreamOperation operation = StreamOperation::Status;
    std::uint8_t source_mask = 0u;

    [[nodiscard]] std::vector<std::uint8_t> encode() const
    {
        const std::uint8_t payload[] = {
            static_cast<std::uint8_t>(operation),
            static_cast<std::uint8_t>(source_mask & STREAM_MASK_ALL),
        };
        return encode_frame(STREAM_COMMAND_INPUT, payload);
    }

    [[nodiscard]] static StreamRequest start(
        std::uint8_t selected_mask = STREAM_MASK_ALL)
    {
        return {StreamOperation::Start,
            static_cast<std::uint8_t>(selected_mask & STREAM_MASK_ALL)};
    }

    [[nodiscard]] static StreamRequest mouse()
    {
        return start(STREAM_MASK_MOUSE);
    }

    [[nodiscard]] static StreamRequest keyboard()
    {
        return start(STREAM_MASK_KEYBOARD);
    }

    [[nodiscard]] static StreamRequest controller()
    {
        return start(STREAM_MASK_CONTROLLER);
    }

    [[nodiscard]] static StreamRequest all()
    {
        return start(STREAM_MASK_ALL);
    }

    [[nodiscard]] static StreamRequest stop()
    {
        return {StreamOperation::Stop, 0u};
    }

    [[nodiscard]] static StreamRequest status()
    {
        return {StreamOperation::Status, 0u};
    }

private:
    static std::vector<std::uint8_t> encode_frame(
        std::uint8_t command,
        std::span<const std::uint8_t> payload)
    {
        std::vector<std::uint8_t> frame(5u + payload.size(), 0u);
        frame[0] = 0xDEu;
        frame[1] = 0xADu;
        frame[2] = static_cast<std::uint8_t>(payload.size());
        frame[3] = static_cast<std::uint8_t>(payload.size() >> 8u);
        frame[4] = command;
        for (std::size_t index = 0u; index < payload.size(); ++index)
            frame[5u + index] = payload[index];
        return frame;
    }
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
            if (payload_size == 0u ||
                payload_size > STREAM_MAX_PAYLOAD_BYTES) {
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

[[nodiscard]] inline StreamTiming decode_stream_timing(std::uint16_t raw)
{
    return {raw, static_cast<std::uint16_t>(raw & 0x3FFFu),
        (raw & 0x4000u) != 0u, (raw & 0x8000u) != 0u};
}

[[nodiscard]] inline bool decode_stream_control(
    const StreamFrame& frame,
    StreamControl& control)
{
    if (frame.command != STREAM_COMMAND_INPUT || frame.payload.size() != 3u)
        return false;
    control.operation = static_cast<StreamOperation>(frame.payload[0]);
    control.status = frame.payload[1];
    control.active_mask = frame.payload[2];
    return true;
}

[[nodiscard]] inline bool decode_stream_input_record(
    const StreamFrame& frame,
    StreamInputRecord& record)
{
    if (frame.command != STREAM_COMMAND_INPUT || frame.payload.size() < 8u)
        return false;
    const auto u16 = [&frame](std::size_t offset) {
        return static_cast<std::uint16_t>(frame.payload[offset] |
            (static_cast<std::uint16_t>(frame.payload[offset + 1u]) << 8u));
    };
    const auto u32 = [&frame](std::size_t offset) {
        return static_cast<std::uint32_t>(frame.payload[offset] |
            (static_cast<std::uint32_t>(frame.payload[offset + 1u]) << 8u) |
            (static_cast<std::uint32_t>(frame.payload[offset + 2u]) << 16u) |
            (static_cast<std::uint32_t>(frame.payload[offset + 3u]) << 24u));
    };
    const auto source = static_cast<StreamKind>(frame.payload[0]);
    if (source != StreamKind::Mouse && source != StreamKind::Keyboard &&
        source != StreamKind::Controller)
        return false;
    record.kind = source;
    record.sequence = u32(3u);
    record.timing = decode_stream_timing(u16(1u));
    record.values.assign(frame.payload.begin() + 7u, frame.payload.end());
    return !record.values.empty();
}

} // namespace makxd
