#pragma once

#include <cstdint>

namespace apogee {

inline constexpr std::uint32_t minimum_telemetry_period_ms{250U};
inline constexpr std::uint32_t maximum_telemetry_period_ms{10'000U};

struct FlightConfiguration {
    std::uint32_t telemetry_period_ms{1'000U};
};

}  // namespace apogee
