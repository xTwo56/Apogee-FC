#pragma once

#include <apogee/flight_mode.hpp>
#include <apogee/health.hpp>

#include <cstdint>
#include <optional>

namespace apogee {

class FaultManager {
public:
    inline static constexpr std::uint8_t required_healthy_samples{3U};

    constexpr explicit FaultManager(bool initially_faulted = false) noexcept
        : faulted_{initially_faulted} {}

    [[nodiscard]] constexpr std::optional<FlightEvent> update(
        HealthReport report) noexcept {
        if (!report.healthy()) {
            latched_fault_mask_ = static_cast<std::uint8_t>(
                latched_fault_mask_ | report.fault_mask());
            healthy_streak_ = 0U;

            if (!faulted_) {
                faulted_ = true;
                return FlightEvent::FaultDetected;
            }
            return std::nullopt;
        }

        if (!faulted_) {
            return std::nullopt;
        }

        if (healthy_streak_ < required_healthy_samples) {
            ++healthy_streak_;
        }
        if (healthy_streak_ == required_healthy_samples) {
            faulted_ = false;
            latched_fault_mask_ = 0U;
            return FlightEvent::HealthConfirmed;
        }

        return std::nullopt;
    }

    [[nodiscard]] constexpr bool faulted() const noexcept {
        return faulted_;
    }

    [[nodiscard]] constexpr std::uint8_t healthy_streak() const noexcept {
        return healthy_streak_;
    }

    [[nodiscard]] constexpr HealthReport latched_faults() const noexcept {
        return HealthReport{latched_fault_mask_};
    }

private:
    std::uint8_t latched_fault_mask_{0U};
    std::uint8_t healthy_streak_{0U};
    bool faulted_;
};

}  // namespace apogee
