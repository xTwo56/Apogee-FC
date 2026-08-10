#include <apogee/telemetry.hpp>

#include <cstdint>
#include <limits>

namespace {

constexpr apogee::SensorReadings readings{
    7'425U,
    -1'250,
    318U,
};

constexpr bool field_preservation_passes() {
    apogee::TelemetryCollector collector;
    const auto snapshot =
        collector.capture(98'765U, apogee::FlightMode::Nominal, readings);

    return snapshot.sequence == 0U && snapshot.uptime_ms == 98'765U &&
           snapshot.flight_mode == apogee::FlightMode::Nominal &&
           snapshot.sensor_readings.battery_mv == readings.battery_mv &&
           snapshot.sensor_readings.board_temperature_centi_c ==
               readings.board_temperature_centi_c &&
           snapshot.sensor_readings.solar_current_ma ==
               readings.solar_current_ma;
}

constexpr bool sequential_captures_pass() {
    apogee::TelemetryCollector collector;
    const auto first =
        collector.capture(10U, apogee::FlightMode::Boot, readings);
    const auto second =
        collector.capture(20U, apogee::FlightMode::Safe, readings);
    const auto third =
        collector.capture(30U, apogee::FlightMode::Nominal, readings);

    return first.sequence == 0U && second.sequence == 1U &&
           third.sequence == 2U;
}

constexpr bool independent_collectors_pass() {
    apogee::TelemetryCollector first_collector;
    apogee::TelemetryCollector second_collector;

    const auto first_capture =
        first_collector.capture(1U, apogee::FlightMode::Boot, readings);
    const auto second_capture =
        first_collector.capture(2U, apogee::FlightMode::Safe, readings);
    const auto independent_capture =
        second_collector.capture(3U, apogee::FlightMode::Nominal, readings);

    return first_capture.sequence == 0U && second_capture.sequence == 1U &&
           independent_capture.sequence == 0U;
}

constexpr bool sequence_rollover_passes() {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    apogee::TelemetryCollector collector{maximum};

    const auto maximum_capture =
        collector.capture(1U, apogee::FlightMode::Safe, readings);
    const auto rollover_capture =
        collector.capture(2U, apogee::FlightMode::Safe, readings);

    return maximum_capture.sequence == maximum &&
           rollover_capture.sequence == 0U;
}

constexpr bool all_tests_pass() {
    return field_preservation_passes() && sequential_captures_pass() &&
           independent_collectors_pass() && sequence_rollover_passes();
}

static_assert(all_tests_pass());

}  // namespace

int main() {
    return all_tests_pass() ? 0 : 1;
}
