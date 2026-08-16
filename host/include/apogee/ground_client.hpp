#pragma once

#include <apogee/command_codec.hpp>
#include <apogee/frame_stream_decoder.hpp>
#include <apogee/telemetry_codec.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string_view>

namespace apogee::ground {

enum class LocalCommandStatus {
    Command,
    Quit,
    InvalidSyntax,
};

struct LocalCommandResult {
    LocalCommandStatus status{LocalCommandStatus::InvalidSyntax};
    CommandMessage command{};
};

constexpr std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view whitespace{" \t\r\n"};
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1U);
}

class CommandSequencer {
public:
    [[nodiscard]] LocalCommandResult parse(std::string_view input) noexcept {
        const auto command_text = trim(input);
        if (command_text == "quit") {
            return {LocalCommandStatus::Quit, {}};
        }
        if (command_text == "ping") {
            return make_command(CommandId::Ping, 0U);
        }
        if (command_text == "safe") {
            return make_command(CommandId::EnterSafeMode, 0U);
        }

        constexpr std::string_view period_prefix{"period"};
        if (!command_text.starts_with(period_prefix) ||
            command_text.size() == period_prefix.size() ||
            (command_text[period_prefix.size()] != ' ' &&
             command_text[period_prefix.size()] != '\t')) {
            return {};
        }

        const auto value_text = trim(command_text.substr(period_prefix.size()));
        if (value_text.empty()) {
            return {};
        }

        std::uint32_t period_ms{0U};
        const auto conversion = std::from_chars(
            value_text.data(), value_text.data() + value_text.size(), period_ms);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != value_text.data() + value_text.size()) {
            return {};
        }
        return make_command(CommandId::SetTelemetryPeriod, period_ms);
    }

    [[nodiscard]] constexpr std::uint32_t next_sequence() const noexcept {
        return next_sequence_;
    }

private:
    [[nodiscard]] LocalCommandResult make_command(CommandId command,
                                                   std::uint32_t period_ms) noexcept {
        const CommandMessage message{next_sequence_, command, period_ms};
        ++next_sequence_;
        return {LocalCommandStatus::Command, message};
    }

    std::uint32_t next_sequence_{1U};
};

constexpr const char* mode_name(FlightMode mode) noexcept {
    switch (mode) {
    case FlightMode::Boot:
        return "Boot";
    case FlightMode::Safe:
        return "Safe";
    case FlightMode::Nominal:
        return "Nominal";
    }
    return "Unknown";
}

constexpr const char* command_name(CommandId command) noexcept {
    switch (command) {
    case CommandId::Ping:
        return "Ping";
    case CommandId::EnterSafeMode:
        return "EnterSafeMode";
    case CommandId::SetTelemetryPeriod:
        return "SetTelemetryPeriod";
    }
    return "Unknown";
}

constexpr const char* acknowledgement_name(CommandAckStatus status) noexcept {
    switch (status) {
    case CommandAckStatus::Accepted:
        return "Accepted";
    case CommandAckStatus::InvalidCommand:
        return "InvalidCommand";
    case CommandAckStatus::InvalidArgument:
        return "InvalidArgument";
    case CommandAckStatus::RejectedByState:
        return "RejectedByState";
    }
    return "Unknown";
}

class GroundStreamProcessor {
public:
    GroundStreamProcessor(std::ostream& output, std::ostream& errors) noexcept
        : output_{output}, errors_{errors} {}

    void feed(std::span<const std::uint8_t> bytes) {
        decoder_.push(bytes, [this](const FrameStreamEvent& event) {
            handle_event(event);
        });
    }

    void finish() {
        decoder_.finish([this](const FrameStreamEvent& event) {
            handle_event(event);
        });
    }

    [[nodiscard]] constexpr std::size_t decoded_messages() const noexcept {
        return decoded_messages_;
    }

    [[nodiscard]] constexpr bool malformed_input() const noexcept {
        return malformed_input_;
    }

private:
    void handle_event(const FrameStreamEvent& event) {
        if (event.status != FrameStreamStatus::FrameDecoded) {
            report_stream_error(event);
            return;
        }

        const auto frame_bytes = std::span<const std::uint8_t>{
            event.frame.bytes.data(), event.frame.size};
        const auto frame = decode(frame_bytes);
        if (!frame.ok()) {
            errors_ << "Malformed frame\n";
            malformed_input_ = true;
            return;
        }

        switch (frame.frame.message_type) {
        case MessageType::Telemetry:
            print_telemetry(frame_bytes);
            break;
        case MessageType::CommandAck:
            print_acknowledgement(frame_bytes);
            break;
        case MessageType::Command:
            errors_ << "Unexpected command frame\n";
            malformed_input_ = true;
            break;
        }
    }

    void print_telemetry(std::span<const std::uint8_t> frame_bytes) {
        const auto decoded = decode_telemetry(frame_bytes);
        if (!decoded.ok()) {
            errors_ << "Malformed telemetry frame\n";
            malformed_input_ = true;
            return;
        }

        const auto& snapshot = decoded.message.snapshot;
        const auto& readings = snapshot.sensor_readings;
        output_ << "sequence=" << snapshot.sequence
                << " uptime_ms=" << snapshot.uptime_ms
                << " mode=" << mode_name(snapshot.flight_mode)
                << " battery_mv=" << readings.battery_mv
                << " temperature_centi_c="
                << readings.board_temperature_centi_c
                << " solar_current_ma=" << readings.solar_current_ma
                << " fault_mask=" << decoded.message.latched_fault_mask
                << '\n';
        ++decoded_messages_;
    }

    void print_acknowledgement(std::span<const std::uint8_t> frame_bytes) {
        const auto decoded = decode_command_ack(frame_bytes);
        if (!decoded.ok()) {
            errors_ << "Malformed acknowledgement frame\n";
            malformed_input_ = true;
            return;
        }

        output_ << "ack sequence=" << decoded.message.sequence
                << " command=" << command_name(decoded.message.command)
                << " status=" << acknowledgement_name(decoded.message.status)
                << '\n';
        ++decoded_messages_;
    }

    void report_stream_error(const FrameStreamEvent& event) {
        if (event.status == FrameStreamStatus::NoiseDiscarded) {
            errors_ << "Discarded " << event.discarded_bytes
                    << " noise byte(s)\n";
        } else if (event.status == FrameStreamStatus::Truncated) {
            errors_ << "Truncated frame at end of stream\n";
        } else {
            errors_ << "Rejected malformed frame\n";
        }
        malformed_input_ = true;
    }

    FrameStreamDecoder decoder_;
    std::ostream& output_;
    std::ostream& errors_;
    std::size_t decoded_messages_{0U};
    bool malformed_input_{false};
};

}  // namespace apogee::ground
