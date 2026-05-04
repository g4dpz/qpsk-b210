#ifndef QPSK_B200_TCP_OUTPUT_SERVER_H
#define QPSK_B200_TCP_OUTPUT_SERVER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qpsk_b200 {

/// TCP server that accepts client connections and broadcasts decoded
/// payloads to all connected clients.
///
/// Uses POSIX sockets with poll() for non-blocking multiplexing.
/// Supports multiple simultaneous clients.  Frame order is preserved:
/// write_to_all() sends data to each client in the order it is called.
///
/// Maps to Requirements 12.1–12.8, 9.9, 9.10, 9.12.
class TcpOutputServer {
public:
    /// @param addr  Listen address (e.g. "127.0.0.1").
    /// @param port  Listen port.
    TcpOutputServer(const std::string& addr, uint16_t port);

    ~TcpOutputServer();

    // Non-copyable, non-movable (owns file descriptors)
    TcpOutputServer(const TcpOutputServer&) = delete;
    TcpOutputServer& operator=(const TcpOutputServer&) = delete;
    TcpOutputServer(TcpOutputServer&&) = delete;
    TcpOutputServer& operator=(TcpOutputServer&&) = delete;

    /// Bind and listen on the configured address:port.
    /// Throws std::runtime_error on bind/listen failure.
    void start();

    /// Close all client connections and the listen socket.
    void stop();

    /// Accept new clients (non-blocking check).
    /// @param timeout_ms  poll() timeout in milliseconds (default 100).
    void poll_once(int timeout_ms = 100);

    /// Broadcast data to all connected clients.
    /// Clients that fail to receive (EPIPE / ECONNRESET) are removed.
    void write_to_all(const std::vector<uint8_t>& data);

    /// Number of currently connected clients.
    size_t client_count() const;

private:
    std::string addr_;
    uint16_t port_;

    int listen_fd_ = -1;

    struct ClientInfo {
        int fd;
        std::string addr;
        uint16_t port;
        uint64_t bytes_sent;
    };
    std::vector<ClientInfo> clients_;

    /// Remove a client by index, logging disconnect info.
    void remove_client(size_t index);
};

} // namespace qpsk_b200

#endif // QPSK_B200_TCP_OUTPUT_SERVER_H
