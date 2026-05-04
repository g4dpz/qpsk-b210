#include "qpsk_b200/tcp_input_server.h"

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

TcpInputServer::TcpInputServer(const std::string& addr, uint16_t port,
                               size_t frame_size)
    : addr_(addr), port_(port), frame_size_(frame_size) {}

TcpInputServer::~TcpInputServer() {
    stop();
}

void TcpInputServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(
            std::string("Failed to create TCP input socket: ") +
            std::strerror(errno));
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port_);
    if (::inet_pton(AF_INET, addr_.c_str(), &sa.sin_addr) <= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(
            "Failed to bind TCP input server to " + addr_ + ":" +
            std::to_string(port_) + ": Invalid address");
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&sa),
               sizeof(sa)) < 0) {
        std::string err = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(
            "Failed to bind TCP input server to " + addr_ + ":" +
            std::to_string(port_) + ": " + err);
    }

    if (::listen(listen_fd_, 1) < 0) {
        std::string err = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(
            "Failed to listen on TCP input server " + addr_ + ":" +
            std::to_string(port_) + ": " + err);
    }

    spdlog::info("TCP input server listening on {}:{}", addr_, port_);
}

void TcpInputServer::stop() {
    disconnect_client();

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    buffer_.clear();
}

void TcpInputServer::poll_once(int timeout_ms) {
    if (listen_fd_ < 0) return;

    // Build pollfd array: listen socket + optional client socket
    struct pollfd pfds[2];
    nfds_t nfds = 1;

    pfds[0].fd = listen_fd_;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    if (client_fd_ >= 0) {
        pfds[1].fd = client_fd_;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        nfds = 2;
    }

    int ret = ::poll(pfds, nfds, timeout_ms);
    if (ret < 0) {
        if (errno == EINTR) return;
        spdlog::warn("TCP input server poll error: {}", std::strerror(errno));
        return;
    }
    if (ret == 0) return;

    // Accept new connection (only if no client is currently connected)
    if (pfds[0].revents & POLLIN) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int new_fd = ::accept(listen_fd_,
                              reinterpret_cast<struct sockaddr*>(&client_addr),
                              &addr_len);
        if (new_fd >= 0) {
            char ip_buf[INET_ADDRSTRLEN]{};
            ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
            uint16_t new_port = ntohs(client_addr.sin_port);

            if (client_fd_ >= 0) {
                // Already have a client — reject the new one
                spdlog::warn("TCP input server: rejected connection from "
                             "{}:{} — a client is already connected ({}:{})",
                             ip_buf, new_port, client_addr_, client_port_);
                ::close(new_fd);
            } else {
                client_fd_ = new_fd;
                client_addr_ = ip_buf;
                client_port_ = new_port;
                client_bytes_received_ = 0;

                spdlog::info("TCP input server: client connected from {}:{}",
                             client_addr_, client_port_);
            }
        }
    }

    // Read data from client
    if (nfds > 1 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
        uint8_t tmp[4096];
        ssize_t n = ::recv(client_fd_, tmp, sizeof(tmp), 0);
        if (n > 0) {
            buffer_.insert(buffer_.end(), tmp, tmp + n);
            client_bytes_received_ += static_cast<uint64_t>(n);
        } else {
            disconnect_client();
        }
    }
}

std::vector<uint8_t> TcpInputServer::read_frame() {
    if (buffer_.size() < frame_size_) {
        return {};
    }
    std::vector<uint8_t> frame(buffer_.begin(),
                               buffer_.begin() +
                                   static_cast<std::ptrdiff_t>(frame_size_));
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() +
                      static_cast<std::ptrdiff_t>(frame_size_));
    return frame;
}

bool TcpInputServer::has_frame() const {
    return buffer_.size() >= frame_size_;
}

bool TcpInputServer::connected() const {
    return client_fd_ >= 0;
}

size_t TcpInputServer::buffered_bytes() const {
    return buffer_.size();
}

void TcpInputServer::disconnect_client() {
    if (client_fd_ >= 0) {
        spdlog::info("TCP input server: client disconnected {}:{}, "
                     "bytes received: {}",
                     client_addr_, client_port_, client_bytes_received_);
        ::close(client_fd_);
        client_fd_ = -1;
        client_addr_.clear();
        client_port_ = 0;
        client_bytes_received_ = 0;
    }
}

} // namespace qpsk_b200
