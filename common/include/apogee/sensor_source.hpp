#pragma once

#include <apogee/telemetry.hpp>

namespace apogee {

class SensorSource {
public:
    virtual ~SensorSource() = default;

    [[nodiscard]] virtual SensorReadings read() noexcept = 0;
};

class SimulatedSensorSource final : public SensorSource {
public:
    [[nodiscard]] constexpr SensorReadings read() noexcept override {
        const auto reading = samples_[next_sample_];
        ++next_sample_;
        if (next_sample_ == sample_count_) {
            next_sample_ = 0U;
        }
        return reading;
    }

private:
    inline static constexpr SensorReadings samples_[]{
        SensorReadings{7'400U, 2'200, 320U},
        SensorReadings{7'360U, 2'250, 340U},
        SensorReadings{7'320U, 2'300, 360U},
    };
    inline static constexpr std::uint8_t sample_count_{3U};

    std::uint8_t next_sample_{0U};
};

}  // namespace apogee
