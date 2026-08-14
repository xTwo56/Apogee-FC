#include <apogee/frame_stream_decoder.hpp>
#include <apogee/telemetry_codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>

namespace {

constexpr const char* mode_name(apogee::FlightMode mode) noexcept {
    switch (mode) {
    case apogee::FlightMode::Boot:
        return "Boot";
    case apogee::FlightMode::Safe:
        return "Safe";
    case apogee::FlightMode::Nominal:
        return "Nominal";
    }
    return "Unknown";
}

void print_message(const apogee::TelemetryMessage& message) {
    const auto& snapshot = message.snapshot;
    const auto& readings = snapshot.sensor_readings;
    std::cout << "sequence=" << snapshot.sequence
              << " uptime_ms=" << snapshot.uptime_ms
              << " mode=" << mode_name(snapshot.flight_mode)
              << " battery_mv=" << readings.battery_mv
              << " temperature_centi_c="
              << readings.board_temperature_centi_c
              << " solar_current_ma=" << readings.solar_current_ma
              << " fault_mask=" << message.latched_fault_mask << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: apogee_ground_receiver <binary-file>\n";
        return 1;
    }

    std::ifstream input{argv[1], std::ios::binary};
    if (!input) {
        std::cerr << "Unable to open telemetry file: " << argv[1] << '\n';
        return 1;
    }

    apogee::FrameStreamDecoder decoder;
    std::array<char, 256U> input_buffer{};
    std::array<std::uint8_t, 256U> bytes{};
    std::size_t valid_frames{0U};
    bool malformed{false};

    const auto handle_event = [&](const apogee::FrameStreamEvent& event) {
        if (event.status == apogee::FrameStreamStatus::FrameDecoded) {
            const auto decoded = apogee::decode_telemetry(
                std::span<const std::uint8_t>{event.frame.bytes.data(),
                                              event.frame.size});
            if (decoded.ok()) {
                print_message(decoded.message);
                ++valid_frames;
            } else {
                std::cerr << "Malformed telemetry frame\n";
                malformed = true;
            }
        } else if (event.status == apogee::FrameStreamStatus::NoiseDiscarded) {
            std::cerr << "Discarded " << event.discarded_bytes
                      << " noise byte(s)\n";
            malformed = true;
        } else if (event.status == apogee::FrameStreamStatus::Truncated) {
            std::cerr << "Truncated frame at end of stream\n";
            malformed = true;
        } else {
            std::cerr << "Rejected malformed frame\n";
            malformed = true;
        }
    };

    while (input) {
        input.read(input_buffer.data(),
                   static_cast<std::streamsize>(input_buffer.size()));
        const auto count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            bytes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(
                static_cast<unsigned char>(
                    input_buffer[static_cast<std::size_t>(index)]));
        }
        decoder.push(
            std::span<const std::uint8_t>{bytes.data(),
                                          static_cast<std::size_t>(count)},
            handle_event);
    }

    if (input.bad()) {
        std::cerr << "Error while reading telemetry file\n";
        return 1;
    }

    decoder.finish(handle_event);
    return valid_frames == 0U || malformed ? 1 : 0;
}
