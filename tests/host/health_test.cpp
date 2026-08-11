#include <apogee/health.hpp>

namespace {

constexpr apogee::HealthMonitor monitor;

constexpr bool healthy_readings_pass() {
    const auto report = monitor.evaluate({7'400U, 2'500, 0U});
    return report.healthy() &&
           !report.has_fault(apogee::Fault::BatteryUndervoltage) &&
           !report.has_fault(apogee::Fault::BoardUndertemperature) &&
           !report.has_fault(apogee::Fault::BoardOvertemperature);
}

constexpr bool individual_faults_pass() {
    const auto battery = monitor.evaluate({6'999U, 2'500, 300U});
    const auto under_temperature = monitor.evaluate({7'400U, -1'001, 300U});
    const auto over_temperature = monitor.evaluate({7'400U, 6'001, 300U});

    return !battery.healthy() &&
           battery.has_fault(apogee::Fault::BatteryUndervoltage) &&
           !battery.has_fault(apogee::Fault::BoardUndertemperature) &&
           !battery.has_fault(apogee::Fault::BoardOvertemperature) &&
           !under_temperature.healthy() &&
           !under_temperature.has_fault(
               apogee::Fault::BatteryUndervoltage) &&
           under_temperature.has_fault(
               apogee::Fault::BoardUndertemperature) &&
           !under_temperature.has_fault(
               apogee::Fault::BoardOvertemperature) &&
           !over_temperature.healthy() &&
           !over_temperature.has_fault(apogee::Fault::BatteryUndervoltage) &&
           !over_temperature.has_fault(
               apogee::Fault::BoardUndertemperature) &&
           over_temperature.has_fault(apogee::Fault::BoardOvertemperature);
}

constexpr bool simultaneous_faults_pass() {
    const auto report = monitor.evaluate({6'500U, -1'500, 0U});
    return !report.healthy() &&
           report.has_fault(apogee::Fault::BatteryUndervoltage) &&
           report.has_fault(apogee::Fault::BoardUndertemperature) &&
           !report.has_fault(apogee::Fault::BoardOvertemperature);
}

constexpr bool exact_boundaries_pass() {
    const auto minimums = monitor.evaluate({7'000U, -1'000, 0U});
    const auto maximum_temperature = monitor.evaluate({7'000U, 6'000, 0U});
    return minimums.healthy() && maximum_temperature.healthy();
}

constexpr bool one_unit_violations_pass() {
    const auto battery = monitor.evaluate({6'999U, 2'500, 0U});
    const auto under_temperature = monitor.evaluate({7'000U, -1'001, 0U});
    const auto over_temperature = monitor.evaluate({7'000U, 6'001, 0U});

    return battery.has_fault(apogee::Fault::BatteryUndervoltage) &&
           under_temperature.has_fault(
               apogee::Fault::BoardUndertemperature) &&
           over_temperature.has_fault(apogee::Fault::BoardOvertemperature);
}

constexpr bool all_tests_pass() {
    return healthy_readings_pass() && individual_faults_pass() &&
           simultaneous_faults_pass() && exact_boundaries_pass() &&
           one_unit_violations_pass();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
