#include "ltp_cla/tcp_ingress.h"
#include "ltp_cla/convergence_layer_adapter.h"  // for frame helpers

#include <spdlog/spdlog.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace ltp {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TcpIngress::TcpIngress(const IngressConfig& config)
    : config_(config) {}

TcpIngress::~TcpIngress() {
    stop();
}

// ---------------------------------------------------------------------------
// client_count
// ---------------------------------------------------------------------------

size_t TcpIngress::client_count() const {
    std::lock_guard<std::mutex> lock(clients_mtx_);
    return client_fds_.size();
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void TcpIngress::start() {
    if (running_.load()) return;

    // Create listening socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        spdlog::error("TCP ingress: socket() failed: {}", std::strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.bind_addr.c_str(), &sa.sin_addr) <= 0) {
        spdlog::error("TCP ingress: invalid bind address {}", config_.bind_addr);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        spdlog::error("TCP ingress: bind to {}:{} failed: {}",
                      config_.bind_addr, config_.port, std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, 8) < 0) {
        spdlog::error("TCP ingress: listen() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    // Get the actual port (useful if config_.port was 0)
    struct sockaddr_in bound_sa{};
    socklen_t bound_len = sizeof(bound_sa);
    if (::getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_sa),
                      &bound_len) == 0) {
        listening_port_ = ntohs(bound_sa.sin_port);
    }

    running_ = true;
    thread_ = std::thread(&TcpIngress::run, this);

    spdlog::info("TCP ingress: listening on {}:{}", config_.bind_addr, listening_port_);
}

void TcpIngress::stop() {
    if (!running_.load()) return;
    running_ = false;

    // Close listen socket to unblock accept/poll
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (thread_.joinable()) thread_.join();

    // Close all client connections
    std::lock_guard<std::mutex> lock(clients_mtx_);
    for (int fd : client_fds_) {
        ::close(fd);
    }
    client_fds_.clear();
}

// ---------------------------------------------------------------------------
// run — main server loop: accept + poll clients
// ---------------------------------------------------------------------------

void TcpIngress::run() {
    // Per-client read buffers
    std::unordered_map<int, std::vector<uint8_t>> buffers;

    while (running_.load()) {
        // Build poll set: listen socket + all clients
        std::vector<struct pollfd> pfds;

        {
            struct pollfd listen_pfd{};
            listen_pfd.fd = listen_fd_;
            listen_pfd.events = POLLIN;
            pfds.push_back(listen_pfd);
        }

        {
            std::lock_guard<std::mutex> lock(clients_mtx_);
            for (int fd : client_fds_) {
                struct pollfd cpfd{};
                cpfd.fd = fd;
                cpfd.events = POLLIN;
                pfds.push_back(cpfd);
            }
        }

        int ret = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            spdlog::warn("TCP ingress: poll error: {}", std::strerror(errno));
            break;
        }
        if (ret == 0) continue;

        // Check listen socket for new connections
        if (pfds[0].revents & POLLIN) {
            struct sockaddr_in client_sa{};
            socklen_t client_len = sizeof(client_sa);
            int client_fd = ::accept(listen_fd_,
                                     reinterpret_cast<struct sockaddr*>(&client_sa),
                                     &client_len);
            if (client_fd >= 0) {
                char addr_str[INET_ADDRSTRLEN];
                ::inet_ntop(AF_INET, &client_sa.sin_addr, addr_str, sizeof(addr_str));
                spdlog::info("TCP ingress: client connected from {}:{}",
                             addr_str, ntohs(client_sa.sin_port));

                std::lock_guard<std::mutex> lock(clients_mtx_);
                client_fds_.push_back(client_fd);
                buffers[client_fd] = {};
            }
        }

        // Check client sockets for data
        for (size_t i = 1; i < pfds.size(); ++i) {
            int fd = pfds[i].fd;

            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                spdlog::info("TCP ingress: client fd {} disconnected", fd);
                ::close(fd);
                buffers.erase(fd);
                std::lock_guard<std::mutex> lock(clients_mtx_);
                client_fds_.erase(
                    std::remove(client_fds_.begin(), client_fds_.end(), fd),
                    client_fds_.end());
                continue;
            }

            if (pfds[i].revents & POLLIN) {
                uint8_t read_buf[4096];
                ssize_t n = ::read(fd, read_buf, sizeof(read_buf));
                if (n <= 0) {
                    spdlog::info("TCP ingress: client fd {} disconnected (read={})",
                                 fd, n);
                    ::close(fd);
                    buffers.erase(fd);
                    std::lock_guard<std::mutex> lock(clients_mtx_);
                    client_fds_.erase(
                        std::remove(client_fds_.begin(), client_fds_.end(), fd),
                        client_fds_.end());
                    continue;
                }

                auto& buf = buffers[fd];
                buf.insert(buf.end(), read_buf, read_buf + n);

                // Extract complete framed messages
                std::vector<uint8_t> msg;
                while (extract_framed_message(buf, msg)) {
                    if (on_data_) {
                        on_data_(std::move(msg));
                    }
                    msg.clear();
                }
            }
        }
    }
}

}  // namespace ltp
