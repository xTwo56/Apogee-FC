#include <apogee/ground_client.hpp>

#include <apogee/command_codec.hpp>
#include <apogee/health.hpp>
#include <apogee/telemetry_codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

bool command_parsing_passes() {
    apogee::ground::CommandSequencer sequencer;
    const auto ping = sequencer.parse("ping");
    const auto safe = sequencer.parse("  safe  ");
    const auto period = sequencer.parse("period 4294967295");
    const auto quit = sequencer.parse("quit");

    return ping.status == apogee::ground::LocalCommandStatus::Command &&
           ping.command.sequence == 1U &&
           ping.command.command == apogee::CommandId::Ping &&
           safe.status == apogee::ground::LocalCommandStatus::Command &&
           safe.command.sequence == 2U &&
           safe.command.command == apogee::CommandId::EnterSafeMode &&
           period.status == apogee::ground::LocalCommandStatus::Command &&
           period.command.sequence == 3U &&
           period.command.command == apogee::CommandId::SetTelemetryPeriod &&
           period.command.period_ms == UINT32_MAX &&
           quit.status == apogee::ground::LocalCommandStatus::Quit &&
           sequencer.next_sequence() == 4U;
}

bool invalid_syntax_passes() {
    constexpr std::array invalid_inputs{
        "",
        "unknown",
        "period",
        "period -1",
        "period +1",
        "period 1000 extra",
        "period 4294967296",
    };
    apogee::ground::CommandSequencer sequencer;
    for (const auto* input : invalid_inputs) {
        if (sequencer.parse(input).status !=
            apogee::ground::LocalCommandStatus::InvalidSyntax) {
            return false;
        }
    }
    return sequencer.next_sequence() == 1U;
}

bool transmitted_commands_decode_in_sequence() {
    apogee::ground::CommandSequencer sequencer;
    constexpr std::array inputs{"ping", "safe", "period 0"};
    std::uint32_t expected_sequence{1U};
    for (const auto* input : inputs) {
        const auto parsed = sequencer.parse(input);
        const auto encoded = apogee::encode_command(parsed.command);
        const auto decoded = apogee::decode_command(
            {encoded.frame.bytes.data(), encoded.frame.size});
        if (!encoded.ok() || !decoded.ok() ||
            decoded.message.sequence != expected_sequence++) {
            return false;
        }
    }
    return true;
}

apogee::EncodedFrame telemetry_frame(std::uint32_t sequence) {
    const apogee::TelemetryMessage message{
        {sequence,
         5'000U + sequence,
         apogee::FlightMode::Nominal,
         {7'500U, -250, 320U}},
        static_cast<std::uint32_t>(apogee::Fault::BatteryUndervoltage)};
    return apogee::encode_telemetry(message).frame;
}

apogee::EncodedFrame acknowledgement_frame(std::uint32_t sequence) {
    return apogee::encode_command_ack(
               {sequence,
                apogee::CommandId::SetTelemetryPeriod,
                apogee::CommandAckStatus::InvalidArgument})
        .frame;
}

bool interleaved_fragmented_stream_passes() {
    std::ostringstream output;
    std::ostringstream errors;
    apogee::ground::GroundStreamProcessor processor{output, errors};
    const auto first = telemetry_frame(10U);
    const auto acknowledgement = acknowledgement_frame(11U);
    const auto second = telemetry_frame(12U);

    for (std::size_t index = 0U; index < first.size; ++index) {
        processor.feed({&first.bytes[index], 1U});
    }
    processor.feed({acknowledgement.bytes.data(), 5U});
    processor.feed({acknowledgement.bytes.data() + 5U,
                    acknowledgement.size - 5U});
    processor.feed({second.bytes.data(), 17U});
    processor.feed({second.bytes.data() + 17U, second.size - 17U});
    processor.finish();

    const std::string text = output.str();
    const auto first_position = text.find("sequence=10 uptime_ms=5010");
    const auto ack_position = text.find(
        "ack sequence=11 command=SetTelemetryPeriod status=InvalidArgument");
    const auto second_position = text.find("sequence=12 uptime_ms=5012");
    return first_position != std::string::npos &&
           ack_position != std::string::npos &&
           second_position != std::string::npos &&
           first_position < ack_position && ack_position < second_position &&
           text.find("mode=Nominal battery_mv=7500 temperature_centi_c=-250 "
                     "solar_current_ma=320 fault_mask=1") !=
               std::string::npos &&
           processor.decoded_messages() == 3U &&
           !processor.malformed_input() && errors.str().empty();
}

}  // namespace

int main() {
    const bool passed = command_parsing_passes() && invalid_syntax_passes() &&
                        transmitted_commands_decode_in_sequence() &&
                        interleaved_fragmented_stream_passes();
    return passed ? 0 : 1;
}
