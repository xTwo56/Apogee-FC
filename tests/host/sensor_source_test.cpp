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
    const auto wrapped = source.read();

    return matches(first, {7'400U, 2'200, 320U}) &&
           matches(second, {7'360U, 2'250, 340U}) &&
           matches(third, {7'320U, 2'300, 360U}) && matches(wrapped, first);
}

static_assert(sample_order_values_and_wraparound_pass());

}  // namespace

int main() {
    return sample_order_values_and_wraparound_pass() ? 0 : 1;
}
