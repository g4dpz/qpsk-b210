#include "qpsk_b200/b200_interface.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

#ifdef QPSK_HAS_UHD
#include <uhd/device.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/types/stream_cmd.hpp>
#include <uhd/types/tune_request.hpp>
#endif

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// Error message used by the stub (no-UHD) implementation
// ---------------------------------------------------------------------------
namespace {
[[maybe_unused]]
const char* NO_UHD_MSG =
    "B200Interface: UHD support not compiled. "
    "Rebuild with -DQPSK_ENABLE_UHD=ON";
} // anonymous namespace

// ---------------------------------------------------------------------------
// UHD implementation
// ---------------------------------------------------------------------------
#ifdef QPSK_HAS_UHD

B200Interface::B200Interface(const Config& config) {
    // Discover B200 devices — optionally filter by serial
    uhd::device_addr_t hint;
    if (!config.device_serial.empty()) {
        hint["serial"] = config.device_serial;
    }
    uhd::device_addrs_t devices = uhd::device::find(hint);
    if (devices.empty()) {
        std::string msg = "No B200 device found";
        if (!config.device_serial.empty()) {
            msg += " with serial '" + config.device_serial + "'";
        }
        msg += ". Check USB connection and UHD driver installation.";
        throw std::runtime_error(msg);
    }

    // Create the multi_usrp handle
    usrp_ = uhd::usrp::multi_usrp::make(devices.front().to_string());

    spdlog::info("B200Interface: device discovered and connected (serial={})",
                 devices.front().has_key("serial")
                     ? devices.front()["serial"] : "unknown");
}

B200Interface::~B200Interface() = default;

B200Interface::B200Interface(B200Interface&&) noexcept = default;
B200Interface& B200Interface::operator=(B200Interface&&) noexcept = default;

void B200Interface::configure_tx(const Config& config) {
    usrp_->set_tx_rate(config.sample_rate);
    usrp_->set_tx_freq(uhd::tune_request_t(config.center_freq));
    usrp_->set_tx_gain(config.tx_gain);
    usrp_->set_tx_antenna(config.tx_antenna);

    spdlog::info("B200Interface TX configured: freq={:.0f} Hz, rate={:.0f} sps, "
                 "gain={:.1f} dB, antenna={}",
                 config.center_freq, config.sample_rate,
                 config.tx_gain, config.tx_antenna);

    // Create TX streamer with fc32 host format, sc16 wire format
    uhd::stream_args_t stream_args("fc32", "sc16");
    tx_stream_ = usrp_->get_tx_stream(stream_args);
}

void B200Interface::configure_rx(const Config& config) {
    usrp_->set_rx_rate(config.sample_rate);
    usrp_->set_rx_freq(uhd::tune_request_t(config.center_freq));
    usrp_->set_rx_gain(config.rx_gain);
    usrp_->set_rx_antenna(config.rx_antenna);

    spdlog::info("B200Interface RX configured: freq={:.0f} Hz, rate={:.0f} sps, "
                 "gain={:.1f} dB, antenna={}",
                 config.center_freq, config.sample_rate,
                 config.rx_gain, config.rx_antenna);

    // Create RX streamer with fc32 host format, sc16 wire format
    uhd::stream_args_t stream_args("fc32", "sc16");
    rx_stream_ = usrp_->get_rx_stream(stream_args);
}

void B200Interface::send(const std::vector<std::complex<float>>& samples) {
    if (!tx_stream_) {
        throw std::runtime_error(
            "B200Interface::send(): TX streamer not initialized. "
            "Call configure_tx() first.");
    }

    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst   = true;
    md.has_time_spec  = false;

    size_t total_sent = 0;
    while (total_sent < samples.size()) {
        size_t sent = tx_stream_->send(
            &samples[total_sent],
            samples.size() - total_sent,
            md);
        if (sent == 0) {
            spdlog::warn("B200Interface::send(): zero samples sent, retrying");
        }
        total_sent += sent;
        // After the first chunk, clear start_of_burst
        md.start_of_burst = false;
    }
}

std::vector<std::complex<float>> B200Interface::recv(size_t num_samples) {
    if (!rx_stream_) {
        throw std::runtime_error(
            "B200Interface::recv(): RX streamer not initialized. "
            "Call configure_rx() first.");
    }

    std::vector<std::complex<float>> buffer(num_samples);
    uhd::rx_metadata_t md;

    size_t total_received = 0;
    while (total_received < num_samples) {
        size_t received = rx_stream_->recv(
            &buffer[total_received],
            num_samples - total_received,
            md,
            3.0);  // 3-second timeout

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            spdlog::warn("B200Interface::recv(): timeout, retrying");
            continue;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            spdlog::warn("B200Interface::recv(): overflow detected");
            // Continue — data may still be usable
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE &&
            md.error_code != uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            throw std::runtime_error(
                "B200Interface::recv(): UHD error code " +
                std::to_string(static_cast<int>(md.error_code)));
        }
        total_received += received;
    }

    return buffer;
}

void B200Interface::start_rx_stream() {
    if (!rx_stream_) {
        throw std::runtime_error(
            "B200Interface::start_rx_stream(): RX streamer not initialized. "
            "Call configure_rx() first.");
    }

    uhd::stream_cmd_t cmd(
        uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.stream_now = true;
    rx_stream_->issue_stream_cmd(cmd);

    spdlog::info("B200Interface: RX streaming started");
}

void B200Interface::stop_rx_stream() {
    if (!rx_stream_) {
        throw std::runtime_error(
            "B200Interface::stop_rx_stream(): RX streamer not initialized. "
            "Call configure_rx() first.");
    }

    uhd::stream_cmd_t cmd(
        uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
    rx_stream_->issue_stream_cmd(cmd);

    spdlog::info("B200Interface: RX streaming stopped");
}

// ---------------------------------------------------------------------------
// Stub implementation (no UHD)
// ---------------------------------------------------------------------------
#else  // !QPSK_HAS_UHD

B200Interface::B200Interface(const Config& /*config*/) {
    spdlog::warn("B200Interface: UHD support not compiled. "
                 "Hardware features are unavailable. "
                 "Rebuild with -DQPSK_ENABLE_UHD=ON to enable B200 support.");
}

B200Interface::~B200Interface() = default;

B200Interface::B200Interface(B200Interface&&) noexcept = default;
B200Interface& B200Interface::operator=(B200Interface&&) noexcept = default;

void B200Interface::configure_tx(const Config& /*config*/) {
    throw std::runtime_error(NO_UHD_MSG);
}

void B200Interface::configure_rx(const Config& /*config*/) {
    throw std::runtime_error(NO_UHD_MSG);
}

void B200Interface::send(const std::vector<std::complex<float>>& /*samples*/) {
    throw std::runtime_error(NO_UHD_MSG);
}

std::vector<std::complex<float>> B200Interface::recv(size_t /*num_samples*/) {
    throw std::runtime_error(NO_UHD_MSG);
}

void B200Interface::start_rx_stream() {
    throw std::runtime_error(NO_UHD_MSG);
}

void B200Interface::stop_rx_stream() {
    throw std::runtime_error(NO_UHD_MSG);
}

#endif // QPSK_HAS_UHD

} // namespace qpsk_b200
