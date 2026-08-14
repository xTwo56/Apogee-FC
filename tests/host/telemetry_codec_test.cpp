#include <apogee/telemetry_codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace {

constexpr std::span<const std::uint8_t> encoded_bytes(
    const apogee::EncodedFrame& frame) {
    return {frame.bytes.data(), frame.size};
}

constexpr apogee::TelemetryMessage golden_message() {
    return {
        {0x12345678U,
         0x0102030405060708ULL,
         apogee::FlightMode::Safe,
         {7'200U, -1'234, 300U}},
        static_cast<std::uint32_t>(apogee::Fault::BatteryUndervoltage) |
            static_cast<std::uint32_t>(apogee::Fault::BoardOvertemperature),
    };
}

constexpr bool messages_match(const apogee::TelemetryMessage& actual,
                              const apogee::TelemetryMessage& expected) {
    return actual.snapshot.sequence == expected.snapshot.sequence &&
           actual.snapshot.uptime_ms == expected.snapshot.uptime_ms &&
           actual.snapshot.flight_mode == expected.snapshot.flight_mode &&
           actual.snapshot.sensor_readings.battery_mv ==
               expected.snapshot.sensor_readings.battery_mv &&
           actual.snapshot.sensor_readings.board_temperature_centi_c ==
               expected.snapshot.sensor_readings.board_temperature_centi_c &&
           actual.snapshot.sensor_readings.solar_current_ma ==
               expected.snapshot.sensor_readings.solar_current_ma &&
           actual.latched_fault_mask == expected.latched_fault_mask;
}

constexpr bool golden_frame_passes() {
    constexpr std::array<std::uint8_t, 31U> expected{
        0xA5U, 0x5AU, 0x01U, 0x01U, 0x13U, 0x00U, 0x78U, 0x56U,
        0x34U, 0x12U, 0x08U, 0x07U, 0x06U, 0x05U, 0x04U, 0x03U,
        0x02U, 0x01U, 0x01U, 0x20U, 0x1CU, 0x2EU, 0xFBU, 0x2CU,
        0x01U, 0x05U, 0x00U, 0x00U, 0x00U, 0x48U, 0x76U,
    };

    const auto encoded = apogee::encode_telemetry(golden_message());
    if (!encoded.ok() || encoded.frame.size != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (encoded.frame.bytes[index] != expected[index]) {
            return false;
        }
    }

    const auto decoded = apogee::decode_telemetry(expected);
    return decoded.ok() && messages_match(decoded.message, golden_message());
}

constexpr bool all_flight_modes_pass() {
    constexpr std::array modes{
        apogee::FlightMode::Boot,
        apogee::FlightMode::Safe,
        apogee::FlightMode::Nominal,
    };

    for (const auto mode : modes) {
        auto message = golden_message();
        message.snapshot.flight_mode = mode;
        const auto encoded = apogee::encode_telemetry(message);
        const auto decoded =
            apogee::decode_telemetry(encoded_bytes(encoded.frame));
        if (!encoded.ok() || !decoded.ok() ||
            decoded.message.snapshot.flight_mode != mode) {
            return false;
        }
    }
    return true;
}

constexpr bool integer_extremes_and_sequence_pass() {
    apogee::TelemetryMessage minimums{
        {0U,
         0U,
         apogee::FlightMode::Boot,
         {0U, std::numeric_limits<std::int16_t>::min(), 0U}},
        0U,
    };
    apogee::TelemetryMessage maximums{
        {std::numeric_limits<std::uint32_t>::max(),
         std::numeric_limits<std::uint64_t>::max(),
         apogee::FlightMode::Nominal,
         {std::numeric_limits<std::uint16_t>::max(),
          std::numeric_limits<std::int16_t>::max(),
          std::numeric_limits<std::uint16_t>::max()}},
        apogee::known_fault_mask,
    };

    const auto encoded_minimums = apogee::encode_telemetry(minimums);
    const auto decoded_minimums =
        apogee::decode_telemetry(encoded_bytes(encoded_minimums.frame));
    const auto encoded_maximums = apogee::encode_telemetry(maximums);
    const auto decoded_maximums =
        apogee::decode_telemetry(encoded_bytes(encoded_maximums.frame));

    return encoded_minimums.ok() && decoded_minimums.ok() &&
           messages_match(decoded_minimums.message, minimums) &&
           encoded_maximums.ok() && decoded_maximums.ok() &&
           messages_match(decoded_maximums.message, maximums);
}

constexpr apogee::EncodedFrame encode_payload_frame(apogee::Frame frame) {
    return apogee::encode(frame).frame;
}

constexpr bool invalid_telemetry_fields_pass() {
    apogee::Frame wrong_length;
    wrong_length.payload_length = 18U;
    const auto wrong_length_frame = encode_payload_frame(wrong_length);
    const auto length_result =
        apogee::decode_telemetry(encoded_bytes(wrong_length_frame));

    apogee::Frame invalid_mode;
    invalid_mode.payload_length = apogee::telemetry_payload_size;
    invalid_mode.payload[8U] = 3U;
    const auto invalid_mode_frame = encode_payload_frame(invalid_mode);
    const auto mode_result =
        apogee::decode_telemetry(encoded_bytes(invalid_mode_frame));

    apogee::Frame unknown_fault;
    unknown_fault.payload_length = apogee::telemetry_payload_size;
    unknown_fault.payload[15U] = 0x08U;
    const auto unknown_fault_frame = encode_payload_frame(unknown_fault);
    const auto fault_result =
        apogee::decode_telemetry(encoded_bytes(unknown_fault_frame));

    auto invalid_mode_message = golden_message();
    invalid_mode_message.snapshot.flight_mode =
        static_cast<apogee::FlightMode>(3U);
    const auto mode_encode =
        apogee::encode_telemetry(invalid_mode_message);
    auto unknown_fault_message = golden_message();
    unknown_fault_message.latched_fault_mask = 0x08U;
    const auto fault_encode =
        apogee::encode_telemetry(unknown_fault_message);

    return length_result.status ==
               apogee::TelemetryDecodeStatus::IncorrectPayloadLength &&
           mode_result.status ==
               apogee::TelemetryDecodeStatus::InvalidFlightMode &&
           fault_result.status ==
               apogee::TelemetryDecodeStatus::UnknownFaultBits &&
           mode_encode.status ==
               apogee::TelemetryEncodeStatus::InvalidFlightMode &&
           fault_encode.status ==
               apogee::TelemetryEncodeStatus::UnknownFaultBits;
}

constexpr bool corrupted_frame_propagation_passes() {
    const auto valid = apogee::encode_telemetry(golden_message()).frame;
    auto corrupted = valid.bytes;
    corrupted[10U] ^= 0x01U;

    const auto decoded = apogee::decode_telemetry(
        std::span<const std::uint8_t>{corrupted.data(), valid.size});
    return decoded.status ==
               apogee::TelemetryDecodeStatus::FrameDecodeFailure &&
           decoded.frame_status == apogee::DecodeStatus::CrcMismatch;
}

constexpr bool all_tests_pass() {
    return golden_frame_passes() && all_flight_modes_pass() &&
           integer_extremes_and_sequence_pass() &&
           invalid_telemetry_fields_pass() &&
           corrupted_frame_propagation_passes();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
