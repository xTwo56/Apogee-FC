#include <apogee/flight_mode.hpp>

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

    // Safe mode remains idle until later milestones add explicit events.
    k_sleep(K_FOREVER);
    return 0;
}
