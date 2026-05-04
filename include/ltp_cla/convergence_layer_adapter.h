#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>

namespace ltp {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct ClaConfig {
    std::string tx_addr = "127.0.0.1";
    uint16_t tx_port = 5000;
    std::string rx_addr = "127.0.0.1";
    uint16_t rx_port = 5001;
    uint32_t initial_reconnect_delay_ms = 1000;
    uint32_t max_reconnect_delay_ms = 30000;
};

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

/// Called when a complete LTP segment is received from the RX side.
using SegmentReceivedCallback = std::function<void(std::vector<uint8_t> data)>;

// ---------------------------------------------------------------------------
// Length-prefix framing helpers (4-byte big-endian)
// ---------------------------------------------------------------------------

/// Frame a message with a 4-byte big-endian length prefix.
std::vector<uint8_t> frame_message(const std::vector<uint8_t>& data);

/// Extract a complete framed message from a buffer.
/// Returns true if a complete message was extracted, with the message in `out`
/// and the consumed bytes removed from `buffer`.
bool extract_framed_message(std::vector<uint8_t>& buffer,
                            std::vector<uint8_t>& out);

// ---------------------------------------------------------------------------
// ConvergenceLayerAdapter
// ---------------------------------------------------------------------------

class ConvergenceLayerAdapter {
public:
    explicit ConvergenceLayerAdapter(const ClaConfig& config);
    ~ConvergenceLayerAdapter();

    /// Set the callback for received segments.
    void set_segment_callback(SegmentReceivedCallback cb) {
        on_segment_received_ = std::move(cb);
    }

    /// Enqueue an encoded segment for transmission.
    void send_segment(std::vector<uint8_t> encoded_segment);

    /// Start the TX and RX threads.
    void start();

    /// Stop the TX and RX threads and close connections.
    void stop();

    /// Returns true if the TX connection is established.
    bool is_tx_connected() const { return tx_connected_.load(); }

    /// Returns true if the RX connection is established.
    bool is_rx_connected() const { return rx_connected_.load(); }

private:
    void run_tx();
    void run_rx();
    int connect_to(const std::string& addr, uint16_t port);
    void reconnect_loop(const std::string& addr, uint16_t port,
                        int& fd, std::atomic<bool>& connected_flag,
                        const char* label);

    ClaConfig config_;
    std::atomic<bool> running_{false};

    // TX side
    int tx_fd_ = -1;
    std::atomic<bool> tx_connected_{false};
    std::thread tx_thread_;
    std::mutex tx_queue_mtx_;
    std::condition_variable tx_queue_cv_;
    std::queue<std::vector<uint8_t>> tx_queue_;

    // RX side
    int rx_fd_ = -1;
    std::atomic<bool> rx_connected_{false};
    std::thread rx_thread_;

    SegmentReceivedCallback on_segment_received_;
};

}  // namespace ltp
