#include <apogee/command_codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

constexpr std::span<const std::uint8_t> bytes(
    const apogee::EncodedFrame& frame) noexcept {
    return {frame.bytes.data(), frame.size};
}

template <std::size_t Size>
constexpr bool matches(const apogee::EncodedFrame& frame,
                       const std::array<std::uint8_t, Size>& expected) noexcept {
    if (frame.size != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (frame.bytes[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

constexpr bool golden_commands_pass() {
    constexpr std::array<std::uint8_t, 13U> ping{
        0xA5U, 0x5AU, 0x01U, 0x10U, 0x01U, 0x00U, 0x78U,
        0x56U, 0x34U, 0x12U, 0x01U, 0xD8U, 0x57U};
    constexpr std::array<std::uint8_t, 13U> safe{
        0xA5U, 0x5AU, 0x01U, 0x10U, 0x01U, 0x00U, 0x78U,
        0x56U, 0x34U, 0x12U, 0x02U, 0xBBU, 0x67U};
    constexpr std::array<std::uint8_t, 17U> period{
        0xA5U, 0x5AU, 0x01U, 0x10U, 0x05U, 0x00U, 0x78U, 0x56U, 0x34U,
        0x12U, 0x03U, 0x44U, 0x33U, 0x22U, 0x11U, 0xEBU, 0xA7U};

    const auto ping_result = apogee::encode_command(
        {0x12345678U, apogee::CommandId::Ping, 0U});
    const auto safe_result = apogee::encode_command(
        {0x12345678U, apogee::CommandId::EnterSafeMode, 0U});
    const auto period_result = apogee::encode_command(
        {0x12345678U,
         apogee::CommandId::SetTelemetryPeriod,
         0x11223344U});

    return ping_result.ok() && matches(ping_result.frame, ping) &&
           safe_result.ok() && matches(safe_result.frame, safe) &&
           period_result.ok() && matches(period_result.frame, period);
}

constexpr bool golden_ack_passes() {
    constexpr std::array<std::uint8_t, 14U> expected{
        0xA5U, 0x5AU, 0x01U, 0x11U, 0x02U, 0x00U, 0x78U,
        0x56U, 0x34U, 0x12U, 0x03U, 0x02U, 0x64U, 0x97U};
    const auto encoded = apogee::encode_command_ack(
        {0x12345678U,
         apogee::CommandId::SetTelemetryPeriod,
         apogee::CommandAckStatus::InvalidArgument});
    return encoded.ok() && matches(encoded.frame, expected);
}

constexpr bool round_trips_and_sequence_echo_pass() {
    constexpr std::array commands{
        apogee::CommandMessage{7U, apogee::CommandId::Ping, 0U},
        apogee::CommandMessage{8U, apogee::CommandId::EnterSafeMode, 0U},
        apogee::CommandMessage{
            9U, apogee::CommandId::SetTelemetryPeriod, 0xA1B2C3D4U}};

    for (const auto& command : commands) {
        const auto encoded = apogee::encode_command(command);
        const auto decoded = apogee::decode_command(bytes(encoded.frame));
        if (!encoded.ok() || !decoded.ok() ||
            decoded.message.sequence != command.sequence ||
            decoded.message.command != command.command ||
            decoded.message.period_ms != command.period_ms) {
            return false;
        }

        const auto ack = apogee::encode_command_ack(
            {decoded.message.sequence,
             decoded.message.command,
             apogee::CommandAckStatus::Accepted});
        const auto decoded_ack = apogee::decode_command_ack(bytes(ack.frame));
        if (!ack.ok() || !decoded_ack.ok() ||
            decoded_ack.message.sequence != command.sequence ||
            decoded_ack.message.command != command.command) {
            return false;
        }
    }
    return true;
}

constexpr apogee::EncodedFrame encode_payload(apogee::MessageType type,
                                               std::uint16_t length,
                                               std::uint8_t first,
                                               std::uint8_t second = 0U) {
    apogee::Frame frame;
    frame.message_type = type;
    frame.payload_length = length;
    frame.payload[0U] = first;
    frame.payload[1U] = second;
    return apogee::encode(frame).frame;
}

constexpr bool command_rejections_pass() {
    const auto empty = encode_payload(apogee::MessageType::Command, 0U, 0U);
    const auto long_ping =
        encode_payload(apogee::MessageType::Command, 2U, 0x01U);
    const auto short_period =
        encode_payload(apogee::MessageType::Command, 1U, 0x03U);
    const auto unknown =
        encode_payload(apogee::MessageType::Command, 1U, 0xFFU);
    const auto wrong_type =
        encode_payload(apogee::MessageType::Telemetry, 1U, 0x01U);

    return apogee::decode_command(bytes(empty)).status ==
               apogee::CommandDecodeStatus::InvalidPayloadLength &&
           apogee::decode_command(bytes(long_ping)).status ==
               apogee::CommandDecodeStatus::InvalidPayloadLength &&
           apogee::decode_command(bytes(short_period)).status ==
               apogee::CommandDecodeStatus::InvalidPayloadLength &&
           apogee::decode_command(bytes(unknown)).status ==
               apogee::CommandDecodeStatus::UnknownCommand &&
           apogee::decode_command(bytes(wrong_type)).status ==
               apogee::CommandDecodeStatus::IncorrectMessageType &&
           apogee::encode_command(
               {0U, static_cast<apogee::CommandId>(99), 0U})
                   .status == apogee::CommandEncodeStatus::UnknownCommand;
}

constexpr bool acknowledgement_rejections_pass() {
    const auto short_ack =
        encode_payload(apogee::MessageType::CommandAck, 1U, 0x01U);
    const auto unknown_command =
        encode_payload(apogee::MessageType::CommandAck, 2U, 0xFFU, 0U);
    const auto unknown_status =
        encode_payload(apogee::MessageType::CommandAck, 2U, 0x01U, 4U);
    const auto wrong_type =
        encode_payload(apogee::MessageType::Command, 1U, 0x01U);

    const auto invalid_command = apogee::encode_command_ack(
        {0U,
         static_cast<apogee::CommandId>(99),
         apogee::CommandAckStatus::Accepted});
    const auto invalid_status = apogee::encode_command_ack(
        {0U,
         apogee::CommandId::Ping,
         static_cast<apogee::CommandAckStatus>(99)});

    return apogee::decode_command_ack(bytes(short_ack)).status ==
               apogee::CommandAckDecodeStatus::InvalidPayloadLength &&
           apogee::decode_command_ack(bytes(unknown_command)).status ==
               apogee::CommandAckDecodeStatus::UnknownCommand &&
           apogee::decode_command_ack(bytes(unknown_status)).status ==
               apogee::CommandAckDecodeStatus::UnknownStatus &&
           apogee::decode_command_ack(bytes(wrong_type)).status ==
               apogee::CommandAckDecodeStatus::IncorrectMessageType &&
           invalid_command.status ==
               apogee::CommandAckEncodeStatus::UnknownCommand &&
           invalid_status.status ==
               apogee::CommandAckEncodeStatus::UnknownStatus;
}

constexpr bool frame_failures_propagate() {
    auto command = apogee::encode_command(
                       {42U, apogee::CommandId::SetTelemetryPeriod, 1'000U})
                       .frame;
    command.bytes[10U] ^= 0x01U;
    const auto command_result = apogee::decode_command(bytes(command));

    auto ack = apogee::encode_command_ack(
                   {42U,
                    apogee::CommandId::SetTelemetryPeriod,
                    apogee::CommandAckStatus::Accepted})
                   .frame;
    ack.bytes[11U] ^= 0x01U;
    const auto ack_result = apogee::decode_command_ack(bytes(ack));

    return command_result.status ==
               apogee::CommandDecodeStatus::FrameDecodeFailure &&
           command_result.frame_status == apogee::DecodeStatus::CrcMismatch &&
           ack_result.status ==
               apogee::CommandAckDecodeStatus::FrameDecodeFailure &&
           ack_result.frame_status == apogee::DecodeStatus::CrcMismatch;
}

constexpr bool all_tests_pass() {
    return golden_commands_pass() && golden_ack_passes() &&
           round_trips_and_sequence_echo_pass() &&
           command_rejections_pass() && acknowledgement_rejections_pass() &&
           frame_failures_propagate();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
