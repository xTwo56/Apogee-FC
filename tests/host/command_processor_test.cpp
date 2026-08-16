#include <apogee/command_processor.hpp>

#include <array>
#include <cstdint>

namespace {

constexpr bool acknowledgement_matches(
    const apogee::CommandAckMessage& acknowledgement,
    const apogee::CommandMessage& command,
    apogee::CommandAckStatus status) noexcept {
    return acknowledgement.sequence == command.sequence &&
           acknowledgement.command == command.command &&
           acknowledgement.status == status;
}

constexpr bool default_configuration_passes() {
    const apogee::FlightConfiguration configuration;
    return configuration.telemetry_period_ms == 1'000U;
}

constexpr bool ping_passes_in_every_mode() {
    constexpr std::array modes{
        apogee::FlightMode::Boot,
        apogee::FlightMode::Safe,
        apogee::FlightMode::Nominal,
    };

    for (const auto mode : modes) {
        apogee::FlightModeStateMachine state_machine{mode};
        apogee::FlightConfiguration configuration{2'000U};
        apogee::CommandProcessor processor{state_machine, configuration};
        const apogee::CommandMessage command{
            0x12345678U, apogee::CommandId::Ping, 0U};
        const auto acknowledgement = processor.process(command);

        if (!acknowledgement_matches(
                acknowledgement,
                command,
                apogee::CommandAckStatus::Accepted) ||
            state_machine.mode() != mode ||
            configuration.telemetry_period_ms != 2'000U) {
            return false;
        }
    }
    return true;
}

constexpr bool enter_safe_mode_passes() {
    {
        apogee::FlightModeStateMachine state_machine{apogee::FlightMode::Boot};
        apogee::FlightConfiguration configuration{1'500U};
        apogee::CommandProcessor processor{state_machine, configuration};
        const apogee::CommandMessage command{
            10U, apogee::CommandId::EnterSafeMode, 0U};
        const auto acknowledgement = processor.process(command);
        if (!acknowledgement_matches(
                acknowledgement,
                command,
                apogee::CommandAckStatus::RejectedByState) ||
            state_machine.mode() != apogee::FlightMode::Boot ||
            configuration.telemetry_period_ms != 1'500U) {
            return false;
        }
    }

    {
        apogee::FlightModeStateMachine state_machine{apogee::FlightMode::Safe};
        apogee::FlightConfiguration configuration;
        apogee::CommandProcessor processor{state_machine, configuration};
        const apogee::CommandMessage command{
            11U, apogee::CommandId::EnterSafeMode, 0U};
        const auto acknowledgement = processor.process(command);
        if (!acknowledgement_matches(
                acknowledgement,
                command,
                apogee::CommandAckStatus::Accepted) ||
            state_machine.mode() != apogee::FlightMode::Safe) {
            return false;
        }
    }

    {
        apogee::FlightModeStateMachine state_machine{
            apogee::FlightMode::Nominal};
        apogee::FlightConfiguration configuration;
        apogee::CommandProcessor processor{state_machine, configuration};
        const apogee::CommandMessage first{
            12U, apogee::CommandId::EnterSafeMode, 0U};
        const apogee::CommandMessage second{
            13U, apogee::CommandId::EnterSafeMode, 0U};
        const auto first_ack = processor.process(first);
        const auto second_ack = processor.process(second);
        if (!acknowledgement_matches(first_ack,
                                     first,
                                     apogee::CommandAckStatus::Accepted) ||
            !acknowledgement_matches(second_ack,
                                     second,
                                     apogee::CommandAckStatus::Accepted) ||
            state_machine.mode() != apogee::FlightMode::Safe) {
            return false;
        }
    }

    return true;
}

constexpr bool telemetry_period_boundaries_pass() {
    constexpr std::array modes{
        apogee::FlightMode::Boot,
        apogee::FlightMode::Safe,
        apogee::FlightMode::Nominal,
    };
    constexpr std::array periods{
        apogee::minimum_telemetry_period_ms,
        apogee::maximum_telemetry_period_ms,
    };

    std::uint32_t sequence{20U};
    for (const auto mode : modes) {
        for (const auto period : periods) {
            apogee::FlightModeStateMachine state_machine{mode};
            apogee::FlightConfiguration configuration;
            apogee::CommandProcessor processor{state_machine, configuration};
            const apogee::CommandMessage command{
                sequence++, apogee::CommandId::SetTelemetryPeriod, period};
            const auto acknowledgement = processor.process(command);
            if (!acknowledgement_matches(
                    acknowledgement,
                    command,
                    apogee::CommandAckStatus::Accepted) ||
                configuration.telemetry_period_ms != period ||
                state_machine.mode() != mode) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool invalid_periods_leave_state_unchanged() {
    constexpr std::array periods{
        apogee::minimum_telemetry_period_ms - 1U,
        apogee::maximum_telemetry_period_ms + 1U,
    };

    for (const auto period : periods) {
        apogee::FlightModeStateMachine state_machine{
            apogee::FlightMode::Nominal};
        apogee::FlightConfiguration configuration{2'500U};
        apogee::CommandProcessor processor{state_machine, configuration};
        const apogee::CommandMessage command{
            30U, apogee::CommandId::SetTelemetryPeriod, period};
        const auto acknowledgement = processor.process(command);
        if (!acknowledgement_matches(
                acknowledgement,
                command,
                apogee::CommandAckStatus::InvalidArgument) ||
            configuration.telemetry_period_ms != 2'500U ||
            state_machine.mode() != apogee::FlightMode::Nominal) {
            return false;
        }
    }
    return true;
}

constexpr bool unknown_command_passes() {
    apogee::FlightModeStateMachine state_machine{apogee::FlightMode::Safe};
    apogee::FlightConfiguration configuration{3'000U};
    apogee::CommandProcessor processor{state_machine, configuration};
    const apogee::CommandMessage command{
        99U, static_cast<apogee::CommandId>(99), 0U};
    const auto acknowledgement = processor.process(command);
    return acknowledgement_matches(acknowledgement,
                                   command,
                                   apogee::CommandAckStatus::InvalidCommand) &&
           state_machine.mode() == apogee::FlightMode::Safe &&
           configuration.telemetry_period_ms == 3'000U;
}

constexpr bool all_tests_pass() {
    return default_configuration_passes() && ping_passes_in_every_mode() &&
           enter_safe_mode_passes() && telemetry_period_boundaries_pass() &&
           invalid_periods_leave_state_unchanged() && unknown_command_passes();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
