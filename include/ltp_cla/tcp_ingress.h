#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ltp {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct IngressConfig {
    std::string bind_addr = "127.0.0.1";
    uint16_t port = 4556;
    bool default_reliable = true;  // true = red data, false = green data
};

// ---------------------------------------------------------------------------
// Callback
// ---------------------------------------------------------------------------

/// Called when a complete data block is received from a client.
/// The data is an opaque byte blob (typically a pre-formed BP bundle).
using IngressDataCallback = std::function<void(std::vector<uint8_t> data)>;

// ---------------------------------------------------------------------------
// TcpIngress — TCP server accepting data from upstream ION-DTN
// ---------------------------------------------------------------------------

class TcpIngress {
public:
    explicit TcpIngress(const IngressConfig& config);
    ~TcpIngress();

    /// Set the callback for received data blocks.
    void set_data_callback(IngressDataCallback cb) {
        on_data_ = std::move(cb);
    }

    /// Start the server thread.
    void start();

    /// Stop the server thread and close all connections.
    void stop();

    /// Returns the port the server is listening on (useful if port was 0).
    uint16_t listening_port() const { return listening_port_; }

    /// Returns the number of currently connected clients.
    size_t client_count() const;

private:
    void run();
    void handle_client(int client_fd);

    IngressConfig config_;
    std::atomic<bool> running_{false};
    int listen_fd_ = -1;
    uint16_t listening_port_ = 0;
    std::thread thread_;

    mutable std::mutex clients_mtx_;
    std::vector<int> client_fds_;

    IngressDataCallback on_data_;
};

}  // namespace ltp
