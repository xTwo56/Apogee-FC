#include <apogee/frame_stream_decoder.hpp>
#include <apogee/telemetry_codec.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

struct CapturedEvents {
    std::vector<apogee::EncodedFrame> frames;
    std::size_t rejected{0U};
    std::size_t noise{0U};
    std::size_t truncated{0U};

    void operator()(const apogee::FrameStreamEvent& event) {
        switch (event.status) {
        case apogee::FrameStreamStatus::FrameDecoded:
            frames.push_back(event.frame);
            break;
        case apogee::FrameStreamStatus::NoiseDiscarded:
            noise += event.discarded_bytes;
            break;
        case apogee::FrameStreamStatus::FrameRejected:
            ++rejected;
            break;
        case apogee::FrameStreamStatus::Truncated:
            ++truncated;
            break;
        }
    }
};

apogee::EncodedFrame telemetry_frame(std::uint32_t sequence,
                                     std::uint64_t uptime_ms = 1'000U) {
    const apogee::TelemetryMessage message{
        {sequence, uptime_ms, apogee::FlightMode::Safe, {7'400U, 2'250, 310U}},
        static_cast<std::uint32_t>(apogee::Fault::BatteryUndervoltage)};
    const auto encoded = apogee::encode_telemetry(message);
    return encoded.frame;
}

std::span<const std::uint8_t> bytes(const apogee::EncodedFrame& frame) {
    return {frame.bytes.data(), frame.size};
}

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool byte_at_a_time() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const auto frame = telemetry_frame(1U);
    for (std::size_t index = 0U; index < frame.size; ++index) {
        decoder.push(std::span<const std::uint8_t>{&frame.bytes[index], 1U},
                     events);
    }
    decoder.finish(events);
    return check(events.frames.size() == 1U && events.rejected == 0U &&
                     events.truncated == 0U,
                 "byte-at-a-time decoding failed");
}

bool arbitrary_chunks() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const auto frame = telemetry_frame(2U);
    constexpr std::array<std::size_t, 5U> chunks{3U, 1U, 7U, 4U, 16U};
    std::size_t offset{0U};
    for (const auto requested : chunks) {
        const auto count = std::min(requested, frame.size - offset);
        decoder.push({frame.bytes.data() + offset, count}, events);
        offset += count;
    }
    if (offset < frame.size) {
        decoder.push({frame.bytes.data() + offset, frame.size - offset}, events);
    }
    return check(events.frames.size() == 1U,
                 "arbitrary chunk decoding failed");
}

bool noise_and_back_to_back() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const auto first = telemetry_frame(3U);
    const auto second = telemetry_frame(4U);
    const std::array<std::uint8_t, 3U> prefix{0x00U, 0x11U, 0xA5U};
    const std::array<std::uint8_t, 2U> middle{0x33U, 0x44U};
    decoder.push(prefix, events);
    decoder.push(bytes(first), events);
    decoder.push(middle, events);
    decoder.push(bytes(second), events);
    return check(events.frames.size() == 2U && events.noise == 5U,
                 "noise or back-to-back recovery failed");
}

bool sync_inside_payload() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const auto frame = telemetry_frame(5U, 0x0000000000005AA5ULL);
    decoder.push(bytes(frame), events);
    return check(events.frames.size() == 1U && events.rejected == 0U,
                 "payload sync bytes disrupted decoding");
}

bool crc_failure_recovery() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    auto corrupted = telemetry_frame(6U);
    corrupted.bytes[10U] ^= 0x01U;
    const auto valid = telemetry_frame(7U);
    decoder.push(bytes(corrupted), events);
    decoder.push(bytes(valid), events);
    return check(events.rejected >= 1U && events.frames.size() == 1U,
                 "CRC failure recovery failed");
}

bool invalid_length_recovery() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const std::array<std::uint8_t, 6U> invalid{
        0xA5U, 0x5AU, 0x01U, 0x01U, 0xFFU, 0xFFU};
    const auto valid = telemetry_frame(8U);
    decoder.push(invalid, events);
    decoder.push(bytes(valid), events);
    return check(events.rejected == 1U && events.frames.size() == 1U,
                 "invalid-length recovery failed");
}

bool dropped_byte_recovery() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const auto damaged = telemetry_frame(9U);
    const auto valid = telemetry_frame(10U);
    decoder.push({damaged.bytes.data(), 15U}, events);
    decoder.push({damaged.bytes.data() + 16U, damaged.size - 16U}, events);
    decoder.push(bytes(valid), events);
    return check(events.rejected >= 1U && events.frames.size() == 1U,
                 "dropped-byte recovery failed");
}

bool truncated_final_frame() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    const auto frame = telemetry_frame(11U);
    decoder.push({frame.bytes.data(), frame.size - 2U}, events);
    decoder.finish(events);
    return check(events.frames.empty() && events.truncated == 1U,
                 "truncated final frame was not reported");
}

bool telemetry_order() {
    apogee::FrameStreamDecoder decoder;
    CapturedEvents events;
    for (std::uint32_t sequence = 20U; sequence < 23U; ++sequence) {
        const auto frame = telemetry_frame(sequence);
        decoder.push(bytes(frame), events);
    }
    if (!check(events.frames.size() == 3U,
               "multiple telemetry frames were not decoded")) {
        return false;
    }
    for (std::size_t index = 0U; index < events.frames.size(); ++index) {
        const auto decoded = apogee::decode_telemetry(bytes(events.frames[index]));
        if (!check(decoded.ok() &&
                       decoded.message.snapshot.sequence == 20U + index,
                   "telemetry frame order changed")) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const bool passed = byte_at_a_time() && arbitrary_chunks() &&
                        noise_and_back_to_back() && sync_inside_payload() &&
                        crc_failure_recovery() && invalid_length_recovery() &&
                        dropped_byte_recovery() && truncated_final_frame() &&
                        telemetry_order();
    return passed ? 0 : 1;
}
