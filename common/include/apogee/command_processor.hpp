#pragma once

#include <apogee/command_codec.hpp>
#include <apogee/flight_configuration.hpp>
#include <apogee/flight_mode.hpp>

namespace apogee {

class CommandProcessor {
public:
    constexpr CommandProcessor(FlightModeStateMachine& state_machine,
                               FlightConfiguration& configuration) noexcept
        : state_machine_{state_machine}, configuration_{configuration} {}

    [[nodiscard]] constexpr CommandAckMessage process(
        const CommandMessage& command) noexcept {
        CommandAckStatus status{CommandAckStatus::InvalidCommand};

        switch (command.command) {
        case CommandId::Ping:
            status = CommandAckStatus::Accepted;
            break;
        case CommandId::EnterSafeMode:
            status = enter_safe_mode();
            break;
        case CommandId::SetTelemetryPeriod:
            status = set_telemetry_period(command.period_ms);
            break;
        }

        return {command.sequence, command.command, status};
    }

private:
    [[nodiscard]] constexpr CommandAckStatus enter_safe_mode() noexcept {
        switch (state_machine_.mode()) {
        case FlightMode::Boot:
            return CommandAckStatus::RejectedByState;
        case FlightMode::Safe:
            return CommandAckStatus::Accepted;
        case FlightMode::Nominal:
            return state_machine_.dispatch(FlightEvent::SafeModeRequested)
                       ? CommandAckStatus::Accepted
                       : CommandAckStatus::RejectedByState;
        }
        return CommandAckStatus::RejectedByState;
    }

    [[nodiscard]] constexpr CommandAckStatus set_telemetry_period(
        std::uint32_t period_ms) noexcept {
        if (period_ms < minimum_telemetry_period_ms ||
            period_ms > maximum_telemetry_period_ms) {
            return CommandAckStatus::InvalidArgument;
        }

        configuration_.telemetry_period_ms = period_ms;
        return CommandAckStatus::Accepted;
    }

    FlightModeStateMachine& state_machine_;
    FlightConfiguration& configuration_;
};

}  // namespace apogee
