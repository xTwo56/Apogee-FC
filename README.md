<h1 align="center">Apogee-FC</h1>

<p align="center">
  A Renode-simulated CubeSat flight computer built in C++20 for STM32F4.
</p>

## Contents

* [Highlights](#highlights)
* [Architecture](#architecture)
* [Build and test](#build-and-test)
* [Run the simulation](#run-the-simulation)
* [What this project demonstrates](#what-this-project-demonstrates)
* [Future work](#future-work)

## Highlights

* C++20 firmware on Zephyr RTOS, built for `stm32f4_disco`
* Renode simulation: no physical hardware (a whole satellite) required
* Deterministic spacecraft sensor scenario: battery, board temperature, and solar current
* Explicit flight-mode state machine: `Boot → Safe → Nominal`
* Fault detection with latched fault masks and three-sample healthy recovery
* Versioned binary telemetry protocol with CRC-16/CCITT-FALSE
* UART separation:

  * USART2: human-readable diagnostic logs
  * UART4: binary telemetry and ground commands
* Ground client supports live socket connections and offline telemetry decoding
* Commands: `ping`, `safe`, and configurable telemetry period
* Host-side unit tests for flight logic, health rules, fault recovery, framing, stream decoding, telemetry, and commands
* Firmware compiled with C++ exceptions and RTTI disabled

## Architecture

```text
Simulated sensors
       │
       ▼
Health monitor ──► Fault manager ──► Flight-mode state machine
       │                                      │
       └──────────────► Telemetry snapshot ◄──┘
                                │
                                ▼
                    Binary frame + CRC over UART4
                                │
                                ▼
                     Ground client / offline decoder
```

## Build and test

With Zephyr and its SDK installed:

```bash
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure

west build -p always -b stm32f4_disco flight -d build/flight
```

## Run the simulation

Start Renode from the repository root:

```bash
renode --disable-gui simulation/renode/apogee_fc.resc
```

In another terminal, start the ground client:

```bash
./build/host/apogee_ground_client --connect 127.0.0.1 12345
```

Example commands:

```text
ping
period 250
safe
quit
```

The client decodes incoming telemetry and prints accepted command acknowledgements.

## Future work

* LVGL ground-console screens for telemetry, flight mode, and fault visualisation (particularly looking forward to)
* Interrupt- or DMA-driven UART transport
* Hardware sensor drivers over I2C/SPI
* Watchdog and persistent fault/event logging
* Automated Renode regression scenarios
