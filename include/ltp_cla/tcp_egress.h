#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ltp {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct EgressConfig {
    std::string bind_addr = "127.0.0.1";
    uint16_t port = 4557;
    uint64_t max_buffer_bytes = 10485760;  // 10 MB
};

// ---------------------------------------------------------------------------
// TcpEgress — TCP server delivering data to downstream ION-DTN
// ---------------------------------------------------------------------------

class TcpEgress {
public:
    explicit TcpEgress(const EgressConfig& config);
    ~TcpEgress();

    /// Deliver a data block to all connected clients.
    /// If no clients are connected, the data is buffered.
    void deliver(std::vector<uint8_t> data);

    /// Start the server thread.
    void start();

    /// Stop the server thread and close all connections.
    void stop();

    /// Returns the port the server is listening on.
    uint16_t listening_port() const { return listening_port_; }

    /// Returns the number of currently connected clients.
    size_t client_count() const;

    /// Returns the current buffer utilization in bytes.
    uint64_t buffer_bytes() const;

private:
    void run();
    void flush_buffer_to_clients();
    void write_to_clients(const std::vector<uint8_t>& framed);

    EgressConfig config_;
    std::atomic<bool> running_{false};
    int listen_fd_ = -1;
    uint16_t listening_port_ = 0;
    std::thread thread_;

    mutable std::mutex mtx_;
    std::vector<int> client_fds_;

    // Buffer for data when no clients are connected
    std::deque<std::vector<uint8_t>> buffer_;
    uint64_t buffer_bytes_ = 0;
};

}  // namespace ltp
