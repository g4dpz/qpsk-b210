/// @file main.cpp
/// Main application entry point for the QPSK B200 codec.
///
/// Parses CLI arguments or loads a JSON config file, initializes all
/// components (B200Interface, Encoder, Decoder, TCP servers), spawns
/// TX/RX/TCP worker threads, wires them together via SPSC queues, and
/// handles graceful shutdown on SIGINT/SIGTERM.
///
/// Maps to Requirements 1, 2, 8, 11, 12.

#include "qpsk_b200/b200_interface.h"
#include "qpsk_b200/decoder.h"
#include "qpsk_b200/encoder.h"
#include "qpsk_b200/spsc_queue.h"
#include "qpsk_b200/tcp_input_server.h"
#include "qpsk_b200/tcp_output_server.h"
#include "qpsk_b200/udp_input.h"
#include "qpsk_b200/udp_output.h"
#include "qpsk_b200/types.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Global shutdown flag — set by signal handler
// ---------------------------------------------------------------------------
static std::atomic<bool> g_running{true};

static void signal_handler(int /*signum*/) {
    g_running.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Operating mode
// ---------------------------------------------------------------------------
enum class AppMode { TX_ONLY, RX_ONLY, TX_RX };

// ---------------------------------------------------------------------------
// CLI argument parsing
// ---------------------------------------------------------------------------

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [OPTIONS]\n"
        << "\n"
        << "Options:\n"
        << "  --config <path>         Load configuration from JSON file\n"
        << "  --mode <tx|rx|txrx>     Operating mode (default: txrx)\n"
        << "  --serial <serial>       B200 device serial number\n"
        << "  --center-freq <Hz>      Center frequency (default: 2.4e9)\n"
        << "  --tx-freq <Hz>          TX frequency (overrides center-freq for TX)\n"
        << "  --rx-freq <Hz>          RX frequency (overrides center-freq for RX)\n"
        << "  --sample-rate <Hz>      Sample rate (default: 1e6)\n"
        << "  --tx-gain <dB>          Transmit gain (default: 40)\n"
        << "  --rx-gain <dB>          Receive gain (default: 40)\n"
        << "  --tx-antenna <name>     TX antenna port (default: TX/RX)\n"
        << "  --rx-antenna <name>     RX antenna port (default: RX2)\n"
        << "  --sps <int>             Samples per symbol (default: 4)\n"
        << "  --rrc-rolloff <float>   RRC roll-off factor (default: 0.35)\n"
        << "  --tcp-input-addr <addr> TCP input listen address (default: 127.0.0.1)\n"
        << "  --tcp-input-port <port> TCP input listen port (default: 5000)\n"
        << "  --tcp-output-addr <addr> TCP output listen address (default: 127.0.0.1)\n"
        << "  --tcp-output-port <port> TCP output listen port (default: 5001)\n"
        << "  --fec-enabled <0|1>     Enable FEC (default: 1)\n"
        << "  --fec-code-rate <rate>  FEC code rate: \"1/2\" or \"3/4\" (default: 1/2)\n"
        << "  --udp                   Use UDP mode instead of TCP (for ION-DTN LTP)\n"
        << "  --udp-input-addr <addr> UDP input bind address (default: 127.0.0.1)\n"
        << "  --udp-input-port <port> UDP input bind port (default: 1113)\n"
        << "  --udp-output-addr <addr> UDP output destination address (default: 127.0.0.1)\n"
        << "  --udp-output-port <port> UDP output destination port (default: 1114)\n"
        << "  --acquisition-symbols <N> Training symbols before sync word (default: 128)\n"
        << "  --ramp-symbols <N>      Amplitude ramp symbols (default: 8)\n"
        << "  --help                  Show this help message\n";
}

struct AppOptions {
    qpsk_b200::Config config;
    AppMode mode = AppMode::TX_RX;
};

