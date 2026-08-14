#include <apogee/frame_codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

constexpr std::span<const std::uint8_t> encoded_bytes(
    const apogee::EncodedFrame& frame) {
    return {frame.bytes.data(), frame.size};
}

constexpr bool crc_vector_passes() {
    constexpr std::array<std::uint8_t, 9U> input{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    return apogee::crc16_ccitt_false(input) == 0x29B1U;
}

constexpr apogee::Frame golden_input() {
    apogee::Frame frame;
    frame.payload_length = 3U;
    frame.sequence = 0x12345678U;
    frame.payload[0U] = 0x10U;
    frame.payload[1U] = 0x20U;
    frame.payload[2U] = 0x30U;
    return frame;
}

constexpr bool golden_frame_and_little_endian_pass() {
    constexpr std::array<std::uint8_t, 15U> expected{
        0xA5U, 0x5AU, 0x01U, 0x01U, 0x03U, 0x00U, 0x78U, 0x56U,
        0x34U, 0x12U, 0x10U, 0x20U, 0x30U, 0x0FU, 0xA4U,
    };
    const auto encoded = apogee::encode(golden_input());
    if (!encoded.ok() || encoded.frame.size != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (encoded.frame.bytes[index] != expected[index]) {
            return false;
        }
    }

    const auto decoded = apogee::decode(expected);
    return decoded.ok() && decoded.frame.payload_length == 3U &&
           decoded.frame.sequence == 0x12345678U;
}

constexpr bool zero_length_round_trip_passes() {
    apogee::Frame input;
    input.sequence = 42U;

    const auto encoded = apogee::encode(input);
    const auto decoded = apogee::decode(encoded_bytes(encoded.frame));
    return encoded.ok() && encoded.frame.size == apogee::minimum_frame_size &&
           decoded.ok() && decoded.frame.sequence == input.sequence &&
           decoded.frame.payload_length == 0U;
}

constexpr bool maximum_length_round_trip_passes() {
    apogee::Frame input;
    input.payload_length =
        static_cast<std::uint16_t>(apogee::maximum_payload_size);
    input.sequence = 0x89ABCDEFU;
    for (std::size_t index = 0U; index < input.payload.size(); ++index) {
        input.payload[index] = static_cast<std::uint8_t>(index);
    }

    const auto encoded = apogee::encode(input);
    const auto decoded = apogee::decode(encoded_bytes(encoded.frame));
    if (!encoded.ok() || encoded.frame.size != apogee::maximum_frame_size ||
        !decoded.ok() || decoded.frame.sequence != input.sequence ||
        decoded.frame.payload_length != input.payload_length) {
        return false;
    }
    for (std::size_t index = 0U; index < input.payload.size(); ++index) {
        if (decoded.frame.payload[index] != input.payload[index]) {
            return false;
        }
    }
    return true;
}

constexpr bool encode_failures_pass() {
    apogee::Frame input;
    input.version = 2U;
    const auto version = apogee::encode(input);

    input.version = apogee::protocol_version;
    input.message_type = static_cast<apogee::MessageType>(0x02U);
    const auto type = apogee::encode(input);

    input.message_type = apogee::MessageType::Telemetry;
    input.payload_length = 65U;
    const auto oversized = apogee::encode(input);

    return version.status == apogee::EncodeStatus::UnsupportedVersion &&
           type.status == apogee::EncodeStatus::UnsupportedMessageType &&
           oversized.status == apogee::EncodeStatus::PayloadTooLarge;
}

constexpr bool decode_failures_pass() {
    const auto valid = apogee::encode(golden_input()).frame;

    auto bad_sync = valid.bytes;
    bad_sync[0U] = 0U;
    const auto sync_result = apogee::decode(
        std::span<const std::uint8_t>{bad_sync.data(), valid.size});

    auto bad_version = valid.bytes;
    bad_version[2U] = 2U;
    const auto version_result = apogee::decode(
        std::span<const std::uint8_t>{bad_version.data(), valid.size});

    auto bad_type = valid.bytes;
    bad_type[3U] = 2U;
    const auto type_result = apogee::decode(
        std::span<const std::uint8_t>{bad_type.data(), valid.size});

    auto oversized = valid.bytes;
    oversized[4U] = 65U;
    oversized[5U] = 0U;
    const auto oversized_result = apogee::decode(
        std::span<const std::uint8_t>{oversized.data(), valid.size});

    const auto length_result = apogee::decode(
        std::span<const std::uint8_t>{valid.bytes.data(), valid.size + 1U});
    const auto truncated_result = apogee::decode(
        std::span<const std::uint8_t>{valid.bytes.data(), valid.size - 1U});

    auto bad_crc = valid.bytes;
    bad_crc[valid.size - 1U] ^= 0x01U;
    const auto crc_result = apogee::decode(
        std::span<const std::uint8_t>{bad_crc.data(), valid.size});

    auto corrupted_payload = valid.bytes;
    corrupted_payload[10U] ^= 0x01U;
    const auto corruption_result = apogee::decode(
        std::span<const std::uint8_t>{corrupted_payload.data(), valid.size});

    return sync_result.status == apogee::DecodeStatus::BadSync &&
           version_result.status ==
               apogee::DecodeStatus::UnsupportedVersion &&
           type_result.status ==
               apogee::DecodeStatus::UnsupportedMessageType &&
           oversized_result.status == apogee::DecodeStatus::PayloadTooLarge &&
           length_result.status == apogee::DecodeStatus::LengthMismatch &&
           truncated_result.status == apogee::DecodeStatus::Truncated &&
           crc_result.status == apogee::DecodeStatus::CrcMismatch &&
           corruption_result.status == apogee::DecodeStatus::CrcMismatch;
}

constexpr bool all_tests_pass() {
    return crc_vector_passes() && golden_frame_and_little_endian_pass() &&
           zero_length_round_trip_passes() &&
           maximum_length_round_trip_passes() && encode_failures_pass() &&
           decode_failures_pass();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
