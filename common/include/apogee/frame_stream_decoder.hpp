#pragma once

#include <apogee/frame_codec.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace apogee {

enum class FrameStreamStatus {
    FrameDecoded,
    NoiseDiscarded,
    FrameRejected,
    Truncated,
};

struct FrameStreamEvent {
    FrameStreamStatus status{FrameStreamStatus::FrameDecoded};
    DecodeStatus decode_status{DecodeStatus::Ok};
    EncodedFrame frame{};
    std::size_t discarded_bytes{0U};
};

class FrameStreamDecoder {
public:
    template <typename Handler>
    constexpr void push(std::span<const std::uint8_t> bytes,
                        Handler&& handler) noexcept {
        for (const auto byte : bytes) {
            push_byte(byte, handler);
        }
    }

    template <typename Handler>
    constexpr void finish(Handler&& handler) noexcept {
        if (size_ == 0U) {
            return;
        }

        FrameStreamEvent event;
        event.status = FrameStreamStatus::Truncated;
        event.decode_status = DecodeStatus::Truncated;
        copy_frame(event.frame);
        handler(event);
        size_ = 0U;
        expected_size_ = 0U;
    }

    [[nodiscard]] constexpr std::size_t buffered_size() const noexcept {
        return size_;
    }

private:
    template <typename Handler>
    constexpr void push_byte(std::uint8_t byte, Handler& handler) noexcept {
        if (size_ == 0U) {
            if (byte == sync_first) {
                buffer_[size_++] = byte;
            } else {
                emit_noise(handler, 1U);
            }
            return;
        }

        if (size_ == 1U) {
            if (byte == sync_second) {
                buffer_[size_++] = byte;
            } else if (byte == sync_first) {
                emit_noise(handler, 1U);
            } else {
                size_ = 0U;
                emit_noise(handler, 2U);
            }
            return;
        }

        buffer_[size_++] = byte;
        process_buffer(handler);
    }

    template <typename Handler>
    constexpr void process_buffer(Handler& handler) noexcept {
        while (true) {
            if (size_ < header_size) {
                return;
            }

            const auto payload_length = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(buffer_[4U]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(buffer_[5U]) << 8U));
            if (payload_length > maximum_payload_size) {
                reject(handler, DecodeStatus::PayloadTooLarge);
                if (size_ < header_size) {
                    return;
                }
                continue;
            }

            expected_size_ = minimum_frame_size + payload_length;
            if (size_ < expected_size_) {
                return;
            }

            const auto decoded = decode(
                std::span<const std::uint8_t>{buffer_.data(), expected_size_});
            if (!decoded.ok()) {
                reject(handler, decoded.status);
                if (size_ < header_size) {
                    return;
                }
                continue;
            }

            FrameStreamEvent event;
            event.status = FrameStreamStatus::FrameDecoded;
            copy_frame(event.frame);
            event.frame.size = expected_size_;
            handler(event);
            const auto remaining = size_ - expected_size_;
            std::copy_n(buffer_.begin() +
                            static_cast<std::ptrdiff_t>(expected_size_),
                        remaining,
                        buffer_.begin());
            size_ = remaining;
            expected_size_ = 0U;
            align_to_sync(handler);
            if (size_ < header_size) {
                return;
            }
        }
    }

    template <typename Handler>
    constexpr void reject(Handler& handler, DecodeStatus status) noexcept {
        FrameStreamEvent event;
        event.status = FrameStreamStatus::FrameRejected;
        event.decode_status = status;
        copy_frame(event.frame);
        handler(event);
        rescan();
    }

    constexpr void rescan() noexcept {
        std::size_t sync_offset{size_};
        for (std::size_t index = 1U; index + 1U < size_; ++index) {
            if (buffer_[index] == sync_first &&
                buffer_[index + 1U] == sync_second) {
                sync_offset = index;
                break;
            }
        }

        if (sync_offset < size_) {
            const auto retained = size_ - sync_offset;
            std::copy_n(buffer_.begin() +
                            static_cast<std::ptrdiff_t>(sync_offset),
                        retained,
                        buffer_.begin());
            size_ = retained;
        } else if (buffer_[size_ - 1U] == sync_first) {
            buffer_[0U] = sync_first;
            size_ = 1U;
        } else {
            size_ = 0U;
        }
        expected_size_ = 0U;
    }

    template <typename Handler>
    constexpr void align_to_sync(Handler& handler) noexcept {
        if (size_ == 0U) {
            return;
        }

        std::size_t sync_offset{size_};
        for (std::size_t index = 0U; index + 1U < size_; ++index) {
            if (buffer_[index] == sync_first &&
                buffer_[index + 1U] == sync_second) {
                sync_offset = index;
                break;
            }
        }

        if (sync_offset < size_) {
            if (sync_offset > 0U) {
                emit_noise(handler, sync_offset);
                const auto retained = size_ - sync_offset;
                std::copy_n(buffer_.begin() +
                                static_cast<std::ptrdiff_t>(sync_offset),
                            retained,
                            buffer_.begin());
                size_ = retained;
            }
        } else if (size_ > 0U && buffer_[size_ - 1U] == sync_first) {
            emit_noise(handler, size_ - 1U);
            buffer_[0U] = sync_first;
            size_ = 1U;
        } else {
            emit_noise(handler, size_);
            size_ = 0U;
        }
    }

    constexpr void copy_frame(EncodedFrame& frame) const noexcept {
        std::copy_n(buffer_.begin(), size_, frame.bytes.begin());
        frame.size = size_;
    }

    template <typename Handler>
    static constexpr void emit_noise(Handler& handler,
                                     std::size_t count) noexcept {
        FrameStreamEvent event;
        event.status = FrameStreamStatus::NoiseDiscarded;
        event.discarded_bytes = count;
        handler(event);
    }

    static constexpr std::uint8_t sync_first{0xA5U};
    static constexpr std::uint8_t sync_second{0x5AU};
    static constexpr std::size_t header_size{6U};

    std::array<std::uint8_t, maximum_frame_size> buffer_{};
    std::size_t size_{0U};
    std::size_t expected_size_{0U};
};

}  // namespace apogee
