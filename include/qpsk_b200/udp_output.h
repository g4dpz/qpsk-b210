#ifndef QPSK_B200_UDP_OUTPUT_H
#define QPSK_B200_UDP_OUTPUT_H

#include <cstdint>
#include <string>
#include <vector>

namespace qpsk_b200 {

/// UDP socket that sends decoded payloads as datagrams to a configurable
/// destination address:port.
///
/// Each decoded payload is sent as a single UDP datagram — no framing or
/// connection state needed.  Designed for direct integration with ION-DTN's
/// LTP layer (udpcli).
class UdpOutput {
public:
    /// @param dest_addr  Destination address (e.g. "127.0.0.1").
    /// @param dest_port  Destination port (default: 1114).
    UdpOutput(const std::string& dest_addr, uint16_t dest_port);

    ~UdpOutput();

    // Non-copyable, non-movable (owns file descriptor)
    UdpOutput(const UdpOutput&) = delete;
    UdpOutput& operator=(const UdpOutput&) = delete;
    UdpOutput(UdpOutput&&) = delete;
    UdpOutput& operator=(UdpOutput&&) = delete;

    /// Create the UDP socket.
    /// Throws std::runtime_error on failure.
    void start();

    /// Close the socket.
    void stop();

    /// Send a payload as a single UDP datagram to the configured destination.
    /// @return true on success, false on send failure.
    bool send_datagram(const std::vector<uint8_t>& data);

    /// Whether the socket is open.
    bool is_open() const;

    /// Total number of datagrams sent since start().
    uint64_t datagrams_sent() const;

    /// Total bytes sent since start().
    uint64_t bytes_sent() const;

private:
    std::string dest_addr_;
    uint16_t dest_port_;
    int fd_ = -1;

    uint64_t datagrams_sent_ = 0;
    uint64_t bytes_sent_ = 0;
};

} // namespace qpsk_b200

#endif // QPSK_B200_UDP_OUTPUT_H
