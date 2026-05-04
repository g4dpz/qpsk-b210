#include "qpsk_b200/tcp_output_server.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// MSG_NOSIGNAL is Linux-specific; on macOS we suppress SIGPIPE per-socket
// via SO_NOSIGPIPE (set when accepting clients).
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <spdlog/spdlog.h>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TcpOutputServer::TcpOutputServer(const std::string& addr, uint16_t port)
    : addr_(addr), port_(port) {}

TcpOutputServer::~TcpOutputServer() {
    stop();
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void TcpOutputServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(
            std::string("Failed to create TCP output socket: ") +
            std::strerror(errno));
    }

    // Allow rapid rebind after restart
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port_);
    if (::inet_pton(AF_INET, addr_.c_str(), &sa.sin_addr) <= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(
            "Failed to bind TCP output server to " + addr_ + ":" +
            std::to_string(port_) + ": Invalid address");
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&sa),
               sizeof(sa)) < 0) {
        std::string err = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(
            "Failed to bind TCP output server to " + addr_ + ":" +
            std::to_string(port_) + ": " + err);
    }

    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        std::string err = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(
            "Failed to listen on TCP output server " + addr_ + ":" +
            std::to_string(port_) + ": " + err);
    }

    spdlog::info("TCP output server listening on {}:{}", addr_, port_);
}

void TcpOutputServer::stop() {
    // Close all client connections
    for (auto& c : clients_) {
        if (c.fd >= 0) {
            ::close(c.fd);
        }
    }
    clients_.clear();

    // Close listen socket
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// poll_once — accept new clients
// ---------------------------------------------------------------------------

void TcpOutputServer::poll_once(int timeout_ms) {
    if (listen_fd_ < 0) return;

    struct pollfd pfd{};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        if (errno == EINTR) return;
        spdlog::warn("TCP output server poll error: {}", std::strerror(errno));
        return;
    }
    if (ret == 0) return;  // timeout

    if (pfd.revents & POLLIN) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
                                 &addr_len);
        if (client_fd >= 0) {
            char ip_buf[INET_ADDRSTRLEN]{};
            ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
            uint16_t client_port = ntohs(client_addr.sin_port);

            // On macOS, suppress SIGPIPE on this socket
#ifdef SO_NOSIGPIPE
            int nosig = 1;
            ::setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE,
                         &nosig, sizeof(nosig));
#endif
            clients_.push_back({client_fd, std::string(ip_buf), client_port, 0});
            spdlog::info("TCP output server: client connected from {}:{}",
                         ip_buf, client_port);
        }
    }
}

// ---------------------------------------------------------------------------
// write_to_all — broadcast to every connected client
// ---------------------------------------------------------------------------

void TcpOutputServer::write_to_all(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    // Walk backwards so removal by index doesn't invalidate later indices.
    for (int i = static_cast<int>(clients_.size()) - 1; i >= 0; --i) {
        auto idx = static_cast<size_t>(i);
        const uint8_t* ptr = data.data();
        size_t remaining = data.size();

        while (remaining > 0) {
            ssize_t n = ::send(clients_[idx].fd, ptr, remaining, MSG_NOSIGNAL);
            if (n > 0) {
                ptr += n;
                remaining -= static_cast<size_t>(n);
                clients_[idx].bytes_sent += static_cast<uint64_t>(n);
            } else {
                // Send failed — EPIPE, ECONNRESET, or other error
                remove_client(idx);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

size_t TcpOutputServer::client_count() const {
    return clients_.size();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void TcpOutputServer::remove_client(size_t index) {
    auto& c = clients_[index];
    spdlog::info("TCP output server: client disconnected {}:{}, "
                 "bytes sent: {}",
                 c.addr, c.port, c.bytes_sent);
    ::close(c.fd);
    clients_.erase(clients_.begin() +
                   static_cast<std::ptrdiff_t>(index));
}

} // namespace qpsk_b200
