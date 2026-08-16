#pragma once

namespace apogee {

enum class FlightMode {
    Boot,
    Safe,
    Nominal,
};

enum class FlightEvent {
    BootCompleted,
    HealthConfirmed,
    FaultDetected,
    SafeModeRequested,
};

class FlightModeStateMachine {
public:
    constexpr explicit FlightModeStateMachine(
        FlightMode initial_mode = FlightMode::Boot) noexcept
        : mode_{initial_mode} {}

    [[nodiscard]] constexpr FlightMode mode() const noexcept {
        return mode_;
    }

    constexpr bool dispatch(FlightEvent event) noexcept {
        switch (mode_) {
        case FlightMode::Boot:
            if (event == FlightEvent::BootCompleted) {
                mode_ = FlightMode::Safe;
                return true;
            }
            break;
        case FlightMode::Safe:
            if (event == FlightEvent::HealthConfirmed) {
                mode_ = FlightMode::Nominal;
                return true;
            }
            break;
        case FlightMode::Nominal:
            if (event == FlightEvent::FaultDetected ||
                event == FlightEvent::SafeModeRequested) {
                mode_ = FlightMode::Safe;
                return true;
            }
            break;
        }

        return false;
    }

private:
    FlightMode mode_;
};

}  // namespace apogee
