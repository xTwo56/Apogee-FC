#pragma once

#include <apogee/telemetry.hpp>

#include <cstdint>

namespace apogee {

enum class Fault : std::uint8_t {
    BatteryUndervoltage = 1U << 0U,
    BoardUndertemperature = 1U << 1U,
    BoardOvertemperature = 1U << 2U,
};

class HealthReport {
public:
    constexpr explicit HealthReport(std::uint8_t active_faults = 0U) noexcept
        : active_faults_{active_faults} {}

    [[nodiscard]] constexpr bool healthy() const noexcept {
        return active_faults_ == 0U;
    }

    [[nodiscard]] constexpr bool has_fault(Fault fault) const noexcept {
        return (active_faults_ & static_cast<std::uint8_t>(fault)) != 0U;
    }

    [[nodiscard]] constexpr std::uint8_t fault_mask() const noexcept {
        return active_faults_;
    }

private:
    std::uint8_t active_faults_;
};

class HealthMonitor {
public:
    inline static constexpr std::uint16_t minimum_battery_mv{7'000U};
    inline static constexpr std::int16_t minimum_temperature_centi_c{-1'000};
    inline static constexpr std::int16_t maximum_temperature_centi_c{6'000};

    [[nodiscard]] constexpr HealthReport evaluate(
        SensorReadings readings) const noexcept {
        std::uint8_t active_faults{0U};

        if (readings.battery_mv < minimum_battery_mv) {
            add_fault(active_faults, Fault::BatteryUndervoltage);
        }
        if (readings.board_temperature_centi_c <
            minimum_temperature_centi_c) {
            add_fault(active_faults, Fault::BoardUndertemperature);
        }
        if (readings.board_temperature_centi_c >
            maximum_temperature_centi_c) {
            add_fault(active_faults, Fault::BoardOvertemperature);
        }

        // Solar current is valid at zero while the spacecraft is in eclipse.
        return HealthReport{active_faults};
    }

private:
    static constexpr void add_fault(std::uint8_t& active_faults,
                                    Fault fault) noexcept {
        active_faults = static_cast<std::uint8_t>(
            active_faults | static_cast<std::uint8_t>(fault));
    }
};

}  // namespace apogee
