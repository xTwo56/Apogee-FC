#include <apogee/ground_client.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <string>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t io_buffer_size{256U};

int connect_tcp(const char* host, const char* port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses{nullptr};
    const int lookup_status = getaddrinfo(host, port, &hints, &addresses);
    if (lookup_status != 0) {
        std::cerr << "Address lookup failed: " << gai_strerror(lookup_status)
                  << '\n';
        return -1;
    }

    int connected_socket{-1};
    for (const auto* address = addresses; address != nullptr;
         address = address->ai_next) {
        const int candidate =
            socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate < 0) {
            continue;
        }

        int status{0};
        do {
            status = connect(candidate, address->ai_addr, address->ai_addrlen);
        } while (status < 0 && errno == EINTR);
        if (status == 0) {
            connected_socket = candidate;
            break;
        }
        close(candidate);
    }
    freeaddrinfo(addresses);

    if (connected_socket < 0) {
        std::cerr << "Unable to connect to " << host << ':' << port << '\n';
    }
    return connected_socket;
}

bool wait_until_writable(int socket_fd) {
    pollfd descriptor{socket_fd, POLLOUT, 0};
    while (true) {
        const int status = poll(&descriptor, 1U, -1);
        if (status > 0) {
            return (descriptor.revents & POLLOUT) != 0;
        }
        if (status < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

bool send_all(int socket_fd, const apogee::EncodedFrame& frame) {
    std::size_t offset{0U};
    while (offset < frame.size) {
        const auto sent = send(socket_fd,
                               frame.bytes.data() + offset,
                               frame.size - offset,
                               MSG_NOSIGNAL);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            wait_until_writable(socket_fd)) {
            continue;
        }
        return false;
    }
    return true;
}

int run_offline(const char* path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        std::cerr << "Unable to open telemetry file: " << path << '\n';
        return 1;
    }

    apogee::ground::GroundStreamProcessor processor{std::cout, std::cerr};
    std::array<char, io_buffer_size> input_buffer{};
    std::array<std::uint8_t, io_buffer_size> bytes{};
    while (input) {
        input.read(input_buffer.data(),
                   static_cast<std::streamsize>(input_buffer.size()));
        const auto count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            bytes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(
                static_cast<unsigned char>(
                    input_buffer[static_cast<std::size_t>(index)]));
        }
        processor.feed({bytes.data(), static_cast<std::size_t>(count)});
    }

    if (input.bad()) {
        std::cerr << "Error while reading telemetry file\n";
        return 1;
    }
    processor.finish();
    return processor.decoded_messages() == 0U || processor.malformed_input()
               ? 1
               : 0;
}

int run_live(const char* host, const char* port) {
    const int socket_fd = connect_tcp(host, port);
    if (socket_fd < 0) {
        return 1;
    }

    apogee::ground::GroundStreamProcessor processor{std::cout, std::cerr};
    apogee::ground::CommandSequencer sequencer;
    std::array<std::uint8_t, io_buffer_size> receive_buffer{};
    std::cout << "Connected. Commands: ping, safe, period <milliseconds>, quit\n";

    while (true) {
        std::array<pollfd, 2U> descriptors{{
            {socket_fd, POLLIN, 0},
            {STDIN_FILENO, POLLIN, 0},
        }};
        const int poll_status = poll(descriptors.data(), descriptors.size(), -1);
        if (poll_status < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll failed: " << std::strerror(errno) << '\n';
            close(socket_fd);
            return 1;
        }

        if ((descriptors[0U].revents & POLLIN) != 0) {
            ssize_t received{0};
            do {
                received = recv(socket_fd,
                                receive_buffer.data(),
                                receive_buffer.size(),
                                0);
            } while (received < 0 && errno == EINTR);
            if (received > 0) {
                processor.feed({receive_buffer.data(),
                                static_cast<std::size_t>(received)});
            } else if (received == 0) {
                std::cerr << "Server disconnected\n";
                processor.finish();
                close(socket_fd);
                return 0;
            } else {
                std::cerr << "Socket receive failed: " << std::strerror(errno)
                          << '\n';
                close(socket_fd);
                return 1;
            }
        }

        if ((descriptors[0U].revents & (POLLERR | POLLNVAL)) != 0) {
            std::cerr << "Socket error\n";
            close(socket_fd);
            return 1;
        }
        if ((descriptors[0U].revents & POLLHUP) != 0) {
            std::cerr << "Server disconnected\n";
            processor.finish();
            close(socket_fd);
            return 0;
        }

        if ((descriptors[1U].revents & POLLIN) != 0) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                close(socket_fd);
                return 0;
            }

            const auto parsed = sequencer.parse(line);
            if (parsed.status == apogee::ground::LocalCommandStatus::Quit) {
                close(socket_fd);
                return 0;
            }
            if (parsed.status ==
                apogee::ground::LocalCommandStatus::InvalidSyntax) {
                std::cerr << "Invalid command syntax\n";
                continue;
            }

            const auto encoded = apogee::encode_command(parsed.command);
            if (!encoded.ok()) {
                std::cerr << "Command encoding failed\n";
                continue;
            }
            if (!send_all(socket_fd, encoded.frame)) {
                std::cerr << "Socket send failed: " << std::strerror(errno)
                          << '\n';
                close(socket_fd);
                return 1;
            }
        }

        if ((descriptors[1U].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            close(socket_fd);
            return 0;
        }
    }
}

void print_usage() {
    std::cerr << "Usage:\n"
              << "  apogee_ground_client <binary-file>\n"
              << "  apogee_ground_client --connect <host> <port>\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 2) {
        return run_offline(argv[1]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "--connect") {
        return run_live(argv[2], argv[3]);
    }

    print_usage();
    return 1;
}
