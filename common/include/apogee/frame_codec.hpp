#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace apogee {

inline constexpr std::uint8_t protocol_version{1U};
inline constexpr std::size_t maximum_payload_size{64U};
inline constexpr std::size_t maximum_frame_size{76U};
inline constexpr std::size_t minimum_frame_size{12U};

enum class MessageType : std::uint8_t {
    Telemetry = 0x01U,
};

struct Frame {
    std::uint8_t version{protocol_version};
    MessageType message_type{MessageType::Telemetry};
    std::uint16_t payload_length{0U};
    std::uint32_t sequence{0U};
    std::array<std::uint8_t, maximum_payload_size> payload{};
};

struct EncodedFrame {
    std::array<std::uint8_t, maximum_frame_size> bytes{};
    std::size_t size{0U};
};

enum class EncodeStatus {
    Ok,
    UnsupportedVersion,
    UnsupportedMessageType,
    PayloadTooLarge,
};

struct EncodeResult {
    EncodeStatus status{EncodeStatus::Ok};
    EncodedFrame frame{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == EncodeStatus::Ok;
    }
};

enum class DecodeStatus {
    Ok,
    Truncated,
    BadSync,
    UnsupportedVersion,
    UnsupportedMessageType,
    PayloadTooLarge,
    LengthMismatch,
    CrcMismatch,
};

struct DecodeResult {
    DecodeStatus status{DecodeStatus::Ok};
    Frame frame{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == DecodeStatus::Ok;
    }
};

[[nodiscard]] constexpr std::uint16_t crc16_ccitt_false(
    std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::uint16_t polynomial{0x1021U};
    std::uint16_t crc{0xFFFFU};

    for (const auto byte : bytes) {
        crc = static_cast<std::uint16_t>(
            crc ^ static_cast<std::uint16_t>(
                      static_cast<std::uint16_t>(byte) << 8U));
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? static_cast<std::uint16_t>((crc << 1U) ^ polynomial)
                      : static_cast<std::uint16_t>(crc << 1U);
        }
    }

    return crc;
}

namespace detail {

constexpr void write_u16_le(std::array<std::uint8_t, maximum_frame_size>& bytes,
                            std::size_t offset,
                            std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

constexpr void write_u32_le(std::array<std::uint8_t, maximum_frame_size>& bytes,
                            std::size_t offset,
                            std::uint32_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

[[nodiscard]] constexpr std::uint16_t read_u16_le(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] constexpr std::uint32_t read_u32_le(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

}  // namespace detail

[[nodiscard]] constexpr EncodeResult encode(const Frame& input) noexcept {
    EncodeResult result;

    if (input.version != protocol_version) {
        result.status = EncodeStatus::UnsupportedVersion;
        return result;
    }
    if (input.message_type != MessageType::Telemetry) {
        result.status = EncodeStatus::UnsupportedMessageType;
        return result;
    }
    if (input.payload_length > maximum_payload_size) {
        result.status = EncodeStatus::PayloadTooLarge;
        return result;
    }

    auto& bytes = result.frame.bytes;
    bytes[0U] = 0xA5U;
    bytes[1U] = 0x5AU;
    bytes[2U] = input.version;
    bytes[3U] = static_cast<std::uint8_t>(input.message_type);
    detail::write_u16_le(bytes, 4U, input.payload_length);
    detail::write_u32_le(bytes, 6U, input.sequence);

    for (std::size_t index = 0U; index < input.payload_length; ++index) {
        bytes[10U + index] = input.payload[index];
    }

    const auto crc_offset = 10U + input.payload_length;
    const auto crc = crc16_ccitt_false(
        std::span<const std::uint8_t>{bytes.data() + 2U, 8U +
                                                            input.payload_length});
    detail::write_u16_le(bytes, crc_offset, crc);
    result.frame.size = crc_offset + 2U;
    return result;
}

[[nodiscard]] constexpr DecodeResult decode(
    std::span<const std::uint8_t> bytes) noexcept {
    DecodeResult result;

    if (bytes.size() < minimum_frame_size) {
        result.status = DecodeStatus::Truncated;
        return result;
    }
    if (bytes[0U] != 0xA5U || bytes[1U] != 0x5AU) {
        result.status = DecodeStatus::BadSync;
        return result;
    }
    if (bytes[2U] != protocol_version) {
        result.status = DecodeStatus::UnsupportedVersion;
        return result;
    }
    if (bytes[3U] != static_cast<std::uint8_t>(MessageType::Telemetry)) {
        result.status = DecodeStatus::UnsupportedMessageType;
        return result;
    }

    const auto payload_length = detail::read_u16_le(bytes, 4U);
    if (payload_length > maximum_payload_size) {
        result.status = DecodeStatus::PayloadTooLarge;
        return result;
    }

    const auto expected_size = minimum_frame_size + payload_length;
    if (bytes.size() < expected_size) {
        result.status = DecodeStatus::Truncated;
        return result;
    }
    if (bytes.size() != expected_size) {
        result.status = DecodeStatus::LengthMismatch;
        return result;
    }

    const auto crc_offset = 10U + payload_length;
    const auto expected_crc = detail::read_u16_le(bytes, crc_offset);
    const auto actual_crc = crc16_ccitt_false(
        std::span<const std::uint8_t>{bytes.data() + 2U, 8U + payload_length});
    if (actual_crc != expected_crc) {
        result.status = DecodeStatus::CrcMismatch;
        return result;
    }

    result.frame.version = bytes[2U];
    result.frame.message_type = MessageType::Telemetry;
    result.frame.payload_length = payload_length;
    result.frame.sequence = detail::read_u32_le(bytes, 6U);
    for (std::size_t index = 0U; index < payload_length; ++index) {
        result.frame.payload[index] = bytes[10U + index];
    }
    return result;
}

}  // namespace apogee
