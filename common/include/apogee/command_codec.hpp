#pragma once

#include <apogee/frame_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace apogee {

enum class CommandId {
    Ping,
    EnterSafeMode,
    SetTelemetryPeriod,
};

enum class CommandAckStatus {
    Accepted,
    InvalidCommand,
    InvalidArgument,
    RejectedByState,
};

struct CommandMessage {
    std::uint32_t sequence{0U};
    CommandId command{CommandId::Ping};
    std::uint32_t period_ms{0U};
};

struct CommandAckMessage {
    std::uint32_t sequence{0U};
    CommandId command{CommandId::Ping};
    CommandAckStatus status{CommandAckStatus::Accepted};
};

enum class CommandEncodeStatus {
    Ok,
    UnknownCommand,
    FrameEncodeFailure,
};

struct CommandEncodeResult {
    CommandEncodeStatus status{CommandEncodeStatus::Ok};
    EncodeStatus frame_status{EncodeStatus::Ok};
    EncodedFrame frame{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == CommandEncodeStatus::Ok;
    }
};

enum class CommandDecodeStatus {
    Ok,
    FrameDecodeFailure,
    IncorrectMessageType,
    InvalidPayloadLength,
    UnknownCommand,
};

struct CommandDecodeResult {
    CommandDecodeStatus status{CommandDecodeStatus::Ok};
    DecodeStatus frame_status{DecodeStatus::Ok};
    CommandMessage message{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == CommandDecodeStatus::Ok;
    }
};

enum class CommandAckEncodeStatus {
    Ok,
    UnknownCommand,
    UnknownStatus,
    FrameEncodeFailure,
};

struct CommandAckEncodeResult {
    CommandAckEncodeStatus status{CommandAckEncodeStatus::Ok};
    EncodeStatus frame_status{EncodeStatus::Ok};
    EncodedFrame frame{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == CommandAckEncodeStatus::Ok;
    }
};

enum class CommandAckDecodeStatus {
    Ok,
    FrameDecodeFailure,
    IncorrectMessageType,
    InvalidPayloadLength,
    UnknownCommand,
    UnknownStatus,
};

struct CommandAckDecodeResult {
    CommandAckDecodeStatus status{CommandAckDecodeStatus::Ok};
    DecodeStatus frame_status{DecodeStatus::Ok};
    CommandAckMessage message{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == CommandAckDecodeStatus::Ok;
    }
};