static AppOptions parse_args(int argc, char* argv[]) {
    AppOptions opts;
    opts.config = qpsk_b200::Config::defaults();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }

        // --udp is a standalone flag (no value required)
        if (arg == "--udp") {
            opts.config.use_udp = true;
            continue;
        }

        // All remaining flags require a value
        if (i + 1 >= argc) {
            std::cerr << "Error: option " << arg << " requires a value\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        std::string val = argv[++i];

        if (arg == "--config") {
            opts.config = qpsk_b200::Config::from_json(val);
        } else if (arg == "--mode") {
            if (val == "tx") opts.mode = AppMode::TX_ONLY;
            else if (val == "rx") opts.mode = AppMode::RX_ONLY;
            else if (val == "txrx") opts.mode = AppMode::TX_RX;
            else {
                std::cerr << "Error: --mode must be tx, rx, or txrx\n";
                std::exit(1);
            }
        } else if (arg == "--serial") {
            opts.config.device_serial = val;
        } else if (arg == "--center-freq") {
            opts.config.center_freq = std::stod(val);
        } else if (arg == "--tx-freq") {
            opts.config.tx_freq = std::stod(val);
        } else if (arg == "--rx-freq") {
            opts.config.rx_freq = std::stod(val);
        } else if (arg == "--sample-rate") {
            opts.config.sample_rate = std::stod(val);
        } else if (arg == "--tx-gain") {
            opts.config.tx_gain = std::stod(val);
        } else if (arg == "--rx-gain") {
            opts.config.rx_gain = std::stod(val);
        } else if (arg == "--tx-antenna") {
            opts.config.tx_antenna = val;
        } else if (arg == "--rx-antenna") {
            opts.config.rx_antenna = val;
        } else if (arg == "--sps") {
            opts.config.samples_per_symbol = std::stoi(val);
        } else if (arg == "--rrc-rolloff") {
            opts.config.rrc_rolloff = std::stod(val);
        } else if (arg == "--tcp-input-addr") {
            opts.config.tcp_input_addr = val;
        } else if (arg == "--tcp-input-port") {
            opts.config.tcp_input_port = static_cast<uint16_t>(std::stoi(val));
        } else if (arg == "--tcp-output-addr") {
            opts.config.tcp_output_addr = val;
        } else if (arg == "--tcp-output-port") {
            opts.config.tcp_output_port = static_cast<uint16_t>(std::stoi(val));
        } else if (arg == "--fec-enabled") {
            opts.config.fec_enabled = (val == "1" || val == "true");
        } else if (arg == "--fec-code-rate") {
            if (val == "1/2") {
                opts.config.fec_code_rate = qpsk_b200::CodeRate::RATE_1_2;
            } else if (val == "3/4") {
                opts.config.fec_code_rate = qpsk_b200::CodeRate::RATE_3_4;
            } else {
                std::cerr << "Error: unsupported FEC code rate '" << val
                          << "'. Use \"1/2\" or \"3/4\".\n";
                std::exit(1);
            }
        } else if (arg == "--udp-input-addr") {
            opts.config.udp_input_addr = val;
        } else if (arg == "--udp-input-port") {
            opts.config.udp_input_port = static_cast<uint16_t>(std::stoi(val));
        } else if (arg == "--udp-output-addr") {
            opts.config.udp_output_addr = val;
        } else if (arg == "--udp-output-port") {
            opts.config.udp_output_port = static_cast<uint16_t>(std::stoi(val));
        } else if (arg == "--acquisition-symbols") {
            opts.config.acquisition_symbols = std::stoi(val);
        } else if (arg == "--ramp-symbols") {
            opts.config.ramp_symbols = std::stoi(val);
        } else {
            std::cerr << "Error: unknown option " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    // Validate — always validate after all CLI processing,
    // since CLI flags may override values loaded from JSON.
    opts.config.validate();

    return opts;
}

// ---------------------------------------------------------------------------
// Thread functions
// ---------------------------------------------------------------------------

/// TCP Input thread: accepts connections, reads bytes, enqueues to TX queue.
static void tcp_input_thread_fn(
    qpsk_b200::TcpInputServer& server,
    qpsk_b200::SpscQueue<std::vector<uint8_t>>& tcp_to_tx,
    const std::atomic<bool>& running)
{
    spdlog::info("TCP input thread started");
    bool was_connected = false;
    auto last_data_time = std::chrono::steady_clock::now();
    size_t last_buffered = 0;

    try {
        while (running.load(std::memory_order_acquire)) {
            server.poll_once(50);  // 50ms poll timeout

            // Send complete frames
            while (server.has_frame()) {
                auto frame_data = server.read_frame();
                if (frame_data.empty()) break;

                spdlog::info("TCP input: enqueuing {} byte frame to TX",
                             frame_data.size());
                if (!tcp_to_tx.try_push(std::move(frame_data))) {
                    spdlog::warn("TCP→TX queue overflow: dropped {} byte chunk",
                                 frame_data.size());
                }
                last_data_time = std::chrono::steady_clock::now();
                last_buffered = 0;
            }

            // Flush remaining buffered data when client disconnects
            if (was_connected && !server.connected()) {
                auto leftover = server.flush();
                if (!leftover.empty()) {
                    spdlog::info("TCP input: flushing {} remaining bytes to TX",
                                 leftover.size());
                    if (!tcp_to_tx.try_push(std::move(leftover))) {
                        spdlog::warn("TCP→TX queue overflow on flush");
                    }
                }
            }

            // Flush on idle timeout: if data has been sitting in the buffer
            // for more than 100ms without growing, send it
            size_t current_buffered = server.buffered_bytes();
            if (current_buffered > 0 && current_buffered == last_buffered) {
                auto now = std::chrono::steady_clock::now();
                auto idle_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_data_time).count();
                if (idle_ms > 100) {
                    auto data = server.flush();
                    if (!data.empty()) {
                        spdlog::info("TCP input: idle flush {} bytes to TX",
                                     data.size());
                        if (!tcp_to_tx.try_push(std::move(data))) {
                            spdlog::warn("TCP→TX queue overflow on idle flush");
                        }
                    }
                    last_buffered = 0;
                }
            } else if (current_buffered != last_buffered) {
                last_data_time = std::chrono::steady_clock::now();
                last_buffered = current_buffered;
            }

            was_connected = server.connected();
        }
    } catch (const std::exception& e) {
        spdlog::error("TCP input thread error: {}", e.what());
    }
    spdlog::info("TCP input thread stopped");
}

