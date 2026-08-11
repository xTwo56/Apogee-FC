#include <apogee/fault_manager.hpp>
#include <apogee/sensor_source.hpp>

namespace {

constexpr bool matches(apogee::SensorReadings actual,
                       apogee::SensorReadings expected) {
    return actual.battery_mv == expected.battery_mv &&
           actual.board_temperature_centi_c ==
               expected.board_temperature_centi_c &&
           actual.solar_current_ma == expected.solar_current_ma;
}

constexpr bool sample_order_values_and_wraparound_pass() {
    apogee::SimulatedSensorSource simulated_source;
    apogee::SensorSource& source = simulated_source;

    const auto first = source.read();
    const auto second = source.read();
    const auto third = source.read();
    const auto battery_fault = source.read();
    const auto combined_faults = source.read();
    const auto recovery_first = source.read();
    const auto recovery_second = source.read();
    const auto recovery_third = source.read();
    const auto wrapped = source.read();

    return matches(first, {7'400U, 2'200, 320U}) &&
           matches(second, {7'380U, 2'250, 340U}) &&
           matches(third, {7'360U, 2'300, 360U}) &&
           matches(battery_fault, {6'900U, 2'400, 300U}) &&
           matches(combined_faults, {6'800U, 6'100, 280U}) &&
           matches(recovery_first, {7'340U, 2'350, 320U}) &&
           matches(recovery_second, {7'320U, 2'400, 340U}) &&
           matches(recovery_third, {7'300U, 2'450, 360U}) &&
           matches(wrapped, first);
}

constexpr bool scenario_transitions_pass() {
    apogee::SimulatedSensorSource source;
    apogee::HealthMonitor monitor;
    apogee::FaultManager fault_manager{true};
    apogee::FlightModeStateMachine state_machine;
    static_cast<void>(
        state_machine.dispatch(apogee::FlightEvent::BootCompleted));

    const auto process_sample = [&]() constexpr {
        const auto event = fault_manager.update(monitor.evaluate(source.read()));
        if (event.has_value()) {
            static_cast<void>(state_machine.dispatch(*event));
        }
    };

    process_sample();
    process_sample();
    const bool recovering_in_safe =
        state_machine.mode() == apogee::FlightMode::Safe;
    process_sample();
    const bool entered_nominal =
        state_machine.mode() == apogee::FlightMode::Nominal;
    process_sample();
    const bool faulted_immediately =
        state_machine.mode() == apogee::FlightMode::Safe;
    process_sample();
    const auto combined_faults = fault_manager.latched_faults();
    const bool both_faults_latched =
        state_machine.mode() == apogee::FlightMode::Safe &&
        combined_faults.has_fault(apogee::Fault::BatteryUndervoltage) &&
        combined_faults.has_fault(apogee::Fault::BoardOvertemperature);
    process_sample();
    process_sample();
    process_sample();
    const bool recovered_to_nominal =
        state_machine.mode() == apogee::FlightMode::Nominal &&
        fault_manager.latched_faults().healthy();
    process_sample();
    const bool deterministic_wrap =
        state_machine.mode() == apogee::FlightMode::Nominal;

    return recovering_in_safe && entered_nominal && faulted_immediately &&
           both_faults_latched && recovered_to_nominal && deterministic_wrap;
}

static_assert(sample_order_values_and_wraparound_pass());
static_assert(scenario_transitions_pass());

}  // namespace

int main() {
    return sample_order_values_and_wraparound_pass() &&
                   scenario_transitions_pass()
               ? 0
               : 1;
}