namespace command_detail {

constexpr bool encode_command(CommandId command,
                              std::uint8_t& encoded) noexcept {
    switch (command) {
    case CommandId::Ping:
        encoded = 0x01U;
        return true;
    case CommandId::EnterSafeMode:
        encoded = 0x02U;
        return true;
    case CommandId::SetTelemetryPeriod:
        encoded = 0x03U;
        return true;
    }
    return false;
}

constexpr bool decode_command(std::uint8_t encoded,
                              CommandId& command) noexcept {
    switch (encoded) {
    case 0x01U:
        command = CommandId::Ping;
        return true;
    case 0x02U:
        command = CommandId::EnterSafeMode;
        return true;
    case 0x03U:
        command = CommandId::SetTelemetryPeriod;
        return true;
    default:
        return false;
    }
}

constexpr bool encode_ack_status(CommandAckStatus status,
                                 std::uint8_t& encoded) noexcept {
    switch (status) {
    case CommandAckStatus::Accepted:
        encoded = 0U;
        return true;
    case CommandAckStatus::InvalidCommand:
        encoded = 1U;
        return true;
    case CommandAckStatus::InvalidArgument:
        encoded = 2U;
        return true;
    case CommandAckStatus::RejectedByState:
        encoded = 3U;
        return true;
    }
    return false;
}

constexpr bool decode_ack_status(std::uint8_t encoded,
                                 CommandAckStatus& status) noexcept {
    switch (encoded) {
    case 0U:
        status = CommandAckStatus::Accepted;
        return true;
    case 1U:
        status = CommandAckStatus::InvalidCommand;
        return true;
    case 2U:
        status = CommandAckStatus::InvalidArgument;
        return true;
    case 3U:
        status = CommandAckStatus::RejectedByState;
        return true;
    default:
        return false;
    }
}

constexpr void write_u32_le(Frame& frame,
                            std::size_t offset,
                            std::uint32_t value) noexcept {
    frame.payload[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    frame.payload[offset + 1U] =
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    frame.payload[offset + 2U] =
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    frame.payload[offset + 3U] =
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

[[nodiscard]] constexpr std::uint32_t read_u32_le(
    const Frame& frame,
    std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(frame.payload[offset]) |
           (static_cast<std::uint32_t>(frame.payload[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(frame.payload[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(frame.payload[offset + 3U]) << 24U);
}

}  // namespace command_detail

[[nodiscard]] constexpr CommandEncodeResult encode_command(
    const CommandMessage& message) noexcept {
    CommandEncodeResult result;
    std::uint8_t command_id{0U};
    if (!command_detail::encode_command(message.command, command_id)) {
        result.status = CommandEncodeStatus::UnknownCommand;
        return result;
    }

    Frame frame;
    frame.message_type = MessageType::Command;
    frame.sequence = message.sequence;
    frame.payload[0U] = command_id;
    frame.payload_length = 1U;
    if (message.command == CommandId::SetTelemetryPeriod) {
        frame.payload_length = 5U;
        command_detail::write_u32_le(frame, 1U, message.period_ms);
    }

    const auto encoded = encode(frame);
    result.frame_status = encoded.status;
    result.frame = encoded.frame;
    if (!encoded.ok()) {
        result.status = CommandEncodeStatus::FrameEncodeFailure;
    }
    return result;
}

[[nodiscard]] constexpr CommandDecodeResult decode_command(
    std::span<const std::uint8_t> bytes) noexcept {
    CommandDecodeResult result;
    const auto decoded = decode(bytes);
    result.frame_status = decoded.status;
    if (!decoded.ok()) {
        result.status = CommandDecodeStatus::FrameDecodeFailure;
        return result;
    }
    if (decoded.frame.message_type != MessageType::Command) {
        result.status = CommandDecodeStatus::IncorrectMessageType;
        return result;
    }
    if (decoded.frame.payload_length < 1U) {
        result.status = CommandDecodeStatus::InvalidPayloadLength;
        return result;
    }

    CommandId command{CommandId::Ping};
    if (!command_detail::decode_command(decoded.frame.payload[0U], command)) {
        result.status = CommandDecodeStatus::UnknownCommand;
        return result;
    }
    const std::uint16_t required_length =
        command == CommandId::SetTelemetryPeriod ? 5U : 1U;
    if (decoded.frame.payload_length != required_length) {
        result.status = CommandDecodeStatus::InvalidPayloadLength;
        return result;
    }

    result.message.sequence = decoded.frame.sequence;
    result.message.command = command;
    if (command == CommandId::SetTelemetryPeriod) {
        result.message.period_ms =
            command_detail::read_u32_le(decoded.frame, 1U);
    }
    return result;
}

[[nodiscard]] constexpr CommandAckEncodeResult encode_command_ack(
    const CommandAckMessage& message) noexcept {
    CommandAckEncodeResult result;
    std::uint8_t command_id{0U};
    std::uint8_t status{0U};
    if (!command_detail::encode_command(message.command, command_id)) {
        result.status = CommandAckEncodeStatus::UnknownCommand;
        return result;
    }
    if (!command_detail::encode_ack_status(message.status, status)) {
        result.status = CommandAckEncodeStatus::UnknownStatus;
        return result;
    }

    Frame frame;
    frame.message_type = MessageType::CommandAck;
    frame.payload_length = 2U;
    frame.sequence = message.sequence;
    frame.payload[0U] = command_id;
    frame.payload[1U] = status;

    const auto encoded = encode(frame);
    result.frame_status = encoded.status;
    result.frame = encoded.frame;
    if (!encoded.ok()) {
        result.status = CommandAckEncodeStatus::FrameEncodeFailure;
    }
    return result;
}

[[nodiscard]] constexpr CommandAckDecodeResult decode_command_ack(
    std::span<const std::uint8_t> bytes) noexcept {
    CommandAckDecodeResult result;
    const auto decoded = decode(bytes);
    result.frame_status = decoded.status;
    if (!decoded.ok()) {
        result.status = CommandAckDecodeStatus::FrameDecodeFailure;
        return result;
    }
    if (decoded.frame.message_type != MessageType::CommandAck) {
        result.status = CommandAckDecodeStatus::IncorrectMessageType;
        return result;
    }
    if (decoded.frame.payload_length != 2U) {
        result.status = CommandAckDecodeStatus::InvalidPayloadLength;
        return result;
    }
    if (!command_detail::decode_command(decoded.frame.payload[0U],
                                        result.message.command)) {
        result.status = CommandAckDecodeStatus::UnknownCommand;
        return result;
    }
    if (!command_detail::decode_ack_status(decoded.frame.payload[1U],
                                           result.message.status)) {
        result.status = CommandAckDecodeStatus::UnknownStatus;
        return result;
    }

    result.message.sequence = decoded.frame.sequence;
    return result;
}

}  // namespace apogee
