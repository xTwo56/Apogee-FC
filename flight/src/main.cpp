#include <apogee/fault_manager.hpp>
#include <apogee/flight_mode.hpp>
#include <apogee/health.hpp>
#include <apogee/sensor_source.hpp>
#include <apogee/telemetry.hpp>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(apogee_flight, LOG_LEVEL_INF);

namespace {

const char* mode_name(apogee::FlightMode mode) {
    switch (mode) {
    case apogee::FlightMode::Boot:
        return "Boot";
    case apogee::FlightMode::Safe:
        return "Safe";
    case apogee::FlightMode::Nominal:
        return "Nominal";
    }

    return "Unknown";
}

}  // namespace

int main() {
    LOG_INF("Apogee-FC flight computer booting");

    apogee::FlightModeStateMachine state_machine;
    LOG_INF("Initial flight mode: Boot");

    if (state_machine.dispatch(apogee::FlightEvent::BootCompleted)) {
        LOG_INF("Flight mode transition: Boot -> Safe");
    }

    apogee::SimulatedSensorSource sensor_source;
    apogee::TelemetryCollector telemetry_collector;
    apogee::HealthMonitor health_monitor;
    apogee::FaultManager fault_manager{true};

    // Evaluate health before capturing each deterministic telemetry sample.
    while (true) {
        const auto readings = sensor_source.read();
        const auto health_report = health_monitor.evaluate(readings);
        const auto event = fault_manager.update(health_report);

        if (event.has_value()) {
            const auto previous_mode = state_machine.mode();
            if (state_machine.dispatch(*event)) {
                LOG_INF("Flight mode transition: %s -> %s",
                        mode_name(previous_mode),
                        mode_name(state_machine.mode()));
            }
        }

        const auto uptime_ms = static_cast<std::uint64_t>(k_uptime_get());
        const auto snapshot = telemetry_collector.capture(
            uptime_ms, state_machine.mode(), readings);
        const auto latched_fault_mask =
            fault_manager.latched_faults().fault_mask();

        LOG_INF("telemetry sequence=%u uptime_ms=%llu mode=%s"
                " battery_mv=%u temperature_centi_c=%d solar_current_ma=%u"
                " latched_fault_mask=0x%02x",
                static_cast<unsigned int>(snapshot.sequence),
                static_cast<unsigned long long>(snapshot.uptime_ms),
                mode_name(snapshot.flight_mode),
                static_cast<unsigned int>(
                    snapshot.sensor_readings.battery_mv),
                static_cast<int>(
                    snapshot.sensor_readings.board_temperature_centi_c),
                static_cast<unsigned int>(
                    snapshot.sensor_readings.solar_current_ma),
                static_cast<unsigned int>(latched_fault_mask));

        k_sleep(K_SECONDS(1));
    }
}
