#include <apogee/fault_manager.hpp>

namespace {

constexpr apogee::HealthMonitor monitor;
constexpr auto healthy = monitor.evaluate({7'400U, 2'500, 0U});
constexpr auto battery_fault = monitor.evaluate({6'900U, 2'500, 0U});
constexpr auto under_temperature_fault =
    monitor.evaluate({7'400U, -1'100, 0U});
constexpr auto over_temperature_fault =
    monitor.evaluate({7'400U, 6'100, 0U});

constexpr bool is_event(std::optional<apogee::FlightEvent> event,
                        apogee::FlightEvent expected) {
    return event.has_value() && *event == expected;
}

constexpr bool detection_and_duplicate_suppression_pass() {
    apogee::FaultManager manager;

    const auto detection = manager.update(battery_fault);
    const bool detected =
        is_event(detection, apogee::FlightEvent::FaultDetected) &&
        manager.faulted() && manager.healthy_streak() == 0U &&
        manager.latched_faults().has_fault(
            apogee::Fault::BatteryUndervoltage);

    const auto duplicate = manager.update(battery_fault);
    return detected && !duplicate.has_value() && manager.faulted() &&
           manager.healthy_streak() == 0U;
}

constexpr bool initially_faulted_recovery_pass() {
    apogee::FaultManager manager{true};

    const auto first = manager.update(healthy);
    const auto second = manager.update(healthy);
    const auto third = manager.update(healthy);

    return !first.has_value() && !second.has_value() &&
           is_event(third, apogee::FlightEvent::HealthConfirmed) &&
           !manager.faulted();
}

constexpr bool recovery_and_event_timing_pass() {
    apogee::FaultManager manager;
    const auto detection = manager.update(under_temperature_fault);
    const auto first_healthy = manager.update(healthy);
    const bool first_pending = !first_healthy.has_value() && manager.faulted() &&
                               manager.healthy_streak() == 1U;
    const auto second_healthy = manager.update(healthy);
    const bool second_pending =
        !second_healthy.has_value() && manager.faulted() &&
        manager.healthy_streak() == 2U;
    const auto third_healthy = manager.update(healthy);
    const bool confirmed =
        is_event(third_healthy, apogee::FlightEvent::HealthConfirmed) &&
        !manager.faulted() && manager.healthy_streak() == 3U &&
        manager.latched_faults().healthy();
    const auto later_healthy = manager.update(healthy);

    return is_event(detection, apogee::FlightEvent::FaultDetected) &&
           first_pending && second_pending && confirmed &&
           !later_healthy.has_value() && manager.healthy_streak() == 3U;
}

constexpr bool interrupted_recovery_pass() {
    apogee::FaultManager manager;
    const auto detection = manager.update(battery_fault);
    const auto first_healthy = manager.update(healthy);
    const auto second_healthy = manager.update(healthy);
    const bool recovering = !first_healthy.has_value() &&
                            !second_healthy.has_value() &&
                            manager.healthy_streak() == 2U;

    const auto interruption = manager.update(over_temperature_fault);
    const bool reset = !interruption.has_value() && manager.faulted() &&
                       manager.healthy_streak() == 0U;
    const auto restarted_first = manager.update(healthy);
    const auto restarted_second = manager.update(healthy);
    const auto restarted_third = manager.update(healthy);

    return is_event(detection, apogee::FlightEvent::FaultDetected) &&
           recovering && reset && !restarted_first.has_value() &&
           !restarted_second.has_value() &&
           is_event(restarted_third, apogee::FlightEvent::HealthConfirmed);
}

constexpr bool multiple_latched_faults_pass() {
    apogee::FaultManager manager;
    const auto first = manager.update(battery_fault);
    const auto second = manager.update(over_temperature_fault);
    const auto latched = manager.latched_faults();

    return is_event(first, apogee::FlightEvent::FaultDetected) &&
           !second.has_value() &&
           latched.has_fault(apogee::Fault::BatteryUndervoltage) &&
           latched.has_fault(apogee::Fault::BoardOvertemperature) &&
           !latched.has_fault(apogee::Fault::BoardUndertemperature);
}

constexpr bool counter_saturation_pass() {
    apogee::FaultManager manager;
    static_cast<void>(manager.update(battery_fault));
    static_cast<void>(manager.update(healthy));
    static_cast<void>(manager.update(healthy));
    static_cast<void>(manager.update(healthy));

    for (unsigned int sample = 0U; sample < 300U; ++sample) {
        if (manager.update(healthy).has_value()) {
            return false;
        }
    }
    return manager.healthy_streak() ==
           apogee::FaultManager::required_healthy_samples;
}

constexpr bool all_tests_pass() {
    return detection_and_duplicate_suppression_pass() &&
           initially_faulted_recovery_pass() &&
           recovery_and_event_timing_pass() && interrupted_recovery_pass() &&
           multiple_latched_faults_pass() && counter_saturation_pass();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
