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
// CLI argument parsing
// ---------------------------------------------------------------------------

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [OPTIONS]\n"
        << "\n"
        << "Options:\n"
        << "  --config <path>         Load configuration from JSON file\n"
        << "  --center-freq <Hz>      Center frequency (default: 2.4e9)\n"
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
        << "  --help                  Show this help message\n";
}

static qpsk_b200::Config parse_args(int argc, char* argv[]) {
    qpsk_b200::Config cfg = qpsk_b200::Config::defaults();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }

        // All remaining flags require a value
        if (i + 1 >= argc) {
            std::cerr << "Error: option " << arg << " requires a value\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        std::string val = argv[++i];

        if (arg == "--config") {
            cfg = qpsk_b200::Config::from_json(val);
        } else if (arg == "--center-freq") {
            cfg.center_freq = std::stod(val);
        } else if (arg == "--sample-rate") {
            cfg.sample_rate = std::stod(val);
        } else if (arg == "--tx-gain") {
            cfg.tx_gain = std::stod(val);
        } else if (arg == "--rx-gain") {
            cfg.rx_gain = std::stod(val);
        } else if (arg == "--tx-antenna") {
            cfg.tx_antenna = val;
        } else if (arg == "--rx-antenna") {
            cfg.rx_antenna = val;
        } else if (arg == "--sps") {
            cfg.samples_per_symbol = std::stoi(val);
        } else if (arg == "--rrc-rolloff") {
            cfg.rrc_rolloff = std::stod(val);
        } else if (arg == "--tcp-input-addr") {
            cfg.tcp_input_addr = val;
        } else if (arg == "--tcp-input-port") {
            cfg.tcp_input_port = static_cast<uint16_t>(std::stoi(val));
        } else if (arg == "--tcp-output-addr") {
            cfg.tcp_output_addr = val;
        } else if (arg == "--tcp-output-port") {
            cfg.tcp_output_port = static_cast<uint16_t>(std::stoi(val));
        } else if (arg == "--fec-enabled") {
            cfg.fec_enabled = (val == "1" || val == "true");
        } else if (arg == "--fec-code-rate") {
            if (val == "1/2") {
                cfg.fec_code_rate = qpsk_b200::CodeRate::RATE_1_2;
            } else if (val == "3/4") {
                cfg.fec_code_rate = qpsk_b200::CodeRate::RATE_3_4;
            } else {
                std::cerr << "Error: unsupported FEC code rate '" << val
                          << "'. Use \"1/2\" or \"3/4\".\n";
                std::exit(1);
            }
        } else {
            std::cerr << "Error: unknown option " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    // Validate — always validate after all CLI processing,
    // since CLI flags may override values loaded from JSON.
    cfg.validate();

    return cfg;
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
    try {
        while (running.load(std::memory_order_acquire)) {
            server.poll_once(100);  // 100ms poll timeout

            while (server.has_frame()) {
                auto frame_data = server.read_frame();
                if (frame_data.empty()) break;

                if (!tcp_to_tx.try_push(std::move(frame_data))) {
                    spdlog::warn("TCP→TX queue overflow: dropped {} byte chunk",
                                 frame_data.size());
                }
            }
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
    const std::atomic<bool>& running)
{
    spdlog::info("TX thread started");
    try {
        while (running.load(std::memory_order_acquire)) {
            std::vector<uint8_t> payload;
            if (!tcp_to_tx.pop_wait(payload, std::chrono::milliseconds(100))) {
                continue;  // timeout — check running flag and retry
            }

            // Encode payload into complex samples
            auto samples = encoder.encode(payload);

            // Stream to B200
            try {
                b200.send(samples);
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

    // Number of samples to receive per iteration — roughly one frame's worth
    constexpr size_t RX_BATCH_SIZE = 4096;

    try {
        b200.start_rx_stream();
    } catch (const std::runtime_error& e) {
        spdlog::warn("Could not start RX stream (UHD may not be available): {}",
                     e.what());
        // Stay in the loop so the thread can be joined on shutdown
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

            auto decoded = decoder.decode(samples);
            if (decoded.has_value() && !decoded->empty()) {
                if (!rx_to_tcp.try_push(std::move(*decoded))) {
                    spdlog::warn("RX→TCP queue overflow: dropped decoded payload");
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("RX thread error: {}", e.what());
    }

    try {
        b200.stop_rx_stream();
    } catch (const std::runtime_error&) {
        // Ignore — UHD may not be available
    }

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

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // ---- Install signal handlers ----
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ---- Parse configuration ----
    qpsk_b200::Config cfg;
    try {
        cfg = parse_args(argc, argv);
    } catch (const std::exception& e) {
        spdlog::error("Configuration error: {}", e.what());
        return 1;
    }

    spdlog::info("QPSK B200 Codec starting");
    spdlog::info("  Center freq : {:.3f} MHz", cfg.center_freq / 1e6);
    spdlog::info("  Sample rate : {:.3f} MSPS", cfg.sample_rate / 1e6);
    spdlog::info("  TX gain     : {:.1f} dB", cfg.tx_gain);
    spdlog::info("  RX gain     : {:.1f} dB", cfg.rx_gain);
    spdlog::info("  FEC         : {}", cfg.fec_enabled ? "enabled" : "disabled");
    if (cfg.fec_enabled) {
        spdlog::info("  FEC rate    : {}",
                     cfg.fec_code_rate == qpsk_b200::CodeRate::RATE_1_2
                         ? "1/2" : "3/4");
    }
    spdlog::info("  TCP input   : {}:{}", cfg.tcp_input_addr, cfg.tcp_input_port);
    spdlog::info("  TCP output  : {}:{}", cfg.tcp_output_addr, cfg.tcp_output_port);

    // ---- Initialize components ----
    std::unique_ptr<qpsk_b200::B200Interface> b200;
    try {
        b200 = std::make_unique<qpsk_b200::B200Interface>(cfg);
        b200->configure_tx(cfg);
        b200->configure_rx(cfg);
        spdlog::info("B200 interface initialized");
    } catch (const std::exception& e) {
        spdlog::warn("B200 initialization failed: {}. "
                     "TX/RX threads will run in degraded mode.", e.what());
        // Create the interface anyway — the stub will throw on send/recv
        // but the threads handle that gracefully.
        try {
            b200 = std::make_unique<qpsk_b200::B200Interface>(cfg);
        } catch (...) {
            // Even construction failed — create a minimal stub
            spdlog::warn("B200 interface construction failed; "
                         "TX/RX threads will idle.");
        }
    }

    qpsk_b200::Encoder encoder(cfg);
    qpsk_b200::Decoder decoder(cfg);

    qpsk_b200::TcpInputServer tcp_input(
        cfg.tcp_input_addr, cfg.tcp_input_port);
    qpsk_b200::TcpOutputServer tcp_output(
        cfg.tcp_output_addr, cfg.tcp_output_port);

    // ---- Start TCP servers ----
    try {
        tcp_input.start();
        spdlog::info("TCP input server listening on {}:{}",
                     cfg.tcp_input_addr, cfg.tcp_input_port);
    } catch (const std::exception& e) {
        spdlog::error("Failed to start TCP input server: {}", e.what());
        return 1;
    }

    try {
        tcp_output.start();
        spdlog::info("TCP output server listening on {}:{}",
                     cfg.tcp_output_addr, cfg.tcp_output_port);
    } catch (const std::exception& e) {
        spdlog::error("Failed to start TCP output server: {}", e.what());
        tcp_input.stop();
        return 1;
    }

    // ---- Create SPSC queues ----
    qpsk_b200::SpscQueue<std::vector<uint8_t>> tcp_to_tx(64);
    qpsk_b200::SpscQueue<std::vector<uint8_t>> rx_to_tcp(64);

    // ---- Spawn worker threads ----
    spdlog::info("Spawning worker threads");

    std::thread tcp_in_thread(tcp_input_thread_fn,
                              std::ref(tcp_input),
                              std::ref(tcp_to_tx),
                              std::cref(g_running));

    // TX and RX threads require a valid B200Interface.
    // If construction failed, these threads idle until shutdown.
    std::thread tx_thread;
    std::thread rx_thread;

    if (b200) {
        tx_thread = std::thread(tx_thread_fn,
                                std::ref(encoder),
                                std::ref(*b200),
                                std::ref(tcp_to_tx),
                                std::cref(g_running));

        rx_thread = std::thread(rx_thread_fn,
                                std::ref(decoder),
                                std::ref(*b200),
                                std::ref(rx_to_tcp),
                                std::cref(g_running));
    } else {
        spdlog::warn("B200 not available — TX/RX threads not started");
    }

    std::thread tcp_out_thread(tcp_output_thread_fn,
                               std::ref(tcp_output),
                               std::ref(rx_to_tcp),
                               std::cref(g_running));

    spdlog::info("All threads running. Press Ctrl+C to stop.");

    // ---- Wait for shutdown signal ----
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("Shutdown signal received — stopping threads");

    // ---- Join all threads ----
    tcp_in_thread.join();
    if (tx_thread.joinable()) tx_thread.join();
    if (rx_thread.joinable()) rx_thread.join();
    tcp_out_thread.join();

    // ---- Stop TCP servers ----
    tcp_input.stop();
    tcp_output.stop();

    spdlog::info("QPSK B200 Codec stopped");
    return 0;
}
