#ifndef QPSK_B200_TCP_INPUT_SERVER_H
#define QPSK_B200_TCP_INPUT_SERVER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qpsk_b200 {

/// TCP server that accepts a single client connection, reads its byte
/// stream, and segments incoming data into frame-sized payloads for the
/// encoder.
///
/// The RF link is point-to-point, so only one data source feeds the
/// transmitter at a time.  If a new client connects while one is already
/// connected, the existing connection is closed and replaced.
///
/// Maps to Requirements 11.1–11.8, 9.7, 9.8, 9.11.
class TcpInputServer {
public:
    /// @param addr       Listen address (e.g. "127.0.0.1").
    /// @param port       Listen port.
    /// @param frame_size Target frame payload size in bytes for segmentation.
    TcpInputServer(const std::string& addr, uint16_t port,
                   size_t frame_size = 1024);

    ~TcpInputServer();

    // Non-copyable, non-movable (owns file descriptors)
    TcpInputServer(const TcpInputServer&) = delete;
    TcpInputServer& operator=(const TcpInputServer&) = delete;
    TcpInputServer(TcpInputServer&&) = delete;
    TcpInputServer& operator=(TcpInputServer&&) = delete;

    /// Bind and listen on the configured address:port.
    /// Throws std::runtime_error on bind/listen failure.
    void start();

    /// Close the client connection (if any) and the listen socket.
    void stop();

    /// Accept a new client (replacing any existing one) and read data.
    /// @param timeout_ms  poll() timeout in milliseconds (default 100).
    void poll_once(int timeout_ms = 100);

    /// Return one frame-sized chunk from the buffer, or an empty vector
    /// if not enough data has accumulated.
    std::vector<uint8_t> read_frame();

    /// Flush all buffered data regardless of frame_size.
    /// Returns whatever is in the buffer (may be less than frame_size).
    std::vector<uint8_t> flush();

    /// Check whether a full frame-sized chunk is available in the buffer.
    bool has_frame() const;

    /// Whether a client is currently connected.
    bool connected() const;

    /// Number of bytes currently buffered (not yet consumed by read_frame).
    size_t buffered_bytes() const;

private:
    std::string addr_;
    uint16_t port_;
    size_t frame_size_;

    int listen_fd_ = -1;
    int client_fd_ = -1;
    std::string client_addr_;
    uint16_t client_port_ = 0;
    uint64_t client_bytes_received_ = 0;

    std::vector<uint8_t> buffer_;

    /// Close the current client connection, logging disconnect info.
    void disconnect_client();
};

} // namespace qpsk_b200

#endif // QPSK_B200_TCP_INPUT_SERVER_H
