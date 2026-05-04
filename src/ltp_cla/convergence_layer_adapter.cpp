#include "ltp_cla/convergence_layer_adapter.h"

#include <spdlog/spdlog.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>

namespace ltp {

// ---------------------------------------------------------------------------
// Length-prefix framing helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> frame_message(const std::vector<uint8_t>& data) {
    uint32_t len = static_cast<uint32_t>(data.size());
    std::vector<uint8_t> framed;
    framed.reserve(4 + data.size());
    // 4-byte big-endian length prefix
    framed.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    framed.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    framed.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    framed.push_back(static_cast<uint8_t>(len & 0xFF));
    framed.insert(framed.end(), data.begin(), data.end());
    return framed;
}

bool extract_framed_message(std::vector<uint8_t>& buffer,
                            std::vector<uint8_t>& out) {
    if (buffer.size() < 4) return false;

    uint32_t len = (static_cast<uint32_t>(buffer[0]) << 24) |
                   (static_cast<uint32_t>(buffer[1]) << 16) |
                   (static_cast<uint32_t>(buffer[2]) << 8) |
                   static_cast<uint32_t>(buffer[3]);

    if (buffer.size() < 4 + len) return false;

    out.assign(buffer.begin() + 4, buffer.begin() + 4 + len);
    buffer.erase(buffer.begin(), buffer.begin() + 4 + len);
    return true;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ConvergenceLayerAdapter::ConvergenceLayerAdapter(const ClaConfig& config)
    : config_(config) {}

ConvergenceLayerAdapter::~ConvergenceLayerAdapter() {
    stop();
}

// ---------------------------------------------------------------------------
// send_segment — enqueue for TX thread
// ---------------------------------------------------------------------------

void ConvergenceLayerAdapter::send_segment(std::vector<uint8_t> encoded_segment) {
    {
        std::lock_guard<std::mutex> lock(tx_queue_mtx_);
        tx_queue_.push(std::move(encoded_segment));
    }
    tx_queue_cv_.notify_one();
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void ConvergenceLayerAdapter::start() {
    if (running_.load()) return;
    running_ = true;
    tx_thread_ = std::thread(&ConvergenceLayerAdapter::run_tx, this);
    rx_thread_ = std::thread(&ConvergenceLayerAdapter::run_rx, this);
}

void ConvergenceLayerAdapter::stop() {
    if (!running_.load()) return;
    running_ = false;
    tx_queue_cv_.notify_all();

    if (tx_thread_.joinable()) tx_thread_.join();
    if (rx_thread_.joinable()) rx_thread_.join();

    if (tx_fd_ >= 0) { ::close(tx_fd_); tx_fd_ = -1; }
    if (rx_fd_ >= 0) { ::close(rx_fd_); rx_fd_ = -1; }
    tx_connected_ = false;
    rx_connected_ = false;
}

// ---------------------------------------------------------------------------
// connect_to — create a TCP client connection
// ---------------------------------------------------------------------------

int ConvergenceLayerAdapter::connect_to(const std::string& addr, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        spdlog::error("CLA: socket() failed: {}", std::strerror(errno));
        return -1;
    }

    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (::inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) <= 0) {
        spdlog::error("CLA: invalid address {}", addr);
        ::close(fd);
        return -1;
    }

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

// ---------------------------------------------------------------------------
// reconnect_loop — exponential backoff reconnection
// ---------------------------------------------------------------------------

void ConvergenceLayerAdapter::reconnect_loop(
    const std::string& addr, uint16_t port,
    int& fd, std::atomic<bool>& connected_flag,
    const char* label) {

    uint32_t delay_ms = config_.initial_reconnect_delay_ms;
    int attempt = 0;

    while (running_.load()) {
        attempt++;
        spdlog::info("CLA {}: connection attempt {} to {}:{}",
                     label, attempt, addr, port);

        fd = connect_to(addr, port);
        if (fd >= 0) {
            connected_flag = true;
            spdlog::info("CLA {}: connected to {}:{}", label, addr, port);
            return;
        }

        spdlog::warn("CLA {}: connection failed, retrying in {}ms",
                     label, delay_ms);

        // Sleep with periodic checks for shutdown
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(delay_ms);
        while (running_.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Exponential backoff
        delay_ms = std::min(delay_ms * 2, config_.max_reconnect_delay_ms);
    }
}

// ---------------------------------------------------------------------------
// run_tx — TX thread: dequeue segments, frame, write to QPSK TX
// ---------------------------------------------------------------------------

void ConvergenceLayerAdapter::run_tx() {
    reconnect_loop(config_.tx_addr, config_.tx_port,
                   tx_fd_, tx_connected_, "TX");

    while (running_.load()) {
        std::vector<uint8_t> segment;
        {
            std::unique_lock<std::mutex> lock(tx_queue_mtx_);
            tx_queue_cv_.wait(lock, [this] {
                return !tx_queue_.empty() || !running_.load();
            });
            if (!running_.load()) break;
            segment = std::move(tx_queue_.front());
            tx_queue_.pop();
        }

        auto framed = frame_message(segment);

        // Write the framed segment
        size_t written = 0;
        while (written < framed.size() && running_.load()) {
            ssize_t n = ::write(tx_fd_, framed.data() + written,
                                framed.size() - written);
            if (n <= 0) {
                spdlog::warn("CLA TX: write failed: {}", std::strerror(errno));
                tx_connected_ = false;
                ::close(tx_fd_);
                tx_fd_ = -1;
                reconnect_loop(config_.tx_addr, config_.tx_port,
                               tx_fd_, tx_connected_, "TX");
                if (!running_.load()) return;
                // Retry the write from the beginning
                written = 0;
                continue;
            }
            written += static_cast<size_t>(n);
        }
    }
}

// ---------------------------------------------------------------------------
// run_rx — RX thread: read from QPSK RX, extract framed segments
// ---------------------------------------------------------------------------

void ConvergenceLayerAdapter::run_rx() {
    reconnect_loop(config_.rx_addr, config_.rx_port,
                   rx_fd_, rx_connected_, "RX");

    std::vector<uint8_t> buffer;
    uint8_t read_buf[4096];

    while (running_.load()) {
        // Poll with timeout so we can check running_ periodically
        struct pollfd pfd{};
        pfd.fd = rx_fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 200);  // 200ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
            spdlog::warn("CLA RX: poll error: {}", std::strerror(errno));
            break;
        }
        if (ret == 0) continue;  // timeout

        if (pfd.revents & (POLLERR | POLLHUP)) {
            spdlog::warn("CLA RX: connection lost");
            rx_connected_ = false;
            ::close(rx_fd_);
            rx_fd_ = -1;
            buffer.clear();
            reconnect_loop(config_.rx_addr, config_.rx_port,
                           rx_fd_, rx_connected_, "RX");
            if (!running_.load()) return;
            continue;
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = ::read(rx_fd_, read_buf, sizeof(read_buf));
            if (n <= 0) {
                spdlog::warn("CLA RX: read returned {}: {}",
                             n, n == 0 ? "EOF" : std::strerror(errno));
                rx_connected_ = false;
                ::close(rx_fd_);
                rx_fd_ = -1;
                buffer.clear();
                reconnect_loop(config_.rx_addr, config_.rx_port,
                               rx_fd_, rx_connected_, "RX");
                if (!running_.load()) return;
                continue;
            }

            buffer.insert(buffer.end(), read_buf, read_buf + n);

            // Extract complete messages
            std::vector<uint8_t> msg;
            while (extract_framed_message(buffer, msg)) {
                if (on_segment_received_) {
                    on_segment_received_(std::move(msg));
                }
                msg.clear();
            }
        }
    }
}

}  // namespace ltp
