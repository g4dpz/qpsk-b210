#ifndef QPSK_B200_UDP_INPUT_H
#define QPSK_B200_UDP_INPUT_H

#include <cstdint>
#include <string>
#include <vector>

namespace qpsk_b200 {

/// UDP socket that receives datagrams on a configurable address:port.
/// Each received datagram is one complete payload to transmit (no framing
/// needed — UDP preserves message boundaries).
///
/// Designed for direct integration with ION-DTN's LTP layer, where each
/// UDP datagram maps to one LTP segment = one QPSK RF burst.
class UdpInput {
public:
    /// @param addr  Bind address (e.g. "127.0.0.1" or "0.0.0.0").
    /// @param port  Bind port (default: 1113, ION's default LTP UDP port).
    UdpInput(const std::string& addr, uint16_t port);

    ~UdpInput();

    // Non-copyable, non-movable (owns file descriptor)
    UdpInput(const UdpInput&) = delete;
    UdpInput& operator=(const UdpInput&) = delete;
    UdpInput(UdpInput&&) = delete;
    UdpInput& operator=(UdpInput&&) = delete;

    /// Bind the UDP socket to the configured address:port.
    /// Throws std::runtime_error on failure.
    void start();

    /// Close the socket.
    void stop();

    /// Poll for an incoming datagram.
    /// @param timeout_ms  poll() timeout in milliseconds (default 50).
    /// @return The received datagram payload, or an empty vector if none available.
    std::vector<uint8_t> recv_datagram(int timeout_ms = 50);

    /// Whether the socket is open and bound.
    bool is_open() const;

    /// Total number of datagrams received since start().
    uint64_t datagrams_received() const;

    /// Total bytes received since start().
    uint64_t bytes_received() const;

private:
    std::string addr_;
    uint16_t port_;
    int fd_ = -1;

    uint64_t datagrams_received_ = 0;
    uint64_t bytes_received_ = 0;
};

} // namespace qpsk_b200

#endif // QPSK_B200_UDP_INPUT_H
