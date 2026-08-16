#include <apogee/flight_mode.hpp>

#include <array>
#include <cstddef>

namespace {

struct Expectation {
    bool transitioned;
    apogee::FlightMode mode;
};

constexpr std::array modes{
    apogee::FlightMode::Boot,
    apogee::FlightMode::Safe,
    apogee::FlightMode::Nominal,
};

constexpr std::array events{
    apogee::FlightEvent::BootCompleted,
    apogee::FlightEvent::HealthConfirmed,
    apogee::FlightEvent::FaultDetected,
    apogee::FlightEvent::SafeModeRequested,
};

constexpr std::array expectations{
    std::array{
        Expectation{true, apogee::FlightMode::Safe},
        Expectation{false, apogee::FlightMode::Boot},
        Expectation{false, apogee::FlightMode::Boot},
        Expectation{false, apogee::FlightMode::Boot},
    },
    std::array{
        Expectation{false, apogee::FlightMode::Safe},
        Expectation{true, apogee::FlightMode::Nominal},
        Expectation{false, apogee::FlightMode::Safe},
        Expectation{false, apogee::FlightMode::Safe},
    },
    std::array{
        Expectation{false, apogee::FlightMode::Nominal},
        Expectation{false, apogee::FlightMode::Nominal},
        Expectation{true, apogee::FlightMode::Safe},
        Expectation{true, apogee::FlightMode::Safe},
    },
};

constexpr bool all_mode_event_combinations_pass() {
    for (std::size_t mode_index = 0; mode_index < modes.size(); ++mode_index) {
        for (std::size_t event_index = 0; event_index < events.size();
             ++event_index) {
            apogee::FlightModeStateMachine state_machine{modes[mode_index]};
            const auto expected = expectations[mode_index][event_index];

            if (state_machine.dispatch(events[event_index]) !=
                    expected.transitioned ||
                state_machine.mode() != expected.mode) {
                return false;
            }
        }
    }

    return true;
}

static_assert(all_mode_event_combinations_pass());

}  // namespace

int main() {
    return all_mode_event_combinations_pass() ? 0 : 1;
}
