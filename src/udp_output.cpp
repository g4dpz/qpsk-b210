#include "qpsk_b200/udp_output.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace qpsk_b200 {

UdpOutput::UdpOutput(const std::string& dest_addr, uint16_t dest_port)
    : dest_addr_(dest_addr), dest_port_(dest_port) {}

UdpOutput::~UdpOutput() {
    stop();
}

void UdpOutput::start() {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error(
            std::string("Failed to create UDP output socket: ") +
            std::strerror(errno));
    }

    datagrams_sent_ = 0;
    bytes_sent_ = 0;

    spdlog::info("UDP output socket ready, destination {}:{}",
                 dest_addr_, dest_port_);
}

void UdpOutput::stop() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool UdpOutput::send_datagram(const std::vector<uint8_t>& data) {
    if (fd_ < 0 || data.empty()) return false;

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dest_port_);
    if (::inet_pton(AF_INET, dest_addr_.c_str(), &dest.sin_addr) <= 0) {
        spdlog::warn("UDP output: invalid destination address '{}'", dest_addr_);
        return false;
    }

    ssize_t n = ::sendto(fd_, data.data(), data.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&dest),
                         sizeof(dest));
    if (n < 0) {
        spdlog::warn("UDP output sendto error: {}", std::strerror(errno));
        return false;
    }

    datagrams_sent_++;
    bytes_sent_ += static_cast<uint64_t>(n);
    return true;
}

bool UdpOutput::is_open() const {
    return fd_ >= 0;
}

uint64_t UdpOutput::datagrams_sent() const {
    return datagrams_sent_;
}

uint64_t UdpOutput::bytes_sent() const {
    return bytes_sent_;
}

} // namespace qpsk_b200
