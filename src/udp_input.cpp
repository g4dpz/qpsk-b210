#include "qpsk_b200/udp_input.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace qpsk_b200 {

// Maximum UDP datagram size we'll accept.  LTP segments over a local link
// are typically well under 64 KB; 65535 is the UDP maximum.
static constexpr size_t MAX_DATAGRAM_SIZE = 65535;

UdpInput::UdpInput(const std::string& addr, uint16_t port)
    : addr_(addr), port_(port) {}

UdpInput::~UdpInput() {
    stop();
}

void UdpInput::start() {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error(
            std::string("Failed to create UDP input socket: ") +
            std::strerror(errno));
    }

    int opt = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port_);
    if (::inet_pton(AF_INET, addr_.c_str(), &sa.sin_addr) <= 0) {
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            "Failed to bind UDP input socket to " + addr_ + ":" +
            std::to_string(port_) + ": Invalid address");
    }

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        std::string err = std::strerror(errno);
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            "Failed to bind UDP input socket to " + addr_ + ":" +
            std::to_string(port_) + ": " + err);
    }

    datagrams_received_ = 0;
    bytes_received_ = 0;

    spdlog::info("UDP input socket bound to {}:{}", addr_, port_);
}

void UdpInput::stop() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::vector<uint8_t> UdpInput::recv_datagram(int timeout_ms) {
    if (fd_ < 0) return {};

    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        if (errno == EINTR) return {};
        spdlog::warn("UDP input poll error: {}", std::strerror(errno));
        return {};
    }
    if (ret == 0) return {};  // timeout

    if (!(pfd.revents & POLLIN)) return {};

    std::vector<uint8_t> buf(MAX_DATAGRAM_SIZE);
    ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0, nullptr, nullptr);
    if (n <= 0) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            spdlog::warn("UDP input recvfrom error: {}", std::strerror(errno));
        }
        return {};
    }

    buf.resize(static_cast<size_t>(n));
    datagrams_received_++;
    bytes_received_ += static_cast<uint64_t>(n);

    return buf;
}

bool UdpInput::is_open() const {
    return fd_ >= 0;
}

uint64_t UdpInput::datagrams_received() const {
    return datagrams_received_;
}

uint64_t UdpInput::bytes_received() const {
    return bytes_received_;
}

} // namespace qpsk_b200
