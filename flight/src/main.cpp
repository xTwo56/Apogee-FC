#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(apogee_flight, LOG_LEVEL_INF);

int main() {
    LOG_INF("Apogee-FC flight computer booting");
    return 0;
}
