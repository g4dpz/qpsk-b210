#ifndef QPSK_B200_B200_INTERFACE_H
#define QPSK_B200_B200_INTERFACE_H

#include <complex>
#include <cstddef>
#include <vector>

#include "qpsk_b200/types.h"

#ifdef QPSK_HAS_UHD
#include <uhd/usrp/multi_usrp.hpp>
#endif

namespace qpsk_b200 {

/// Wraps the Ettus UHD multi_usrp API for B200 hardware interaction.
///
/// When compiled with QPSK_HAS_UHD defined, this class discovers a B200
/// device, configures TX/RX chains, and provides send/recv methods that
/// wrap UHD streamer calls with fc32 host format and sc16 wire format.
///
/// When compiled without UHD support, all methods except the constructor
/// throw std::runtime_error indicating that UHD is not available.
class B200Interface {
public:
    /// Construct a B200Interface.
    ///
    /// With UHD: discovers a B200 device and creates a multi_usrp handle.
    /// Throws std::runtime_error if no device is found.
    ///
    /// Without UHD: logs a warning that UHD is not available.
    explicit B200Interface(const Config& config);

    ~B200Interface();

    // Non-copyable, movable
    B200Interface(const B200Interface&) = delete;
    B200Interface& operator=(const B200Interface&) = delete;
    B200Interface(B200Interface&&) noexcept;
    B200Interface& operator=(B200Interface&&) noexcept;

    /// Configure the TX chain: sample rate, center frequency, gain, antenna.
    /// Logs configured parameters via spdlog.
    void configure_tx(const Config& config);

    /// Configure the RX chain: sample rate, center frequency, gain, antenna.
    /// Logs configured parameters via spdlog.
    void configure_rx(const Config& config);

    /// Send complex float samples to the B200 TX streamer.
    void send(const std::vector<std::complex<float>>& samples);

    /// Receive complex float samples from the B200 RX streamer.
    std::vector<std::complex<float>> recv(size_t num_samples);

    /// Issue a stream command to start continuous RX streaming.
    void start_rx_stream();

    /// Issue a stream command to stop RX streaming.
    void stop_rx_stream();

private:
#ifdef QPSK_HAS_UHD
    uhd::usrp::multi_usrp::sptr usrp_;
    uhd::tx_streamer::sptr tx_stream_;
    uhd::rx_streamer::sptr rx_stream_;
#endif
};

} // namespace qpsk_b200

#endif // QPSK_B200_B200_INTERFACE_H
