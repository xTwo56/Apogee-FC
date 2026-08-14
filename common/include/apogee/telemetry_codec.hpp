#pragma once

#include <apogee/frame_codec.hpp>
#include <apogee/health.hpp>
#include <apogee/telemetry.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace apogee {

inline constexpr std::uint16_t telemetry_payload_size{19U};
inline constexpr std::uint32_t known_fault_mask{
    static_cast<std::uint32_t>(Fault::BatteryUndervoltage) |
    static_cast<std::uint32_t>(Fault::BoardUndertemperature) |
    static_cast<std::uint32_t>(Fault::BoardOvertemperature)};

struct TelemetryMessage {
    TelemetrySnapshot snapshot;
    std::uint32_t latched_fault_mask;
};

enum class TelemetryEncodeStatus {
    Ok,
    InvalidFlightMode,
    UnknownFaultBits,
    FrameEncodeFailure,
};

struct TelemetryEncodeResult {
    TelemetryEncodeStatus status{TelemetryEncodeStatus::Ok};
    EncodeStatus frame_status{EncodeStatus::Ok};
    EncodedFrame frame{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == TelemetryEncodeStatus::Ok;
    }
};

enum class TelemetryDecodeStatus {
    Ok,
    FrameDecodeFailure,
    IncorrectPayloadLength,
    InvalidFlightMode,
    UnknownFaultBits,
};

struct TelemetryDecodeResult {
    TelemetryDecodeStatus status{TelemetryDecodeStatus::Ok};
    DecodeStatus frame_status{DecodeStatus::Ok};
    TelemetryMessage message{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == TelemetryDecodeStatus::Ok;
    }
};

namespace telemetry_detail {

constexpr bool encode_flight_mode(FlightMode mode,
                                  std::uint8_t& encoded) noexcept {
    switch (mode) {
    case FlightMode::Boot:
        encoded = 0U;
        return true;
    case FlightMode::Safe:
        encoded = 1U;
        return true;
    case FlightMode::Nominal:
        encoded = 2U;
        return true;
    }
    return false;
}

constexpr bool decode_flight_mode(std::uint8_t encoded,
                                  FlightMode& mode) noexcept {
    switch (encoded) {
    case 0U:
        mode = FlightMode::Boot;
        return true;
    case 1U:
        mode = FlightMode::Safe;
        return true;
    case 2U:
        mode = FlightMode::Nominal;
        return true;
    default:
        return false;
    }
}

constexpr void write_u16_le(Frame& frame,
                            std::size_t offset,
                            std::uint16_t value) noexcept {
    frame.payload[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    frame.payload[offset + 1U] =
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

constexpr void write_u32_le(Frame& frame,
                            std::size_t offset,
                            std::uint32_t value) noexcept {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        frame.payload[offset + byte] = static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(byte * 8U)) & 0xFFU);
    }
}

constexpr void write_u64_le(Frame& frame,
                            std::size_t offset,
                            std::uint64_t value) noexcept {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        frame.payload[offset + byte] = static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(byte * 8U)) & 0xFFU);
    }
}

[[nodiscard]] constexpr std::uint16_t read_u16_le(const Frame& frame,
                                                   std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(frame.payload[offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(frame.payload[offset + 1U]) << 8U));
}

[[nodiscard]] constexpr std::uint32_t read_u32_le(const Frame& frame,
                                                   std::size_t offset) noexcept {
    std::uint32_t value{0U};
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        value |= static_cast<std::uint32_t>(frame.payload[offset + byte])
                 << static_cast<unsigned int>(byte * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint64_t read_u64_le(const Frame& frame,
                                                   std::size_t offset) noexcept {
    std::uint64_t value{0U};
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        value |= static_cast<std::uint64_t>(frame.payload[offset + byte])
                 << static_cast<unsigned int>(byte * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::int16_t decode_temperature(
    std::uint16_t encoded) noexcept {
    if (encoded <= 0x7FFFU) {
        return static_cast<std::int16_t>(encoded);
    }
    return static_cast<std::int16_t>(static_cast<std::int32_t>(encoded) -
                                     65'536);
}

}  // namespace telemetry_detail

[[nodiscard]] constexpr TelemetryEncodeResult encode_telemetry(
    const TelemetryMessage& message) noexcept {
    TelemetryEncodeResult result;
    std::uint8_t encoded_mode{0U};
    if (!telemetry_detail::encode_flight_mode(message.snapshot.flight_mode,
                                              encoded_mode)) {
        result.status = TelemetryEncodeStatus::InvalidFlightMode;
        return result;
    }
    if ((message.latched_fault_mask & ~known_fault_mask) != 0U) {
        result.status = TelemetryEncodeStatus::UnknownFaultBits;
        return result;
    }

    Frame frame;
    frame.message_type = MessageType::Telemetry;
    frame.payload_length = telemetry_payload_size;
    frame.sequence = message.snapshot.sequence;
    telemetry_detail::write_u64_le(frame, 0U, message.snapshot.uptime_ms);
    frame.payload[8U] = encoded_mode;
    telemetry_detail::write_u16_le(
        frame, 9U, message.snapshot.sensor_readings.battery_mv);
    telemetry_detail::write_u16_le(
        frame,
        11U,
        static_cast<std::uint16_t>(
            message.snapshot.sensor_readings.board_temperature_centi_c));
    telemetry_detail::write_u16_le(
        frame, 13U, message.snapshot.sensor_readings.solar_current_ma);
    telemetry_detail::write_u32_le(frame, 15U, message.latched_fault_mask);

    const auto encoded = encode(frame);
    result.frame_status = encoded.status;
    result.frame = encoded.frame;
    if (!encoded.ok()) {
        result.status = TelemetryEncodeStatus::FrameEncodeFailure;
    }
    return result;
}

[[nodiscard]] constexpr TelemetryDecodeResult decode_telemetry(
    std::span<const std::uint8_t> bytes) noexcept {
    TelemetryDecodeResult result;
    const auto decoded = decode(bytes);
    result.frame_status = decoded.status;
    if (!decoded.ok()) {
        result.status = TelemetryDecodeStatus::FrameDecodeFailure;
        return result;
    }
    if (decoded.frame.payload_length != telemetry_payload_size) {
        result.status = TelemetryDecodeStatus::IncorrectPayloadLength;
        return result;
    }

    FlightMode flight_mode{FlightMode::Boot};
    if (!telemetry_detail::decode_flight_mode(decoded.frame.payload[8U],
                                              flight_mode)) {
        result.status = TelemetryDecodeStatus::InvalidFlightMode;
        return result;
    }

    const auto fault_mask = telemetry_detail::read_u32_le(decoded.frame, 15U);
    if ((fault_mask & ~known_fault_mask) != 0U) {
        result.status = TelemetryDecodeStatus::UnknownFaultBits;
        return result;
    }

    result.message.snapshot.sequence = decoded.frame.sequence;
    result.message.snapshot.uptime_ms =
        telemetry_detail::read_u64_le(decoded.frame, 0U);
    result.message.snapshot.flight_mode = flight_mode;
    result.message.snapshot.sensor_readings.battery_mv =
        telemetry_detail::read_u16_le(decoded.frame, 9U);
    result.message.snapshot.sensor_readings.board_temperature_centi_c =
        telemetry_detail::decode_temperature(
            telemetry_detail::read_u16_le(decoded.frame, 11U));
    result.message.snapshot.sensor_readings.solar_current_ma =
        telemetry_detail::read_u16_le(decoded.frame, 13U);
    result.message.latched_fault_mask = fault_mask;
    return result;
}

}  // namespace apogee