/// TX thread: dequeues byte chunks, encodes, streams to B200.
static void tx_thread_fn(
    qpsk_b200::Encoder& encoder,
    qpsk_b200::B200Interface& b200,
    qpsk_b200::SpscQueue<std::vector<uint8_t>>& tcp_to_tx,
    const qpsk_b200::Config& config,
    const std::atomic<bool>& running)
{
    spdlog::info("TX thread started");

    // Lead-in/lead-out silence to give hardware time to ramp and RX to lock
    constexpr size_t LEAD_SAMPLES = 4096;

    // Ramp length in samples (linear amplitude taper)
    const size_t ramp_samples = static_cast<size_t>(
        config.ramp_symbols * config.samples_per_symbol);

    try {
        while (running.load(std::memory_order_acquire)) {
            std::vector<uint8_t> payload;
            if (!tcp_to_tx.pop_wait(payload, std::chrono::milliseconds(100))) {
                continue;  // timeout — check running flag and retry
            }

            // Encode payload into complex samples
            auto samples = encoder.encode(payload);

            spdlog::info("TX: sending {} payload bytes → {} IQ samples",
                         payload.size(), samples.size());

            // Apply linear amplitude ramp-up to the first ramp_samples
            if (ramp_samples > 0 && samples.size() > 2 * ramp_samples) {
                for (size_t i = 0; i < ramp_samples; ++i) {
                    float gain = static_cast<float>(i) /
                                 static_cast<float>(ramp_samples);
                    samples[i] *= gain;
                }
                // Apply linear amplitude ramp-down to the last ramp_samples
                size_t start = samples.size() - ramp_samples;
                for (size_t i = 0; i < ramp_samples; ++i) {
                    float gain = static_cast<float>(ramp_samples - i) /
                                 static_cast<float>(ramp_samples);
                    samples[start + i] *= gain;
                }
            }

            // Pad with silence before and after the burst
            std::vector<std::complex<float>> padded;
            padded.reserve(LEAD_SAMPLES + samples.size() + LEAD_SAMPLES);
            padded.insert(padded.end(), LEAD_SAMPLES,
                          std::complex<float>(0.0f, 0.0f));
            padded.insert(padded.end(), samples.begin(), samples.end());
            padded.insert(padded.end(), LEAD_SAMPLES,
                          std::complex<float>(0.0f, 0.0f));

            // Stream to B200
            try {
                b200.send(padded);
                spdlog::info("TX: burst sent ({} total samples including padding)",
                             padded.size());
            } catch (const std::runtime_error& e) {
                spdlog::warn("TX send failed (UHD may not be available): {}",
                             e.what());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("TX thread error: {}", e.what());
    }
    spdlog::info("TX thread stopped");
}

/// RX thread: receives samples from B200, decodes, enqueues to TCP output queue.
static void rx_thread_fn(
    qpsk_b200::Decoder& decoder,
    qpsk_b200::B200Interface& b200,
    qpsk_b200::SpscQueue<std::vector<uint8_t>>& rx_to_tcp,
    const std::atomic<bool>& running)
{
    spdlog::info("RX thread started");

    constexpr size_t RX_BATCH_SIZE = 4096;
    // Only attempt decode when we have at least this many new samples
    // (avoids wasting CPU on tiny noise-only buffers)
    constexpr size_t MIN_DECODE_SAMPLES = 16384;
    constexpr size_t MAX_BUFFER_SIZE = 262144;

    std::vector<std::complex<float>> rx_buffer;
    rx_buffer.reserve(MAX_BUFFER_SIZE);
    size_t samples_since_last_decode = 0;

    try {
        b200.start_rx_stream();
    } catch (const std::runtime_error& e) {
        spdlog::warn("Could not start RX stream (UHD may not be available): {}",
                     e.what());
        while (running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        spdlog::info("RX thread stopped (no UHD)");
        return;
    }

    try {
        while (running.load(std::memory_order_acquire)) {
            std::vector<std::complex<float>> samples;
            try {
                samples = b200.recv(RX_BATCH_SIZE);
            } catch (const std::runtime_error& e) {
                spdlog::warn("RX recv failed: {}", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (samples.empty()) continue;

            rx_buffer.insert(rx_buffer.end(), samples.begin(), samples.end());
            samples_since_last_decode += samples.size();

            // Only attempt decode when we have enough new samples
            if (samples_since_last_decode < MIN_DECODE_SAMPLES) {
                continue;
            }

            // Reset decoder state before each attempt for clean sync
            decoder.reset();
            auto decoded = decoder.decode(rx_buffer);
            samples_since_last_decode = 0;

            if (decoded.has_value() && !decoded->empty()) {
                spdlog::info("RX: decoded {} bytes!", decoded->size());
                if (!rx_to_tcp.try_push(std::move(*decoded))) {
                    spdlog::warn("RX→TCP queue overflow: dropped decoded payload");
                }
                rx_buffer.clear();
            } else {
                // Trim buffer — keep only the most recent portion
                if (rx_buffer.size() > MAX_BUFFER_SIZE) {
                    size_t discard = rx_buffer.size() - MAX_BUFFER_SIZE / 2;
                    rx_buffer.erase(rx_buffer.begin(),
                                    rx_buffer.begin() + static_cast<long>(discard));
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("RX thread error: {}", e.what());
    }

    try {
        b200.stop_rx_stream();
    } catch (const std::runtime_error&) {}

    spdlog::info("RX thread stopped");
}

/// TCP Output thread: dequeues decoded payloads, writes to all connected clients.
static void tcp_output_thread_fn(
    qpsk_b200::TcpOutputServer& server,
    qpsk_b200::SpscQueue<std::vector<uint8_t>>& rx_to_tcp,
    const std::atomic<bool>& running)
{
    spdlog::info("TCP output thread started");
    try {
        while (running.load(std::memory_order_acquire)) {
            // Accept new clients
            server.poll_once(10);

            // Drain the queue
            std::vector<uint8_t> payload;
            while (rx_to_tcp.try_pop(payload)) {
                server.write_to_all(payload);
            }

            // Brief sleep to avoid busy-spinning when queue is empty
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Drain remaining items on shutdown
        std::vector<uint8_t> payload;
        while (rx_to_tcp.try_pop(payload)) {
            server.write_to_all(payload);
        }
    } catch (const std::exception& e) {
        spdlog::error("TCP output thread error: {}", e.what());
    }
    spdlog::info("TCP output thread stopped");
}

/// UDP Input thread: receives datagrams, enqueues each directly to TX queue.
/// Each datagram = one complete LTP segment = one QPSK burst.
static void udp_input_thread_fn(
    qpsk_b200::UdpInput& udp_in,
    qpsk_b200::SpscQueue<std::vector<uint8_t>>& udp_to_tx,
    const std::atomic<bool>& running)
{
    spdlog::info("UDP input thread started");
    try {
        while (running.load(std::memory_order_acquire)) {
            auto datagram = udp_in.recv_datagram(50);
            if (datagram.empty()) continue;

            spdlog::debug("UDP input: received {} byte datagram",
                          datagram.size());
            if (!udp_to_tx.try_push(std::move(datagram))) {
                spdlog::warn("UDP→TX queue overflow: dropped datagram");
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("UDP input thread error: {}", e.what());
    }
    spdlog::info("UDP input thread stopped");
}

/// UDP Output thread: dequeues decoded payloads, sends each as a UDP datagram.
static void udp_output_thread_fn(
    qpsk_b200::UdpOutput& udp_out,
    qpsk_b200::SpscQueue<std::vector<uint8_t>>& rx_to_udp,
    const std::atomic<bool>& running)
{
    spdlog::info("UDP output thread started");
    try {
        while (running.load(std::memory_order_acquire)) {
            std::vector<uint8_t> payload;
            if (!rx_to_udp.pop_wait(payload, std::chrono::milliseconds(100))) {
                continue;  // timeout — check running flag and retry
            }

            spdlog::debug("UDP output: sending {} byte datagram", payload.size());
            if (!udp_out.send_datagram(payload)) {
                spdlog::warn("UDP output: failed to send {} byte datagram",
                             payload.size());
            }
        }

        // Drain remaining items on shutdown
        std::vector<uint8_t> payload;
        while (rx_to_udp.try_pop(payload)) {
            udp_out.send_datagram(payload);
        }
    } catch (const std::exception& e) {
        spdlog::error("UDP output thread error: {}", e.what());
    }
    spdlog::info("UDP output thread stopped");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // ---- Install signal handlers ----
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ---- Parse configuration ----
    AppOptions opts;
    try {
        opts = parse_args(argc, argv);
    } catch (const std::exception& e) {
        spdlog::error("Configuration error: {}", e.what());
        return 1;
    }

    const auto& cfg = opts.config;
    const auto mode = opts.mode;

    spdlog::info("QPSK B200 Codec starting");
    spdlog::info("  Mode        : {}",
                 mode == AppMode::TX_ONLY ? "TX only" :
                 mode == AppMode::RX_ONLY ? "RX only" : "TX+RX");
    if (!cfg.device_serial.empty()) {
        spdlog::info("  Device      : serial={}", cfg.device_serial);
    }
    spdlog::info("  TX freq     : {:.3f} MHz", cfg.effective_tx_freq() / 1e6);
    spdlog::info("  RX freq     : {:.3f} MHz", cfg.effective_rx_freq() / 1e6);
    spdlog::info("  Sample rate : {:.3f} MSPS", cfg.sample_rate / 1e6);
    spdlog::info("  TX gain     : {:.1f} dB", cfg.tx_gain);
    spdlog::info("  RX gain     : {:.1f} dB", cfg.rx_gain);
    spdlog::info("  FEC         : {}", cfg.fec_enabled ? "enabled" : "disabled");
    if (cfg.fec_enabled) {
        spdlog::info("  FEC rate    : {}",
                     cfg.fec_code_rate == qpsk_b200::CodeRate::RATE_1_2
                         ? "1/2" : "3/4");
    }
    if (cfg.use_udp) {
        spdlog::info("  Transport   : UDP");
        spdlog::info("  UDP input   : {}:{}", cfg.udp_input_addr, cfg.udp_input_port);
        spdlog::info("  UDP output  : {}:{}", cfg.udp_output_addr, cfg.udp_output_port);
    } else {
        spdlog::info("  Transport   : TCP");
        spdlog::info("  TCP input   : {}:{}", cfg.tcp_input_addr, cfg.tcp_input_port);
        spdlog::info("  TCP output  : {}:{}", cfg.tcp_output_addr, cfg.tcp_output_port);
    }
    spdlog::info("  Acquisition : {} symbols", cfg.acquisition_symbols);
    spdlog::info("  Ramp        : {} symbols", cfg.ramp_symbols);

    // ---- Initialize components ----
    std::unique_ptr<qpsk_b200::B200Interface> b200;
    try {
        b200 = std::make_unique<qpsk_b200::B200Interface>(cfg);
        if (mode == AppMode::TX_ONLY || mode == AppMode::TX_RX) {
            b200->configure_tx(cfg);
        }
        if (mode == AppMode::RX_ONLY || mode == AppMode::TX_RX) {
            b200->configure_rx(cfg);
        }
        spdlog::info("B200 interface initialized");
    } catch (const std::exception& e) {
        spdlog::warn("B200 initialization failed: {}. "
                     "TX/RX threads will run in degraded mode.", e.what());
        try {
            b200 = std::make_unique<qpsk_b200::B200Interface>(cfg);
        } catch (...) {
            spdlog::warn("B200 interface construction failed; "
                         "TX/RX threads will idle.");
        }
    }

    qpsk_b200::Encoder encoder(cfg);
    qpsk_b200::Decoder decoder(cfg);

    // ---- Create I/O layer (TCP or UDP) ----
    std::unique_ptr<qpsk_b200::TcpInputServer> tcp_input;
    std::unique_ptr<qpsk_b200::TcpOutputServer> tcp_output;
    std::unique_ptr<qpsk_b200::UdpInput> udp_input;
    std::unique_ptr<qpsk_b200::UdpOutput> udp_output;

    if (cfg.use_udp) {
        // UDP mode
        if (mode == AppMode::TX_ONLY || mode == AppMode::TX_RX) {
            udp_input = std::make_unique<qpsk_b200::UdpInput>(
                cfg.udp_input_addr, cfg.udp_input_port);
            try {
                udp_input->start();
            } catch (const std::exception& e) {
                spdlog::error("Failed to start UDP input: {}", e.what());
                return 1;
            }
        }
        if (mode == AppMode::RX_ONLY || mode == AppMode::TX_RX) {
            udp_output = std::make_unique<qpsk_b200::UdpOutput>(
                cfg.udp_output_addr, cfg.udp_output_port);
            try {
                udp_output->start();
            } catch (const std::exception& e) {
                spdlog::error("Failed to start UDP output: {}", e.what());
                if (udp_input) udp_input->stop();
                return 1;
            }
        }
    } else {
        // TCP mode (existing behavior)
        tcp_input = std::make_unique<qpsk_b200::TcpInputServer>(
            cfg.tcp_input_addr, cfg.tcp_input_port);
        tcp_output = std::make_unique<qpsk_b200::TcpOutputServer>(
            cfg.tcp_output_addr, cfg.tcp_output_port);

        if (mode == AppMode::TX_ONLY || mode == AppMode::TX_RX) {
            try {
                tcp_input->start();
                spdlog::info("TCP input server listening on {}:{}",
                             cfg.tcp_input_addr, cfg.tcp_input_port);
            } catch (const std::exception& e) {
                spdlog::error("Failed to start TCP input server: {}", e.what());
                return 1;
            }
        }

        if (mode == AppMode::RX_ONLY || mode == AppMode::TX_RX) {
            try {
                tcp_output->start();
                spdlog::info("TCP output server listening on {}:{}",
                             cfg.tcp_output_addr, cfg.tcp_output_port);
            } catch (const std::exception& e) {
                spdlog::error("Failed to start TCP output server: {}", e.what());
                if (mode == AppMode::TX_RX) tcp_input->stop();
                return 1;
            }
        }
    }

    // ---- Create SPSC queues ----
    qpsk_b200::SpscQueue<std::vector<uint8_t>> input_to_tx(64);
    qpsk_b200::SpscQueue<std::vector<uint8_t>> rx_to_output(64);

    // ---- Spawn worker threads ----
    spdlog::info("Spawning worker threads");

    std::thread input_thread;
    std::thread tx_thread;
    std::thread rx_thread;
    std::thread output_thread;

    // TX path: input (TCP or UDP) → encoder → B200 TX
    if (mode == AppMode::TX_ONLY || mode == AppMode::TX_RX) {
        if (cfg.use_udp) {
            input_thread = std::thread(udp_input_thread_fn,
                                       std::ref(*udp_input),
                                       std::ref(input_to_tx),
                                       std::cref(g_running));
        } else {
            input_thread = std::thread(tcp_input_thread_fn,
                                       std::ref(*tcp_input),
                                       std::ref(input_to_tx),
                                       std::cref(g_running));
        }

        if (b200) {
            tx_thread = std::thread(tx_thread_fn,
                                    std::ref(encoder),
                                    std::ref(*b200),
                                    std::ref(input_to_tx),
                                    std::cref(cfg),
                                    std::cref(g_running));
        }
    }

    // RX path: B200 RX → decoder → output (TCP or UDP)
    if (mode == AppMode::RX_ONLY || mode == AppMode::TX_RX) {
        if (b200) {
            rx_thread = std::thread(rx_thread_fn,
                                    std::ref(decoder),
                                    std::ref(*b200),
                                    std::ref(rx_to_output),
                                    std::cref(g_running));
        }

        if (cfg.use_udp) {
            output_thread = std::thread(udp_output_thread_fn,
                                        std::ref(*udp_output),
                                        std::ref(rx_to_output),
                                        std::cref(g_running));
        } else {
            output_thread = std::thread(tcp_output_thread_fn,
                                        std::ref(*tcp_output),
                                        std::ref(rx_to_output),
                                        std::cref(g_running));
        }
    }

    spdlog::info("All threads running. Press Ctrl+C to stop.");

    // ---- Wait for shutdown signal ----
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("Shutdown signal received — stopping threads");

    // ---- Join all threads ----
    if (input_thread.joinable()) input_thread.join();
    if (tx_thread.joinable()) tx_thread.join();
    if (rx_thread.joinable()) rx_thread.join();
    if (output_thread.joinable()) output_thread.join();

    // ---- Stop I/O layer ----
    if (cfg.use_udp) {
        if (udp_input) udp_input->stop();
        if (udp_output) udp_output->stop();
    } else {
        if (tcp_input) tcp_input->stop();
        if (tcp_output) tcp_output->stop();
    }

    spdlog::info("QPSK B200 Codec stopped");
    return 0;
}
