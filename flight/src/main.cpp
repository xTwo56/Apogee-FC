#include <apogee/flight_mode.hpp>
#include <apogee/sensor_source.hpp>
#include <apogee/telemetry.hpp>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(apogee_flight, LOG_LEVEL_INF);

int main() {
    LOG_INF("Apogee-FC flight computer booting");

    apogee::FlightModeStateMachine state_machine;
    LOG_INF("Initial flight mode: Boot");

    if (state_machine.dispatch(apogee::FlightEvent::BootCompleted)) {
        LOG_INF("Flight mode transition: Boot -> Safe");
    }

    apogee::SimulatedSensorSource sensor_source;
    apogee::TelemetryCollector telemetry_collector;

    // Sample deterministic housekeeping telemetry while remaining in Safe.
    while (true) {
        const auto readings = sensor_source.read();
        const auto uptime_ms = static_cast<std::uint64_t>(k_uptime_get());
        const auto snapshot = telemetry_collector.capture(
            uptime_ms, state_machine.mode(), readings);

        LOG_INF("telemetry sequence=%u uptime_ms=%llu mode=Safe"
                " battery_mv=%u temperature_centi_c=%d solar_current_ma=%u",
                static_cast<unsigned int>(snapshot.sequence),
                static_cast<unsigned long long>(snapshot.uptime_ms),
                static_cast<unsigned int>(
                    snapshot.sensor_readings.battery_mv),
                static_cast<int>(
                    snapshot.sensor_readings.board_temperature_centi_c),
                static_cast<unsigned int>(
                    snapshot.sensor_readings.solar_current_ma));

        k_sleep(K_SECONDS(1));
    }
}
