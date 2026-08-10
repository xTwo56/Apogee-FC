#pragma once

#include <apogee/flight_mode.hpp>

#include <cstdint>

namespace apogee {

struct SensorReadings {
    std::uint16_t battery_mv;
    std::int16_t board_temperature_centi_c;
    std::uint16_t solar_current_ma;
};

struct TelemetrySnapshot {
    std::uint32_t sequence;
    std::uint64_t uptime_ms;
    FlightMode flight_mode;
    SensorReadings sensor_readings;
};

class TelemetryCollector {
public:
    constexpr explicit TelemetryCollector(
        std::uint32_t initial_sequence = 0U) noexcept
        : next_sequence_{initial_sequence} {}

    [[nodiscard]] constexpr TelemetrySnapshot capture(
        std::uint64_t uptime_ms,
        FlightMode flight_mode,
        SensorReadings sensor_readings) noexcept {
        const TelemetrySnapshot snapshot{
            next_sequence_, uptime_ms, flight_mode, sensor_readings};

        // Unsigned arithmetic gives the sequence counter defined rollover.
        ++next_sequence_;
        return snapshot;
    }

private:
    std::uint32_t next_sequence_;
};

}  // namespace apogee
