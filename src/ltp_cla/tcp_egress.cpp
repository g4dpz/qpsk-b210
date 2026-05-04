#include "ltp_cla/tcp_egress.h"
#include "ltp_cla/convergence_layer_adapter.h"  // for frame_message

#include <spdlog/spdlog.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace ltp {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TcpEgress::TcpEgress(const EgressConfig& config)
    : config_(config) {}

TcpEgress::~TcpEgress() {
    stop();
}

// ---------------------------------------------------------------------------
// client_count / buffer_bytes
// ---------------------------------------------------------------------------

size_t TcpEgress::client_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return client_fds_.size();
}

uint64_t TcpEgress::buffer_bytes() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_bytes_;
}

// ---------------------------------------------------------------------------
// deliver — send data to clients or buffer
// ---------------------------------------------------------------------------

void TcpEgress::deliver(std::vector<uint8_t> data) {
    auto framed = frame_message(data);

    std::lock_guard<std::mutex> lock(mtx_);

    if (!client_fds_.empty()) {
        // Write directly to all connected clients
        write_to_clients(framed);
    } else {
        // Buffer the data
        uint64_t framed_size = framed.size();

        // Enforce capacity: discard oldest if needed
        while (buffer_bytes_ + framed_size > config_.max_buffer_bytes &&
               !buffer_.empty()) {
            spdlog::warn("TCP egress: buffer overflow, discarding oldest block "
                         "({} bytes)", buffer_.front().size());
            buffer_bytes_ -= buffer_.front().size();
            buffer_.pop_front();
        }

        buffer_bytes_ += framed_size;
        buffer_.push_back(std::move(framed));

        spdlog::debug("TCP egress: buffered {} bytes (total buffer: {} bytes)",
                      framed_size, buffer_bytes_);
    }
}

// ---------------------------------------------------------------------------
// write_to_clients — write framed data to all connected clients
// ---------------------------------------------------------------------------

void TcpEgress::write_to_clients(const std::vector<uint8_t>& framed) {
    // Must be called with mtx_ held
    std::vector<int> dead_fds;

    for (int fd : client_fds_) {
        size_t written = 0;
        while (written < framed.size()) {
            ssize_t n = ::write(fd, framed.data() + written,
                                framed.size() - written);
            if (n <= 0) {
                spdlog::info("TCP egress: client fd {} write failed, removing", fd);
                dead_fds.push_back(fd);
                break;
            }
            written += static_cast<size_t>(n);
        }
    }

    // Remove dead clients
    for (int fd : dead_fds) {
        ::close(fd);
        client_fds_.erase(
            std::remove(client_fds_.begin(), client_fds_.end(), fd),
            client_fds_.end());
    }
}

// ---------------------------------------------------------------------------
// flush_buffer_to_clients — deliver buffered data to newly connected clients
// ---------------------------------------------------------------------------

void TcpEgress::flush_buffer_to_clients() {
    // Must be called with mtx_ held
    while (!buffer_.empty() && !client_fds_.empty()) {
        write_to_clients(buffer_.front());
        buffer_bytes_ -= buffer_.front().size();
        buffer_.pop_front();
    }
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void TcpEgress::start() {
    if (running_.load()) return;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        spdlog::error("TCP egress: socket() failed: {}", std::strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.bind_addr.c_str(), &sa.sin_addr) <= 0) {
        spdlog::error("TCP egress: invalid bind address {}", config_.bind_addr);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        spdlog::error("TCP egress: bind to {}:{} failed: {}",
                      config_.bind_addr, config_.port, std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, 8) < 0) {
        spdlog::error("TCP egress: listen() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    struct sockaddr_in bound_sa{};
    socklen_t bound_len = sizeof(bound_sa);
    if (::getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_sa),
                      &bound_len) == 0) {
        listening_port_ = ntohs(bound_sa.sin_port);
    }

    running_ = true;
    thread_ = std::thread(&TcpEgress::run, this);

    spdlog::info("TCP egress: listening on {}:{}", config_.bind_addr, listening_port_);
}

void TcpEgress::stop() {
    if (!running_.load()) return;
    running_ = false;

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (thread_.joinable()) thread_.join();

    std::lock_guard<std::mutex> lock(mtx_);
    for (int fd : client_fds_) {
        ::close(fd);
    }
    client_fds_.clear();
}

// ---------------------------------------------------------------------------
// run — main server loop: accept connections
// ---------------------------------------------------------------------------

void TcpEgress::run() {
    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            break;
        }
        if (ret == 0) continue;

        if (pfd.revents & POLLIN) {
            struct sockaddr_in client_sa{};
            socklen_t client_len = sizeof(client_sa);
            int client_fd = ::accept(listen_fd_,
                                     reinterpret_cast<struct sockaddr*>(&client_sa),
                                     &client_len);
            if (client_fd >= 0) {
                char addr_str[INET_ADDRSTRLEN];
                ::inet_ntop(AF_INET, &client_sa.sin_addr, addr_str, sizeof(addr_str));
                spdlog::info("TCP egress: client connected from {}:{}",
                             addr_str, ntohs(client_sa.sin_port));

                std::lock_guard<std::mutex> lock(mtx_);
                client_fds_.push_back(client_fd);

                // Flush any buffered data to the new client
                flush_buffer_to_clients();
            }
        }
    }
}

}  // namespace ltp
